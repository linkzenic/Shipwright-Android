#include "SohMenu.h"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/enhancementTypes.h"
#include "SohModals.h"
#include "soh/OTRGlobals.h"
#include <soh/GameVersions.h>
#include "soh/ResourceManagerHelpers.h"
#include "UIWidgets.hpp"
#include <spdlog/fmt/fmt.h>

extern "C" {
#include "include/z64audio.h"
#include "variables.h"
#include "transformation_masks/assets/mm_asset_loader.h"
#include "mods/pak_loader/pak_loader.h"
}

namespace SohGui {

extern std::shared_ptr<SohMenu> mSohMenu;
extern std::shared_ptr<SohModalWindow> mModalWindow;
using namespace UIWidgets;

static std::map<int32_t, const char*> imguiScaleOptions = {
    { 0, "Small" },
    { 1, "Normal" },
    { 2, "Large" },
    { 3, "X-Large" },
};

static const std::map<int32_t, const char*> menuThemeOptions = {
    { UIWidgets::Colors::Red, "Red" },
    { UIWidgets::Colors::DarkRed, "Dark Red" },
    { UIWidgets::Colors::Orange, "Orange" },
    { UIWidgets::Colors::Green, "Green" },
    { UIWidgets::Colors::DarkGreen, "Dark Green" },
    { UIWidgets::Colors::LightBlue, "Light Blue" },
    { UIWidgets::Colors::Blue, "Blue" },
    { UIWidgets::Colors::DarkBlue, "Dark Blue" },
    { UIWidgets::Colors::Indigo, "Indigo" },
    { UIWidgets::Colors::Violet, "Violet" },
    { UIWidgets::Colors::Purple, "Purple" },
    { UIWidgets::Colors::Brown, "Brown" },
    { UIWidgets::Colors::Gray, "Gray" },
    { UIWidgets::Colors::DarkGray, "Dark Gray" },
};

static const std::map<int32_t, const char*> textureFilteringMap = {
    { Fast::FILTER_THREE_POINT, "Three-Point" },
    { Fast::FILTER_LINEAR, "Linear" },
    { Fast::FILTER_NONE, "None" },
};

static const std::map<int32_t, const char*> notificationPosition = {
    { 0, "Top Left" }, { 1, "Top Right" }, { 2, "Bottom Left" }, { 3, "Bottom Right" }, { 4, "Hidden" },
};

static const std::map<int32_t, const char*> bootSequenceLabels = {
    { BOOTSEQUENCE_DEFAULT, "Default" },        { BOOTSEQUENCE_AUTHENTIC, "Authentic" },
    { BOOTSEQUENCE_FILESELECT, "File Select" }, { BOOTSEQUENCE_DEBUGWARPSCREEN, "Debug Warp Screen" },
    { BOOTSEQUENCE_WARPPOINT, "Warp Point" },
};

const char* GetGameVersionString(uint32_t index) {
    uint32_t gameVersion = ResourceMgr_GetGameVersion(index);
    switch (gameVersion) {
        case OOT_NTSC_US_10:
            return "NTSC 1.0";
        case OOT_NTSC_US_11:
            return "NTSC 1.1";
        case OOT_NTSC_US_12:
            return "NTSC 1.2";
        case OOT_NTSC_US_GC:
            return "NTSC-U GC";
        case OOT_NTSC_JP_GC:
            return "NTSC-J GC";
        case OOT_NTSC_JP_GC_CE:
            return "NTSC-J GC (Collector's Edition)";
        case OOT_NTSC_US_MQ:
            return "NTSC-U MQ";
        case OOT_NTSC_JP_MQ:
            return "NTSC-J MQ";
        case OOT_PAL_10:
            return "PAL 1.0";
        case OOT_PAL_11:
            return "PAL 1.1";
        case OOT_PAL_GC:
            return "PAL GC";
        case OOT_PAL_MQ:
            return "PAL MQ";
        case OOT_PAL_GC_DBG1:
        case OOT_PAL_GC_DBG2:
            return "PAL GC-D";
        case OOT_PAL_GC_MQ_DBG:
            return "PAL MQ-D";
        case OOT_IQUE_CN:
            return "IQUE CN";
        case OOT_IQUE_TW:
            return "IQUE TW";
        default:
            return "UNKNOWN";
    }
}

#include "message_data_static.h"
extern "C" MessageTableEntry* sNesMessageEntryTablePtr;
extern "C" MessageTableEntry* sGerMessageEntryTablePtr;
extern "C" MessageTableEntry* sFraMessageEntryTablePtr;
extern "C" MessageTableEntry* sJpnMessageEntryTablePtr;

static const std::array<MessageTableEntry**, LANGUAGE_MAX> messageTables = {
    &sNesMessageEntryTablePtr, &sGerMessageEntryTablePtr, &sFraMessageEntryTablePtr, &sJpnMessageEntryTablePtr
};

void SohMenu::UpdateLanguageMap(std::map<int32_t, const char*>& languageMap) {
    for (int32_t i = LANGUAGE_ENG; i < LANGUAGE_MAX; i++) {
        if (*messageTables.at(i) != NULL) {
            if (!languageMap.contains(i)) {
                languageMap.insert(std::make_pair(i, languages.at(i)));
            }
        } else {
            languageMap.erase(i);
        }
    }
}

void SohMenu::AddMenuSettings() {
    // Add Settings Menu
    AddMenuEntry("Settings", CVAR_SETTING("Menu.SettingsSidebarSection"));
    AddSidebarEntry("Settings", "General", 2);
    WidgetPath path = { "Settings", "General", SECTION_COLUMN_1 };

    // General - Settings
    AddWidget(path, "Menu Settings", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Menu Theme", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_SETTING("Menu.Theme"))
        .RaceDisable(false)
        .Options(ComboboxOptions()
                     .Tooltip("Changes the Theme of the Menu Widgets.")
                     .ComboMap(menuThemeOptions)
                     .DefaultIndex(Colors::LightBlue));
#if not defined(__SWITCH__) and not defined(__WIIU__)
    AddWidget(path, "Menu Controller Navigation", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_IMGUI_CONTROLLER_NAV)
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip(
            "Allows controller navigation of the port menu (Settings, Enhancements,...)\nCAUTION: "
            "This will disable game inputs while the menu is visible.\n\nD-pad to move between "
            "items, A to select, B to move up in scope."));
    AddWidget(path, "Allow background inputs", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("AllowBackgroundInputs"))
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) {
            SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
                        CVarGetInteger(CVAR_SETTING("AllowBackgroundInputs"), 1) ? "1" : "0");
        })
        .Options(CheckboxOptions()
                     .Tooltip("Allows controller inputs to be picked up by the game even when the game window isn't "
                              "the focused window.")
                     .DefaultValue(1));
    AddWidget(path, "Menu Background Opacity", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_SETTING("Menu.BackgroundOpacity"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().DefaultValue(0.85f).IsPercentage().Tooltip(
            "Sets the opacity of the background of the port menu."));

    AddWidget(path, "General Settings", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Cursor Always Visible", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("CursorVisibility"))
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) {
            Ship::Context::GetInstance()->GetWindow()->SetForceCursorVisibility(
                CVarGetInteger(CVAR_SETTING("CursorVisibility"), 0));
        })
        .Options(CheckboxOptions().Tooltip("Makes the cursor always visible, even in full screen."));
