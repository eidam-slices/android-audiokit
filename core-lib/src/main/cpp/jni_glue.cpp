// Shared JNI glue that forwards JVM callbacks to the native AudioEngine.
// This file is platform-agnostic and will be compiled into core_native so
// both Android and desktop shared libraries get the same JNI symbols.

#include <jni.h>
#include <unordered_map>
#include <mutex>
#include <string>
#include <vector>
#include "audio_engine.h"
#include "result_listener.h"
#include "audio_source.h"

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "NativeLibGlue"
#define LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)
#else
#include <cstdio>
#define LOGI(fmt, ...) std::fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) std::fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#endif

static JavaVM* gJavaVM = nullptr;

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    gJavaVM = vm;
    return JNI_VERSION_1_6;
}

// Adapter: forwards native AnalysisResult to a Java callback object.
class JniResultListener : public IResultListener {
public:
    JniResultListener(JNIEnv* env, jobject javaCallback) {
        mJavaCallback = env->NewGlobalRef(javaCallback);
        jclass cls = env->GetObjectClass(javaCallback);
        mOnResultMethod = env->GetMethodID(cls, "onFrequencyChanged", "(FFF)V");
        env->DeleteLocalRef(cls);
    }

    ~JniResultListener() override {
        if (mJavaCallback && gJavaVM) {
            JNIEnv* env = nullptr;
            if (gJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
                env->DeleteGlobalRef(mJavaCallback);
            }
        }
    }

    void onAnalysisResult(const AnalysisResult& result) override {
        if (!gJavaVM) return;
        JNIEnv* env = nullptr;
        bool attached = false;
        jint r = gJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (r == JNI_EDETACHED) {
            // Attach current thread if necessary. Android's AttachCurrentThread signature
            // accepts (JNIEnv**), while some JVMs use (void**). Use conditional macro.
#ifdef __ANDROID__
            if (gJavaVM->AttachCurrentThread(&env, nullptr) == JNI_OK) attached = true; else return;
#else
            if (gJavaVM->AttachCurrentThread((void**)&env, nullptr) == JNI_OK) attached = true; else return;
#endif
        }
        env->CallVoidMethod(mJavaCallback, mOnResultMethod, result.frequencyHz, result.confidence, result.rms);
        if (attached) gJavaVM->DetachCurrentThread();
    }

private:
    jobject mJavaCallback = nullptr;
    jmethodID mOnResultMethod = nullptr;
};

// Track adapters per engine so we can remove them on stop/destroy
static std::unordered_map<AudioEngine*, JniResultListener*> gAdapters;
static std::mutex gAdaptersMutex;

// Expose Java_cz_eidam_lib_NativeLib_* symbols for all platforms. Compiling
// the JNI glue into the core_native static lib ensures the Java native
// methods are available in the final shared libraries on both Android and
// desktop (no per-platform shim required).
extern "C" JNIEXPORT jlong JNICALL
Java_cz_eidam_lib_NativeLib_createEngine(JNIEnv* env, jobject thiz, jint algorithmId, jint bufferSize, jint hopSize, jfloat confidenceThreshold, jfloat minInputDb) {
    AudioEngine* engine = new AudioEngine(static_cast<int>(algorithmId), static_cast<int>(bufferSize), static_cast<int>(hopSize), static_cast<float>(confidenceThreshold), static_cast<float>(minInputDb));
    return reinterpret_cast<jlong>(engine);
}

