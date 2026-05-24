

plugins {
    alias(libs.plugins.kotlin.jvm)
    id("maven-publish")
}

group = "cz.eidam"
version = "0.1.0"


kotlin {
    jvmToolchain(21)
}

dependencies {
    api(project(":core-lib"))
}

publishing {
    publications {
        register<MavenPublication>("desktop") {
            groupId = "cz.eidam"
            artifactId = "kotlin-audiokit"
            from(components["java"])
        }
    }
}

enum class Platform(val os: Os, val arch: Arch) {
    MacX64(Os.Macintosh, Arch.X64),
    MacAarch64(Os.Macintosh, Arch.Aarch64),
    LinuxX64(Os.Linux, Arch.X64),

    // LinuxAarch64(System.Linux, Architecture.Aarch64),
    WinX64(Os.Windows, Arch.X64)
    // WinAarch64(System.Windows, Architecture.Aarch64)
}
enum class Os(val aliases: List<String>) {
    Linux(listOf("linux")),
    Windows(listOf("win")),
    Macintosh(listOf("mac"))
}
enum class Arch(val aliases: List<String>) {
    X64(listOf("x86_64", "amd64", "x64")),
    Aarch64(listOf("aarch64", "arm64"))
}

val platform = let {
    val os = Os.values().singleOrNull { entry ->
        entry.aliases.any { alias -> osName.contains(alias) }
    } ?: throw IllegalStateException("Unsupported OS: $osName")

    val arch = Arch.values().singleOrNull { entry ->
        entry.aliases.any { alias -> osArch.contains(alias) }
    } ?: throw IllegalStateException("Unsupported architecture: $osArch")

    Platform.values().singleOrNull {
        it.os == os && it.arch == arch
    } ?: throw IllegalStateException("Unsupported platform: $osName $osArch")
}

val platformDir: String = when (platform) {
    Platform.MacAarch64 -> "macos-aarch64"
    Platform.MacX64 -> "macos-x86_64"
    Platform.LinuxX64 -> "linux-x86_64"
    Platform.WinX64 -> "windows-x86_64"
}

val osName get() = System.getProperty("os.name").lowercase()
val osArch get() = System.getProperty("os.arch").lowercase()


// C++ CMake úkoly s inteligentní detekcí cesty pro Mac a IDE
val cmakeConfigure = tasks.register<Exec>("cmakeConfigure") {
    workingDir = file("src/main/cpp")
    val javaHome = System.getProperty("java.home").replace("\\", "/")
    val cmakeExecutable = file("/opt/homebrew/bin/cmake").let { if (it.exists()) it.absolutePath else "cmake" }

    commandLine(
        cmakeExecutable,
        "-B", "build",
        "-S", ".",
        "-DJAVA_HOME=$javaHome",
        "-DCMAKE_C_COMPILER=/usr/bin/clang",
        "-DCMAKE_CXX_COMPILER=/usr/bin/clang++",
        "-DPLATFORM_DIR=$platformDir",
        "-DBUILD_SHARED_LIBS=OFF",
        "-DRTAUDIO_BUILD_SHARED_LIBS=OFF",
        "-DRTAUDIO_BUILD_STATIC_LIBS=ON"
    )
}

val cmakeBuild = tasks.register<Exec>("cmakeBuild") {
    dependsOn(cmakeConfigure)
    workingDir = file("src/main/cpp")
    val cmakeExecutable = file("/opt/homebrew/bin/cmake").let { if (it.exists()) it.absolutePath else "cmake" }
    commandLine(cmakeExecutable, "--build", "build", "--config", "Release")
}

tasks.named<ProcessResources>("processResources") {
    dependsOn(cmakeBuild)

    // CMake already outputs into build/resources/main/natives/<platformDir>/
    // processResources just ensures they're in the JAR under natives/
    from("build/clibs") {
        include("**/*.dylib", "**/*.so", "**/*.dll")
        into("natives")
    }
}

tasks.named<Delete>("clean") {
    delete("src/main/cpp/build")
    delete("build/clibs")
}


// !!! TENTO UPRAVENÝ BLOK DEJ NA ÚPLNÝ KONEC SOUBORU !!!

tasks.jar {
    // 1. Zabalíme nativní knihovny sesypané z GitHub Actions
    from("build/clibs") {
        include("**/*.dylib", "**/*.so", "**/*.dll")
        into("natives")
    }

    // 2. Vezmeme zkompilované třídy z tohoto modulu (desktop-lib)
    val mainSourceSet = extensions.getByType<SourceSetContainer>()["main"]
    from(mainSourceSet.output)

    // 3. !!! TOHLE PŘIBALÍ I TVŮJ MODUL CORE-LIB !!!
    // Najdeme projekt :core-lib, vezmeme jeho zkompilované třídy a vložíme je do JARu
    val coreLibProject = project(":core-lib")
    dependsOn(coreLibProject.tasks.named("compileKotlin")) // Pojistka, aby se nejdřív zkompiloval core-lib

    val coreLibSourceSet = coreLibProject.extensions.getByType<SourceSetContainer>()["main"]
    from(coreLibSourceSet.output)
}