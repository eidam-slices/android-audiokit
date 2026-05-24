#include "audio_source.h"
#include "RtAudio.h"

#include <algorithm>
#include <vector>
#include <mutex>
#include <cstdio>
#include <cmath>
#include "../../../../core-lib/src/main/cpp/include/audio_source.h"
// Desktop audio source using RtAudio only.

struct AudioSourceManager::Impl {
    std::vector<AudioConsumer*> consumers;
    std::mutex mtx;

    RtAudio dac;
    RtAudio::StreamParameters inputParams;
    bool streamOpen = false;

    // -1 znamená: použij výchozí systémové zařízení (Default)
    int32_t currentDeviceId = -1;

    Impl() {
        if (!initRtAudio()) {
            fprintf(stderr, "RtAudio init failed\n");
        }
    }

    ~Impl() {
        closeStream();
    }

    // Pomocná metoda pro bezpečné zavření streamu před restartem
    void closeStream() {
        if (streamOpen) {
            try {
                if (dac.isStreamRunning()) dac.stopStream();
                if (dac.isStreamOpen()) dac.closeStream();
            } catch (...) {
                fprintf(stderr, "Error closing RtAudio stream\n");
            }
            streamOpen = false;
        }
    }

    bool initRtAudio() {
        if (dac.getDeviceCount() < 1) {
            fprintf(stderr, "No audio devices found\n");
            return false;
        }

        // POKUD JE NASTAVENO ID, POUŽIJEME HO. JINAK VEZMEME DEFAULT.
        if (currentDeviceId >= 0) {
            inputParams.deviceId = static_cast<unsigned int>(currentDeviceId);
        } else {
            inputParams.deviceId = dac.getDefaultInputDevice();
        }

        inputParams.nChannels = 1; // mono input
        inputParams.firstChannel = 0;

        // Query device info and try preferred/supported sample rates.
        RtAudio::DeviceInfo info = dac.getDeviceInfo(inputParams.deviceId);
        fprintf(stderr, "RtAudio device id=%u name=%s\n", inputParams.deviceId, info.name.c_str());
        if (!info.sampleRates.empty()) {
            fprintf(stderr, "RtAudio supported sample rates:");
            for (auto r : info.sampleRates) fprintf(stderr, " %u", r);
            fprintf(stderr, "\n");
        }
        if (info.preferredSampleRate > 0) {
            fprintf(stderr, "RtAudio preferred sample rate: %u\n", info.preferredSampleRate);
        }

        std::vector<unsigned int> candidates;
        if (info.preferredSampleRate > 0) candidates.push_back(info.preferredSampleRate);
        for (auto r : info.sampleRates) candidates.push_back(r);
        unsigned int common[] = {48000, 44100, 96000, 32000, 22050, 16000};
        for (auto r : common) {
            if (std::find(candidates.begin(), candidates.end(), r) == candidates.end()) candidates.push_back(r);
        }

        unsigned int bufferFrames = 512;
        for (auto rate : candidates) {
            RtAudioErrorType err = dac.openStream(nullptr, &inputParams, RTAUDIO_FLOAT32, rate, &bufferFrames, &rtAudioCallback, (void*)this);
            if (err == RTAUDIO_NO_ERROR) {
                streamOpen = true;
                fprintf(stderr, "RtAudio initialized (device: %u, channels: %u, sample rate: %u Hz, bufferFrames: %u)\n",
                        inputParams.deviceId, inputParams.nChannels, rate, bufferFrames);
                return true;
            } else {
                fprintf(stderr, "RtAudio openStream failed for rate %u: %s\n", rate, dac.getErrorText().c_str());
            }
        }

        fprintf(stderr, "RtAudio error during init - all candidate sample rates failed\n");
        return false;
    }

