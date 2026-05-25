# Projektová analýza (TREE)

## 1) Modulový strom a odpovědnosti

- `core-lib`
  - Kotlin API kontrakty (`AudioEngine`, `EngineOptions`, `Algorithm`, `AnalysisResult`)
  - Kotlin JNI vstup (`NativeLib`) + desktop loader (`JarLibLoader`)
  - C++ analýza (`AudioEngine` + Aubio)
  - C++ JNI glue (`jni_glue.cpp`) - sdílené JNI symboly pro Android i Desktop
  - C++ interface `AudioSourceManager` (deklarace) + `AudioConsumer`
- `android-lib`
  - Android AAR
  - Oboe backend implementace `AudioSourceManager` (`android_audio_source.cpp`)
  - Android convenience factory (`Engines.kt`)
- `desktop-lib`
  - JVM modul + nativní desktop knihovna (`libaudiokit`)
  - RtAudio backend implementace `AudioSourceManager` (`desktop_audio_source.cpp`)
  - balení nativních knihoven do JAR (`build.gradle.kts`)
- `test-app`
  - Android Compose ukázka (single/dual algoritmy, výpis device listu)
- `desktop-test-app`
  - Compose Desktop ukázka (start/stop, výpis device listu, manuální `setDeviceId`)

## 2) Build a link vrstvy

### Native vrstva

- `core-lib/src/main/cpp/CMakeLists.txt`
  - staví `aubio` (static)
  - staví `core_native` (static) z `core_audio_engine.cpp`
- `android-lib/src/main/cpp/CMakeLists.txt`
  - přidá `core_native` jako subdir
  - staví `audiokit` (shared)
  - přidává `jni_glue.cpp` přímo do shared knihovny
  - linkuje `oboe::oboe`, `log`, `m`
- `desktop-lib/src/main/cpp/CMakeLists.txt`
  - přidá `core_native` + `rtaudio`
  - staví `audiokit` (shared)
  - přidává `jni_glue.cpp` přímo do shared knihovny

### Kotlin/JVM vrstva

- `core-lib` je základní JVM API modul
- `android-lib` dependuje na `core-lib` a přidává Android packaging + Oboe
- `desktop-lib` dependuje na `core-lib` a přibaluje nativní knihovny do JARu

## 3) Runtime logika - detailní tok dat

### 3.1 Společná engine část (Aubio)

Soubor: `core-lib/src/main/cpp/core_audio_engine.cpp`

- `AudioEngine` implementuje `AudioConsumer`.
- Audio callback (Oboe/RtAudio) volá `pushSamples(...)`.
- `pushSamples` ukládá mono sample do lock-free ring bufferu (`kRingBufferSize=16384`).
- `analysisLoop` na samostatném threadu:
  - čeká na data (`mDataCv`)
  - po `hopSize` blocích plní `fvec`
  - volá `aubio_pitch_do`
  - filtruje podle `mMinInputDb` a `mConfidenceThreshold`
  - throttluje callbacky (`>= 1 Hz` změna nebo `>= 120 ms`)
  - posílá výsledek přes `IResultListener`

Konfigurace:
- start používá `prepareAubio(48000)` (hardcoded)
- `updateOptions(...)` je asynchronní (pending config + `mReconfigRequested`)
- při reconfig se znovu vytvoří Aubio instance

### 3.2 Android backend (Oboe)

Soubor: `android-lib/src/main/cpp/android_audio_source.cpp`

- `AudioSourceManager::Impl` dědí z `oboe::AudioStreamCallback`
- drží:
  - `mDeviceId`
  - seznam consumerů
  - `mHopSize` (pro `framesPerCallback`)
  - jednoduchou cache seznamu Android zařízení (`audioDevices`)
- `startIfNeeded()`:
  - sestaví Oboe stream (Input, Mono, Float, LowLatency, Shared)
  - nastavuje `setDeviceId(mDeviceId)`
  - nastavuje `setFramesPerCallback(mHopSize)`
  - sample rate není explicitně nastavována (nechá se na Oboe)
- `setDeviceId(...)`:
  - uloží nové id
  - pokud stream běží, provede stop + restart

Android device list:
- JNI v `jni_glue.cpp` tahá `AudioManager.getDevices(GET_DEVICES_INPUTS)`
- mapuje typy na přátelská jména
- registruje do `AudioSourceManager::registerAndroidDevice`

### 3.3 Desktop backend (RtAudio)

Soubor: `desktop-lib/src/main/cpp/desktop_audio_source.cpp`

- `AudioSourceManager::Impl` drží:
  - `RtAudio dac`
  - `currentDeviceId`
  - seznam consumerů
  - stream stav (`streamOpen`)
