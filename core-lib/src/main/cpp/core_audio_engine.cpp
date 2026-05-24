#include "include/audio_engine.h"
#include <cmath>
#include <cstdio>

// Provide lightweight logging for core (platform-neutral)
#define LOGI(fmt, ...) std::fprintf(stderr, "[AudioEngine] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) std::fprintf(stderr, "[AudioEngine][ERROR] " fmt "\n", ##__VA_ARGS__)

static const char* algorithmNameFromId(int algorithmId) {
    switch (algorithmId) {
        case 1: return "yinfft";
        case 2: return "schmitt";
        case 3: return "yinfast";
        case 4: return "mcomb";
        case 5: return "fcomb";
        case 6: return "specacf";
        case 7: return "default";
        default: return "yin";
    }
}

AudioEngine::AudioEngine(int algorithmId, int bufferSize, int hopSize, float confidenceThreshold, float minInputDb)
    : mAlgorithmId(algorithmId), mBufferSize(bufferSize), mHopSize(hopSize), mConfidenceThreshold(confidenceThreshold), mMinInputDb(minInputDb),
      mPendingAlgorithmId(algorithmId), mPendingBufferSize(bufferSize), mPendingHopSize(hopSize), mPendingConfidenceThreshold(confidenceThreshold), mPendingMinInputDb(minInputDb) {
}

AudioEngine::~AudioEngine() {
    stop();
    releaseAubio();
}

void AudioEngine::pushSamples(const float* data, int32_t numFrames, int32_t channelCount) {
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

void AudioEngine::releaseAubio() {
    if (mPitchOutput) { del_fvec(mPitchOutput); mPitchOutput = nullptr; }
    if (mPitchInput) { del_fvec(mPitchInput); mPitchInput = nullptr; }
    if (mPitchDetector) { del_aubio_pitch(mPitchDetector); mPitchDetector = nullptr; }
}

bool AudioEngine::prepareAubio(uint_t sampleRate) {
    releaseAubio();
    mSampleRate = static_cast<int>(sampleRate);

    const char* algName = algorithmNameFromId(mAlgorithmId);
    mPitchDetector = new_aubio_pitch(algName, mBufferSize, mHopSize, sampleRate);
    if (mPitchDetector == nullptr) {
        mPitchDetector = new_aubio_pitch("yin", mBufferSize, mHopSize, sampleRate);
        if (mPitchDetector == nullptr) {
            return false;
        }
    }

    aubio_pitch_set_unit(mPitchDetector, "Hz");
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

bool AudioEngine::start(IResultListener* listener) {
    if (mRunning.load(std::memory_order_acquire)) return true;

    if (!prepareAubio(48000)) {
        return false;
    }

    mListener = listener;

    AudioSourceManager::instance().addConsumer(this);

    mRunning.store(true, std::memory_order_release);
    mAnalysisThread = std::thread(&AudioEngine::analysisLoop, this);
    return true;
}

void AudioEngine::stop() {
    if (!mRunning.load(std::memory_order_acquire)) return;
    mRunning.store(false, std::memory_order_release);
    mDataCv.notify_all();

    if (mAnalysisThread.joinable()) mAnalysisThread.join();

    AudioSourceManager::instance().removeConsumer(this);

    mListener = nullptr;
}

bool AudioEngine::updateOptions(int algorithmId, int bufferSize, int hopSize, float confidenceThreshold, float minInputDb) {
    if (mPitchDetector == nullptr) {
        std::unique_lock<std::mutex> lock(mConfigMutex);
        mAlgorithmId = algorithmId;
        mBufferSize = bufferSize;
        mHopSize = hopSize;
        mConfidenceThreshold = confidenceThreshold;
        mMinInputDb = minInputDb;
        return true;
    }

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

void AudioEngine::analysisLoop() {
    LOGI("AnalysisLoop started for engine alg=%d", mAlgorithmId);
    while (mRunning.load(std::memory_order_acquire)) {
        if (mPitchDetector == nullptr || mPitchInput == nullptr || mPitchOutput == nullptr) break;

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
            if (!prepareAubio(mSampleRate)) {
                LOGE("Reconfiguration failed");
            }
            continue;
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

        for (int i = 0; i < mHopSize; ++i) {
            const float sample = mRingBuffer[read & kRingBufferMask];
            fvec_set_sample(mPitchInput, sample, i);
            ++read;
        }

        float sumSquares = 0.0f;
        for (int i = 0; i < mHopSize; ++i) {
            const float sample = fvec_get_sample(mPitchInput, i);
            sumSquares += sample * sample;
        }

        const float inputRms = std::sqrt(sumSquares / static_cast<float>(mHopSize));
        const float inputDb = (inputRms > 0.0f && std::isfinite(inputRms)) ? (20.0f * std::log10(inputRms)) : -120.0f;

        mReadIndex.store(read, std::memory_order_release);

        aubio_pitch_do(mPitchDetector, mPitchInput, mPitchOutput);
        const float detectedFrequency = fvec_get_sample(mPitchOutput, 0);
        float confidence = aubio_pitch_get_confidence(mPitchDetector);
        if (!std::isfinite(confidence)) confidence = 0.0f;
        if (confidence < 0.0f) confidence = 0.0f;
        if (confidence > 1.0f) confidence = 1.0f;

        if (detectedFrequency <= 0.0f) continue;
        if (inputDb < mMinInputDb) continue;
        if (mConfidenceThreshold > 0.0f && confidence < mConfidenceThreshold) continue;

        const auto now = std::chrono::steady_clock::now();
        const bool changedEnough = std::fabs(detectedFrequency - mLastSentFrequency) >= 1.0f;
        const bool staleEnough = mLastSentAt.time_since_epoch().count() == 0 || (now - mLastSentAt) >= std::chrono::milliseconds(120);

        if (changedEnough || staleEnough) {
            mLastSentFrequency = detectedFrequency;
            mLastSentAt = now;
            if (mListener) {
                AnalysisResult r{detectedFrequency, confidence, inputRms, std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()};
                mListener->onAnalysisResult(r);
            }
        }
    }
    LOGI("AnalysisLoop ended for engine alg=%d", mAlgorithmId);
}
