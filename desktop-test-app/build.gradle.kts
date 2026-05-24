plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.compose)
}

group = "cz.eidam"
version = "unspecified"

kotlin {
    jvmToolchain(21)
}

dependencies {
    implementation(compose.desktop.currentOs)
    implementation(compose.material3)
    implementation(project(":core-lib"))
    implementation(project(":desktop-lib"))
}

tasks.withType<JavaExec>().configureEach {
    systemProperty("java.library.path", file("../desktop-lib/build/libs").absolutePath)
}