#include <jni.h>
#include <android/log.h>
#include <oboe/Oboe.h>

#include <aubio.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#define LOG_TAG "AudioEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 1. NEJDŘÍVE DEFINUJEME GLOBÁLNÍ PROSTŘEDÍ PRO JVM
JavaVM* gJavaVM = nullptr;

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    gJavaVM = vm;
    return JNI_VERSION_1_6;
}

// 2. TŘÍDA AUDIO ENGINU (Zpracovává data a posílá frekvenci do Kotlinu)
class AudioEngine : public oboe::AudioStreamCallback {
public:
    AudioEngine() = default;

    ~AudioEngine() {
        stop();
    }

    // Pomocná metoda pro bezpečné smazání JNI referencí
    void cleanUpJni(JNIEnv* env) {
        if (mCallbackObject != nullptr) {
            env->DeleteGlobalRef(mCallbackObject);
            mCallbackObject = nullptr;
            mCallbackMethod = nullptr;
        }
    }

    // Pomocná metoda pro odeslání frekvence, confidence a rms do Kotlinu
    void sendFrequencyToKotlin(float frequencyHz, float confidence, float rms) {
        if (gJavaVM == nullptr || mCallbackObject == nullptr || mCallbackMethod == nullptr) return;

        JNIEnv* env = nullptr;
        jint res = gJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
        bool threadAttached = false;

        if (res == JNI_EDETACHED) {
            if (gJavaVM->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                threadAttached = true;
            }
        }

        if (env != nullptr) {
            // voláme Kotlin callback s frekvencí, confidence a rms (float, float, float)
            env->CallVoidMethod(mCallbackObject, mCallbackMethod, frequencyHz, confidence, rms);

            if (threadAttached) {
                gJavaVM->DetachCurrentThread();
            }
        }
    }

    bool start(JNIEnv* env, jobject callbackInstance, int algorithmId, int bufferSize, int hopSize, float confidenceThreshold, float minInputDb) {
        if (mStream != nullptr) return true; // Už běží

        LOGI("Startování Audio Enginu...");

        // store requested config
        mAlgorithmId = algorithmId;
        mBufferSize = bufferSize;
        mHopSize = hopSize;
        mConfidenceThreshold = confidenceThreshold;
        mMinInputDb = minInputDb;

        // Globální reference na callback, aby ho nesmazal GC
        mCallbackObject = env->NewGlobalRef(callbackInstance);
        jclass callbackClass = env->GetObjectClass(mCallbackObject);
        // aktualizovaná signatura: onFrequencyChanged(float frequency, float confidence, float rms)
        mCallbackMethod = env->GetMethodID(callbackClass, "onFrequencyChanged", "(FFF)V");
        env->DeleteLocalRef(callbackClass);

        if (mCallbackMethod == nullptr) {
            LOGE("Nepodařilo se najít Kotlin callback onFrequencyChanged(FFF)V");
            cleanUpJni(env);
            return false;
        }

        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Input)
               ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
               ->setSharingMode(oboe::SharingMode::Shared)
               ->setFormat(oboe::AudioFormat::Float)
                       ->setChannelCount(oboe::ChannelCount::Mono)
                       ->setFramesPerCallback(static_cast<int32_t>(mHopSize))
               ->setCallback(this);

        oboe::Result result = builder.openStream(mStream);
        if (result != oboe::Result::OK) {
            LOGE("Nepodařilo se otevřít Oboe stream: %s", oboe::convertToText(result));
            cleanUpJni(env);
            return false;
        }

        if (!prepareAubio(mStream->getSampleRate())) {
            LOGE("Nepodařilo se připravit Aubio pitch detektor");
            mStream->close();
            mStream = nullptr;
            cleanUpJni(env);
            return false;
        }

        LOGI("Aubio připraven: sampleRate=%u buffer=%d hop=%d", mStream->getSampleRate(), mBufferSize, mHopSize);

        result = mStream->requestStart();
        if (result != oboe::Result::OK) {
            LOGE("Nepodařilo se spustit Oboe stream: %s", oboe::convertToText(result));
            mStream->close();
            mStream = nullptr;
            releaseAubio();
            cleanUpJni(env);
            return false;
        }

        mRunning.store(true, std::memory_order_release);
        mAnalysisThread = std::thread(&AudioEngine::analysisLoop, this);

