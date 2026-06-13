/**
 * spiritual_stones.cpp — see spiritual_stones.h for the spec.
 *
 * State layout (per save):
 *   - passive[3]      : 0/1 buff toggle per stone
 *   - warp[3]         : entranceId == -1 means "no warp set"
 *
 * Lifetime hooks:
 *   - SaveManager AddInit/Save/Load: persist passive + warp per slot.
 *   - GameInteractor OnOpenText: inject the three "warp to ..." custom
 *     messages with TWO_WAY_CHOICE.
 *   - GameInteractor OnSceneSpawnActors: re-spawn statue actors for any warp
 *     point that lives in the freshly loaded scene. The actors are defined
 *     in mods/actors/spiritual_stone_statue.c (somaria-cubes-style hijack of
 *     ACTOR_EN_LIGHTBOX) and drawn from there.
 *   - z_player.c calls SpiritualStone_TickHold(play, this) from
 *     Player_UpdateCommon — same hook point used by Sw97_TickShadowExchange.
 */

#include "spiritual_stones.h"

#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"

// OPEN_DISPS / CLOSE_DISPS in macros.h redeclare these two symbols inline at
// every call site. Including frame_interpolation.h is not enough on MSVC — the
// in-block redeclaration inside the macro takes the linkage of the surrounding
// C++ context, so the linker hunts for a mangled C++ name. Force the C symbol
// at file scope so the macro's redeclaration matches. (Same trick as
// PropHunt.cpp / TriforceThief.cpp / VisualAgony.cpp.)
extern "C" {
    void FrameInterpolation_RecordOpenChild(const void* a, int b);
    void FrameInterpolation_RecordCloseChild(void);
}

