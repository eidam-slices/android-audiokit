package cz.eidam.lib

// ...existing code...

/**
 * Core API for audio pitch detection engines.
 *
 * Design goals:
 * - Small "core" module provides interfaces and result types.
 * - Pluggable engines (Aubio, others) implement [AudioEngine].
 * - Engines accept configuration (algorithm, buffer/hop sizes).
 * - Results are delivered via a lightweight callback (lambda) or by
 *   subclassing if a user prefers to implement their own engine.
 */

/** Result of a single analysis step. */
data class AnalysisResult(
    val frequencyHz: Float,
    val confidence: Float,
    /** RMS value for the analysed block. If engine doesn't provide it, NaN. */
    val rms: Float = Float.NaN,
    /** timestamp in millis when the result was produced (best-effort). */
    val timestampMs: Long = System.currentTimeMillis()
)

/** Simple alias for the callback that receives analysis results. */
typealias ResultListener = (AnalysisResult) -> Unit

/** Supported algorithms. Engines that don't support selection can ignore this. */
enum class Algorithm {
    YIN,
    YIN_FFT,
    SCHMITT,
    YIN_FAST,
    MCOMB,
    FCOMB,
    SPECACF,
    DEFAULT
}

/** Engine configuration (hints). Engines may choose to honour or ignore some fields. */
data class EngineOptions(
    val algorithm: Algorithm = Algorithm.YIN,
    val bufferSize: Int = 2048,
    val hopSize: Int = 512,
    /** Minimum confidence required before emitting a result. 0 = emit everything. */
    val confidenceThreshold: Float = 0.15f,
    /** Minimum input loudness in dBFS-ish scale before emitting a result. */
    val minInputDb: Float = -90f
)

/** Primary interface for audio engines. */
interface AudioEngine {
    /**
     * Start the engine and begin delivering results to [listener].
     *
     * Returns true when the engine started successfully, false otherwise.
     */
    fun start(listener: ResultListener): Boolean

    /** Stop the engine and release any native resources. */
    fun stop()

    /** Current options used by the engine. */
    val options: EngineOptions
    /** Update engine options at runtime (if supported). Returns true when request accepted. */
    fun updateOptions(options: EngineOptions): Boolean
}

/**
 * Small convenience factory for engines. Add other engines here in the future.
 */
object Engines {
    fun aubio(options: EngineOptions = EngineOptions()): AudioEngine = AubioEngine(options)
}

// --- Concrete Kotlin wrapper around the native Aubio implementation ---
/**
 * [AubioEngine] is a thin Kotlin-side wrapper that delegates to the native
 * implementation via [NativeLib]. It adapts the native callback shape into
 * [AnalysisResult] and keeps the callback object alive while running.
 */
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

    protected fun finalize() {
        try {
            NativeLib.destroyEngine(nativeHandle)
        } catch (_: Throwable) {
            // ignore
        }
    }
}

