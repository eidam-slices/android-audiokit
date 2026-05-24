package cz.eidam.test_app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.*
import androidx.compose.ui.tooling.preview.Devices
import cz.eidam.test_app.ui.theme.KotlinaudiolibTheme


enum class Screens {
    Single, Dual, Devices
}

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        setContent {
            KotlinaudiolibTheme {
                var screen by remember { mutableStateOf(Screens.Single) }

                when (screen) {
                    Screens.Single -> SingleAlgorithm(
                        onOpenDualMode = {
                            screen = Screens.Dual
                        },
                        onOpenDevices = {
                            screen = Screens.Devices
                        }
                    )
                    Screens.Dual -> DualAlgorithmScreen(
                        onBack = {
                            screen = Screens.Single
                        }
                    )

                    Screens.Devices -> Devices(
                        onBack = {
                            screen = Screens.Single
                        }
                    )

                }
            }
        }
    }
}