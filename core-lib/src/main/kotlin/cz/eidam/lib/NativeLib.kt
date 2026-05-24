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