extern "C" {
#include "z64.h"
#include "global.h"
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

// Pull the statue actor in as part of this translation unit. The .c file lives
// under mods/actors/ for consistency with somaria_cubes.c — and like that one,
// it is not in vcxproj; consumers #include it directly.
//
// We wrap the .c include in extern "C" so the OPEN_DISPS / CLOSE_DISPS macros
// inside Statue_Draw inherit C linkage on their inline FrameInterpolation_*
// redeclarations — otherwise MSVC would mangle them and the link would fail
// (same issue PropHunt.cpp warns about).
extern "C" {
#include "../actors/spiritual_stone_statue.h"
#include "../actors/spiritual_stone_statue.c"
}

// ============================================================================
// State
// ============================================================================

namespace {

struct StoneWarp {
    s32 entranceId; // -1 = unset
    s16 sceneId;
    s8 roomNum;
    s16 rotY;
    Vec3f pos;
};

struct StonesState {
    u8 passive[SPIRITUAL_STONE_COUNT];
    StoneWarp warp[SPIRITUAL_STONE_COUNT];
};

StonesState gState = {};

// Reset to a clean state — used both on InitFile and at the top of LoadFile
// (so a save written without our section comes back to defaults).
void ResetState() {
    for (int i = 0; i < SPIRITUAL_STONE_COUNT; ++i) {
        gState.passive[i] = 0;
        gState.warp[i].entranceId = -1;
        gState.warp[i].sceneId = -1;
        gState.warp[i].roomNum = 0;
        gState.warp[i].rotY = 0;
        gState.warp[i].pos = { 0.0f, 0.0f, 0.0f };
    }
}

// Tap-to-warp prompt plumbing. When a short press releases on a stone with
// an existing warp, we set sPendingWarpStone and open the custom textbox.
// The per-frame tick then watches for msgMode == MSGMODE_NONE and reads
// choiceIndex to decide whether to actually warp.
s32 sPendingWarpStone = -1;
s32 sHoldFrames[SPIRITUAL_STONE_COUNT] = { 0, 0, 0 };

// Text IDs for the yes/no warp prompt — picked from an empty range above the
// custom-message shop block (0x9100..0x94FF) and below 0xFFFD.
constexpr uint16_t kStoneWarpTextIdBase = 0x9FA0;

constexpr const char* kMessageTableId = "SpiritualStones";

// ============================================================================
// Helpers
// ============================================================================

// Master toggle (NEI "Spells" tab). Default ON. When OFF, the spiritual stones
// behave VANILLA: no passive buffs, no warp prompt, no statues, no equip hijack.
// Gated at runtime (in the hooks + the public API) so toggling takes effect
// without a restart.
inline bool StonesEnabled() {
    return CVarGetInteger("gMods.SpiritualStones.Enabled", 1) != 0;
}

inline s32 StoneItemId(int stone) {
    switch (stone) {
        case SPIRITUAL_STONE_KOKIRI: return ITEM_KOKIRI_EMERALD;
        case SPIRITUAL_STONE_GORON:  return ITEM_GORON_RUBY;
        case SPIRITUAL_STONE_ZORA:   return ITEM_ZORA_SAPPHIRE;
    }
    return ITEM_NONE;
}

inline s32 StoneQuestPoint(int stone) {
    // 0x12 = QUEST_KOKIRI_EMERALD, 0x13 = QUEST_GORON_RUBY, 0x14 = QUEST_ZORA_SAPPHIRE.
    return QUEST_KOKIRI_EMERALD + stone;
}

inline s32 StoneOwned(int stone) {
    return CHECK_QUEST_ITEM(StoneQuestPoint(stone));
}

inline int CursorPointToStone(s16 cursorPoint) {
    if (cursorPoint >= QUEST_KOKIRI_EMERALD && cursorPoint <= QUEST_ZORA_SAPPHIRE) {
        return cursorPoint - QUEST_KOKIRI_EMERALD;
    }
    return -1;
}

// Returns the C-button slot index (0..6) currently bound to a given stone
// item, or -1 if none. Mirrors how Sw97_TickShadowExchange scans buttonItems
// — including DPad slots when the DpadEquips CVar is on.
int StoneBoundCButtonSlot(int stone) {
    s32 itemId = StoneItemId(stone);
    // buttonItems[0]=B, [1..3]=C-Left/Down/Right, [4..7]=DPad U/D/L/R.
    int maxSlot = CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) ? 7 : 3;
    for (int slot = 1; slot <= maxSlot; ++slot) {
        if (gSaveContext.equips.buttonItems[slot] == itemId &&
            (slot > 3 || gSaveContext.equips.cButtonSlots[slot - 1] == 0xFF)) {
            return slot;
        }
    }
    return -1;
}

u16 ButtonMaskForSlot(int slot) {
    switch (slot) {
        case 1: return BTN_CLEFT;
        case 2: return BTN_CDOWN;
        case 3: return BTN_CRIGHT;
        case 4: return BTN_DUP;
        case 5: return BTN_DDOWN;
        case 6: return BTN_DLEFT;
        case 7: return BTN_DRIGHT;
    }
    return 0;
}

const char* StoneNameEnglish(int stone) {
    switch (stone) {
        case SPIRITUAL_STONE_KOKIRI: return "Kokiri Emerald";
        case SPIRITUAL_STONE_GORON:  return "Goron's Ruby";
        case SPIRITUAL_STONE_ZORA:   return "Zora's Sapphire";
    }
    return "Spiritual Stone";
}

// ============================================================================
// Warp execution — mirrors the in-game branch of Warping.cpp's Warp().
// ============================================================================

void ExecuteWarp(int stone) {
    if (gPlayState == nullptr) return;
    const StoneWarp& w = gState.warp[stone];
    if (w.entranceId < 0) return;

    gPlayState->nextEntranceIndex = w.entranceId;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_INSTANT;

    gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = w.entranceId;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].roomIndex = w.roomNum;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].pos = w.pos;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].yaw = w.rotY;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].playerParams = 0xDFF;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK_FAST;
    gSaveContext.respawnFlag = 1;

    static HOOK_ID hookId = 0;
    hookId = REGISTER_VB_SHOULD(VB_INFLICT_VOID_DAMAGE, {
        *should = false;
        GameInteractor::Instance->UnregisterGameHookForID<GameInteractor::OnVanillaBehavior>(hookId);
    });
}