#endif
    AddWidget(path, "Search In Sidebar", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("Menu.SidebarSearch"))
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) {
            if (CVarGetInteger(CVAR_SETTING("Menu.SidebarSearch"), 0)) {
                mSohMenu->InsertSidebarSearch();
            } else {
                mSohMenu->RemoveSidebarSearch();
            }
        })
        .Options(CheckboxOptions().Tooltip(
            "Displays the Search menu as a sidebar entry in Settings instead of in the header."));
    AddWidget(path, "Search Input Autofocus", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("Menu.SearchAutofocus"))
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip(
            "Search input box gets autofocus when visible. Does not affect using other widgets."));
    AddWidget(path, "Reset Button Combination:", WIDGET_CVAR_BTN_SELECTOR)
        .CVar("gSettings.ResetBtn")
        .Options(BtnSelectorOptions().DefaultValue(BTN_CUSTOM_MODIFIER2));
    AddWidget(path, "Open App Files Folder", WIDGET_BUTTON)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) {
            std::string filesPath = Ship::Context::GetInstance()->GetAppDirectoryPath();
            SDL_OpenURL(std::string("file:///" + std::filesystem::absolute(filesPath).string()).c_str());
        })
        .Options(ButtonOptions().Tooltip("Opens the folder that contains the save and mods folders, etc."));

    AddWidget(path, "Boot", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Boot Sequence", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_SETTING("BootSequence"))
        .RaceDisable(false)
        .Options(ComboboxOptions()
                     .DefaultIndex(BOOTSEQUENCE_DEFAULT)
                     .LabelPosition(LabelPositions::Far)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .ComboMap(bootSequenceLabels)
                     .Tooltip("Configure what happens when starting or resetting the game.\n\n"
                              "Default: LUS logo -> N64 logo\n"
                              "Authentic: N64 logo only\n"
                              "File Select: Skip to file select menu\n"
                              "Debug Warp Screen: Skip to the debug warp screen\n"
                              "Warp Point: Skip to active warp point (if set), see Dev Tools -> General"));

    AddWidget(path, "Languages", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Translate Title Screen", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("TitleScreenTranslation"))
        .RaceDisable(false);
    AddWidget(path, "Language", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_SETTING("Languages"))
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            auto options = std::static_pointer_cast<UIWidgets::ComboboxOptions>(info.options);
            SohMenu::UpdateLanguageMap(options->comboMap);
        })
        .Options(ComboboxOptions()
                     .LabelPosition(LabelPositions::Far)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .ComboMap(languages)
                     .DefaultIndex(LANGUAGE_ENG));
    AddWidget(path, "Accessibility", WIDGET_SEPARATOR_TEXT);
