package cz.eidam.test_app

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Button
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import cz.eidam.lib.AnalysisResult
import cz.eidam.lib.Engines
import cz.eidam.lib.EngineOptions
import cz.eidam.lib.Algorithm
import cz.eidam.test_app.ui.theme.KotlinaudiolibTheme
import java.util.Locale

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        setContent {
            KotlinaudiolibTheme {
                val context = LocalContext.current

                // 1. INICIALIZACE engine (Aubio wrapper) a stavů UI
                var selectedAlgorithm by remember { mutableStateOf(Algorithm.YIN_FFT) }
                var bufferSize by remember { mutableStateOf(8192) }
                var hopSize by remember { mutableStateOf(1024) }
                val availableAlgorithms = remember { Algorithm.entries }
                val bufferSizeSteps = remember { listOf(2048, 4096, 8192, 16384, 32768, 65536) }
                val hopSizeSteps = remember { listOf(256, 512, 1024, 2048, 4096, 8192) }

                fun currentOptions() = EngineOptions(
                    algorithm = selectedAlgorithm,
                    bufferSize = bufferSize,
                    hopSize = hopSize,
                    confidenceThreshold = 0.0f,
                    minInputDb = -70f
                )

                val engine = remember { Engines.aubio(currentOptions()) }

                var frekvenceHz by remember { mutableStateOf(0.0f) }
                var confidence by remember { mutableStateOf(0.0f) }
                var rms by remember { mutableStateOf(0.0f) }
                var jeSpusteno by remember { mutableStateOf(false) }

                fun applyAlgorithm(newAlgorithm: Algorithm) {
                    selectedAlgorithm = newAlgorithm
                    engine.updateOptions(currentOptions())
                }

                fun increaseBufferSize() {
                    val nextBufferSize = bufferSizeSteps.firstOrNull { it > bufferSize } ?: return
                    bufferSize = nextBufferSize
                    engine.updateOptions(currentOptions())
                }

                fun decreaseBufferSize() {
                    val previousBufferSize = bufferSizeSteps.lastOrNull { it < bufferSize } ?: return
                    bufferSize = previousBufferSize
                    if (hopSize >= bufferSize) {
                        hopSize = hopSizeSteps.lastOrNull { it < bufferSize } ?: hopSize
                    }
                    engine.updateOptions(currentOptions())
                }

                fun increaseHopSize() {
                    val nextHopSize = hopSizeSteps.firstOrNull { it > hopSize && it < bufferSize } ?: return
                    hopSize = nextHopSize
                    engine.updateOptions(currentOptions())
                }

                fun decreaseHopSize() {
                    val previousHopSize = hopSizeSteps.lastOrNull { it < hopSize } ?: return
                    hopSize = previousHopSize
                    engine.updateOptions(currentOptions())
                }

                // jednoduchý listener, který engine zavolá z native callbacku
                val resultListener: (AnalysisResult) -> Unit = { result ->
                    frekvenceHz = result.frequencyHz
                    confidence = result.confidence
                    rms = result.rms
                }

                var maOpravneni by remember {
                    mutableStateOf(
                        ContextCompat.checkSelfPermission(
                            context,
                            Manifest.permission.RECORD_AUDIO
                        ) == PackageManager.PERMISSION_GRANTED
                    )
                }

                val launcherOpravneni = rememberLauncherForActivityResult(
                    contract = ActivityResultContracts.RequestPermission()
                ) { schvaleno ->
                    maOpravneni = schvaleno
                    if (schvaleno) {
                        val started = engine.start(resultListener)
                        if (started) jeSpusteno = true
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
                        Text(text = "Detekovaná frekvence z mikrofonu:", fontSize = 16.sp)
                        Text(
                            text = "Algoritmus: ${selectedAlgorithm.name}",
                            fontSize = 14.sp,
                            modifier = Modifier.padding(top = 4.dp, bottom = 8.dp)
                        )

                        Text(
                            text = "Buffer size: $bufferSize · Hop size: $hopSize",
                            fontSize = 13.sp,
                            modifier = Modifier.padding(bottom = 8.dp)
                        )

                        Button(
                            onClick = {
                                val currentIndex = availableAlgorithms.indexOf(selectedAlgorithm)
                                val next = availableAlgorithms[(currentIndex + 1) % availableAlgorithms.size]
                                applyAlgorithm(next)
                            }
                        ) {
                            Text("Přepnout algoritmus")
                        }

                        Row(
                            modifier = Modifier.padding(top = 8.dp),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Button(
                                onClick = { decreaseBufferSize() },
                                enabled = bufferSizeSteps.any { it < bufferSize }
                            ) {
                                Text("Buffer -")
                            }

                            Button(
                                onClick = { increaseBufferSize() },
                                enabled = bufferSizeSteps.any { it > bufferSize }
                            ) {
                                Text("Buffer +")
                            }
                        }

                        Row(
                            modifier = Modifier.padding(top = 8.dp),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Button(
                                onClick = { decreaseHopSize() },
                                enabled = hopSizeSteps.any { it < hopSize }
                            ) {
                                Text("Hop -")
                            }

                            Button(
                                onClick = { increaseHopSize() },
                                enabled = hopSizeSteps.any { it > hopSize && it < bufferSize }
                            ) {
                                Text("Hop +")
                            }
                        }

                        Text(
                            text = when (selectedAlgorithm) {
                                Algorithm.YIN_FFT -> "YIN_FFT: často funguje dobře pro basy, ale confidence může být 0."
                                Algorithm.SCHMITT, Algorithm.MCOMB, Algorithm.FCOMB -> "Tento algoritmus nemusí mít smysluplnou confidence hodnotu."
                                else -> ""
                            },
                            fontSize = 12.sp,
                            modifier = Modifier.padding(top = 4.dp, bottom = 8.dp)
                        )

                        Text(
                            text = if (frekvenceHz > 0f) String.format(Locale.US, "%.2f Hz", frekvenceHz) else "—",
                            fontSize = 42.sp,
                            modifier = Modifier.padding(vertical = 12.dp)
                        )

                        // Confidence a RMS (dB)
                        Text(
                            text = if (frekvenceHz > 0f) {
                                val confText = if (confidence >= 0f) String.format(Locale.US, "Confidence: %.0f%%", confidence * 100f) else String.format(Locale.US, "Confidence: %.3f", confidence)
                                val db = if (rms.isFinite() && rms > 0f) 20.0 * kotlin.math.log10(rms.toDouble()) else Double.NEGATIVE_INFINITY
                                val dbText = if (db.isFinite()) String.format(Locale.US, "Loudness: %.1f dB", db) else "Loudness: —"
                                "$confText · $dbText"
                            } else "",
                            fontSize = 14.sp,
                        )

                        if (!jeSpusteno) {
                            Button(
                                onClick = {
                                    if (maOpravneni) {
                                        val started = engine.start(resultListener)
                                        if (started) jeSpusteno = true
                                    } else {
                                        launcherOpravneni.launch(Manifest.permission.RECORD_AUDIO)
                                    }
                                }
                            ) {
                                Text("Spustit nahrávání")
                            }
                        } else {
                            Button(
                                onClick = {
                                    engine.stop()
                                    jeSpusteno = false
                                    frekvenceHz = 0.0f
                                    confidence = 0.0f
                                    rms = 0.0f
                                }
                            ) {
                                Text("Zastavit nahrávání")
                            }
                        }
                    }
                }
            }
        }
    }
}