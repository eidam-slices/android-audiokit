package cz.eidam.lib

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

/** Supported algorithms. */
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

/** Engine configuration (hints). */
data class EngineOptions(
    val algorithm: Algorithm = Algorithm.YIN_FFT,
    val bufferSize: Int = 8192,
    val hopSize: Int = 1024,
    val confidenceThreshold: Float = 0.0f,
    val minInputDb: Float = -90f
)

/** Primary interface for audio engines. */
interface AudioEngine {
    fun start(listener: ResultListener): Boolean
    fun stop()
    val options: EngineOptions
    fun updateOptions(options: EngineOptions): Boolean
}
