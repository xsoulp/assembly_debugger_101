#include "NativeDebugger.h"
#include <jni.h>
#include <stdexcept>

namespace {
asm101::NativeDebugger debugger;
void error(JNIEnv *env, const std::exception &e) {
    env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), e.what());
}
} // namespace
extern "C" JNIEXPORT jstring JNICALL Java_com_xsoulp_asm101_NativeBridge_build(JNIEnv *env, jclass,
                                                                               jstring text) {
    const char *s = env->GetStringUTFChars(text, nullptr);
    if (!s)
        return nullptr;
    std::string source(s);
    env->ReleaseStringUTFChars(text, s);
    try {
        debugger.build(source);
        return env->NewStringUTF(debugger.snapshot().c_str());
    } catch (const std::exception &e) {
        error(env, e);
        return nullptr;
    }
}
extern "C" JNIEXPORT jstring JNICALL Java_com_xsoulp_asm101_NativeBridge_start(JNIEnv *env,
                                                                               jclass) {
    try {
        debugger.start();
        return env->NewStringUTF(debugger.snapshot().c_str());
    } catch (const std::exception &e) {
        error(env, e);
        return nullptr;
    }
}
extern "C" JNIEXPORT jstring JNICALL Java_com_xsoulp_asm101_NativeBridge_step(JNIEnv *env, jclass) {
    try {
        debugger.step();
        return env->NewStringUTF(debugger.snapshot().c_str());
    } catch (const std::exception &e) {
        error(env, e);
        return nullptr;
    }
}
extern "C" JNIEXPORT jstring JNICALL Java_com_xsoulp_asm101_NativeBridge_stop(JNIEnv *env, jclass) {
    debugger.stop();
    return env->NewStringUTF(debugger.snapshot().c_str());
}
