package cz.eidam.lib

object NativeLib {
    init {
        System.loadLibrary("audiokit")
    }

    // start with explicit engine options: algorithmId (ordinal of Algorithm), bufferSize, hopSize,
    // confidenceThreshold and minInputDb
    external fun start(
        algorithmId: Int,
        bufferSize: Int,
        hopSize: Int,
        confidenceThreshold: Float,
        minInputDb: Float,
        callback: FrequencyCallback
    ): Boolean
    external fun stop()
    external fun updateOptions(
        algorithmId: Int,
        bufferSize: Int,
        hopSize: Int,
        confidenceThreshold: Float,
        minInputDb: Float
    ): Boolean
}

interface FrequencyCallback {
    // nyní předáváme frekvenci, confidence (0..1) a rms
    fun onFrequencyChanged(value: Float, confidence: Float, rms: Float)
}