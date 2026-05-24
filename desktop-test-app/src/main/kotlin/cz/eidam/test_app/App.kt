package cz.eidam.test_app

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import cz.eidam.lib.AnalysisResult
import cz.eidam.lib.AubioEngine

@Composable
fun App() {
    var running by remember { mutableStateOf(false) }
    var lastResult by remember { mutableStateOf<AnalysisResult?>(null) }
    var logMessages by remember { mutableStateOf(listOf<String>()) }
    var engine by remember { mutableStateOf<AubioEngine?>(null) }

    LaunchedEffect(Unit) {
        println("[App] Initializing DesktopAudioEngine...")
        engine = AubioEngine()
        logMessages = logMessages + "[App] Engine initialized"
    }

    DisposableEffect(Unit) {
        onDispose {
            if (running) {
                engine?.stop()
            }
            engine?.destroy()
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFFF5F5F5))
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = "Desktop Audio Detection",
            fontSize = 24.sp,
            modifier = Modifier.padding(bottom = 16.dp)
        )

        // Control buttons
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(bottom = 16.dp),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Button(
                onClick = {
                    if (!running && engine != null) {
                        running = engine!!.start { result ->
                            lastResult = result
                            @Suppress("DefaultLocale")
                            val msg = String.format(
                                "[%.1f Hz, conf=%.2f, RMS=%.4f]",
                                result.frequencyHz,
                                result.confidence,
                                result.rms
                            )
                            logMessages = (logMessages + msg).takeLast(30)
                        }
                        if (running) {
                            logMessages = logMessages + "[App] Engine started"
                        } else {
                            logMessages = logMessages + "[App] Failed to start engine"
                        }
                    }
                },
                enabled = !running && engine != null
            ) {
                Text("Start")
            }

            Spacer(modifier = Modifier.width(8.dp))

            Button(
                onClick = {
                    if (running && engine != null) {
                        engine!!.stop()
                        running = false
                        logMessages = logMessages + "[App] Engine stopped"
                    }
                },
                enabled = running
            ) {
                Text("Stop")
            }

            Spacer(modifier = Modifier.width(8.dp))

            Button(
                onClick = {
                    logMessages = emptyList()
                }
            ) {
                Text("Clear")
            }
        }

        // Display last result
        if (lastResult != null) {
            Card(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(bottom = 16.dp)
            ) {
                Column(
                    modifier = Modifier.padding(16.dp)
                ) {
                    Text("Last Result:", fontSize = 14.sp)
                    Text(
                        text = String.format(
                            "Frequency: %.2f Hz\nConfidence: %.2f\nRMS: %.4f",
                            lastResult!!.frequencyHz,
                            lastResult!!.confidence,
                            lastResult!!.rms
                        ),
                        fontSize = 12.sp,
                        modifier = Modifier.padding(top = 8.dp)
                    )
                }
            }
        }

        // Log output
        Text(text = "Activity Log", fontSize = 14.sp, modifier = Modifier.padding(bottom = 8.dp))
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .background(Color.White),
            tonalElevation = 1.dp
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
                    .padding(8.dp)
            ) {
                logMessages.forEach { msg ->
                    Text(
                        text = msg,
                        fontSize = 10.sp,
                        modifier = Modifier.padding(vertical = 2.dp),
                        color = Color.DarkGray
                    )
                }
            }
        }

        // Status bar
        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = if (running) "✓ Running" else "○ Stopped",
            fontSize = 12.sp,
            color = if (running) Color.Green else Color.Red
        )
    }
}