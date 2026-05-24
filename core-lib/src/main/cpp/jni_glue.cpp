// Shared JNI glue that forwards JVM callbacks to the native AudioEngine.
// This file is platform-agnostic and will be compiled into core_native so
// both Android and desktop shared libraries get the same JNI symbols.

#include <jni.h>
#include <unordered_map>
#include <mutex>
#include "audio_engine.h"
#include "result_listener.h"

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