#if defined(_WIN32) || defined(__APPLE__) || defined(ESPEAK)
    AddWidget(path, "Text to Speech", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("A11yTTS"))
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip("Enables text to speech for in game dialog"));
#endif
    AddWidget(path, "Disable Idle Camera Re-Centering", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("A11yDisableIdleCam"))
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip("Disables the automatic re-centering of the camera when idle."));
    AddWidget(path, "Disable Screen Flash for Finishing Blow", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("A11yNoScreenFlashForFinishingBlow"))
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip("Disables the white screen flash on enemy kill."));
    AddWidget(path, "Disable Jabu Wobble", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("A11yNoJabuWobble"))
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip("Disable the geometry wobble and camera distortion inside Jabu."));
    AddWidget(path, "EXPERIMENTAL", WIDGET_SEPARATOR_TEXT).Options(TextOptions().Color(Colors::Orange));
    AddWidget(path, "ImGui Menu Scaling", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_SETTING("ImGuiScale"))
        .RaceDisable(false)
        .Options(ComboboxOptions()
                     .ComboMap(imguiScaleOptions)
                     .Tooltip("Changes the scaling of the ImGui menu elements.")
                     .DefaultIndex(1)
                     .ComponentAlignment(ComponentAlignments::Right)
                     .LabelPosition(LabelPositions::Far))
        .Callback([](WidgetInfo& info) { OTRGlobals::Instance->ScaleImGui(); });

    // General - About
    path.column = SECTION_COLUMN_2;

    AddWidget(path, "About", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Ship Of Harkinian", WIDGET_TEXT);
    if (gGitCommitTag[0] != 0) {
        AddWidget(path, gBuildVersion, WIDGET_TEXT);
    } else {
        AddWidget(path, ("Branch: " + std::string(gGitBranch)), WIDGET_TEXT);
        AddWidget(path, ("Commit: " + std::string(gGitCommitHash)), WIDGET_TEXT);
    }
    for (uint32_t i = 0; i < ResourceMgr_GetNumGameVersions(); i++) {
        AddWidget(path, GetGameVersionString(i), WIDGET_TEXT);
    }

    // Audio Settings
    path.sidebarName = "Audio";
    path.column = SECTION_COLUMN_1;
    AddSidebarEntry("Settings", "Audio", 3);

    AddWidget(path, "Master Volume: %d %%", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_SETTING("Volume.Master"))
        .RaceDisable(false)
        .Options(IntSliderOptions().Min(0).Max(100).DefaultValue(40).ShowButtons(true).Format(""));
    AddWidget(path, "Main Music Volume: %d %%", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_SETTING("Volume.MainMusic"))
        .RaceDisable(false)
        .Options(IntSliderOptions().Min(0).Max(100).DefaultValue(100).ShowButtons(true).Format(""))
        .Callback([](WidgetInfo& info) {
            Audio_SetGameVolume(SEQ_PLAYER_BGM_MAIN,
                                ((float)CVarGetInteger(CVAR_SETTING("Volume.MainMusic"), 100) / 100.0f));
        });
    AddWidget(path, "Sub Music Volume: %d %%", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_SETTING("Volume.SubMusic"))
        .RaceDisable(false)
        .Options(IntSliderOptions().Min(0).Max(100).DefaultValue(100).ShowButtons(true).Format(""))
        .Callback([](WidgetInfo& info) {
            Audio_SetGameVolume(SEQ_PLAYER_BGM_SUB,
                                ((float)CVarGetInteger(CVAR_SETTING("Volume.SubMusic"), 100) / 100.0f));
        });
    AddWidget(path, "Fanfare Volume: %d %%", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_SETTING("Volume.Fanfare"))
        .RaceDisable(false)
        .Options(IntSliderOptions().Min(0).Max(100).DefaultValue(100).ShowButtons(true).Format(""))
        .Callback([](WidgetInfo& info) {
            Audio_SetGameVolume(SEQ_PLAYER_FANFARE,
                                ((float)CVarGetInteger(CVAR_SETTING("Volume.Fanfare"), 100) / 100.0f));
        });
    AddWidget(path, "Sound Effects Volume: %d %%", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_SETTING("Volume.SFX"))
        .RaceDisable(false)
        .Options(IntSliderOptions().Min(0).Max(100).DefaultValue(100).ShowButtons(true).Format(""))
        .Callback([](WidgetInfo& info) {
            Audio_SetGameVolume(SEQ_PLAYER_SFX, ((float)CVarGetInteger(CVAR_SETTING("Volume.SFX"), 100) / 100.0f));
        });
    AddWidget(path, "Audio API (Needs reload)", WIDGET_AUDIO_BACKEND).RaceDisable(false);

    // Graphics Settings
    static int32_t maxFps = 360;
    const char* tooltip = "Uses Matrix Interpolation to create extra frames, resulting in smoother graphics. This is "
                          "purely visual and does not impact game logic, execution of glitches etc.\n\nA higher target "
                          "FPS than your monitor's refresh rate will waste resources, and might give a worse result.";
    path.sidebarName = "Graphics";
    AddSidebarEntry("Settings", "Graphics", 3);
    AddWidget(path, "Graphics Options", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Toggle Fullscreen", WIDGET_BUTTON)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) { Ship::Context::GetInstance()->GetWindow()->ToggleFullscreen(); })
        .Options(ButtonOptions().Tooltip("Toggles Fullscreen On/Off."));
    AddWidget(path, "Internal Resolution", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_INTERNAL_RESOLUTION)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) {
            Ship::Context::GetInstance()->GetWindow()->SetResolutionMultiplier(
                CVarGetFloat(CVAR_INTERNAL_RESOLUTION, 1));
        })
        .PreFunc([](WidgetInfo& info) {
            if (mSohMenu->disabledMap.at(DISABLE_FOR_ADVANCED_RESOLUTION_ON).active &&
                mSohMenu->disabledMap.at(DISABLE_FOR_VERTICAL_RES_TOGGLE_ON).active) {
                info.activeDisables.push_back(DISABLE_FOR_ADVANCED_RESOLUTION_ON);
                info.activeDisables.push_back(DISABLE_FOR_VERTICAL_RES_TOGGLE_ON);
            } else if (mSohMenu->disabledMap.at(DISABLE_FOR_LOW_RES_MODE_ON).active) {
                info.activeDisables.push_back(DISABLE_FOR_LOW_RES_MODE_ON);
            }
        })
        .Options(
            FloatSliderOptions()
                .Tooltip("Multiplies your output resolution by the value inputted, as a more intensive but effective "
                         "form of anti-aliasing.")
                .ShowButtons(false)
                .IsPercentage()
                .Min(0.5f)
                .Max(2.0f));
