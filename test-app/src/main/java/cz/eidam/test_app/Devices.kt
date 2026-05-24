package cz.eidam.test_app

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import cz.eidam.lib.AudioDevice
import cz.eidam.lib.NativeLib

@Composable
fun Devices(
    onBack: () -> Unit = {},
) {
    val context = LocalContext.current

    var devices by remember { mutableStateOf(emptyList<AudioDevice>()) }

    Scaffold(
        modifier = Modifier.fillMaxSize(),
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(it),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {


            Text("Devices", Modifier.padding(12.dp), style = MaterialTheme.typography.headlineSmall)

            Button({
                val loaded = NativeLib.getAvailableDevices(context)
//                devices = getAndroidInputDevices(context)
                devices = loaded

            }) { Text("Load devices") }

            Column {
                devices.forEach {
                    ListItem(
                        headlineContent = {
                            Text(it.name)
                        },
                        supportingContent = {
                            Text("ID: ${it.id}")
                        }
                    )

                }
            }


            Spacer(modifier = Modifier.weight(1f))
            Button(onBack, Modifier.padding(12.dp)) {
                Text("Back")
            }
        }

    }
}

private fun getAndroidInputDevices(context: Context): List<AudioDevice> {
    val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager

    // Filtrujeme POUZE vstupní hardwarová zařízení (mikrofony, linky)
    val deviceInfos = audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS)

    deviceInfos.forEach { info ->
        println(

            buildString {
                append("adress: ")
                appendLine(info.address)
                append("type: ")
                appendLine(info.type)
                append("isSource: ")
                appendLine(info.isSource)
                append("productName: ")
                appendLine(info.productName)
                append("id: ")
                appendLine(info.id)
                appendLine()
                appendLine()
                appendLine("------------")
            }
        )

    }

    return deviceInfos.map { info ->
        val friendlyName = when (info.type) {
            AudioDeviceInfo.TYPE_BUILTIN_MIC -> "Vestavěný mikrofon"
            AudioDeviceInfo.TYPE_WIRED_HEADSET -> "Wired Headset (Jack)"
            AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "Bluetooth Headset (SCO)"
            AudioDeviceInfo.TYPE_BLUETOOTH_A2DP -> "Bluetooth LE Mikrofon"
            AudioDeviceInfo.TYPE_USB_DEVICE -> "USB Zvukovka / Vstup"
            AudioDeviceInfo.TYPE_USB_HEADSET -> "USB Headset"
            AudioDeviceInfo.TYPE_DOCK -> "USB Dock / Audio rozhraní (AirPods)"
            AudioDeviceInfo.TYPE_LINE_ANALOG -> "Line-in Analogový vstup"
            else -> {
                // Záložní řešení: Pokud typ v switchi nemáme, vezmeme produktové jméno
                val prodName = info.productName?.toString()
                if (!prodName.isNullOrEmpty()) {
                    prodName
                } else {
                    "Externí mikrofon (Typ ${info.type})"
                }
            }
        }

        // Zabalíme do tvého stávajícího modelu cz.eidam.lib.AudioDevice
        AudioDevice(id = info.id, name = friendlyName)
    }
}