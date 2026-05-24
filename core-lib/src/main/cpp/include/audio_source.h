// Shared audio source / consumer interface for core and platform modules
#pragma once

#include <cstdint>

class AudioConsumer {
public:
    virtual ~AudioConsumer() = default;
    // pushSamples is called from audio input callback; should be non-blocking
    virtual void pushSamples(const float* data, int32_t numFrames, int32_t channelCount) = 0;
};

class AudioSourceManager {
public:
    static AudioSourceManager& instance();
    void addConsumer(AudioConsumer* consumer);
    void removeConsumer(AudioConsumer* consumer);

private:
    AudioSourceManager();
    ~AudioSourceManager();
    struct Impl;
    Impl* mImpl = nullptr;
};
