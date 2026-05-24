package cz.eidam.lib

class AubioEngine(override var options: EngineOptions = EngineOptions()) : AudioEngine {
    private var listener: ResultListener? = null
    private var nativeCallbackRef: FrequencyCallback? = null
    private val nativeHandle: Long = NativeLib.createEngine(options.algorithm.ordinal, options.bufferSize, options.hopSize, options.confidenceThreshold, options.minInputDb)

    override fun start(listener: ResultListener): Boolean {
        this.listener = listener
        val cb = object : FrequencyCallback {
            override fun onFrequencyChanged(value: Float, confidence: Float, rms: Float) {
                val result = AnalysisResult(frequencyHz = value, confidence = confidence, rms = rms)
                this@AubioEngine.listener?.invoke(result)
            }
        }
        nativeCallbackRef = cb
        return try {
            NativeLib.engineStart(nativeHandle, cb)
        } catch (_: Throwable) {
            nativeCallbackRef = null
            false
        }
    }

    override fun stop() {
        try {
            NativeLib.engineStop(nativeHandle)
        } finally {
            nativeCallbackRef = null
            listener = null
        }
    }

    override fun updateOptions(options: EngineOptions): Boolean {
        this.options = options
        return try {
            NativeLib.engineUpdateOptions(nativeHandle, options.algorithm.ordinal, options.bufferSize, options.hopSize, options.confidenceThreshold, options.minInputDb)
        } catch (_: Throwable) {
            false
        }
    }

    fun destroy() {
        try {
            NativeLib.destroyEngine(nativeHandle)
        } catch (_: Throwable) {
            // ignore
        }
    }
}
