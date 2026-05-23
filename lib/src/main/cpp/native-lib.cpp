// JNI glue only. Core logic lives in audio_engine.cpp / audio_source_manager.cpp

#include <jni.h>
#include <android/log.h>
#include "audio_engine.h"

#define LOG_TAG "NativeLibGlue"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

JavaVM* gJavaVM = nullptr;

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    gJavaVM = vm;
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jlong JNICALL
Java_cz_eidam_lib_NativeLib_createEngine(JNIEnv* env, jobject thiz, jint algorithmId, jint bufferSize, jint hopSize, jfloat confidenceThreshold, jfloat minInputDb) {
    AudioEngine* engine = new AudioEngine(static_cast<int>(algorithmId), static_cast<int>(bufferSize), static_cast<int>(hopSize), static_cast<float>(confidenceThreshold), static_cast<float>(minInputDb));
    return reinterpret_cast<jlong>(engine);
}

extern "C" JNIEXPORT void JNICALL
Java_cz_eidam_lib_NativeLib_destroyEngine(JNIEnv* env, jobject thiz, jlong handle) {
    AudioEngine* engine = reinterpret_cast<AudioEngine*>(handle);
    if (engine) {
        delete engine;
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_cz_eidam_lib_NativeLib_engineStart(JNIEnv* env, jobject thiz, jlong handle, jobject callback) {
    AudioEngine* engine = reinterpret_cast<AudioEngine*>(handle);
    if (!engine) return JNI_FALSE;
    return engine->start(env, callback) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_cz_eidam_lib_NativeLib_engineStop(JNIEnv* env, jobject thiz, jlong handle) {
    AudioEngine* engine = reinterpret_cast<AudioEngine*>(handle);
    if (!engine) return;
    engine->stop();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_cz_eidam_lib_NativeLib_engineUpdateOptions(JNIEnv* env, jobject thiz, jlong handle, jint algorithmId, jint bufferSize, jint hopSize, jfloat confidenceThreshold, jfloat minInputDb) {
    AudioEngine* engine = reinterpret_cast<AudioEngine*>(handle);
    if (!engine) return JNI_FALSE;
    return engine->updateOptions(static_cast<int>(algorithmId), static_cast<int>(bufferSize), static_cast<int>(hopSize), static_cast<float>(confidenceThreshold), static_cast<float>(minInputDb)) ? JNI_TRUE : JNI_FALSE;
}