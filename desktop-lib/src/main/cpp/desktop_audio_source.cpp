#include "audio_source.h"
#include "RtAudio.h"

#include <algorithm>
#include <vector>
#include <mutex>
#include <cstdio>
#include <cmath>
// Desktop audio source using RtAudio only.

struct AudioSourceManager::Impl {
    std::vector<AudioConsumer*> consumers;
    std::mutex mtx;

    RtAudio dac;
    RtAudio::StreamParameters inputParams;
    bool streamOpen = false;

    Impl() {
        if (!initRtAudio()) {
            fprintf(stderr, "RtAudio init failed\n");
        }
    }

    ~Impl() {
        if (streamOpen) {
            try {
                if (dac.isStreamRunning()) dac.stopStream();
                if (dac.isStreamOpen()) dac.closeStream();
            } catch (...) {
                fprintf(stderr, "Error closing RtAudio stream\n");
            }
        }
    }

    bool initRtAudio() {
        if (dac.getDeviceCount() < 1) {
            fprintf(stderr, "No audio devices found\n");
            return false;
        }

        inputParams.deviceId = dac.getDefaultInputDevice();
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
                // Log error text and try next candidate
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
        
        // Push samples to registered consumers
        {
            std::lock_guard<std::mutex> lock(self->mtx);
            for (auto* c : self->consumers) {
                if (c) c->pushSamples(input, nFrames, 1);
            }
        }
        
        // Compute and log RMS for debugging
        float rmsSum = 0.0f;
        for (unsigned int i = 0; i < nFrames; ++i) {
            rmsSum += input[i] * input[i];
        }
        float rms = std::sqrt(rmsSum / nFrames);
        
        // Log every 50 callbacks (~500ms at 48kHz, 512 buffers)
        static int callCount = 0;
        if (++callCount >= 50) {
            callCount = 0;
            float dbVal = 20.0f * std::log10(std::max(rms, 1e-6f));
            fprintf(stderr, "[RtAudio] RMS: %.4f (%.1f dB)\n", rms, dbVal);
        }
        
        return 0; // continue stream
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