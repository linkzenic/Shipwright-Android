#include "SohMenu.h"
#include "SohGui.hpp"
#include "soh/OTRGlobals.h"
#include "UIWidgets.hpp"

namespace SohGui {

extern std::shared_ptr<SohMenu> mSohMenu;
using namespace UIWidgets;

// "Wind Waker Style" — the home for the Wind Waker-flavoured rendering features. The internal CVar keys
// keep their original "ToonLighting" / "WorldLighting" names so existing settings/saves are unaffected;
// only the navigation moved here from Settings.
void SohMenu::AddMenuWindWakerStyle() {
    AddMenuEntry("Wind Waker Style", CVAR_SETTING("Menu.WindWakerStyleSidebarSection"));

    // ===========================================================================================
    // Cel Shading — relights actors/objects with a single dominant light and a soft toon ramp.
    // ===========================================================================================
    auto hideUnlessCelEnabled = [](WidgetInfo& info) {
        info.isHidden = !CVarGetInteger(CVAR_ENHANCEMENT("Graphics.ToonLighting.Enabled"), 1);
    };
    WidgetPath path = { "Wind Waker Style", "Cel Shading", SECTION_COLUMN_1 };
    // 3 columns with the controls kept in column 1 (like the Audio page) so the sliders sit in a narrow
    // left strip and the game stays visible behind the menu while you tune the values.
    AddSidebarEntry("Wind Waker Style", "Cel Shading", 3);
    AddWidget(path, "Enable Cel Shading", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("Graphics.ToonLighting.Enabled"))
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Re-lights actors and objects with a single dominant light and a soft Wind Waker-style ramp. "
            "Only affects objects, not the static scene. Pairs well with cel-shaded texture packs."));
    AddWidget(path, "Options", WIDGET_SEPARATOR_TEXT).PreFunc(hideUnlessCelEnabled);
    AddWidget(path, "Reset All to Defaults", WIDGET_BUTTON)
        .PreFunc(hideUnlessCelEnabled)
        .Callback([](WidgetInfo& info) {
            // Clearing each CVar drops it back to the slider's DefaultValue (the same value the renderer
            // falls back to), so this restores the default look without hardcoding the numbers twice.
            CVarClear(CVAR_ENHANCEMENT("Graphics.ToonLighting.RampCenter"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.ToonLighting.RampSoftness"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.ToonLighting.HighlightIntensity"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.ToonLighting.ShadowIntensity"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.ToonLighting.PointLightRange"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.ToonLighting.TransitionTime"));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(ButtonOptions().Tooltip("Resets all the Cel Shading sliders below to their default values."));
    AddWidget(path, "Ramp Center", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.ToonLighting.RampCenter"))
        .RaceDisable(false)
        .PreFunc(hideUnlessCelEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("Where the dark-to-light transition sits. Higher = more of the surface "
                              "stays in shadow.")
                     .Min(0.0f)
                     .Max(1.0f)
                     .DefaultValue(0.5f)
                     .IsPercentage());
    AddWidget(path, "Ramp Softness", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.ToonLighting.RampSoftness"))
        .RaceDisable(false)
        .PreFunc(hideUnlessCelEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("Width of the transition band. Low = a hard cel edge; high = a softer "
                              "gradient.")
                     .Format("%.2f") // 2 decimals; the 0.01 step makes the drag land on hundredths (no snap)
                     .Min(0.01f)
                     .Max(0.2f)
                     .DefaultValue(0.02f));
    AddWidget(path, "Highlight Intensity", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.ToonLighting.HighlightIntensity"))
        .RaceDisable(false)
        .PreFunc(hideUnlessCelEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("Brightness of the lit side. Higher = brighter highlights.")
                     .Min(0.0f)
                     .Max(2.0f)
                     .DefaultValue(0.6f)
                     .IsPercentage());
    AddWidget(path, "Shadow Intensity", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.ToonLighting.ShadowIntensity"))
        .RaceDisable(false)
        .PreFunc(hideUnlessCelEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("How dark the shadow side gets. 0% = no shadow (flat), 100% = full "
                              "shadow down to ambient.")
                     .Min(0.0f)
                     .Max(1.0f)
                     .DefaultValue(0.6f)
                     .IsPercentage());
    AddWidget(path, "Point Light Range", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.ToonLighting.PointLightRange"))
        .RaceDisable(false)
        .PreFunc(hideUnlessCelEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("Extends how far a point light can remain an object's key light, as a "
                              "multiplier on its actual radius (key selection only — the game's real "
                              "lighting is unchanged). Raise it so an orbiting fairy keeps lighting nearby "
                              "objects even when it swings to its far side. 1x = the light's literal range.")
                     .Format("%.1fx")
                     .Min(1.0f)
                     .Max(4.0f)
                     .DefaultValue(1.5f));
    AddWidget(path, "Transition Time", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.ToonLighting.TransitionTime"))
        .RaceDisable(false)
        .PreFunc(hideUnlessCelEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("How long the key light takes to ease from one source to another. Higher "
                              "= slower, more deliberate travel between the sun and a fairy/torch.")
                     .Format("%.1fs")
                     .Min(0.1f)
                     .Max(6.0f)
                     .DefaultValue(1.0f));
    AddWidget(path, "Debug", WIDGET_SEPARATOR_TEXT).PreFunc(hideUnlessCelEnabled);
    AddWidget(path, "Light Source Viewer", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_DEVELOPER_TOOLS("ToonLighting.ShowDebug"))
        .PreFunc(hideUnlessCelEnabled)
        .Options(CheckboxOptions().Tooltip(
            "Draws a debug ray from each actor for every candidate light (coloured by the light, longer "
            "when stronger), a cyan range ring around each point light, and a bold magenta needle down "
            "the chosen key light, so you can see which light is winning and where the key points."));
    AddWidget(path, "Highlight Lit Objects", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_DEVELOPER_TOOLS("ToonLighting.HighlightBands"))
        .PreFunc(hideUnlessCelEnabled)
        .Options(CheckboxOptions().Tooltip(
            "Renders every cel-shaded object as flat white on the lit side and flat black in shadow (the "
            "texture is discarded), so it is obvious which draws are being relit — handy for confirming "
            "whether large surfaces like water or lava are getting relit."));

    // ===========================================================================================
    // Lights — Wind Waker flame-flicker tweaks (Misc) plus the cast light pools (Light Casting). On by
    // default. Internal CVar keys keep their "WorldLighting" names.
    // ===========================================================================================
    auto hideUnlessLightCastEnabled = [](WidgetInfo& info) {
        info.isHidden = !CVarGetInteger(CVAR_ENHANCEMENT("Graphics.WorldLighting.Enabled"), 0);
    };
    // Navi's pool sliders need both Light Casting and Navi Light Casting on.
    auto hideUnlessNaviCast = [](WidgetInfo& info) {
        info.isHidden = !CVarGetInteger(CVAR_ENHANCEMENT("Graphics.WorldLighting.Enabled"), 0) ||
                        !CVarGetInteger(CVAR_ENHANCEMENT("Graphics.WorldLighting.UseNaviLight"), 1);
    };
    // Rotation Speed / Size Flicker sit with the cast-pool controls, but hide entirely while "Use Wind Waker
    // default movement" is on (the renderer pins them to the authentic 1x).
    auto hideUnlessCustomMovement = [](WidgetInfo& info) {
        info.isHidden = !CVarGetInteger(CVAR_ENHANCEMENT("Graphics.WorldLighting.Enabled"), 0) ||
                        CVarGetInteger(CVAR_ENHANCEMENT("Graphics.WorldLighting.WWDefaultMovement"), 1);
    };
    path.sidebarName = "Lights";
    path.column = SECTION_COLUMN_1;
    AddSidebarEntry("Wind Waker Style", "Lights", 2); // Misc in column 1, Light Casting in column 2

    // --- Misc: scene-wide tweaks, independent of the cast pools ---
    AddWidget(path, "Misc", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Hide Vanilla Torch Glow", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldLighting.HideVanillaGlow"))
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Hides the original flat, billboarded, flickering glow circle the game draws over torches and "
            "other glow lights (it clashes with the cast pools). Applies while Light Casting is on."));
    AddWidget(path, "Improve Flame Flicker", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldLighting.ImproveFlameFlicker"))
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Replaces the game's fast, jagged per-frame torch/flame flicker with a slow, organic Wind Waker "
            "flicker. Applied at the source, so it affects the vanilla scene lighting and Cel Shading even "
            "when Light Casting is off."));
    AddWidget(path, "Flicker Speed", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldLighting.FlickerSpeed"))
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            info.options->disabled =
                !CVarGetInteger(CVAR_ENHANCEMENT("Graphics.WorldLighting.ImproveFlameFlicker"), 1);
            info.options->disabledTooltip = "Enable \"Improve Flame Flicker\" to adjust this.";
        })
        .Options(FloatSliderOptions()
                     .Tooltip("How often flames pick a new brightness for the Wind Waker flicker. Higher = "
                              "faster; lower = a lazier flame.")
                     .Format("%.2fx")
                     .Min(0.1f)
                     .Max(3.0f)
                     .DefaultValue(1.0f));
    AddWidget(path, "Navi's Light Tint", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldLighting.NaviSaturation"))
        .RaceDisable(false)
        .Options(FloatSliderOptions()
                     .Tooltip("Tints Navi's light toward her current colour. Her light is normally white, but "
                              "she changes colour when targeting (yellow on enemies, and so on); raise this to "
                              "let a little of that colour through. Applied at the source, so it tints her cast "
                              "pool, the objects she lights under Cel Shading, and the vanilla lighting alike. "
                              "0% = white.")
                     .Min(0.0f)
                     .Max(1.0f)
                     .DefaultValue(0.2f)
                     .IsPercentage());

    // --- Light Casting: the cast light pools ---
    // A slider with a "Reset" button to its right that clears only that slider's CVar (matching the
    // Cosmetics Editor's Silly tab). Drawn as a WIDGET_CUSTOM because the declarative widgets can't nudge the
    // cursor between the slider and a same-line button — the label sits above the bar, so the button has to
    // be dropped two rows to line up with it. `format` may be nullptr to use the default. The PreFunc hides
    // the whole row when light casting (and, for Navi rows, Navi casting) is off.
    auto addSliderWithReset = [&](const char* label, const char* cvar, float minVal, float maxVal, float defVal,
                                  const char* format, bool isPercentage, auto hideFunc, const char* tooltip) {
        AddWidget(path, label, WIDGET_CUSTOM).PreFunc(hideFunc).CustomFunction([=](WidgetInfo&) {
            float sliderWidth = ImGui::GetContentRegionAvail().x - 90.0f; // leave room for the Reset button
            if (sliderWidth < 80.0f) {
                sliderWidth = 80.0f;
            }
            auto opts = FloatSliderOptions()
                            .Tooltip(tooltip)
                            .Min(minVal)
                            .Max(maxVal)
                            .DefaultValue(defVal)
                            .Size(ImVec2(sliderWidth, 0.0f))
                            .Color(THEME_COLOR);
            if (format != nullptr) {
                opts.Format(format);
            }
            if (isPercentage) {
                opts.IsPercentage();
            }
            CVarSliderFloat(label, cvar, opts);
            // Drop the button down to the slider bar (the label is on the row above it).
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::CalcTextSize("g").y * 2));
            std::string resetId = "Reset##WWL_" + std::string(label);
            if (Button(resetId.c_str(),
                       ButtonOptions().Size(ImVec2(80.0f, 36.0f)).Padding(ImVec2(5.0f, 0.0f)).Color(THEME_COLOR))) {
                CVarClear(cvar);
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
        });
    };
    // Light Casting fills the second column, beside the Misc section.
    path.column = SECTION_COLUMN_2;
    AddWidget(path, "Light Casting", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Enable Light Casting", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldLighting.Enabled"))
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Casts a pool of light from each point light (torch, fairy, ...) onto the surrounding world "
            "geometry, Wind Waker-style. Affects only the static world, not actors/objects (lit by Cel "
            "Shading)."));
    AddWidget(path, "Enable Navi Light Casting", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldLighting.UseNaviLight"))
        .RaceDisable(false)
        .PreFunc(hideUnlessLightCastEnabled)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Also cast a pool from Link's fairy (Navi). Navi darts around quickly, so her pool moves a lot."));
    AddWidget(path, "Use Wind Waker default movement", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldLighting.WWDefaultMovement"))
        .RaceDisable(false)
        .PreFunc(hideUnlessLightCastEnabled)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Pins the pool's tumble and size pulse to the authentic Wind Waker rates. Turn off to reveal and "
            "set Rotation Speed and Size Flicker yourself."));
    AddWidget(path, "Rotation Speed", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldLighting.RotationSpeed"))
        .RaceDisable(false)
        .PreFunc(hideUnlessCustomMovement)
        .Options(FloatSliderOptions()
                     .Tooltip("Speed of the Wind Waker two-axis tumble that animates the pool's faceted "
                              "edges, as a multiplier on the authentic rate. 1.0 = authentic; 0 = static.")
                     .Format("%.2fx")
                     .Min(0.0f)
                     .Max(3.0f)
                     .DefaultValue(1.0f));
    AddWidget(path, "Size Flicker", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldLighting.SizeFlicker"))
        .RaceDisable(false)
        .PreFunc(hideUnlessCustomMovement)
        .Options(FloatSliderOptions()
                     .Tooltip("Depth of the Wind Waker size pulse — the pool's dominant flicker. The orb "
                              "gently grows/shrinks on a slow random walk (re-rolled every ~0.2 s, eased). "
                              "1.0 = authentic (~5%); 0 = steady. (Navi is excluded — she isn't a flame.)")
                     .Format("%.2f")
                     .Min(0.0f)
                     .Max(3.0f)
                     .DefaultValue(1.0f));
    addSliderWithReset("Cast Size", CVAR_ENHANCEMENT("Graphics.WorldLighting.SphereSize"), 0.1f, 4.0f, 0.5f,
                       "%.2fx", false, hideUnlessLightCastEnabled,
                       "Size of each light's cast pool, as a multiplier on the light's radius. Smaller keeps "
                       "the pool tight around the source; larger spreads it wider.");
    addSliderWithReset("Navi Cast Size", CVAR_ENHANCEMENT("Graphics.WorldLighting.NaviSphereSize"), 0.1f, 4.0f,
                       0.75f, "%.2fx", false, hideUnlessNaviCast,
                       "Navi's pool size, separate from the main Cast Size, so you can keep Navi tight "
                       "without shrinking the torches.");
    addSliderWithReset("Light Intensity", CVAR_ENHANCEMENT("Graphics.WorldLighting.Intensity"), 0.0f, 2.0f,
                       0.2f, nullptr, true, hideUnlessLightCastEnabled, "Brightness of the cast light pools.");
    addSliderWithReset("Navi Light Intensity", CVAR_ENHANCEMENT("Graphics.WorldLighting.NaviIntensity"), 0.0f,
                       2.0f, 0.2f, nullptr, true, hideUnlessNaviCast,
                       "Navi's pool brightness, separate from the main Light Intensity.");
    AddWidget(path, "Debug", WIDGET_SEPARATOR_TEXT).PreFunc(hideUnlessLightCastEnabled);
    AddWidget(path, "Show Light Spheres", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_DEVELOPER_TOOLS("WorldLighting.ShowLightSpheres"))
        .PreFunc(hideUnlessLightCastEnabled)
        .Options(CheckboxOptions().Tooltip(
            "Overlays a translucent faceted shell of each light's icosphere — the volume used for its cast "
            "pool — tinted by the light, so you can see where the pools are, their size, and their spin. "
            "(The renderer has no line primitive, so this is a shell rather than a true wireframe.)"));

    // ===========================================================================================
    // Actor Shadows — Wind Waker-style shape shadows: each actor casts its own silhouette onto the ground
    // (following slopes), from the same key light Cel Shading uses, with a soft edge. Replaces the vanilla
    // blob/feet shadows. Internal CVar keys use "WorldShadows"; the UI says "Actor Shadows".
    // ===========================================================================================
    auto hideUnlessShadowsEnabled = [](WidgetInfo& info) {
        info.isHidden = !CVarGetInteger(CVAR_ENHANCEMENT("Graphics.WorldShadows.Enabled"), 0);
    };
    path.sidebarName = "Actor Shadows";
    path.column = SECTION_COLUMN_1;
    AddSidebarEntry("Wind Waker Style", "Actor Shadows", 3);
    AddWidget(path, "Enable Actor Shadows", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldShadows.Enabled"))
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Replaces the vanilla actor shadows with a shape-based drop shadow for each actor (Link, NPCs, "
            "enemies, items, ...): its own silhouette cast from the single key light Cel Shading picks, wrapped "
            "onto the real ground so it follows slopes and bumps. Off by default (vanilla shadows). Uses the "
            "Cel Shading key selection, but works whether or not Cel Shading itself is on."));
    AddWidget(path, "Suppress Vanilla Shadows", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldShadows.SuppressVanillaShadows"))
        .RaceDisable(false)
        .PreFunc(hideUnlessShadowsEnabled)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Hide the original game's actor shadows (Link's feet, the NPC/enemy circles, the horse shadow, "
            "the sign and snake-statue texture shadows) so only the new shape shadows show. Turn off to draw "
            "both."));
    AddWidget(path, "Options", WIDGET_SEPARATOR_TEXT).PreFunc(hideUnlessShadowsEnabled);
    AddWidget(path, "Reset All to Defaults", WIDGET_BUTTON)
        .PreFunc(hideUnlessShadowsEnabled)
        .Callback([](WidgetInfo& info) {
            CVarClear(CVAR_ENHANCEMENT("Graphics.WorldShadows.SuppressVanillaShadows"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.WorldShadows.Opacity"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.WorldShadows.Length"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.WorldShadows.SlabDepth"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.WorldShadows.SlabRise"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.WorldShadows.MaxDistance"));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(ButtonOptions().Tooltip("Resets all the Actor Shadows sliders below to their default values."));
    AddWidget(path, "Opacity", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldShadows.Opacity"))
        .RaceDisable(false)
        .PreFunc(hideUnlessShadowsEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("How dark the shadow's core is. 0 = invisible; higher = darker.")
                     .Min(0.0f)
                     .Max(1.0f)
                     .DefaultValue(0.2f)
                     .IsPercentage());
    AddWidget(path, "Length", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldShadows.Length"))
        .RaceDisable(false)
        .PreFunc(hideUnlessShadowsEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("How long the shadow may get. The key light is raised toward straight-overhead "
                              "before projecting, so a low light still casts a short shadow tucked under the "
                              "actor (like the vanilla shadow). Lower = always short and steep; higher = lets "
                              "a low light stretch the shadow out further.")
                     .Format("%.2f")
                     .Min(0.0f)
                     .Max(1.0f)
                     .DefaultValue(0.2f));
    AddWidget(path, "Slab Depth", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldShadows.SlabDepth"))
        .RaceDisable(false)
        .PreFunc(hideUnlessShadowsEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("How far below the feet the shadow conforms to the ground. The shadow is a thin "
                              "stencil 'slab' at the feet that wraps onto whatever ground is inside it. Higher = "
                              "follows ground that dips further (steeper inclines), but past a ledge the shadow "
                              "creeps further down the drop. Lower = clings tight to the feet and won't spill "
                              "over cliff edges, but may clip on steep slopes.")
                     .Format("%.0f")
                     .Min(5.0f)
                     .Max(200.0f)
                     .DefaultValue(8.0f));
    AddWidget(path, "Slab Rise", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldShadows.SlabRise"))
        .RaceDisable(false)
        .PreFunc(hideUnlessShadowsEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("How far ABOVE the feet the shadow can climb onto rising ground. Raise this so the "
                              "shadow still appears where an incline rises higher than the actor's feet (without "
                              "it, the shadow vanishes on up-slopes). Too high starts to catch the actor's own "
                              "lower legs, so keep it just above the ground rise you need.")
                     .Format("%.0f")
                     .Min(0.0f)
                     .Max(120.0f)
                     .DefaultValue(8.0f));
    AddWidget(path, "Render Distance: %d", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_ENHANCEMENT("Graphics.WorldShadows.MaxDistance"))
        .RaceDisable(false)
        .PreFunc(hideUnlessShadowsEnabled)
        .Options(IntSliderOptions()
                     .Tooltip("Performance: actors farther than this from the camera get no shape shadow (each "
                              "shadow redraws the actor's whole silhouette once per tap, so distant ones cost "
                              "more than they're worth). Lower to gain frames in crowded scenes; raise for "
                              "shadows that stay visible into the distance.")
                     .Min(300)
                     .Max(5000)
                     .DefaultValue(800)
                     .ShowButtons(true)
                     .Format("%d"));
    AddWidget(path, "Debug", WIDGET_SEPARATOR_TEXT).PreFunc(hideUnlessShadowsEnabled);
    AddWidget(path, "Show Shadow Volume", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_DEVELOPER_TOOLS("WorldShadows.ShowVolume"))
        .PreFunc(hideUnlessShadowsEnabled)
        .Options(CheckboxOptions().Tooltip(
            "Draws the actual 3D shadow volume translucently so you can see its shape: black top/bottom caps, "
            "blue side walls. The ground inside this volume is what gets shadowed."));
}

} // namespace SohGui
