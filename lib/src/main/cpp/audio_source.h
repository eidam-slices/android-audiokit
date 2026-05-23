#pragma once

#include <vector>
#include <mutex>

// Forward declare
class AudioConsumer {
public:
    virtual ~AudioConsumer() = default;
    // Called from audio callback thread - must be lock-free/fast. Implementations should enqueue samples.
    virtual void pushSamples(const float* data, int32_t numFrames, int32_t channelCount) = 0;
};

class AudioSourceManager {
public:
    static AudioSourceManager& instance();

    // register/unregister consumers
    void addConsumer(AudioConsumer* consumer);
    void removeConsumer(AudioConsumer* consumer);

private:
    AudioSourceManager();
    ~AudioSourceManager();

    // non-copyable
    AudioSourceManager(const AudioSourceManager&) = delete;
    AudioSourceManager& operator=(const AudioSourceManager&) = delete;

    struct Impl;
    Impl* mImpl{nullptr};
};
