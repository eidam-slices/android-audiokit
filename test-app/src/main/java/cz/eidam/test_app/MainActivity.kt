package cz.eidam.test_app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.*
import cz.eidam.test_app.ui.theme.KotlinaudiolibTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        setContent {
            KotlinaudiolibTheme {
                var showDualMode by remember { mutableStateOf(false) }

                if (showDualMode) {
                    DualAlgorithmScreen(
                        onBack = {
                            showDualMode = false
                        }
                    )
                } else {
                    SingleAlgorithm(
                        onOpenDualMode = {
                            showDualMode = true
                        }
                    )
                }
            }
        }
    }
}