        LOGI("Audio Engine úspěšně spuštěn.");
        return true;
    }

    // Request runtime update of algorithm/buffer/hop. If engine is running,
    // the actual reconfiguration is performed on the analysis thread to avoid
    // concurrent use of Aubio objects.
    bool updateOptions(int algorithmId, int bufferSize, int hopSize, float confidenceThreshold, float minInputDb) {
        if (mStream == nullptr) {
            // not running yet — just apply immediately
            std::unique_lock<std::mutex> lock(mConfigMutex);
            mAlgorithmId = algorithmId;
            mBufferSize = bufferSize;
            mHopSize = hopSize;
            mConfidenceThreshold = confidenceThreshold;
            mMinInputDb = minInputDb;
            return true;
        }

        // if running, set pending values and notify analysis thread
        {
            std::unique_lock<std::mutex> lock(mConfigMutex);
            mPendingAlgorithmId = algorithmId;
            mPendingBufferSize = bufferSize;
            mPendingHopSize = hopSize;
            mPendingConfidenceThreshold = confidenceThreshold;
            mPendingMinInputDb = minInputDb;
            mReconfigRequested.store(true, std::memory_order_release);
        }
        mDataCv.notify_one();
        return true;
    }

    void stop() {
        if (mStream == nullptr) return;

        LOGI("Zastavování Audio Enginu...");

        // 1. Vyžádáme zastavení hardware streamu
        oboe::Result result = mStream->requestStop();
        if (result != oboe::Result::OK) {
            LOGE("Chyba při requestStop: %s", oboe::convertToText(result));
        }

        // 2. KLÍČOVÝ KROK: Počkáme, dokud se stream opravdu nezastaví (timeout 500ms)
        oboe::StreamState inputState = oboe::StreamState::Stopping;
        oboe::StreamState nextState = oboe::StreamState::Unknown;

        // Tento řádek zablokuje vlákno, dokud se stav nezmění ze "Stopping" na cokoli dalšího (Stopped)
        mStream->waitForStateChange(inputState, &nextState, 500'000'000);

        // 3. Teprve když máme jistotu, že hardware nahrávání ukončil, stream bezpečně zavřeme
        mStream->close();
        mStream = nullptr;

        mRunning.store(false, std::memory_order_release);
        mDataCv.notify_all();

        if (mAnalysisThread.joinable()) {
            mAnalysisThread.join();
        }

        releaseAubio();

        // Uvolnění JNI referencí
        if (gJavaVM != nullptr && mCallbackObject != nullptr) {
            JNIEnv* env = nullptr;
            if (gJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
                cleanUpJni(env);
            }
        }
        LOGI("Audio Engine bezpečně zastaven a uvolněn.");
    }

    // Oboe callback, který běží na vlastním audio vlákně
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *audioStream, void *audioData, int32_t numFrames) override {
        auto *floatData = static_cast<const float *>(audioData);
        if (floatData == nullptr) return oboe::DataCallbackResult::Continue;

        if (!mRunning.load(std::memory_order_acquire)) {
            return oboe::DataCallbackResult::Continue;
        }

        const int32_t channelCount = audioStream->getChannelCount();

        pushSamples(floatData, numFrames, channelCount);

        return oboe::DataCallbackResult::Continue;
    }

