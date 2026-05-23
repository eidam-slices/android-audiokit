package cz.eidam.test_app

import android.Manifest
import android.content.pm.PackageManager
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import cz.eidam.lib.Algorithm
import cz.eidam.lib.Engines
import cz.eidam.lib.EngineOptions
import cz.eidam.lib.FrequencyCallback
import java.util.Locale

@Composable
fun DualAlgorithmScreen(
    onBack: () -> Unit = {}
) {
    val context = LocalContext.current
    val availableAlgorithms = remember { Algorithm.entries }

    var primaryAlgorithm by remember { mutableStateOf(Algorithm.YIN_FFT) }
    var secondaryAlgorithm by remember { mutableStateOf(Algorithm.YIN) }
    var bufferSize by remember { mutableStateOf(8192) }
    var hopSize by remember { mutableStateOf(1024) }

    val bufferSizeSteps = remember { listOf(2048, 4096, 8192, 16384, 32768, 65536) }
    val hopSizeSteps = remember { listOf(256, 512, 1024, 2048, 4096, 8192) }

    var primaryFrequency by remember { mutableStateOf(0.0f) }
    var secondaryFrequency by remember { mutableStateOf(0.0f) }
    var primaryConfidence by remember { mutableStateOf(0.0f) }
    var secondaryConfidence by remember { mutableStateOf(0.0f) }
    var primaryRms by remember { mutableStateOf(0.0f) }
    var secondaryRms by remember { mutableStateOf(0.0f) }
    var isRunning by remember { mutableStateOf(false) }

    val engineA = remember { Engines.aubio(EngineOptions(algorithm = primaryAlgorithm, bufferSize = bufferSize, hopSize = hopSize)) }
    val engineB = remember { Engines.aubio(EngineOptions(algorithm = secondaryAlgorithm, bufferSize = bufferSize, hopSize = hopSize)) }

    val callbackA = remember {
        object : FrequencyCallback {
            override fun onFrequencyChanged(value: Float, confidence: Float, rms: Float) {
                primaryFrequency = value
                primaryConfidence = confidence
                primaryRms = rms
            }
        }
    }

    val callbackB = remember {
        object : FrequencyCallback {
            override fun onFrequencyChanged(value: Float, confidence: Float, rms: Float) {
                secondaryFrequency = value
                secondaryConfidence = confidence
                secondaryRms = rms
            }
        }
    }

    var hasPermission by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.RECORD_AUDIO
            ) == PackageManager.PERMISSION_GRANTED
        )
    }

    fun resetValues() {
        primaryFrequency = 0.0f
        secondaryFrequency = 0.0f
        primaryConfidence = 0.0f
        secondaryConfidence = 0.0f
        primaryRms = 0.0f
        secondaryRms = 0.0f
    }

    fun stopDual() {
        engineA.stop()
        engineB.stop()
        isRunning = false
        resetValues()
    }

    fun startDual() {
        val startedA = engineA.start { result ->
            primaryFrequency = result.frequencyHz
            primaryConfidence = result.confidence
            primaryRms = result.rms
        }
        val startedB = engineB.start { result ->
            secondaryFrequency = result.frequencyHz
            secondaryConfidence = result.confidence
            secondaryRms = result.rms
        }
        isRunning = startedA && startedB
    }

    fun syncNativeOptions() {
        // update Kotlin-side engine options and propagate to native instances
        engineA.updateOptions(EngineOptions(algorithm = primaryAlgorithm, bufferSize = bufferSize, hopSize = hopSize, confidenceThreshold = 0.0f, minInputDb = -70f))
        engineB.updateOptions(EngineOptions(algorithm = secondaryAlgorithm, bufferSize = bufferSize, hopSize = hopSize, confidenceThreshold = 0.0f, minInputDb = -70f))
    }

    fun nextAlgorithm(current: Algorithm): Algorithm {
        val currentIndex = availableAlgorithms.indexOf(current)
        return availableAlgorithms[(currentIndex + 1) % availableAlgorithms.size]
    }

    fun increaseBufferSize() {
        val nextBufferSize = bufferSizeSteps.firstOrNull { it > bufferSize } ?: return
        bufferSize = nextBufferSize
        syncNativeOptions()
    }

    fun decreaseBufferSize() {
        val previousBufferSize = bufferSizeSteps.lastOrNull { it < bufferSize } ?: return
        bufferSize = previousBufferSize
        if (hopSize >= bufferSize) {
            hopSize = hopSizeSteps.lastOrNull { it < bufferSize } ?: hopSize
        }
        syncNativeOptions()
    }

    fun increaseHopSize() {
        val nextHopSize = hopSizeSteps.firstOrNull { it > hopSize && it < bufferSize } ?: return
        hopSize = nextHopSize
        syncNativeOptions()
    }

    fun decreaseHopSize() {
        val previousHopSize = hopSizeSteps.lastOrNull { it < hopSize } ?: return
        hopSize = previousHopSize
        syncNativeOptions()
    }

    val launcher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission()
    ) { granted ->
        hasPermission = granted
        if (granted) {
            startDual()
        } else {
            Toast.makeText(context, "Oprávnění odmítnuto.", Toast.LENGTH_SHORT).show()
        }
    }

    Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            Text(text = "Dual algoritmy", fontSize = 22.sp)
            Text(
                text = "Srovnání dvou pitch algoritmů na jednom vstupu",
                fontSize = 13.sp,
                modifier = Modifier.padding(top = 4.dp, bottom = 8.dp)
            )

            Text(
                text = "Buffer size: $bufferSize · Hop size: $hopSize",
                fontSize = 13.sp,
                modifier = Modifier.padding(bottom = 8.dp)
            )

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = {
                    primaryAlgorithm = nextAlgorithm(primaryAlgorithm)
                    syncNativeOptions()
                }) {
                    Text("Alg A: ${primaryAlgorithm.name}")
                }
                Button(onClick = {
                    secondaryAlgorithm = nextAlgorithm(secondaryAlgorithm)
                    syncNativeOptions()
                }) {
                    Text("Alg B: ${secondaryAlgorithm.name}")
                }
            }

            Text(
                text = "A: ${String.format(Locale.US, "%.2f Hz", primaryFrequency)} · Confidence: ${String.format(Locale.US, "%.0f%%", primaryConfidence * 100f)}",
                fontSize = 15.sp,
                modifier = Modifier.padding(top = 16.dp, bottom = 4.dp)
            )
            Text(
                text = "B: ${String.format(Locale.US, "%.2f Hz", secondaryFrequency)} · Confidence: ${String.format(Locale.US, "%.0f%%", secondaryConfidence * 100f)}",
                fontSize = 15.sp,
                modifier = Modifier.padding(bottom = 8.dp)
            )

            Text(
                text = "RMS A: ${String.format(Locale.US, "%.5f", primaryRms)} · RMS B: ${String.format(Locale.US, "%.5f", secondaryRms)}",
                fontSize = 12.sp,
                modifier = Modifier.padding(bottom = 12.dp)
            )

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { decreaseBufferSize() }, enabled = bufferSizeSteps.any { it < bufferSize }) {
                    Text("Buffer -")
                }
                Button(onClick = { increaseBufferSize() }, enabled = bufferSizeSteps.any { it > bufferSize }) {
                    Text("Buffer +")
                }
            }

            Row(
                modifier = Modifier.padding(top = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(onClick = { decreaseHopSize() }, enabled = hopSizeSteps.any { it < hopSize }) {
                    Text("Hop -")
                }
                Button(onClick = { increaseHopSize() }, enabled = hopSizeSteps.any { it > hopSize && it < bufferSize }) {
                    Text("Hop +")
                }
            }

            Row(
                modifier = Modifier.padding(top = 16.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(
                    onClick = {
                        if (isRunning) {
                            stopDual()
                        } else {
                            if (hasPermission) {
                                startDual()
                            } else {
                                launcher.launch(Manifest.permission.RECORD_AUDIO)
                            }
                        }
                    }
                ) {
                    Text(if (isRunning) "Zastavit" else "Spustit")
                }

                Button(onClick = {
                    if (isRunning) stopDual()
                    onBack()
                }) {
                    Text("Zpět")
                }
            }

            Spacer(modifier = Modifier.padding(top = 8.dp))
        }
    }
}
