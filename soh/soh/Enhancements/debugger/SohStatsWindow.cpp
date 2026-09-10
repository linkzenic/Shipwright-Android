#include "SohStatsWindow.h"
#include "soh/OTRGlobals.h"
#if defined(__ANDROID__)
#include <jni.h>
#include <SDL2/SDL.h>
#include <string>
#endif

#if defined(__ANDROID__)
static std::string GetAndroidPlatformLabel() {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        return "Android";
    }

    jclass activityClass = env->GetObjectClass(activity);
    if (activityClass == nullptr) {
        env->DeleteLocalRef(activity);
        return "Android";
    }

    jmethodID versionMethod = env->GetMethodID(activityClass, "getAndroidVersionFromNative", "()Ljava/lang/String;");
    if (versionMethod == nullptr) {
        env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(activity);
        return "Android";
    }

    jstring versionString = (jstring)env->CallObjectMethod(activity, versionMethod);
    if (versionString == nullptr) {
        env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(activity);
        return "Android";
    }

    const char* versionChars = env->GetStringUTFChars(versionString, nullptr);
    std::string platformLabel = versionChars != nullptr ? versionChars : "Android";
    if (versionChars != nullptr) {
        env->ReleaseStringUTFChars(versionString, versionChars);
    }

    env->DeleteLocalRef(versionString);
    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
    return platformLabel;
}
#endif

void SohStatsWindow::DrawElement() {
    const float framerate = ImGui::GetIO().Framerate;
    const float deltatime = ImGui::GetIO().DeltaTime;
    ImGui::PushFont(OTRGlobals::Instance->fontMonoLarger);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

#if defined(_WIN32)
    ImGui::Text("Platform: Windows");
#elif defined(__IOS__)
    ImGui::Text("Platform: iOS");
#elif defined(__APPLE__)
    ImGui::Text("Platform: macOS");
#elif defined(__ANDROID__)
    ImGui::Text("Platform: %s", GetAndroidPlatformLabel().c_str());
#elif defined(__linux__)
    ImGui::Text("Platform: Linux");
#else
    ImGui::Text("Platform: Unknown");
#endif
    ImGui::Text("Status: %0.3f ms/frame (%0.1f FPS)", deltatime * 1000.0f, framerate);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}
