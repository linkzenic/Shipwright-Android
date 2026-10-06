#ifdef __ANDROID__
#include <SDL2/SDL.h>
#include <jni.h>
#include <ship/Context.h>

// Java owns storage selection and permission prompts. Do not start the game
// against a fallback directory if setup has failed.
extern "C" int Android_WaitForSetup(void) {
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) {
        return 0;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID wait = env->GetStaticMethodID(cls, "waitForSetupFromNative", "()V");
    jmethodID getPath = env->GetStaticMethodID(cls, "getDataRootPathFromNative", "()Ljava/lang/String;");
    bool ready = false;
    if (wait && getPath && !env->ExceptionCheck()) {
        env->CallStaticVoidMethod(cls, wait);
        if (!env->ExceptionCheck()) {
            auto path = static_cast<jstring>(env->CallStaticObjectMethod(cls, getPath));
            if (path && !env->ExceptionCheck()) {
                const char* utf = env->GetStringUTFChars(path, nullptr);
                if (utf) {
                    ready = utf[0] != '\0';
                    if (ready) {
                        Ship::Context::SetAndroidDataRootPath(utf);
                    }
                    env->ReleaseStringUTFChars(path, utf);
                }
                env->DeleteLocalRef(path);
            }
        }
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ready = false;
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return ready;
}
#endif