// ============================================================================
// Statue summon
// ============================================================================

void SummonStatueHere(PlayState* play, int stone) {
    Player* player = GET_PLAYER(play);
    StoneWarp& w = gState.warp[stone];
    w.entranceId = gSaveContext.entranceIndex;
    w.sceneId = play->sceneNum;
    w.roomNum = play->roomCtx.curRoom.num;
    w.pos = player->actor.world.pos;
    w.rotY = player->actor.shape.rot.y;
    // Spawn the visible statue immediately. Subsequent scene re-entries
    // re-spawn it via the OnSceneSpawnActors hook below.
    SpiritualStoneStatue_Spawn(play, &w.pos, w.rotY, stone);
    Audio_PlaySoundGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// Re-spawn statues that belong to the freshly-loaded scene. Called from the
// OnSceneSpawnActors hook so the actor list is ready to accept new spawns.
void SpawnStatuesForCurrentScene() {
    if (!StonesEnabled()) return;
    if (gPlayState == nullptr) return;
    for (int i = 0; i < SPIRITUAL_STONE_COUNT; ++i) {
        StoneWarp& w = gState.warp[i];
        if (w.entranceId < 0) continue;
        if (w.sceneId != gPlayState->sceneNum) continue;
        SpiritualStoneStatue_Spawn(gPlayState, &w.pos, w.rotY, i);
    }
}

// ============================================================================
// Custom message — yes/no warp prompt
// ============================================================================

void BuildWarpMessage(int stone, uint16_t* textId, bool* loadFromMessageTable) {
    // Format uses the friendly AutoFormat tokens:
    //   %g / %w  → color escape pair
    //   &        → NEWLINE
    //   \x1B     → TWO_WAY_CHOICE — everything after this is the y/n options.
    // The first option after \x1B is the "Yes" slot (choiceIndex == 0).
    std::string body = std::string("Warp to your %g") + StoneNameEnglish(stone) +
                       "%w waypoint?\x1B%gOK&No%w";
    CustomMessage msg(body);
    msg.AutoFormat();
    msg.LoadIntoFont();
    *loadFromMessageTable = false;
}

void OnOpenTextDispatch(uint16_t* textId, bool* loadFromMessageTable) {
    if (!StonesEnabled()) return;
    if (*textId < kStoneWarpTextIdBase) return;
    int stone = *textId - kStoneWarpTextIdBase;
    if (stone < 0 || stone >= SPIRITUAL_STONE_COUNT) return;
    BuildWarpMessage(stone, textId, loadFromMessageTable);
}

void OpenWarpPrompt(PlayState* play, int stone) {
    sPendingWarpStone = stone;
    Player* player = GET_PLAYER(play);
    if (player != nullptr) {
        player->stateFlags1 |= PLAYER_STATE1_IN_CUTSCENE;
    }
    Message_StartTextbox(play, kStoneWarpTextIdBase + stone, nullptr);
}

// ============================================================================
// SaveManager glue
// ============================================================================

constexpr const char* kSaveSectionName = "spiritualStonesData";

void SaveSection(SaveContext* saveContext, int sectionID, bool fullSave) {
    for (int i = 0; i < SPIRITUAL_STONE_COUNT; ++i) {
        const std::string base = std::string("stone") + std::to_string(i);
        SaveManager::Instance->SaveData(base + "_passive", gState.passive[i]);
        SaveManager::Instance->SaveData(base + "_entrance", gState.warp[i].entranceId);
        SaveManager::Instance->SaveData(base + "_scene", gState.warp[i].sceneId);
        SaveManager::Instance->SaveData(base + "_room", gState.warp[i].roomNum);
        SaveManager::Instance->SaveData(base + "_roty", gState.warp[i].rotY);
        SaveManager::Instance->SaveData(base + "_x", gState.warp[i].pos.x);
        SaveManager::Instance->SaveData(base + "_y", gState.warp[i].pos.y);
        SaveManager::Instance->SaveData(base + "_z", gState.warp[i].pos.z);
    }
}

void LoadSection() {
    ResetState();
    for (int i = 0; i < SPIRITUAL_STONE_COUNT; ++i) {
        const std::string base = std::string("stone") + std::to_string(i);
        SaveManager::Instance->LoadData(base + "_passive", gState.passive[i], (u8)0);
        SaveManager::Instance->LoadData(base + "_entrance", gState.warp[i].entranceId, (s32)-1);
        SaveManager::Instance->LoadData(base + "_scene", gState.warp[i].sceneId, (s16)-1);
        SaveManager::Instance->LoadData(base + "_room", gState.warp[i].roomNum, (s8)0);
        SaveManager::Instance->LoadData(base + "_roty", gState.warp[i].rotY, (s16)0);
        SaveManager::Instance->LoadData(base + "_x", gState.warp[i].pos.x, 0.0f);
        SaveManager::Instance->LoadData(base + "_y", gState.warp[i].pos.y, 0.0f);
        SaveManager::Instance->LoadData(base + "_z", gState.warp[i].pos.z, 0.0f);
    }
}

void InitFile(bool isDebug) {
    ResetState();
    sPendingWarpStone = -1;
    for (int i = 0; i < SPIRITUAL_STONE_COUNT; ++i) sHoldFrames[i] = 0;
}

// Drawing happens inside the statue actor (spiritual_stone_statue.c). No
// per-frame draw hook needed here.

// ============================================================================
// Boot registration
// ============================================================================

void Register() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    SaveManager::Instance->AddInitFunction(InitFile);
    SaveManager::Instance->AddSaveFunction(kSaveSectionName, 1, SaveSection, true, -1);
    SaveManager::Instance->AddLoadFunction(kSaveSectionName, 1, LoadSection);

    CustomMessageManager::Instance->AddCustomMessageTable(kMessageTableId);

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnOpenText>(OnOpenTextDispatch);
    // Re-spawn statues for the current scene each time it loads.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneSpawnActors>(SpawnStatuesForCurrentScene);
}

