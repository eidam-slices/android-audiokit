# Desktop Audio Detection Test - RtAudio Implementation

## Overview
This directory contains a test application (`desktop-test-app`) that uses the `desktop-lib` native library to perform real-time audio pitch detection using RtAudio backend on macOS, Linux, or Windows.

## What's Implemented

### 1. **RtAudio Backend** (`desktop-lib/src/main/cpp/desktop_audio_source.cpp`)
- Uses **RtAudio** for cross-platform audio input (CoreAudio on macOS, ALSA/PulseAudio on Linux, WASAPI on Windows)
- Streams mono audio at 48 kHz with 512-frame buffer
- **RMS (Root Mean Square)** is calculated for each buffer and logged to stderr:
  ```
  [RtAudio] RMS: 0.0234 (dB value)
  ```
  Logged every ~500ms for monitoring audio levels

### 2. **Kotlin Wrapper** (`desktop-test-app/src/main/kotlin/cz/eidam/test_app/DesktopAudioEngine.kt`)
- Wraps JNI interface from `desktop-lib` (via `NativeLibDesktop`)
- Implements `AudioEngine` interface from `core-lib`
- Supports start/stop/destroy operations

### 3. **Compose Desktop UI** (`desktop-test-app/src/main/kotlin/cz/eidam/test_app/Main.kt`)
- Start/Stop buttons for audio capture
- Real-time display of:
  - Detected frequency (Hz)
  - Confidence level (0.0-1.0)
  - RMS value (raw + dB)
- Activity log showing recent results (last 30 entries)

## Building

### Prerequisites
- macOS with CMake installed: `brew install cmake`
- Java/Gradle build system
- Android NDK 30+ (if building Android too)

### Build Steps

1. **Native Desktop Library**
   ```bash
   cd desktop-lib/src/main/cpp
   mkdir -p build && cd build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   cmake --build . --config Release
   # Output: libaudiokit.dylib (macOS), libaudiokit.so (Linux), audiokit.dll (Windows)
   ```

2. **Copy Native Library**
   ```bash
   cp desktop-lib/src/main/cpp/build/libaudiokit.dylib desktop-lib/build/libs/
   ```

3. **Compile Kotlin + Gradle**
   ```bash
   ./gradlew desktop-test-app:assemble
   ```

## Running the Test App

### Option 1: Gradle Compose Task
```bash
export LD_LIBRARY_PATH=/path/to/repo/desktop-lib/build/libs:$LD_LIBRARY_PATH
export DYLD_LIBRARY_PATH=/path/to/repo/desktop-lib/build/libs:$DYLD_LIBRARY_PATH  # macOS
./gradlew desktop-test-app:hotDev
```

### Option 2: Direct Java Execution (if JAR is bundled)
```bash
java -Djava.library.path=desktop-lib/build/libs \
     -jar desktop-test-app/build/libs/desktop-test-app-all.jar
```

### Option 3: Check RMS Output in Console
Once running, open the app and click "Start". You should see in console logs:
```
[App] Engine initialized
[App] Engine started
[RtAudio] RMS: 0.0145 (-36.8 dB)
[RtAudio] RMS: 0.0234 (-32.6 dB)
...
```

## Troubleshooting

### "audiokit not found"
1. Make sure the `.dylib` (or `.so` / `.dll`) is in the same directory where Java can find it
2. Set `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH` before running:
   ```bash
   export DYLD_LIBRARY_PATH=$(pwd)/desktop-lib/build/libs:$DYLD_LIBRARY_PATH
   ```

### RtAudio device not found
- On macOS: CoreAudio should be available by default
- On Linux: Install ALSA/PulseAudio dev packages (`sudo apt install libasound2-dev`)
- On Windows: WASAPI is built-in

### No audio input
- Check microphone permissions (macOS might require authorization)
- Verify audio device is connected and working
- Check system audio input levels

## Architecture Notes

- **core-lib**: Platform-independent Aubio + engine logic (C++ headers, pitch detection)
- **desktop-lib**: Cross-platform audio I/O via RtAudio + JNI glue
- **desktop-test-app**: Simple Compose UI + Kotlin wrapper

The RtAudio callback runs in real-time on the audio thread and pushes samples to `AudioEngine` (from core-lib) which performs Aubio pitch detection and reports results via callback.

## Performance

- **RMS Calculation**: O(n) per buffer (computational overhead: ~1% on typical quad-core)
- **JNI Overhead**: Negligible for ~10 callbacks/sec (results reported ~10Hz)
- **Memory**: ~5-10 MB for Aubio + RtAudio resources

## Future Improvements

1. Add Gradle task to copy native libs post-build automatically
2. Bundle multi-platform native binaries into single JAR
3. Add more detailed logging options (frequency trend, confidence smoothing)
4. Support other backends (JACK for Linux/macOS, WDM for Windows)
