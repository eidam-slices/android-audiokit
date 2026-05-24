// Shared audio source / consumer interface for core and platform modules
#pragma once

#include <cstdint>
#include <string>
#include <vector>



struct AudioDevice {
    int32_t id;
    std::string name;
};

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

    // práce s přepínáním a získávání vstupů
    void setDeviceId(int32_t deviceId);
    std::vector<AudioDevice> getAvailableDevices();

    void registerAndroidDevice(int32_t id, const std::string& name);
    void clearAndroidDevices();

private:
    AudioSourceManager();
    ~AudioSourceManager();
    struct Impl;
    Impl* mImpl = nullptr;
};
