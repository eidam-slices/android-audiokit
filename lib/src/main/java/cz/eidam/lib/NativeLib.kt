package cz.eidam.lib

@Suppress("unused")
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


    // Instance-based engine API
    external fun createEngine(
        algorithmId: Int,
        bufferSize: Int,
        hopSize: Int,
        confidenceThreshold: Float,
        minInputDb: Float
    ): Long

    external fun destroyEngine(handle: Long)

    external fun engineStart(handle: Long, callback: FrequencyCallback): Boolean

    external fun engineStop(handle: Long)

    external fun engineUpdateOptions(
        handle: Long,
        algorithmId: Int,
        bufferSize: Int,
        hopSize: Int,
        confidenceThreshold: Float,
        minInputDb: Float
    ): Boolean
}

@Suppress("unused")
interface FrequencyCallback {
    // nyní předáváme frekvenci, confidence (0..1) a rms
    fun onFrequencyChanged(value: Float, confidence: Float, rms: Float)
}
 