#ifndef __WIIU__
    AddWidget(path, "Anti-aliasing (MSAA)", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_MSAA_VALUE)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) {
            Ship::Context::GetInstance()->GetWindow()->SetMsaaLevel(CVarGetInteger(CVAR_MSAA_VALUE, 1));
        })
        .Options(
            IntSliderOptions()
                .Tooltip("Activates MSAA (multi-sample anti-aliasing) from 2x up to 8x, to smooth the edges of "
                         "rendered geometry.\n"
                         "Higher sample count will result in smoother edges on models, but may reduce performance.")
                .Min(1)
                .Max(8)
                .DefaultValue(1));
#endif
    auto fps = CVarGetInteger(CVAR_SETTING("InterpolationFPS"), 20);
    const char* fpsFormat = fps == 20 ? "Original (%d)" : "%d";
    AddWidget(path, "Current FPS", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_SETTING("InterpolationFPS"))
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) {
            auto options = std::static_pointer_cast<IntSliderOptions>(info.options);
            int32_t defaultValue = options->defaultValue;
            if (CVarGetInteger(info.cVar, defaultValue) == defaultValue) {
                options->format = "Original (%d)";
            } else {
                options->format = "%d";
            }
        })
        .PreFunc([](WidgetInfo& info) {
            if (mSohMenu->disabledMap.at(DISABLE_FOR_MATCH_REFRESH_RATE_ON).active)
                info.activeDisables.push_back(DISABLE_FOR_MATCH_REFRESH_RATE_ON);
        })
        .Options(IntSliderOptions().Tooltip(tooltip).Min(20).Max(maxFps).DefaultValue(20).Format(fpsFormat));
    AddWidget(path, "Match Refresh Rate", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("MatchRefreshRate"))
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip("Matches interpolation value to the refresh rate of your display."));
    AddWidget(path, "Renderer API (Needs reload)", WIDGET_VIDEO_BACKEND).RaceDisable(false);
    AddWidget(path, "Enable Vsync", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_VSYNC_ENABLED)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) { info.isHidden = mSohMenu->disabledMap.at(DISABLE_FOR_NO_VSYNC).active; })
        .Options(CheckboxOptions()
                     .Tooltip("Removes tearing, but clamps your max FPS to your displays refresh rate.")
                     .DefaultValue(true));
    AddWidget(path, "Windowed Fullscreen", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SDL_WINDOWED_FULLSCREEN)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = mSohMenu->disabledMap.at(DISABLE_FOR_NO_WINDOWED_FULLSCREEN).active;
        })
        .Options(CheckboxOptions().Tooltip("Enables Windowed Fullscreen Mode."));
    AddWidget(path, "Allow multi-windows", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENABLE_MULTI_VIEWPORTS)
        .RaceDisable(false)
        .PreFunc(
            [](WidgetInfo& info) { info.isHidden = mSohMenu->disabledMap.at(DISABLE_FOR_NO_MULTI_VIEWPORT).active; })
        .Options(CheckboxOptions()
                     .Tooltip("Allows multiple windows to be opened at once. Requires a reload to take effect.")
                     .DefaultValue(true));
    AddWidget(path, "Texture Filter (Needs reload)", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_TEXTURE_FILTER)
        .RaceDisable(false)
        .Options(ComboboxOptions().Tooltip("Sets the applied Texture Filtering.").ComboMap(textureFilteringMap));

    path.column = SECTION_COLUMN_2;
    AddWidget(path, "Advanced Graphics Options", WIDGET_SEPARATOR_TEXT);

    // Controls
    path.sidebarName = "Controls";
    path.column = SECTION_COLUMN_1;
    AddSidebarEntry("Settings", "Controls", 2);
    AddWidget(path, "Clear Devices", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            SohGui::mModalWindow->RegisterPopup(
                "Clear Config",
                "This will completely erase the controls config, including registered devices.\nContinue?", "Clear",
                "Cancel",
                []() {
                    Ship::Context::GetInstance()->GetConsoleVariables()->ClearBlock(CVAR_PREFIX_SETTING ".Controllers");
                    uint8_t bits = 0;
                    Ship::Context::GetInstance()->GetControlDeck()->Init(&bits);
                },
                nullptr);
        })
        .Options(ButtonOptions().Size(Sizes::Inline));
    AddWidget(path, "Controller Bindings", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Popout Bindings Window", WIDGET_WINDOW_BUTTON)
        .CVar(CVAR_WINDOW("ControllerConfiguration"))
        .RaceDisable(false)
        .WindowName("Configure Controller")
        .HideInSearch(true)
        .Options(WindowButtonOptions().Tooltip("Enables the separate Bindings Window."));

    // Input Viewer
    path.sidebarName = "Input Viewer";
    AddSidebarEntry("Settings", path.sidebarName, 3);
    AddWidget(path, "Input Viewer", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Toggle Input Viewer", WIDGET_WINDOW_BUTTON)
        .CVar(CVAR_WINDOW("InputViewer"))
        .RaceDisable(false)
        .WindowName("Input Viewer")
        .HideInSearch(true)
        .Options(WindowButtonOptions().Tooltip("Toggles the Input Viewer.").EmbedWindow(false));

    AddWidget(path, "Input Viewer Settings", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Popout Input Viewer Settings", WIDGET_WINDOW_BUTTON)
        .CVar(CVAR_WINDOW("InputViewerSettings"))
        .RaceDisable(false)
        .WindowName("Input Viewer Settings")
        .HideInSearch(true)
        .Options(WindowButtonOptions().Tooltip("Enables the separate Input Viewer Settings Window."));

    // Notifications
    path.sidebarName = "Notifications";
    path.column = SECTION_COLUMN_1;
    AddSidebarEntry("Settings", path.sidebarName, 3);
    AddWidget(path, "Position", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_SETTING("Notifications.Position"))
        .RaceDisable(false)
        .Options(ComboboxOptions()
                     .Tooltip("Which corner of the screen notifications appear in.")
                     .ComboMap(notificationPosition)
                     .DefaultIndex(3));
    AddWidget(path, "Duration (seconds):", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_SETTING("Notifications.Duration"))
        .RaceDisable(false)
        .Options(FloatSliderOptions()
                     .Tooltip("How long notifications are displayed for.")
                     .Format("%.1f")
                     .Step(0.1f)
                     .Min(3.0f)
                     .Max(30.0f)
                     .DefaultValue(10.0f));
    AddWidget(path, "Background Opacity", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_SETTING("Notifications.BgOpacity"))
        .RaceDisable(false)
        .Options(FloatSliderOptions()
                     .Tooltip("How opaque the background of notifications is.")
                     .DefaultValue(0.5f)
                     .IsPercentage());
    AddWidget(path, "Size:", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_SETTING("Notifications.Size"))
        .RaceDisable(false)
        .Options(FloatSliderOptions()
                     .Tooltip("How large notifications are.")
                     .Format("%.1f")
                     .Step(0.1f)
                     .Min(1.0f)
                     .Max(5.0f)
                     .DefaultValue(1.8f));
    AddWidget(path, "Test Notification", WIDGET_BUTTON)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) {
            Notification::Emit({
                .itemIcon = "__OTR__textures/icon_item_24_static/gQuestIconGoldSkulltulaTex",
                .prefix = "This",
                .message = "is a",
                .suffix = "test.",
            });
        })
        .Options(ButtonOptions().Tooltip("Displays a test notification."));
    AddWidget(path, "Mute Notification Sound", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("Notifications.Mute"))
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip("Prevent notifications from playing a sound."));

    // Mod Menu
    path.sidebarName = "Mod Menu";
    AddSidebarEntry("Settings", path.sidebarName, 1);
    AddWidget(path, "Popout Mod Menu Window", WIDGET_WINDOW_BUTTON)
        .CVar(CVAR_WINDOW("ModMenu"))
        .WindowName("Mod Menu")
        .HideInSearch(true)
        .Options(WindowButtonOptions().Tooltip("Enables the separate Mod Menu Window."));

    // =========================================================================
    // Skijer's NEI - 3 columns: Custom Items | MM Masks | Randomizer
    // =========================================================================
    path.sidebarName = "Skijer's NEI";
    path.column = SECTION_COLUMN_1;
    AddSidebarEntry("Settings", path.sidebarName, 3);

    // ===================== COLUMN 1: Custom Items =====================
    AddWidget(path, "Custom Items", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Enable Extra Equipment", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_RANDOMIZER_SETTING("ExtEquipment"))
        .RaceDisable(false)
        .PostFunc([](WidgetInfo& info) {
            // Mirror to the runtime CVar that gates page-2 visibility, L-toggle, and ownership grants.
            CVarSetInteger("gCheats.ExtEquip.Enabled",
                           CVarGetInteger(CVAR_RANDOMIZER_SETTING("ExtEquipment"), 0));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(CheckboxOptions().Tooltip(
            "Adds 12 new equipment pieces (3 swords, 3 shields, 3 tunics, 3 boots).\n"
            "Press L on the equipment page to toggle between vanilla and extended equipment.\n"
            "Seed-locked rando setting (also enables the in-game equipment system)."));

    AddWidget(path, "Enable Sage Spells", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_RANDOMIZER_SETTING("SW97Spells"))
        .Options(CheckboxOptions().Tooltip(
            "Allows the elemental medallions to grant elemental damage (seed-locked rando setting).\n"
            "Two paths (each consumes magic):\n"
            "  • Spell: Medallion alone acts as a passive elemental source (Fire = Din's Fire equivalent, Water = melt red ice, etc.).\n"
            "  • Projectile: Medallion + Bow (adult) or Slingshot (child) imbues the shot with the element.\n"
            "Synced with the same setting in the Randomizer menu (Logic Tricks)."));

    // Roc's Items MM Animations - requires mm.o2r
    AddWidget(path, "Roc's Items Use MM Animations", WIDGET_CVAR_CHECKBOX)
        .CVar("gEnhancements.RocsItemsUseMmAnims")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!MmAssets_IsAvailable()) {
                CVarSetInteger("gEnhancements.RocsItemsUseMmAnims", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0.";
            }
        })
        .Options(CheckboxOptions().Tooltip("Use MM animations for Roc's Feather and Roc's Cape jumps.\n"
                                           "Roc's Feather: Backflip on ground jump.\n"
                                           "Roc's Cape: Backflip on ground jump, roll jump on double jump.\n\n"
                                           "REQUIRES: mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0"));

    AddWidget(path, "Invert Roc's Items Animations", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.RocsItems.InvertAnims")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!CVarGetInteger("gEnhancements.RocsItemsUseMmAnims", 0)) {
                CVarSetInteger("gMods.RocsItems.InvertAnims", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Enable 'Roc's Items Use MM Animations' first.";
            }
        })
        .Options(CheckboxOptions().Tooltip("Swaps the animation order for Roc's items.\n"
                                           "OFF: Ground = Backflip, Double = Roll Jump\n"
                                           "ON:  Ground = Roll Jump, Double = Backflip\n\n"
                                           "REQUIRES: Roc's Items Use MM Animations"));

    AddWidget(path, "Page Navigation", WIDGET_SEPARATOR_TEXT);

    static std::map<int32_t, const char*> pageSwitchMap = {
        { 0, "L Button" },
        { 1, "A Button" },
        { 2, "Both (L + A)" },
    };
    AddWidget(path, "Page Switch Button", WIDGET_CVAR_COMBOBOX)
        .CVar("gMods.PageSwitch.Button")
        .RaceDisable(false)
        .Options(ComboboxOptions()
                     .ComboMap(pageSwitchMap)
                     .DefaultIndex(2)
                     .Tooltip("Choose which button switches inventory pages in the pause menu.\n"
                              "Applies to all inventory pages (vanilla, custom items, MM masks)."));

    AddWidget(path, "Mask Transformations", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Kafei Mask Transform", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.KafeiMaskTransform")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!std::filesystem::exists("nei/N64_Kafei.pak")) {
                CVarSetInteger("gMods.KafeiMaskTransform", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires N64_Kafei.pak in nei/ folder.";
            }
        })
        .Options(CheckboxOptions().Tooltip("Wearing the Kafei Mask transforms Link into Kafei.\n"
                                           "Adult Link becomes Adult Kafei, Child Link becomes Child Kafei.\n"
                                           "Remove the mask to revert.\n\n"
                                           "REQUIRES: nei/N64_Kafei.pak"));

    // ===================== COLUMN 2: MM Masks =====================
    path.column = SECTION_COLUMN_2;

    AddWidget(path, "MM Masks", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Extra Mask Effects", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.TransformMasks.ExtraEffects")
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip("Enables custom visual effects according to the mask being worn.\n"
                                           "Each mask has unique visual enhancements."));

    AddWidget(path, "Enable Transformation Masks", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.TransformMasks.Enabled")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!MmAssets_IsAvailable()) {
                CVarSetInteger("gMods.TransformMasks.Enabled", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0.\n"
                                                "Download 2Ship, extract your MM ROM, then copy mm.o2r here.";
            }
        })
        .Options(CheckboxOptions().Tooltip("Allows you to transform with certain masks like in Majora's Mask.\n"
                                           "Equip transformation masks from the MM Masks inventory page.\n\n"
                                           "REQUIRES: mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0"));

    AddWidget(path, "Instant Transform", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.TransformMasks.InstantTransform")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!MmAssets_IsAvailable()) {
                CVarSetInteger("gMods.TransformMasks.InstantTransform", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0.";
            } else if (!CVarGetInteger("gMods.TransformMasks.ExtraEffects", 0)) {
                CVarSetInteger("gMods.TransformMasks.InstantTransform", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Enable Extra Mask Effects first.";
            }
        })
        .Options(CheckboxOptions().Tooltip("Skip the transformation cutscene animation.\n"
                                           "Transform instantly when equipping a transformation mask.\n\n"
                                           "REQUIRES: Extra Mask Effects + mm.o2r"));

    AddWidget(path, "Include MM Masks Inventory", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.MmMasks.InventoryEnabled")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!MmAssets_IsAvailable()) {
                CVarSetInteger("gMods.MmMasks.InventoryEnabled", 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0.";
            }
        })
        .PostFunc([](WidgetInfo& info) {
            if (CVarGetInteger("gMods.MmMasks.InventoryEnabled", 0)) {
                CVarSetInteger("gMods.TransformMasks.Enabled", 1);
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
        })
        .Options(CheckboxOptions().Tooltip(
            "Adds a 3rd inventory page with all 24 MM masks.\n"
            "Transformation masks (Deku, Goron, Zora, Fierce Deity) trigger transformations.\n"
            "Removes OOT Goron/Zora masks from randomizer pool.\n\n"
            "REQUIRES: mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0"));

    AddWidget(path, "Instant Blast Mask", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.BlastMask.Instant")
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip("Removes the cooldown on Blast Mask.\n"
                                           "Normally there is a 310-frame (~5 second) cooldown between uses."));

    AddWidget(path, "Invisible Non-Transformation Masks", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("HideNonTransformationMasks"))
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip(
            "Turns all MM non-transformation masks invisible while still maintaining their effects.\n"
            "Transformation masks (Deku, Goron, Zora, Fierce Deity) remain visible.\n"
            "Only affects MM masks; vanilla OOT child masks are unaffected (use Invisible Bunny Hood for OOT bunny hood)."));

    // Pikachu Transformation is always enabled — use Pokeball item to transform

    AddWidget(path, "Custom Models (.pak)", WIDGET_SEPARATOR_TEXT);

    // Build model combobox maps per age (triggers lazy init of PakLoader)
    {
        s32 pakCount = PakLoader_GetModelCount();

        // Adult models
        std::map<int32_t, const char*> adultModelMap;
        adultModelMap[-1] = "Default Link";
        for (s32 i = 0; i < pakCount; i++) {
            if (PakLoader_ModelHasAdult(i)) {
                adultModelMap[i] = PakLoader_GetModelName(i);
            }
        }

        // Child models
        std::map<int32_t, const char*> childModelMap;
        childModelMap[-1] = "Default Link";
        for (s32 i = 0; i < pakCount; i++) {
            if (PakLoader_ModelHasChild(i)) {
                childModelMap[i] = PakLoader_GetModelName(i);
            }
        }

        AddWidget(path, "Enable Custom Player Model", WIDGET_CVAR_CHECKBOX)
            .CVar("gMods.PakLoader.Enabled")
            .RaceDisable(false)
            .PreFunc([](WidgetInfo& info) {
                if (PakLoader_GetModelCount() == 0) {
                    CVarSetInteger("gMods.PakLoader.Enabled", 0);
                    info.options->disabled = true;
                    info.options->disabledTooltip = "No .pak files found.\n"
                                                    "Place ModLoader64 .pak model files in the mods/ folder.";
                }
            })
            .PostFunc([](WidgetInfo& info) {
                if (CVarGetInteger("gMods.PakLoader.Enabled", 0)) {
                    s32 adultIdx = CVarGetInteger("gMods.PakLoader.AdultModel", -1);
                    s32 childIdx = CVarGetInteger("gMods.PakLoader.ChildModel", -1);
                    PakLoader_SelectAdultModel(adultIdx);
                    PakLoader_SelectChildModel(childIdx);
                } else {
                    PakLoader_SelectAdultModel(-1);
                    PakLoader_SelectChildModel(-1);
                }
            })
            .Options(CheckboxOptions().Tooltip("Replaces Link's model with a custom model from a .pak file.\n"
                                               "Place ModLoader64 zzplayas .pak files in the mods/ folder.\n"
                                               "You can choose different models for Adult and Child Link."));

        AddWidget(path, "Adult Link Model", WIDGET_CVAR_COMBOBOX)
            .CVar("gMods.PakLoader.AdultModel")
            .RaceDisable(false)
            .PreFunc([](WidgetInfo& info) {
                if (PakLoader_GetModelCount() == 0 || !CVarGetInteger("gMods.PakLoader.Enabled", 0)) {
                    info.options->disabled = true;
                }
            })
            .PostFunc([](WidgetInfo& info) {
                if (CVarGetInteger("gMods.PakLoader.Enabled", 0)) {
                    PakLoader_SelectAdultModel(CVarGetInteger("gMods.PakLoader.AdultModel", -1));
                }
            })
            .Options(ComboboxOptions()
                         .ComboMap(adultModelMap)
                         .DefaultIndex(-1)
                         .Tooltip("Choose a custom model for Adult Link."));

        AddWidget(path, "Child Link Model", WIDGET_CVAR_COMBOBOX)
            .CVar("gMods.PakLoader.ChildModel")
            .RaceDisable(false)
            .PreFunc([](WidgetInfo& info) {
                if (PakLoader_GetModelCount() == 0 || !CVarGetInteger("gMods.PakLoader.Enabled", 0)) {
                    info.options->disabled = true;
                }
            })
            .PostFunc([](WidgetInfo& info) {
                if (CVarGetInteger("gMods.PakLoader.Enabled", 0)) {
                    PakLoader_SelectChildModel(CVarGetInteger("gMods.PakLoader.ChildModel", -1));
                }
            })
            .Options(ComboboxOptions()
                         .ComboMap(childModelMap)
                         .DefaultIndex(-1)
                         .Tooltip("Choose a custom model for Child Link."));

        // Equipment-only paks
        std::map<int32_t, const char*> equipModelMap;
        equipModelMap[-1] = "Default Equipment";
        for (s32 i = 0; i < pakCount; i++) {
            if (PakLoader_ModelIsEquipmentOnly(i)) {
                equipModelMap[i] = PakLoader_GetModelName(i);
            }
        }

        if (equipModelMap.size() > 1) { // More than just "Default"
            AddWidget(path, "Equipment Pack", WIDGET_CVAR_COMBOBOX)
                .CVar("gMods.PakLoader.Equipment")
                .RaceDisable(false)
                .PreFunc([](WidgetInfo& info) {
                    if (!CVarGetInteger("gMods.PakLoader.Enabled", 0)) {
                        info.options->disabled = true;
                    }
                })
                .PostFunc([](WidgetInfo& info) {
                    if (CVarGetInteger("gMods.PakLoader.Enabled", 0)) {
                        PakLoader_SelectEquipment(CVarGetInteger("gMods.PakLoader.Equipment", -1));
                    }
                })
                .Options(ComboboxOptions()
                             .ComboMap(equipModelMap)
                             .DefaultIndex(-1)
                             .Tooltip("Choose a custom equipment pack.\n"
                                      "Replaces swords, shields, and other items.\n"
                                      "Overrides equipment from the body model."));
        }
    }

    // ===================== COLUMN 3: Randomizer =====================
    path.column = SECTION_COLUMN_3;

    AddWidget(path, "Randomizer", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Enable All NEI Rando", WIDGET_BUTTON)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) {
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("SkijerCustomItems"), 1);
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("ExtEquipment"), 1);
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("SW97Spells"), 1);
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("RocsFeather"), 1);
            // Mirror to runtime CVars so page-2 inventory and ext-equip system are reachable in-game.
            CVarSetInteger("gMods.CustomItems.Enabled", 1);
            CVarSetInteger("gCheats.ExtEquip.Enabled", 1);
            if (MmAssets_IsAvailable() && CVarGetInteger("gMods.MmMasks.InventoryEnabled", 0)) {
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksAll"), 1);
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksTransform"), 0);
            }
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(ButtonOptions().Tooltip(
            "One-click enables all NEI seed-locked rando settings:\n"
            "  • Skijer's Custom Items\n"
            "  • All MM Masks (requires mm.o2r + 'Include MM Masks Inventory')\n"
            "  • Extended Equipment\n"
            "  • Sage Spells (SW97)\n"
            "  • Roc's Feather"));

    AddWidget(path, "Disable All NEI Rando", WIDGET_BUTTON)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) {
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("SkijerCustomItems"), 0);
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("ExtEquipment"), 0);
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("SW97Spells"), 0);
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("RocsFeather"), 0);
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksAll"), 0);
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksTransform"), 0);
            // Mirror disable to runtime CVars (page-2 inventory + ext-equip system off).
            CVarSetInteger("gMods.CustomItems.Enabled", 0);
            CVarSetInteger("gCheats.ExtEquip.Enabled", 0);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(ButtonOptions().Tooltip("Turns off all 5 NEI seed-locked rando settings."));

    AddWidget(path, "Enable Custom Items", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_RANDOMIZER_SETTING("SkijerCustomItems"))
        .PostFunc([](WidgetInfo& info) {
            // Mirror to the runtime CVar that gates page-2 visibility in the pause menu.
            CVarSetInteger("gMods.CustomItems.Enabled",
                           CVarGetInteger(CVAR_RANDOMIZER_SETTING("SkijerCustomItems"), 0));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(CheckboxOptions().Tooltip("Enables the 24 custom items on the second inventory page (seed-locked rando setting).\n"
                                           "When enabled, these items are also added to the randomizer pool and gated logic paths.\n"
                                           "When disabled, page 2 is inaccessible and items are not in rando.\n"
                                           "Synced with the same setting in the Randomizer menu."));

    AddWidget(path, "Add All MM Masks to Rando", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_RANDOMIZER_SETTING("MmMasksAll"))
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!MmAssets_IsAvailable()) {
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksAll"), 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0.";
            } else if (!CVarGetInteger("gMods.MmMasks.InventoryEnabled", 0)) {
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksAll"), 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Enable 'Include MM Masks Inventory' first.";
            }
        })
        .PostFunc([](WidgetInfo& info) {
            if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("MmMasksAll"), 0)) {
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksTransform"), 0);
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
        })
        .Options(CheckboxOptions().Tooltip("Adds all 24 MM masks to the randomizer item pool.\n"
                                           "Masks can be found at random locations like custom items.\n"
                                           "Removes OOT Goron/Zora masks from pool.\n\n"
                                           "Seed-locked rando setting.\n"
                                           "REQUIRES: 'Include MM Masks Inventory' enabled"));

    AddWidget(path, "Add Transformation Masks to Rando", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_RANDOMIZER_SETTING("MmMasksTransform"))
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!MmAssets_IsAvailable()) {
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksTransform"), 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Requires mm.o2r from 2Ship2Harkinian Keiichi Alfa 4.0.0.";
            } else if (!CVarGetInteger("gMods.MmMasks.InventoryEnabled", 0)) {
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksTransform"), 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "Enable 'Include MM Masks Inventory' first.";
            } else if (CVarGetInteger(CVAR_RANDOMIZER_SETTING("MmMasksAll"), 0)) {
                CVarSetInteger(CVAR_RANDOMIZER_SETTING("MmMasksTransform"), 0);
                info.options->disabled = true;
                info.options->disabledTooltip = "'Add All MM Masks to Rando' already includes transformation masks.";
            }
        })
        .Options(CheckboxOptions().Tooltip("Adds only the 4 transformation masks (Deku, Goron, Zora, Fierce Deity)\n"
                                           "to the randomizer item pool.\n"
                                           "Removes OOT Goron/Zora masks from pool.\n\n"
                                           "Seed-locked rando setting.\n"
                                           "REQUIRES: 'Include MM Masks Inventory' enabled"));

    AddWidget(path, "Add Extended Equipment to Rando", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_RANDOMIZER_SETTING("ExtEquipment"))
        .RaceDisable(false)
        .PostFunc([](WidgetInfo& info) {
            CVarSetInteger("gCheats.ExtEquip.Enabled",
                           CVarGetInteger(CVAR_RANDOMIZER_SETTING("ExtEquipment"), 0));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(CheckboxOptions().Tooltip(
            "Adds the 12 extended equipment pieces (3 swords, 3 shields, 3 tunics, 3 boots) to the randomizer pool.\n"
            "Press L on the equipment page to toggle between vanilla and extended equipment.\n\n"
            "Seed-locked rando setting (also enables the in-game equipment system)."));

    AddWidget(path, "Bomb Arrows: Auto-grant with Bomb Bag", WIDGET_CVAR_CHECKBOX)
        .CVar("gMods.BombArrows.AutoGrantOnBag")
        .RaceDisable(false)
        .Options(CheckboxOptions().Tooltip(
            "Cheat: automatically gives ITEM_BOMB_ARROWS the moment you obtain any bomb bag.\n"
            "When on, the new bow/slingshot arrow wheel will show a Bomb entry as soon as the\n"
            "bag is yours. Has no effect on the randomizer item pool."));
}

} // namespace SohGui
