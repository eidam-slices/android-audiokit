#pragma once

#include "audio_source.h"
#include "result_listener.h"
#include <aubio.h>

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

class AudioEngine : public AudioConsumer {
public:
    AudioEngine(int algorithmId = 0, int bufferSize = 2048, int hopSize = 512, float confidenceThreshold = 0.15f, float minInputDb = -90.0f);
    ~AudioEngine();

    // AudioConsumer callback - called from audio source (audio thread)
    void pushSamples(const float* data, int32_t numFrames, int32_t channelCount) override;

    // Lifecycle - platform provides an IResultListener implementation
    bool start(IResultListener* listener);
    void stop();
    bool updateOptions(int algorithmId, int bufferSize, int hopSize, float confidenceThreshold, float minInputDb);

private:
    bool prepareAubio(uint_t sampleRate);
    void releaseAubio();
    void analysisLoop();

    // config
    std::mutex mConfigMutex;
    std::atomic<bool> mReconfigRequested{false};
    int mAlgorithmId;
    int mBufferSize;
    int mHopSize;
    float mConfidenceThreshold;
    float mMinInputDb;
    int mPendingAlgorithmId;
    int mPendingBufferSize;
    int mPendingHopSize;
    float mPendingConfidenceThreshold;
    float mPendingMinInputDb;

    // aubio
    aubio_pitch_t* mPitchDetector = nullptr;
    fvec_t* mPitchInput = nullptr;
    fvec_t* mPitchOutput = nullptr;
    int mSampleRate = 48000;

    // audio stream is managed by AudioSourceManager; engine keeps ring buffer
    static constexpr size_t kRingBufferSize = 16384;
    static constexpr size_t kRingBufferMask = kRingBufferSize - 1;
    std::array<float, kRingBufferSize> mRingBuffer{};
    std::atomic<size_t> mWriteIndex{0};
    std::atomic<size_t> mReadIndex{0};
    std::condition_variable mDataCv;
    std::mutex mDataMutex;

    // threading
    std::atomic<bool> mRunning{false};
    std::thread mAnalysisThread;

    // result listener (platform provided)
    IResultListener* mListener = nullptr;

    float mLastSentFrequency = 0.0f;
    std::chrono::steady_clock::time_point mLastSentAt{};
};