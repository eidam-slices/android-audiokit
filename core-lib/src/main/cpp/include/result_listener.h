#pragma once

#include <cstdint>

struct AnalysisResult {
    float frequencyHz;
    float confidence;
    float rms;
    int64_t timestampMs;
};

class IResultListener {
public:
    virtual ~IResultListener() = default;
    virtual void onAnalysisResult(const AnalysisResult& result) = 0;
};
