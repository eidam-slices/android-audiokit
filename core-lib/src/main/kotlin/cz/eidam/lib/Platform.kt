package cz.eidam.lib

enum class Platform(val os: Os, val arch: Arch, val dir: String) {
    MacX64(Os.Mac, Arch.X64, "macos-x86_64"),
    MacAarch64(Os.Mac, Arch.Aarch64, "macos-aarch64"),
    LinuxX64(Os.Linux, Arch.X64, "linux-x86_64"),
    WinX64(Os.Windows, Arch.X64, "windows-x86_64"),
    ;

    companion object {
        fun current(): Platform {

            val osName = System.getProperty("os.name").lowercase()
            val osArch = System.getProperty("os.arch").lowercase()

            val os = Os.values().singleOrNull { os ->
                os.aliases.any { osName.contains(it) }
            } ?: throw IllegalArgumentException("OS '$osName' is not supported")

            val arch = Arch.values().singleOrNull { arch ->
                arch.aliases.any { osArch.contains(it) }
            } ?: throw IllegalArgumentException("Architecture '$osArch' is not supported")

            return Platform.values().singleOrNull { it.os == os && it.arch == arch }
                ?: throw IllegalArgumentException("Platform with OS '$osName' and architecture '$osArch' is not supported")
        }
    }
}

enum class Os(val aliases: List<String>, val extension: String) {
    Mac(listOf("mac"), "dylib"),
    Linux(listOf("linux"), "so"),
    Windows(listOf("win"), "dll"),
}

enum class Arch(val aliases: List<String>) {
    Aarch64(listOf("aarch64", "arm64")),
    X64(listOf("x86_64", "amd64", "x64"))
}