private:
    static constexpr float kMinConfidence = 0.70f;
    static constexpr size_t kRingBufferSize = 16384;
    static constexpr size_t kRingBufferMask = kRingBufferSize - 1;

    std::shared_ptr<oboe::AudioStream> mStream = nullptr;
    jobject mCallbackObject = nullptr;
    jmethodID mCallbackMethod = nullptr;

    // configurable parameters (can be changed at runtime)
    std::mutex mConfigMutex;
    std::atomic<bool> mReconfigRequested{false};
    int mAlgorithmId = 0; // 0=YIN,1=YIN_FFT,2=SCHMITT
    int mBufferSize = 2048;
    int mHopSize = 512;
    float mConfidenceThreshold = 0.15f;
    float mMinInputDb = -90.0f;
    int mPendingAlgorithmId = 0;
    int mPendingBufferSize = 2048;
    int mPendingHopSize = 512;
    float mPendingConfidenceThreshold = 0.15f;
    float mPendingMinInputDb = -90.0f;

    aubio_pitch_t* mPitchDetector = nullptr;
    fvec_t* mPitchInput = nullptr;
    fvec_t* mPitchOutput = nullptr;
    std::atomic<bool> mRunning{false};
    std::thread mAnalysisThread;

    std::array<float, kRingBufferSize> mRingBuffer{};
    std::atomic<size_t> mWriteIndex{0};
    std::atomic<size_t> mReadIndex{0};
    std::condition_variable mDataCv;
    std::mutex mDataMutex;
    float mLastSentFrequency = 0.0f;
    std::chrono::steady_clock::time_point mLastSentAt{};

    bool prepareAubio(uint_t sampleRate) {
        releaseAubio();

        // choose algorithm name
        const char* algName = "yin";
        switch (mAlgorithmId) {
            case 1: algName = "yinfft"; break;
            case 2: algName = "schmitt"; break;
            case 3: algName = "yinfast"; break;
            case 4: algName = "mcomb"; break;
            case 5: algName = "fcomb"; break;
            case 6: algName = "specacf"; break;
            case 7: algName = "default"; break;
            default: algName = "yin"; break;
        }

        mPitchDetector = new_aubio_pitch(algName, mBufferSize, mHopSize, sampleRate);
        if (mPitchDetector == nullptr) {
            // fallback to 'yin'
            mPitchDetector = new_aubio_pitch("yin", mBufferSize, mHopSize, sampleRate);
            if (mPitchDetector == nullptr) {
                return false;
            }
        }

        aubio_pitch_set_unit(mPitchDetector, "Hz");
        // set reasonable defaults; allow later tuning
        aubio_pitch_set_tolerance(mPitchDetector, 0.80f);
        aubio_pitch_set_silence(mPitchDetector, -90.0f);

        mPitchInput = new_fvec(mHopSize);
        mPitchOutput = new_fvec(1);

        if (mPitchInput == nullptr || mPitchOutput == nullptr) {
            releaseAubio();
            return false;
        }

        mWriteIndex.store(0, std::memory_order_release);
        mReadIndex.store(0, std::memory_order_release);
        mLastSentFrequency = 0.0f;
        mLastSentAt = std::chrono::steady_clock::time_point{};
        return true;
    }

    void pushSamples(const float* data, int32_t numFrames, int32_t channelCount) {
        const size_t frames = static_cast<size_t>(numFrames);
        size_t write = mWriteIndex.load(std::memory_order_relaxed);
        size_t read = mReadIndex.load(std::memory_order_acquire);

        for (size_t i = 0; i < frames; ++i) {
            const float sample = data[i * static_cast<size_t>(channelCount)];
            mRingBuffer[write & kRingBufferMask] = sample;
            ++write;

            if (write - read > kRingBufferSize) {
                read = write - kRingBufferSize;
                mReadIndex.store(read, std::memory_order_release);
            }
        }

        mWriteIndex.store(write, std::memory_order_release);
        mDataCv.notify_one();
    }

    void analysisLoop() {
        LOGI("Analýza frekvence spuštěna.");

        while (mRunning.load(std::memory_order_acquire)) {
            if (mPitchDetector == nullptr || mPitchInput == nullptr || mPitchOutput == nullptr) {
                break;
            }

            size_t read = mReadIndex.load(std::memory_order_acquire);
            const size_t write = mWriteIndex.load(std::memory_order_acquire);
            if (write - read < static_cast<size_t>(mHopSize)) {
                std::unique_lock<std::mutex> lock(mDataMutex);
                mDataCv.wait_for(lock, std::chrono::milliseconds(20), [this]() {
                    return !mRunning.load(std::memory_order_acquire) ||
                           (mWriteIndex.load(std::memory_order_acquire) - mReadIndex.load(std::memory_order_acquire) >= static_cast<size_t>(mHopSize));
                });
                continue;
            }

            // handle runtime reconfiguration request (performed on analysis thread)
            if (mReconfigRequested.load(std::memory_order_acquire)) {
                std::unique_lock<std::mutex> cfgLock(mConfigMutex);
                mAlgorithmId = mPendingAlgorithmId;
                mBufferSize = mPendingBufferSize;
                mHopSize = mPendingHopSize;
                mConfidenceThreshold = mPendingConfidenceThreshold;
                mMinInputDb = mPendingMinInputDb;
                mReconfigRequested.store(false, std::memory_order_release);
                cfgLock.unlock();

                LOGI("Reconfiguring Aubio: alg=%d buf=%d hop=%d", mAlgorithmId, mBufferSize, mHopSize);
                // reinitialize aubio with new parameters
                if (!prepareAubio(mStream->getSampleRate())) {
                    LOGE("Reconfiguration failed");
                }
                // continue to next loop to pick up new hop size
                continue;
            }

            for (int i = 0; i < mHopSize; ++i) {
                const float sample = mRingBuffer[read & kRingBufferMask];
                fvec_set_sample(mPitchInput, sample, i);
                ++read;
            }

            float sumSquares = 0.0f;
            float maxAbs = 0.0f;
            for (int i = 0; i < mHopSize; ++i) {
                const float sample = fvec_get_sample(mPitchInput, i);
                sumSquares += sample * sample;
                const float absSample = std::fabs(sample);
                if (absSample > maxAbs) {
                    maxAbs = absSample;
                }
            }

            const float inputRms = std::sqrt(sumSquares / static_cast<float>(mHopSize));
            LOGI("Vstupní blok: rms=%.6f maxAbs=%.6f", inputRms, maxAbs);

            mReadIndex.store(read, std::memory_order_release);

            aubio_pitch_do(mPitchDetector, mPitchInput, mPitchOutput);
            const float detectedFrequency = fvec_get_sample(mPitchOutput, 0);
            float confidence = aubio_pitch_get_confidence(mPitchDetector);

                            // normalize/clamp confidence so UI gets a usable value in [0,1]
            if (!std::isfinite(confidence)) confidence = 0.0f;
            if (confidence < 0.0f) confidence = 0.0f;
            if (confidence > 1.0f) confidence = 1.0f;

            const float inputDb = (inputRms > 0.0f && std::isfinite(inputRms)) ? (20.0f * std::log10(inputRms)) : -120.0f;
            LOGI("Aubio pitch raw: %.2f Hz, confidence(clamped): %.3f, inputDb: %.1f", detectedFrequency, confidence, inputDb);

            // ignore non-positive frequencies
            if (detectedFrequency <= 0.0f) {
                continue;
            }

            if (inputDb < mMinInputDb) {
                continue;
            }

            if (mConfidenceThreshold > 0.0f && confidence < mConfidenceThreshold) {
                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            const bool changedEnough = std::fabs(detectedFrequency - mLastSentFrequency) >= 1.0f;
            const bool staleEnough = mLastSentAt.time_since_epoch().count() == 0 ||
                    (now - mLastSentAt) >= std::chrono::milliseconds(120);

            if (changedEnough || staleEnough) {
                mLastSentFrequency = detectedFrequency;
                mLastSentAt = now;
                LOGI("Detekovaná frekvence: %.2f Hz (confidence %.3f, rms %.6f, db %.1f)", detectedFrequency, confidence, inputRms, inputDb);
                sendFrequencyToKotlin(detectedFrequency, confidence, inputRms);
            }
        }

        LOGI("Analýza frekvence ukončena.");
    }

    void releaseAubio() {
        if (mPitchOutput != nullptr) {
            del_fvec(mPitchOutput);
            mPitchOutput = nullptr;
        }

        if (mPitchInput != nullptr) {
            del_fvec(mPitchInput);
            mPitchInput = nullptr;
        }

        if (mPitchDetector != nullptr) {
            del_aubio_pitch(mPitchDetector);
            mPitchDetector = nullptr;
        }

        mWriteIndex.store(0, std::memory_order_release);
        mReadIndex.store(0, std::memory_order_release);
    }
};