    static int rtAudioCallback(void* outputBuffer, void* inputBuffer, unsigned int nFrames,
                               double streamTime, RtAudioStreamStatus status, void* userData) {
        (void)outputBuffer;
        (void)streamTime;
        if (status) fprintf(stderr, "RtAudio stream status: %i\n", status);

        Impl* self = static_cast<Impl*>(userData);
        if (!self || !inputBuffer) return 0;

        float* input = static_cast<float*>(inputBuffer);

        {
            std::lock_guard<std::mutex> lock(self->mtx);
            for (auto* c : self->consumers) {
                if (c) c->pushSamples(input, nFrames, 1);
            }
        }

        float rmsSum = 0.0f;
        for (unsigned int i = 0; i < nFrames; ++i) {
            rmsSum += input[i] * input[i];
        }
        float rms = std::sqrt(rmsSum / nFrames);

        static int callCount = 0;
        if (++callCount >= 50) {
            callCount = 0;
            float dbVal = 20.0f * std::log10(std::max(rms, 1e-6f));
            fprintf(stderr, "[RtAudio] RMS: %.4f (%.1f dB)\n", rms, dbVal);
        }

        return 0;
    }

    void startStream() {
        if (streamOpen && !dac.isStreamRunning()) {
            try {
                dac.startStream();
                fprintf(stderr, "RtAudio stream started\n");
            } catch (...) {
                fprintf(stderr, "Error starting stream\n");
            }
        }
    }

    void stopStream() {
        if (streamOpen && dac.isStreamRunning()) {
            try {
                dac.stopStream();
                fprintf(stderr, "RtAudio stream stopped\n");
            } catch (...) {
                fprintf(stderr, "Error stopping stream\n");
            }
        }
    }

    void addConsumer(AudioConsumer* c) {
        std::lock_guard<std::mutex> lock(mtx);
        consumers.push_back(c);
        startStream();
    }

    void removeConsumer(AudioConsumer* c) {
        std::lock_guard<std::mutex> lock(mtx);
        consumers.erase(std::remove(consumers.begin(), consumers.end(), c), consumers.end());
        if (consumers.empty()) stopStream();
    }

    // NOVÁ IMPLEMENTACE METODY PRO ZMĚNU ZAŘÍZENÍ UVNITŘ IMPL
    void setDeviceId(int32_t deviceId) {
        std::lock_guard<std::mutex> lock(mtx);
        if (currentDeviceId == deviceId) return;

        currentDeviceId = deviceId;

        // Pokud stream běží, bezpečně ho restartujeme s novým ID
        if (streamOpen) {
            bool wasRunning = dac.isStreamRunning();

            // Dočasně uvolníme zámek na stopnutí a re-init, abychom neriskovali deadlock s callbackem
            mtx.unlock();
            closeStream();
            bool ok = initRtAudio();
            mtx.lock();

            if (ok && wasRunning && !consumers.empty()) {
                startStream();
            }
        }
    }
};

// Implement the DesktopAudioSourceManager API expected by core.

AudioSourceManager& AudioSourceManager::instance() {
    static AudioSourceManager inst;
    return inst;
}

AudioSourceManager::AudioSourceManager() : mImpl(new Impl()) {}
AudioSourceManager::~AudioSourceManager() { delete mImpl; }

void AudioSourceManager::addConsumer(AudioConsumer* consumer) { mImpl->addConsumer(consumer); }
void AudioSourceManager::removeConsumer(AudioConsumer* consumer) { mImpl->removeConsumer(consumer); }

// PROPOJENÍ NATIVNÍHO API S IMPL PRO ZMĚNU ID
void AudioSourceManager::setDeviceId(int32_t deviceId) {
    mImpl->setDeviceId(deviceId);
}

std::vector<AudioDevice> AudioSourceManager::getAvailableDevices() {
    std::vector<AudioDevice> list;
    RtAudio dac;

    // V RtAudio v6 získáme vektor reálných ID přímo od systému
    std::vector<unsigned int> deviceIds = dac.getDeviceIds();

    for (unsigned int id : deviceIds) {
        try {
            RtAudio::DeviceInfo info = dac.getDeviceInfo(id);
            // Zajímá nás pouze zařízení, které má alespoň 1 vstupní kanál
            if (info.inputChannels > 0) {
                AudioDevice dev;
                dev.id = static_cast<int32_t>(id); // Uložíme reálné systémové ID
                dev.name = info.name;

                if (info.isDefaultInput) {
                    dev.name += " (Výchozí)";
                }

                list.push_back(dev);
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "Exception querying device %u: %s\n", id, e.what());
        } catch (...) {
            fprintf(stderr, "Unknown error querying device %u\n", id);
        }
    }
    return list;
}
void AudioSourceManager::registerAndroidDevice(int32_t id, const std::string& name) {}
void AudioSourceManager::clearAndroidDevices() {}