static RegisterShipInitFunc gSpiritualStonesInit(Register);

} // namespace

// ============================================================================
// Public API (extern "C" — called from C files: kaleido, z_player)
// ============================================================================

extern "C" s32 SpiritualStone_TryToggleAtCursor(PlayState* play, Input* input) {
    if (!StonesEnabled()) return false;
    if (!CHECK_BTN_ALL(input->press.button, BTN_A)) return false;
    s16 cursorPoint = play->pauseCtx.cursorPoint[PAUSE_QUEST];
    int stone = CursorPointToStone(cursorPoint);
    if (stone < 0) return false;
    if (!StoneOwned(stone)) return false;

    gState.passive[stone] = !gState.passive[stone];
    Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    return true;
}

extern "C" s32 SpiritualStone_TryEquipAtCursor(PlayState* play, Input* input) {
    if (!StonesEnabled()) return false;
    s16 cursorPoint = play->pauseCtx.cursorPoint[PAUSE_QUEST];
    int stone = CursorPointToStone(cursorPoint);
    if (stone < 0) return false;
    if (!StoneOwned(stone)) return false;

    // C-button or DPad press (DPad gated by DpadEquips, same as Sw97).
    s32 targetCBtn = -1;
    if (CHECK_BTN_ALL(input->press.button, BTN_CLEFT)) {
        targetCBtn = 0;
    } else if (CHECK_BTN_ALL(input->press.button, BTN_CDOWN)) {
        targetCBtn = 1;
    } else if (CHECK_BTN_ALL(input->press.button, BTN_CRIGHT)) {
        targetCBtn = 2;
    } else if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0)) {
        if (CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
            targetCBtn = 3;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
            targetCBtn = 4;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
            targetCBtn = 5;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
            targetCBtn = 6;
        }
    }
    if (targetCBtn < 0) return false;

    s32 itemToEquip = StoneItemId(stone);
    s32 buttonIndex = targetCBtn + 1; // buttonItems[0] is B button
    gSaveContext.equips.buttonItems[buttonIndex] = itemToEquip;
    if (targetCBtn < 3) {
        gSaveContext.equips.cButtonSlots[targetCBtn] = 0xFF; // SW97 sentinel
    }
    Interface_LoadItemIcon1(play, buttonIndex);

    Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    return true;
}

