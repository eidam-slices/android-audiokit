package cz.eidam.lib

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

class AudioRecorder {

    // MutableStateFlow drží aktuální hodnotu a automaticky upozorňuje Compose UI na změny
    private val _frequencyFlow = MutableStateFlow(0.0f)
    val frequencyFlow: StateFlow<Float> = _frequencyFlow.asStateFlow()
    // confidence z Aubio (0..1 může být i záporné při neurčitosti)
    private val _confidenceFlow = MutableStateFlow(0.0f)
    val confidenceFlow: StateFlow<Float> = _confidenceFlow.asStateFlow()
    // RMS z analyzovaného bloku
    private val _rmsFlow = MutableStateFlow(0.0f)
    val rmsFlow: StateFlow<Float> = _rmsFlow.asStateFlow()

    // Sledování stavu, zda engine zrovna běží
    private val _isRecording = MutableStateFlow(false)
    val isRecording: StateFlow<Boolean> = _isRecording.asStateFlow()

    // Implementace callbacku, kterou předáme do C++
    private val frequencyCallback = object : FrequencyCallback {
        override fun onFrequencyChanged(value: Float, confidence: Float, rms: Float) {
            // C++ volá tuto metodu z nativního audio vlákna.
            // StateFlow je thread-safe, takže můžeme hodnoty bezpečně zapsat.
            _frequencyFlow.value = value
            _confidenceFlow.value = confidence
            _rmsFlow.value = rms
        }
    }

    /**
     * Nastartuje Oboe audio engine na pozadí.
     * @return true, pokud se mikrofon úspěšně spustil.
     */
    fun start(): Boolean {
        if (_isRecording.value) return true // Už běží

        // start native engine with sane defaults (algorithm YIN, buffer 2048, hop 512)
        val success = NativeLib.start(Algorithm.YIN.ordinal, 2048, 512, 0.15f, -90f, frequencyCallback)
        if (success) {
            _isRecording.value = true
        }
        return success
    }

    /**
     * Zastaví Oboe stream a uvolní JNI reference.
     */
    fun stop() {
        if (!_isRecording.value) return

        NativeLib.stop()
        _isRecording.value = false
        _frequencyFlow.value = 0.0f // Resetujeme hodnotu na nulu
        _confidenceFlow.value = 0.0f
        _rmsFlow.value = 0.0f
    }
}