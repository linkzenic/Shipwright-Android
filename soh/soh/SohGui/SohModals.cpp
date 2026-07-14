#include "SohModals.h"
#include <imgui.h>
#include <vector>
#include <string>
#include <libultraship/bridge.h>
#include <libultraship/libultraship.h>
#include "UIWidgets.hpp"
#include "SohGui.hpp"
#include "soh/OTRGlobals.h"
#include "z64.h"

#ifdef __ANDROID__
#include <atomic>
#include <functional>
#include <jni.h>
#include <SDL2/SDL.h>
static std::atomic<bool> androidDialogOpen{ false };
static std::function<void()> androidBtn1Callback;
static std::function<void()> androidBtn2Callback;

extern "C" void JNICALL
Java_com_dishii_soh_MainActivity_nativeDialogResult(JNIEnv* env, jobject obj, jint result) {
    if (result == 0 && androidBtn1Callback) androidBtn1Callback();
    if (result == 1 && androidBtn2Callback) androidBtn2Callback();
    androidDialogOpen = false;
}
#endif

extern "C" PlayState* gPlayState;
struct SohModal {
    std::string title_;
    std::string message_;
    std::string button1_;
    std::string button2_;
    std::function<void()> button1callback_;
    std::function<void()> button2callback_;
};
std::vector<SohModal> modals;

bool closePopup = false;

void SohModalWindow::Draw() {
    if (!IsVisible()) {
        return;
    }
    DrawElement();
    // Sync up the IsVisible flag if it was changed by ImGui
    SyncVisibilityConsoleVariable();
}

void SohModalWindow::DrawElement() {
    if (modals.size() > 0) {
        SohModal curModal = modals.at(0);
        if (!ImGui::IsPopupOpen(curModal.title_.c_str())) {
            ImGui::OpenPopup(curModal.title_.c_str());
        }
        if (closePopup) {
            ImGui::CloseCurrentPopup();
            modals.erase(modals.begin());
            closePopup = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal(curModal.title_.c_str(), NULL,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("%s", curModal.message_.c_str());
            UIWidgets::PushStyleButton(THEME_COLOR);
            if (ImGui::Button(curModal.button1_.c_str())) {
                if (curModal.button1callback_ != nullptr) {
                    curModal.button1callback_();
                }
                ImGui::CloseCurrentPopup();
                modals.erase(modals.begin());
            }
            UIWidgets::PopStyleButton();
            if (curModal.button2_ != "") {
                ImGui::SameLine();
                UIWidgets::PushStyleButton(THEME_COLOR);
                if (ImGui::Button(curModal.button2_.c_str())) {
                    if (curModal.button2callback_ != nullptr) {
                        curModal.button2callback_();
                    }
                    ImGui::CloseCurrentPopup();
                    modals.erase(modals.begin());
                }
                UIWidgets::PopStyleButton();
            }
            ImGui::EndPopup();
        }
    }
}

void SohModalWindow::RegisterPopup(std::string title, std::string message, std::string button1, std::string button2,
                                   std::function<void()> button1callback, std::function<void()> button2callback) {
#ifdef __ANDROID__
    androidBtn1Callback = button1callback;
    androidBtn2Callback = button2callback;
    androidDialogOpen = true;
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetMethodID(cls, "showAlertDialog",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    env->CallVoidMethod(activity, m,
        env->NewStringUTF(title.c_str()),
        env->NewStringUTF(message.c_str()),
        env->NewStringUTF(button1.c_str()),
        env->NewStringUTF(button2.c_str()));
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return;
#endif
    modals.push_back({ title, message, button1, button2, button1callback, button2callback });
}

size_t SohModalWindow::PopupsQueued() {
#ifdef __ANDROID__
    return androidDialogOpen ? 1 : 0;
#endif
    return modals.size();
}

bool SohModalWindow::IsPopupOpen(std::string title) {
    return !modals.empty() && modals.at(0).title_ == title;
}

void SohModalWindow::DismissPopup() {
    closePopup = true;
}