extern "C" void SpiritualStone_TickHold(PlayState* play, Player* player) {
    if (!StonesEnabled()) return;
    if (play == nullptr || player == nullptr) return;

    // If a warp prompt is open, watch for its close and act on the choice.
    if (sPendingWarpStone >= 0 && play->msgCtx.msgMode == MSGMODE_NONE) {
        int stone = sPendingWarpStone;
        sPendingWarpStone = -1;
        player->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
        // choiceIndex: 0 == first option (Yes/OK), 1 == No
        if (play->msgCtx.choiceIndex == 0) {
            ExecuteWarp(stone);
        }
    }

    // Don't process hold input while a textbox or pause is up.
    if (play->msgCtx.msgMode != MSGMODE_NONE) return;
    if (play->pauseCtx.state != 0 || play->pauseCtx.debugState != 0) return;

    Input* input = &play->state.input[0];
    u16 cur = input->cur.button;

    for (int stone = 0; stone < SPIRITUAL_STONE_COUNT; ++stone) {
        int slot = StoneBoundCButtonSlot(stone);
        if (slot < 0) {
            sHoldFrames[stone] = 0;
            continue;
        }
        u16 mask = ButtonMaskForSlot(slot);
        bool held = (cur & mask) != 0;

        if (held) {
            if (sHoldFrames[stone] >= 0) {
                sHoldFrames[stone]++;
                if (sHoldFrames[stone] >= SPIRITUAL_STONE_SUMMON_HOLD_FRAMES) {
                    SummonStatueHere(play, stone);
                    sHoldFrames[stone] = -1; // sentinel: summoned this hold
                }
            }
        } else {
            // Release
            s32 frames = sHoldFrames[stone];
            sHoldFrames[stone] = 0;
            if (frames > 0 && frames < SPIRITUAL_STONE_SUMMON_HOLD_FRAMES &&
                gState.warp[stone].entranceId >= 0 && sPendingWarpStone < 0) {
                OpenWarpPrompt(play, stone);
                // Only one prompt per frame.
                break;
            }
        }
    }
}

extern "C" s32 SpiritualStone_IsPassiveActive(s32 stone) {
    if (!StonesEnabled()) return 0;
    if (stone < 0 || stone >= SPIRITUAL_STONE_COUNT) return 0;
    return gState.passive[stone];
}

extern "C" s32 SpiritualStone_KokiriWalkActive(void) {
    if (!StonesEnabled()) return 0;
    return gState.passive[SPIRITUAL_STONE_KOKIRI] && StoneOwned(SPIRITUAL_STONE_KOKIRI);
}

extern "C" s32 SpiritualStone_GoronClimbActive(void) {
    if (!StonesEnabled()) return 0;
    return gState.passive[SPIRITUAL_STONE_GORON] && StoneOwned(SPIRITUAL_STONE_GORON);
}

extern "C" s32 SpiritualStone_ZoraSwimActive(void) {
    if (!StonesEnabled()) return 0;
    return gState.passive[SPIRITUAL_STONE_ZORA] && StoneOwned(SPIRITUAL_STONE_ZORA);
}
