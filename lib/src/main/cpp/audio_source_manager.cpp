#include "audio_source.h"

#include <oboe/Oboe.h>
#include <android/log.h>
#include <memory>
#include <algorithm>

#define LOG_TAG "AudioSourceMgr"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace oboe;

struct AudioSourceManager::Impl : public oboe::AudioStreamCallback {
    Impl() = default;
    ~Impl() { stop(); }

    std::shared_ptr<oboe::AudioStream> mStream = nullptr;
    std::vector<AudioConsumer*> mConsumers;
    std::mutex mConsumersMutex;
    int mHopSize = 512;

    DataCallbackResult onAudioReady(AudioStream* stream, void* audioData, int32_t numFrames) override {
        auto* floatData = static_cast<const float*>(audioData);
        if (floatData == nullptr) return DataCallbackResult::Continue;

        // Forward samples to registered consumers. pushSamples must be fast/non-blocking.
        std::lock_guard<std::mutex> lock(mConsumersMutex);
        for (auto* c : mConsumers) {
            if (c) c->pushSamples(floatData, numFrames, stream->getChannelCount());
        }
        return DataCallbackResult::Continue;
    }

    bool startIfNeeded() {
        if (mStream != nullptr) return true;

        LOGI("Starting Oboe source...");
        AudioStreamBuilder builder;
        builder.setDirection(Direction::Input)
               ->setPerformanceMode(PerformanceMode::LowLatency)
               ->setSharingMode(SharingMode::Shared)
               ->setFormat(AudioFormat::Float)
               ->setChannelCount(ChannelCount::Mono)
               ->setFramesPerCallback(static_cast<int32_t>(mHopSize))
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
};

AudioSourceManager& AudioSourceManager::instance() {
    static AudioSourceManager mgr;
    return mgr;
}

AudioSourceManager::AudioSourceManager() : mImpl(new Impl()) {}
AudioSourceManager::~AudioSourceManager() { delete mImpl; mImpl = nullptr; }

void AudioSourceManager::addConsumer(AudioConsumer* consumer) {
    if (!consumer) return;
    {
        std::lock_guard<std::mutex> lock(mImpl->mConsumersMutex);
        auto it = std::find(mImpl->mConsumers.begin(), mImpl->mConsumers.end(), consumer);
        if (it == mImpl->mConsumers.end()) mImpl->mConsumers.push_back(consumer);
    }
    mImpl->startIfNeeded();
}

void AudioSourceManager::removeConsumer(AudioConsumer* consumer) {
    if (!consumer) return;
    {
        std::lock_guard<std::mutex> lock(mImpl->mConsumersMutex);
        auto it = std::find(mImpl->mConsumers.begin(), mImpl->mConsumers.end(), consumer);
        if (it != mImpl->mConsumers.end()) mImpl->mConsumers.erase(it);
    }
    // if no consumers, stop stream
    {
        std::lock_guard<std::mutex> lock(mImpl->mConsumersMutex);
        if (mImpl->mConsumers.empty()) {
            mImpl->stop();
        }
    }
}