// 3. JEDINÁ GLOBÁLNÍ INSTANCE NAŠEHO ENGINU
std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();

// 4. JNI VRSTVA (MOST PRO KOTLIN) - Musí být na úplném konci souboru
extern "C" JNIEXPORT jboolean JNICALL
Java_cz_eidam_lib_NativeLib_start(JNIEnv* env, jobject thiz, jint algorithmId, jint bufferSize, jint hopSize, jfloat confidenceThreshold, jfloat minInputDb, jobject callback) {
    return engine->start(env, callback, static_cast<int>(algorithmId), static_cast<int>(bufferSize), static_cast<int>(hopSize), static_cast<float>(confidenceThreshold), static_cast<float>(minInputDb));
}

extern "C" JNIEXPORT void JNICALL
Java_cz_eidam_lib_NativeLib_stop(JNIEnv* env, jobject thiz) {
    engine->stop();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_cz_eidam_lib_NativeLib_updateOptions(JNIEnv* env, jobject thiz, jint algorithmId, jint bufferSize, jint hopSize, jfloat confidenceThreshold, jfloat minInputDb) {
    return engine->updateOptions(static_cast<int>(algorithmId), static_cast<int>(bufferSize), static_cast<int>(hopSize), static_cast<float>(confidenceThreshold), static_cast<float>(minInputDb));
}