- `initRtAudio()`:
  - vybírá default input nebo explicitní device id
  - získá `DeviceInfo`
  - skládá kandidátní sample rates (`preferred`, supported + fallback common list)
  - zkouší `openStream(...)` postupně pro kandidáty
- callback posílá mono float data všem consumerům
- `getAvailableDevices()` vrací jen input-capable zařízení přes `dac.getDeviceIds()`

## 4) JNI a Kotlin API lifecycle wrapper

### 4.1 JNI bridge

Soubor: `core-lib/src/main/cpp/jni_glue.cpp`

- JNI exportuje:
  - lifecycle enginu (`createEngine`, `engineStart`, `engineStop`, `destroyEngine`)
  - runtime reconfig (`engineUpdateOptions`)
  - device list (`nativeGetDevices`) + volba aktivniho inputu (`nativeSetDeviceId`)
- Java callback adapter:
  - `JniResultListener` drží global ref callbacku
  - posílá `onFrequencyChanged(float,float,float)`

### 4.2 Kotlin wrapper

Soubory:
- `core-lib/src/main/kotlin/cz/eidam/lib/NativeLib.kt`
- `core-lib/src/main/kotlin/cz/eidam/lib/AubioEngine.kt`

- `NativeLib`:
  - načítá `audiokit` (local `System.loadLibrary` -> fallback `JarLibLoader`)
  - vystavuje native metody + Kotlin helpery `getAvailableDevices`, `setAudioInputDevice`
- `AubioEngine`:
  - drží `nativeHandle`
  - mapuje Kotlin lambda na `FrequencyCallback`
  - expose `start/stop/updateOptions/destroy`

Použití v appkach:
- Android Compose (`SingleAlgorithm`, `DualAlgorithm`) vola `updateOptions` za behu
- Desktop Compose (`App.kt`) umí načíst devices a ručně nastavit `deviceId`

## 5) Co funguje dobře

- Čistá separace: analýza (`core-lib`) vs capture backend (`android-lib`/`desktop-lib`).
- Jednotný JNI surface pro Android i Desktop.
- Runtime update algoritmu/hop/buffer bez full destroy engine.
- Praktické test appky na obou platformách.

## 6) Kvalita kódu - nálezy k vyčištění (prioritizované)

### Kritické / rizikové

1. `desktop-lib/src/main/cpp/desktop_audio_source.cpp`
   - `setDeviceId(...)` používá `std::lock_guard`, ale uvnitř manuálně volá `mtx.unlock()/mtx.lock()`.
   - To je nebezpečné (double-unlock/UB) a může padat pod zátěží.

2. `desktop-lib/src/main/cpp/desktop_audio_source.cpp`
   - `addConsumer/removeConsumer` drží mutex a uvnitř volají `startStream/stopStream`.
   - RtAudio stop/start může čekat na callback; callback také bere stejný mutex -> riziko deadlocku.

3. `android-lib/src/main/cpp/android_audio_source.cpp`
   - `removeConsumer` drží `mConsumersMutex` a při prázdném seznamu volá `stop()`.
   - callback `onAudioReady` bere stejný mutex -> riziko deadlocku při stop/restart.

4. `core-lib/src/main/cpp/jni_glue.cpp` + `core_audio_engine.cpp`
   - opakované `engineStart` může vytvořit další `JniResultListener`, i když engine už běží.
   - mapa `gAdapters.emplace` při existujícím klíči může leaknout nový adapter.

### Vysoké (architektura/konzistence)

5. Sample rate není konzistentní end-to-end.
   - `AudioEngine::start` fixuje `48000`.
   - Android stream si volí sample rate sám, desktop ji vybírá per device.
   - Engine není notifikován o reálném sample rate streamu.

6. API model zařízení je ořezaný (`AudioDevice{id,name}`).
   - chybí metadata: supported sample rates, default/preferred sample rate, default device flag.

7. JNI serializuje device list jako string `"id|name"`.
   - křehké parsování, nemá prostor pro capability metadata.

8. `audio_source.h` má smíšený jazyk komentářů a obecně nejednotný naming styl.

### Střední

9. Duplicity include (`#include <audio_source.h>` + relativní include) v platform sourcech.
10. Logging je nejednotný (stderr, android log, české/anglické texty).
11. `AubioEngine` nemá `Closeable/AutoCloseable` pattern; lifecycle je manuální.
12. `EngineOptions` neřeší sample rate (a tím odděluje pipeline config od capture config).

## 7) Shrnutí stavu pro další krok

- Architektura je dobrá pro rozšíření na „device capabilities + sample rate switching“.
- Největší technický dluh je synchronizace stream restartu a ABI/API model konfigurace.
- Před implementací sample rates je vhodné nejdřív stabilizovat lock model a sjednotit konfigurační vrstvu.
