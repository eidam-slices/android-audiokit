package cz.eidam.lib

import java.nio.file.Files
import java.nio.file.StandardCopyOption

@Suppress("unused")
object NativeLib {
    init {
        // Try standard JNI lookup first (Android or if java.library.path is set)
        try {
            System.loadLibrary("audiokit")
            println("loaded locally")
        } catch (e: UnsatisfiedLinkError) {
            // Fallback: load from JAR resources (desktop usage)
            println("loading from jar resources")
            JarLibLoader.loadLibrary("audiokit")
        }
    }

    external fun createEngine(
        algorithmId: Int,
        bufferSize: Int,
        hopSize: Int,
        confidenceThreshold: Float,
        minInputDb: Float
    ): Long

    external fun destroyEngine(handle: Long)

    external fun engineStart(handle: Long, callback: FrequencyCallback): Boolean

    external fun engineStop(handle: Long)

    external fun engineUpdateOptions(
        handle: Long,
        algorithmId: Int,
        bufferSize: Int,
        hopSize: Int,
        confidenceThreshold: Float,
        minInputDb: Float
    ): Boolean

    // =========================================================================
    // NOVÉ NATIVNÍ METODY PRO ZAŘÍZENÍ
    // =========================================================================

    // Nativní metoda, která z C++ vrátí pole stringů ve formátu ["0|Mikrofon", "1|Sluchatka"]
    private external fun nativeGetDevices(context: Any?): Array<String>

    // Nativní metoda pro předání zvoleného ID dolů do Oboe / RtAudio
    private external fun nativeSetDeviceId(deviceId: Int)

    // =========================================================================
    // VEŘEJNÉ KOTLIN API PRO TVŮJ ENGINE
    // =========================================================================

    /**
     * Vrátí seznam dostupných mikrofonů a vstupů.
     * Na Androidu předávej android.content.Context (např. applicationContext).
     * Na Desktopu volej bez parametru (defaultně se předá null).
     */
    fun getAvailableDevices(context: Any? = null): List<AudioDevice> {
        return try {
            nativeGetDevices(context).map { rawEntry ->
                val parts = rawEntry.split("|", limit = 2)
                val id = parts[0].toInt()
                val name = parts.getOrNull(1) ?: "Neznámé zařízení"
                AudioDevice(id, name)
            }
        } catch (e: Exception) {
            e.printStackTrace()
            emptyList()
        }
    }

    /**
     * Přepne aktivní vstupní zařízení (mikrofon) podle jeho ID.
     */
    fun setAudioInputDevice(deviceId: Int) {
        nativeSetDeviceId(deviceId)
    }
}

@Suppress("unused")
interface FrequencyCallback {
    fun onFrequencyChanged(value: Float, confidence: Float, rms: Float)
}

object JarLibLoader {
    @Suppress("NewApi", "UnsafeDynamicallyLoadedCode")
    fun loadLibrary(name: String) {
//
        val platform = Platform.current()
        val extension = platform.os.extension

        // Resource path in JAR: natives/macos-aarch64/libaudiokit.dylib (etc.)
        val resourcePath = "/natives/${platform.dir}/lib${name}.${extension}"

        // Try to load from JAR resource first
        val resourceStream = JarLibLoader::class.java.getResourceAsStream(resourcePath)
        if (resourceStream != null) {
            // Extract to temp file and load, but verify extracted size
            val tempFile = Files.createTempFile("lib", ".${extension}")
            tempFile.toFile().deleteOnExit()
            resourceStream.use { input ->
                Files.copy(input, tempFile, StandardCopyOption.REPLACE_EXISTING)
            }
            val size = tempFile.toFile().length()
            if (size > 0) {
                println("Loaded native library from JAR resource: $resourcePath -> ${tempFile.toAbsolutePath()}")
                System.load(tempFile.toAbsolutePath().toString())
                return
            } else {
                // extracted file empty: fall back to local build output
                println("Native resource $resourcePath in JAR is empty (size=0), falling back to local build path")
            }
        }

        // If not present in JAR (common during IDE hot-run), try to load from local build output
        val userDir = System.getProperty("user.dir")
        val localPath = java.io.File(userDir, "desktop-lib/build/resources/main/natives/${platform.dir}/lib${name}.${extension}")
        if (localPath.exists() && localPath.length() > 0) {
            System.load(localPath.absolutePath)
            return
        }

        error("Native library not found in JAR: $resourcePath")
    }
}