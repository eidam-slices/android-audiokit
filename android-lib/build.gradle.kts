plugins {
    alias(libs.plugins.android.library)
    id("maven-publish")
}

android {
    namespace = "cz.eidam.android_lib"
    compileSdk = 36

    defaultConfig {
        minSdk = 24

        @Suppress("UnstableApiUsage")
        externalNativeBuild {
            cmake {
                cppFlags("-std=c++17")
                arguments("-DANDROID_STL=c++_shared")
            }
        }

        ndk {
            abiFilters.addAll(listOf("arm64-v8a"))
        }
    }

    buildFeatures {
        prefab = true
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "4.1.2"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }


    //
    publishing {
        singleVariant("release") {
            withSourcesJar()
        }
    }
}

publishing {
    publications {
           register<MavenPublication>("androidRelease") {
               groupId = "cz.eidam"
               artifactId = "android-audiokit"
               version = "0.1.0"

               afterEvaluate {
                   from(components["release"])
               }
           }
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.oboe)

    implementation(project(":core-lib"))
}