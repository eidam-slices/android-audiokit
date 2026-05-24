#include <audio_source.h>

#include <oboe/Oboe.h>
#include <android/log.h>
#include <memory>
#include <algorithm>

#include "../../../../core-lib/src/main/cpp/include/audio_source.h"

#define LOG_TAG "AudioSourceMgr"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace oboe;

struct AudioSourceManager::Impl : public oboe::AudioStreamCallback {
    Impl() = default;

    ~Impl() { stop(); }

    std::shared_ptr<oboe::AudioStream> mStream = nullptr;
    std::vector<AudioConsumer *> mConsumers;
    std::mutex mConsumersMutex;
    int mHopSize = 512;

    int32_t mDeviceId = oboe::kUnspecified;
    std::vector<AudioDevice> audioDevices;
    std::mutex deviceMutex;

    DataCallbackResult onAudioReady(AudioStream *stream, void *audioData, int32_t numFrames) override {
        auto *floatData = static_cast<const float *>(audioData);
        if (floatData == nullptr) return DataCallbackResult::Continue;

        std::lock_guard<std::mutex> lock(mConsumersMutex);
        for (auto *c: mConsumers) {
            if (c) c->pushSamples(floatData, numFrames, stream->getChannelCount());
        }
        return DataCallbackResult::Continue;
    }

    bool startIfNeeded() {
        if (mStream != nullptr) return true;

        // Rychlá kontrola, zda vůbec máme pro koho audio chytat
        {
            std::lock_guard<std::mutex> lock(mConsumersMutex);
            if (mConsumers.empty()) return false;
        }

        LOGI("Starting Oboe source with Device ID: %d", mDeviceId);
        AudioStreamBuilder builder;
        builder.setDirection(Direction::Input)
                ->setPerformanceMode(PerformanceMode::LowLatency)
                ->setSharingMode(SharingMode::Shared)
                ->setFormat(AudioFormat::Float)
                ->setChannelCount(ChannelCount::Mono)
                ->setFramesPerCallback(static_cast<int32_t>(mHopSize))
                ->setDeviceId(mDeviceId)
                ->setCallback(this);

        Result result = builder.openStream(mStream);
        if (result != Result::OK) {
            LOGE("Failed to open Oboe stream: %s", convertToText(result));
            mStream = nullptr;
            return false;
        }

        LOGI("Oboe stream opened: sr=%u buf=%d hop=%d", mStream->getSampleRate(), 0, mHopSize);
        result = mStream->requestStart();
        if (result != Result::OK) {
            LOGE("Failed to start Oboe stream: %s", convertToText(result));
            mStream->close();
            mStream = nullptr;
            return false;
        }
        return true;
    }

    void stop() {
        if (mStream == nullptr) return;
        LOGI("Stopping Oboe source...");
        mStream->requestStop();
        mStream->waitForStateChange(StreamState::Stopping, nullptr, 500'000'000);
        mStream->close();
        mStream = nullptr;
    }

    void setDeviceId(int32_t deviceId) {
        bool needRestart = false;

        // 1. KROK: Zamkneme pouze na okamžik, abychom bezpečně sáhli na proměnné
        {
            std::lock_guard<std::mutex> lock(mConsumersMutex);
            if (mDeviceId == deviceId) return;

            mDeviceId = deviceId;

            // Restart potřebujeme pouze tehdy, pokud stream aktuálně běží
            if (mStream != nullptr) {
                needRestart = true;
            }
        } // <-- ZDE zámek zaniká a mutex se uvolňuje!

        // 2. KROK: Nyní můžeme bezpečně manipulovat se streamem bez rizika deadlocku
        if (needRestart) {
            stop(); // Zde se stream bezpečně zastaví, protože onAudioReady už nevisí na mutexu

            // Znovu nastartujeme (startIfNeeded si interně zkontroluje, jestli má smysl běžet)
            startIfNeeded();
        }
    }
};

AudioSourceManager &AudioSourceManager::instance() {
    static AudioSourceManager mgr;
    return mgr;
}

AudioSourceManager::AudioSourceManager() : mImpl(new Impl()) {
}

AudioSourceManager::~AudioSourceManager() {
    delete mImpl;
    mImpl = nullptr;
}

void AudioSourceManager::addConsumer(AudioConsumer *consumer) {
    if (!consumer) return;
    {
        std::lock_guard<std::mutex> lock(mImpl->mConsumersMutex);
        auto it = std::find(mImpl->mConsumers.begin(), mImpl->mConsumers.end(), consumer);
        if (it == mImpl->mConsumers.end()) mImpl->mConsumers.push_back(consumer);
    }
    mImpl->startIfNeeded();
}

void AudioSourceManager::removeConsumer(AudioConsumer *consumer) {
    if (!consumer) return;
    {
        std::lock_guard<std::mutex> lock(mImpl->mConsumersMutex);
        auto it = std::find(mImpl->mConsumers.begin(), mImpl->mConsumers.end(), consumer);
        if (it != mImpl->mConsumers.end()) mImpl->mConsumers.erase(it);
    }
    {
        std::lock_guard<std::mutex> lock(mImpl->mConsumersMutex);
        if (mImpl->mConsumers.empty()) {
            mImpl->stop();
        }
    }
}

void AudioSourceManager::setDeviceId(int32_t deviceId) {
    mImpl->setDeviceId(deviceId);
}

std::vector<AudioDevice> AudioSourceManager::getAvailableDevices() {
    std::lock_guard<std::mutex> lock(mImpl->deviceMutex);
    return mImpl->audioDevices;
}

void AudioSourceManager::registerAndroidDevice(int32_t id, const std::string& name) {
    std::lock_guard<std::mutex> lock(mImpl->deviceMutex);
    mImpl->audioDevices.push_back({id, name});
}

void AudioSourceManager::clearAndroidDevices() {
    std::lock_guard<std::mutex> lock(mImpl->deviceMutex);
    mImpl->audioDevices.clear();
}