extern "C" JNIEXPORT void JNICALL
Java_cz_eidam_lib_NativeLib_destroyEngine(JNIEnv* env, jobject thiz, jlong handle) {
    AudioEngine* engine = reinterpret_cast<AudioEngine*>(handle);
    if (!engine) return;

    // remove and delete adapter if present
    {
        std::lock_guard<std::mutex> guard(gAdaptersMutex);
        auto it = gAdapters.find(engine);
        if (it != gAdapters.end()) {
            delete it->second;
            gAdapters.erase(it);
        }
    }

    delete engine;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_cz_eidam_lib_NativeLib_engineStart(JNIEnv* env, jobject thiz, jlong handle, jobject callback) {
    AudioEngine* engine = reinterpret_cast<AudioEngine*>(handle);
    if (!engine || callback == nullptr) return JNI_FALSE;

    JniResultListener* adapter = new JniResultListener(env, callback);
    bool started = engine->start(adapter);
    if (!started) {
        delete adapter;
        return JNI_FALSE;
    }

    {
        std::lock_guard<std::mutex> guard(gAdaptersMutex);
        gAdapters.emplace(engine, adapter);
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_cz_eidam_lib_NativeLib_engineStop(JNIEnv* env, jobject thiz, jlong handle) {
    AudioEngine* engine = reinterpret_cast<AudioEngine*>(handle);
    if (!engine) return;

    engine->stop();

    std::lock_guard<std::mutex> guard(gAdaptersMutex);
    auto it = gAdapters.find(engine);
    if (it != gAdapters.end()) {
        delete it->second;
        gAdapters.erase(it);
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_cz_eidam_lib_NativeLib_engineUpdateOptions(JNIEnv* env, jobject thiz, jlong handle, jint algorithmId, jint bufferSize, jint hopSize, jfloat confidenceThreshold, jfloat minInputDb) {
    AudioEngine* engine = reinterpret_cast<AudioEngine*>(handle);
    if (!engine) return JNI_FALSE;
    return engine->updateOptions(static_cast<int>(algorithmId), static_cast<int>(bufferSize), static_cast<int>(hopSize), static_cast<float>(confidenceThreshold), static_cast<float>(minInputDb)) ? JNI_TRUE : JNI_FALSE;
}

// =========================================================================
// INTERFEJS PRO SPRÁVU AUDIO ZAŘÍZENÍ (SPOLEČNÝ PRO OBOE I RTAUDIO)
// =========================================================================

extern "C" JNIEXPORT jobjectArray JNICALL
Java_cz_eidam_lib_NativeLib_nativeGetDevices(JNIEnv *env, jobject thiz, jobject context) {

#ifdef __ANDROID__
    // Vyčistíme lokální paměť Oboe modulu před novým načtením
    AudioSourceManager::instance().clearAndroidDevices();

    if (context != nullptr) {
        jclass contextClass = env->GetObjectClass(context);
        jmethodID getSystemServiceMethod = env->GetMethodID(contextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring audioServiceName = env->NewStringUTF("audio");
        jobject audioManager = env->CallObjectMethod(context, getSystemServiceMethod, audioServiceName);
        env->DeleteLocalRef(audioServiceName);

        if (audioManager != nullptr) {
            jclass audioManagerClass = env->GetObjectClass(audioManager);
            jmethodID getDevicesMethod = env->GetMethodID(audioManagerClass, "getDevices", "(I)[Landroid/media/AudioDeviceInfo;");

            // 1 = AudioManager.GET_DEVICES_INPUTS
            jobjectArray devicesArray = (jobjectArray)env->CallObjectMethod(audioManager, getDevicesMethod, 1);

            if (devicesArray != nullptr) {
                jsize length = env->GetArrayLength(devicesArray);
                for (jsize i = 0; i < length; i++) {
                    jobject device = env->GetObjectArrayElement(devicesArray, i);
                    jclass deviceClass = env->GetObjectClass(device);

                    // 1. Vytáhneme ID zařízení: int id = device.getId();
                    jmethodID getIdMethod = env->GetMethodID(deviceClass, "getId", "()I");
                    jint id = env->CallIntMethod(device, getIdMethod);

                    // 2. Vytáhneme TYP zařízení: int type = device.getType();
                    jmethodID getTypeMethod = env->GetMethodID(deviceClass, "getType", "()I");
                    jint type = env->CallIntMethod(device, getTypeMethod);

                    // 3. Určíme lidský název podle konstant z Android SDK (AudioDeviceInfo.TYPE_*)
                    // 3. Určíme lidský název podle konstant z Android SDK (AudioDeviceInfo.TYPE_*)
                    std::string friendlyName = "Neznámé audio zařízení";
                    switch (type) {
                        case 15: friendlyName = "Vestavěný mikrofon"; break;       // TYPE_BUILTIN_MIC
                        case 3:  friendlyName = "Wired Headset (Jack)"; break;     // TYPE_WIRED_HEADSET
                        case 7:  friendlyName = "Bluetooth Headset (SCO)"; break;  // TYPE_BLUETOOTH_SCO
                        case 26: friendlyName = "Bluetooth LE Mikrofon"; break;    // TYPE_BLUETOOTH_A2DP (BLE Audio)
                        case 11: friendlyName = "USB Zvukovka / Vstup"; break;     // TYPE_USB_DEVICE
                        case 22: friendlyName = "USB Headset / AirPods"; break;    // TYPE_USB_HEADSET
                        case 13: friendlyName = "USB Dock / Audio rozhraní"; break;// TYPE_DOCK (Zde se často hlásí sluchátka přes USB-C)
                        case 5:  friendlyName = "Line-in Analogový vstup"; break;  // TYPE_LINE_ANALOG
                        default: {
                            // Záložní řešení: Zkusíme vytáhnout název produktu od výrobce
                            jmethodID getProductNameMethod = env->GetMethodID(deviceClass, "getProductName", "()Ljava/lang/CharSequence;");
                            jobject productNameCharSeq = env->CallObjectMethod(device, getProductNameMethod);

                            if (productNameCharSeq != nullptr) {
                                jclass charSeqClass = env->GetObjectClass(productNameCharSeq);
                                jmethodID toStringMethod = env->GetMethodID(charSeqClass, "toString", "()Ljava/lang/String;");
                                jstring nameString = (jstring)env->CallObjectMethod(productNameCharSeq, toStringMethod);

                                if (nameString != nullptr) {
                                    const char* nativeName = env->GetStringUTFChars(nameString, nullptr);
                                    friendlyName = std::string(nativeName);
                                    env->ReleaseStringUTFChars(nameString, nativeName);
                                    env->DeleteLocalRef(nameString);
                                } else {
                                    friendlyName = "Externí mikrofon (Typ " + std::to_string(type) + ")";
                                }
                                env->DeleteLocalRef(charSeqClass);
                                env->DeleteLocalRef(productNameCharSeq);
                            } else {
                                // Bezpečný fallback, pokud getProductName() selže a vrátí null
                                friendlyName = "Externí mikrofon (Typ " + std::to_string(type) + ")";
                            }
                            break;
                        }
                    }

                    // Registrace do Oboe modulu s unikátním názvem
                    AudioSourceManager::instance().registerAndroidDevice(id, friendlyName);

                    env->DeleteLocalRef(deviceClass);
                    env->DeleteLocalRef(device);
                }
                env->DeleteLocalRef(devicesArray);
            }
            env->DeleteLocalRef(audioManagerClass);
            env->DeleteLocalRef(audioManager);
        }
        env->DeleteLocalRef(contextClass);
    }
#endif

    // --- SPOLEČNÉ CHOVÁNÍ ---
    // Na desktopu projde živé RtAudio hardwarové vstupy
    // Na Androidu vrátí vektor, který jsme právě naplnili z AudioManageru
    std::vector<AudioDevice> devices = AudioSourceManager::instance().getAvailableDevices();

    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray ret = env->NewObjectArray(devices.size(), stringClass, env->NewStringUTF(""));

    for (size_t i = 0; i < devices.size(); i++) {
        // Formát stringu: "id|název"
        std::string entry = std::to_string(devices[i].id) + "|" + devices[i].name;
        jstring jstr = env->NewStringUTF(entry.c_str());
        env->SetObjectArrayElement(ret, i, jstr);
        env->DeleteLocalRef(jstr);
    }

    return ret;
}

extern "C" JNIEXPORT void JNICALL
Java_cz_eidam_lib_NativeLib_nativeSetDeviceId(JNIEnv *env, jobject thiz, jint deviceId) {
    // Zavolá bezpečný restart s novým ID na aktivní platformě (RtAudio / Oboe)
    AudioSourceManager::instance().setDeviceId(static_cast<int32_t>(deviceId));
}