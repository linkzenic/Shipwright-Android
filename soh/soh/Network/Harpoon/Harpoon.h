#ifndef NETWORK_HARPOON_H
#define NETWORK_HARPOON_H
#ifdef __cplusplus

#include "soh/Network/Network.h"
#include "soh/Network/Harpoon/HarpoonWebSocket.h"
#include <libultraship/libultraship.h>
#include <queue>
#include <mutex>
#include <vector>
#include <memory>

extern "C" {
#include "variables.h"
#include "z64.h"
}

// Forward declarations for Harpoon dummy player
void HarpoonDummyPlayer_Init(Actor* actor, PlayState* play);
void HarpoonDummyPlayer_Update(Actor* actor, PlayState* play);
void HarpoonDummyPlayer_Draw(Actor* actor, PlayState* play);
void HarpoonDummyPlayer_Destroy(Actor* actor, PlayState* play);

// CVar prefix for Harpoon settings
#define CVAR_HARPOON(var) "Remote.Harpoon." var

typedef enum {
    HARPOON_MODE_NONE = 0,
    HARPOON_MODE_HUNGER_GAMES,
    HARPOON_MODE_PROP_HUNT,
    HARPOON_MODE_RANDOMIZER,
} HarpoonGameMode;

typedef enum {
    HARPOON_STATE_DISCONNECTED,
    HARPOON_STATE_LOBBY,
    HARPOON_STATE_MAP_SELECT,
    HARPOON_STATE_COUNTDOWN,
    HARPOON_STATE_HIDING_PHASE,
    HARPOON_STATE_PLAYING,
    HARPOON_STATE_SPECTATING,
    HARPOON_STATE_FINISHED,
} HarpoonGameState;


typedef enum {
    MAP_SELECT_HOST_CHOOSES = 0,
    MAP_SELECT_EVERYONE_CHOOSES = 1,
    MAP_SELECT_RANDOM = 2,
} HarpoonMapSelectMode;

// Client struct with player visual/sync data
typedef struct {
    uint32_t clientId;
    std::string name;
    Color_RGB8 color;
    std::string clientVersion;
    bool online;
    bool self;
    bool isSaveLoaded;
    s16 sceneNum;
    s32 entranceIndex;

    // Ocarina playback state (synced via AUDIO.OCARINA_SFX). Zero-initialised
    // by the map's default-construction; the handler treats note==0xFF as
    // "no note playing" so callers should reset to 0xFF when joining if they
    // want full Anchor parity (acceptable to leave at 0 — first incoming
    // note still plays correctly).
    u8 ocarinaNote;
    f32 ocarinaModulator;
    s8 ocarinaBend;

    // Player visual state
    s32 linkAge;
    PosRot posRot;
    Vec3s jointTable[24];
    u8 movementFlags;
    Vec3s prevTransl;
    Vec3s upperLimbRot;
    s8 currentBoots;
    s8 currentShield;
    s8 currentTunic;
    u32 stateFlags1;
    u32 stateFlags2;
    u8 buttonItem0;
    s8 itemAction;
    s8 heldItemAction;
    u8 modelGroup;
    // Hand / sheath types — drive engine's hand DL selection (open / closed
    // / holding sword / bow / etc.). Synced explicitly from remote so the
    // dummy's hands match what the remote is actually doing.
    s8 leftHandType;
    s8 rightHandType;
    s8 sheathType;
    // Per-frame visual state used by Player_OverrideLimbDrawGameplayCommon
    // to pick the right hand/item DLs at draw time. Without these synced
    // the dummy's hands stay in their default pose even when the remote is
    // running, drawing a bow, holding an item, or in first-person.
    f32 speedXZ;            // controls open→closed hand transition when running
    s8  meleeWeaponState;   // sword swing state — drives weapon trail effect
    u8  fpModeFlag;         // unk_6AD — first-person flag, hides limbs when set
    f32 bowStringDraw;      // unk_858 — bow string stretch (0–1)
    s16 bowArrowState;      // unk_860 — arrow nocking state
    s16 bowDrawAnimFrame;   // unk_834 — bow draw animation frame
    Vec3s headLimbRot;      // head rotation
    s16 upperLimbYawSecondary; // secondary upper-body yaw
    s8 invincibilityTimer;
    f32 unk_85C;
    s16 unk_862;
    s8 actionVar1;

    // Transformation data
    u8 transformation; // MM_PLAYER_FORM_GORON, etc. (0 = human/no transform)
    s16 cylRadius;     // Form-specific collider radius
    s16 cylHeight;     // Form-specific collider height
    s16 cylYShift;     // Form-specific collider Y offset
    u32 mmStateFlags3; // MM stateFlags3 (spike mode, roll active, etc.)
    f32 mmSpeedXZ;     // MM horizontal speed

    // Model type for rendering (transformation masks, prop hunt)
    u8 modelType;     // 0=Link, 1=Goron, 2=Zora, 3=Deku, 4=FierceDeity, 5+=props
    u16 propObjectId; // Object ID for prop hunt (0 = no prop)

    // OOT visual state
    u8 currentMask;             // PlayerMask enum (0=none, 1-8=masks)
    s32 wornMask;               // MM worn mask item ID (ITEM_NONE=no mask, from MmMaskWear system)
    s16 face;                   // actor.shape.face (eye/mouth texture index)
    f32 scaleX, scaleY, scaleZ; // actor.scale

    // MM form visual state (for remote rendering)
    s32 goronAction;     // GoronActionId - determines ball vs standing draw
    u8 eyeIndex;         // Blink state (0=open, 1=half, 2=closed)
    f32 rollSquash;      // Ball deformation factor
    s16 rollSpikeActive; // Spike mode counter (0=off, >0=active)
    s16 rollChargeLevel; // Charge level for energy effects

    // Custom item visual state (for remote rendering)
    u32 customItemFlags; // CI_FLAG_* bitfield
    // Beetle
    Vec3f ciBeetlePos;
    Vec3s ciBeetleRot;
    f32 ciBeetleWingScale;
    u8 ciBeetleState;
    // Gust Jar
    u8 ciGustJarMode;
    u8 ciGustJarElement;
    u8 ciGustJarBlowActive;
    s16 ciGustJarHeatTimer;
    // Fire Rod
    Vec3f ciFireRodProjPos;
    Vec3f ciFireRodProjPos2;
    Vec3f ciFireRodProjPos3;
    u8 ciFireRodProjActive;
    u8 ciFireRodProjCount;
    u8 ciFireRodProjType;
    f32 ciFireRodProjScale;
    Vec3f ciFireRodProjTrail[6];
    // Ice Rod
    Vec3f ciIceRodProjPos;
    Vec3f ciIceRodProjPos2;
    Vec3f ciIceRodProjPos3;
    u8 ciIceRodProjActive;
    u8 ciIceRodProjCount;
    f32 ciIceRodProjScale;
    Vec3f ciIceRodProjTrail[6];
    // Light Rod
    Vec3f ciLightRodProjPos;
    Vec3f ciLightRodProjPos2;
    Vec3f ciLightRodProjPos3;
    u8 ciLightRodProjActive;
    u8 ciLightRodProjCount;
    // Ball and Chain
    u8 ciBallChainThrown;
    s16 ciTimer2;
    Vec3f ciSharedProjPos;
    // Whip
    u8 ciWhipState;
    Vec3f ciWhipTipPos;
    Vec3f ciWhipAttachPos;
    Vec3f ciWhipAttachNormal;
    // Deku Leaf
    u8 ciDekuLeafGliding;
    u8 ciDekuLeafBlowing;
    s16 ciDekuLeafAnimTimer;
    // Shovel
    u8 ciShovelAnimating;
    // Dominion Rod
    u8 ciDominionRodState;
    Vec3f ciDominionRodOrbPos;
    // Switch Hook
    u8 ciSwitchHookState;
    Vec3f ciSwitchHookProjPos;
    // Time Gate
    u8 ciTimeGateItemVisible;
    u8 ciTimeGatePortalActive;
    f32 ciTimeGatePortalAlpha;
    f32 ciTimeGatePortalScale;

    // ── Phase 1 sync additions — items previously missing from sync ───
    // Roc's Feather / Cape
    u8  ciRocsFeatherJumpActive;
    u8  ciRocsJumpCount;
    s16 ciRocsMmAnimTimer;
    // Bomb Arrows
    u8  ciBombArrowActive;
    u8  ciBombArrowState;
    // Demise Destruction
    u8  ciDemiseDestructionActive;
    // Hylia's Grace
    u8  ciHyliasGraceActive;
    u8  ciHyliasGraceState;
    u8  ciHyliasGraceSubPhase;
    s16 ciHyliasGraceTimer;
    u8  ciHyliasGraceForcedBySpell;
    // Zonai Permafrost
    u8  ciZonaiPermafrostActive;
    u8  ciZonaiPermafrostState;
    u8  ciZonaiPermafrostSubPhase;
    s16 ciZonaiPermafrostTimer;
    // Lantern
    u8  ciLanternFireType;
    u8  ciLanternSwinging;
    u8  ciLanternEquipped;
    s16 ciLanternSwingFrame;
    // Minish Cap
    u8  ciMinishCapWarpMode;
    u8  ciMinishCapShrinking;
    u8  ciMinishCapGrowing;
    // Postman Hat
    u8  ciPostmanHatDashing;
    u8  ciPostmanHatArriving;
    s16 ciPostmanHatTransitionTimer;
    // Desire Sensor
    u8  ciDesireSensorActive;
    u8  ciDesireSensorState;
    s16 ciDesireSensorTimer;
    u8  ciDesireSensorResult;

    // Prop Hunt state (from Scooter)
    std::string role; // "seeker" or "hider" (empty = no game)
    s32 propCategory; // 0=env, 1=enemies, 2=npcs
    s32 propIndex;    // 0-9 within category (-1 = no prop / Link)
    s32 propState;    // Current state/variant within prop

    // Somaria Decoy state (Prop Hunt, from Scooter)
    Vec3f somariaDecoyPos[3];
    s16 somariaDecoyRotY[3];
    s32 somariaDecoyPropIdx[3];
    s32 somariaDecoyPropCat[3];
    s32 somariaDecoyPropState[3];
    u8 somariaDecoyActive[3];
    u8 somariaDecoyCount;

    // Map selection state (from Scooter)
    s32 mapSelectIndex;
    bool hasVoted;

    // Triforce Thief — mirror of this client's carrier-timer broadcast.
    // Refreshed by TRIFORCE_THIEF.CARRIER_TIMER_SYNC; HUD arrow label reads it.
    s32 ttRupeesRemaining;

    // Game mode state (from Scooter)
    bool isAlive;
    bool isReady;
    bool isAdmin;
    s16 kills;
    std::string team;

    // Remote somaria cubes
    struct {
        Vec3f pos;
        u8 state; // 0=none, 1=spawn, 2=idle, 3=held, 4=thrown
        u8 form;  // Elegy form (ELEGY_FORM_*)
        f32 scale;
        s16 rotY;
    } remoteCubes[3];
    u8 remoteCubeCount;
    Actor* remoteCubeActors[3];

    // Pak / .o2r skin sync — display names (package.json "name") of the remote's
    // currently selected pak slots. Resolved locally via PakLoader_FindSyncIndexByName
    // against harpoon/skins/. Empty = default Link.
    std::string adultSkinName;
    std::string childSkinName;
    std::string equipSkinName;

    // Forced body model display name — set when the remote's local PakLoader
    // is force-overriding the body skin (Kafei Mask transform, Champion's
    // Tunic, etc.). Takes priority over adultSkinName/childSkinName for the
    // dummy's render. Empty = no force override active.
    std::string forcedSkinName;

    // List of .o2r mods the remote has enabled in their mods/ root (handshake-only).
    // Used for divergence warnings, not for render.
    std::vector<std::string> enabledO2rMods;

    // List of mod names the remote has installed in their harpoon/skins/
    // folder (the catalog they can render OTHER players with). Used to
    // suppress the "you have mod X that they don't" notification when our
    // local mod is in the remote's sync registry — they'll render us
    // correctly even though they don't have it mounted globally.
    std::vector<std::string> harpoonSyncMods;

    // Ptr to the dummy player actor
    Player* player;
} HarpoonClient;

class Harpoon : public Network {
  private:
    uint32_t spawningDummyPlayerForClientId = 0;
    bool shouldRefreshActors = false;

    std::queue<nlohmann::json> incomingPacketQueue;
    std::mutex incomingPacketQueueMutex;

    nlohmann::json PrepClientState();
    void RegisterHooks();
    void RefreshClientActors();
    void SetDummyPlayerClientId(const Actor* actor, uint32_t clientId);

    // Packet handlers
    void HandlePacket_AllClients(nlohmann::json payload);
    void HandlePacket_PlayerUpdate(nlohmann::json payload);
    void HandlePacket_Damage(nlohmann::json payload);
    void HandlePacket_PlayerDied(nlohmann::json payload);
    void HandlePacket_ServerMsg(nlohmann::json payload);
    void HandlePacket_PlayerSfx(nlohmann::json payload);
    void HandlePacket_GiveItem(nlohmann::json payload);
    void HandlePacket_UpdateTeamState(nlohmann::json payload);

    // Scooter packet handlers
    void HandlePacket_GameState(nlohmann::json payload);
    void HandlePacket_Winner(nlohmann::json payload);
    void HandlePacket_ServerInfo(nlohmann::json payload);
    void HandlePacket_RoomJoined(nlohmann::json payload);
    void HandlePacket_RoomLeft(nlohmann::json payload);
    void HandlePacket_RoomList(nlohmann::json payload);
    void HandlePacket_RoleChange(nlohmann::json payload);
    void HandlePacket_MapVote(nlohmann::json payload);
    void HandlePacket_DecoyHit(nlohmann::json payload);
    void HandlePacket_CustomDamage(nlohmann::json payload);
    void HandlePacket_CustomEffect(nlohmann::json payload);

    // Anchor rando packet handlers
    void HandlePacket_SetFlag(nlohmann::json payload);
    void HandlePacket_UnsetFlag(nlohmann::json payload);
    void HandlePacket_SetCheckStatus(nlohmann::json payload);
    void HandlePacket_EntranceDiscovered(nlohmann::json payload);
    void HandlePacket_UpdateDungeonItems(nlohmann::json payload);
    void HandlePacket_TeleportTo(nlohmann::json payload);
    void HandlePacket_UpdateBeansCount(nlohmann::json payload);

    // Skin sync packet handlers
    void HandlePacket_O2rModList(nlohmann::json payload);

    // Harpoon v2 — new lifecycle handlers
    void HandlePacket_HandshakeAck(nlohmann::json payload);
    void HandlePacket_Error(nlohmann::json payload);
    void HandlePacket_GamemodeManifest(nlohmann::json payload);
    void HandlePacket_RoomEvent(nlohmann::json payload);
    void HandlePacket_PhaseChanged(nlohmann::json payload);

    // Harpoon v2 — start-game / map-select / countdown lifecycle
    void HandlePacket_MapSelectBegin(nlohmann::json payload);
    void HandlePacket_MapConfirmed(nlohmann::json payload);
    void HandlePacket_Timer(nlohmann::json payload);
    void HandlePacket_VotingStarted(nlohmann::json payload);
    void HandlePacket_VotingTally(nlohmann::json payload);
    void HandlePacket_VotingResult(nlohmann::json payload);

    // Harpoon v2 — granular PLAYER.* receivers (each forwards to the same
    // HarpoonClient struct the legacy HandlePacket_PlayerUpdate populates)
    void HandlePacket_PlayerTransform(nlohmann::json payload);
    void HandlePacket_PlayerSkeleton(nlohmann::json payload);
    void HandlePacket_PlayerLimbRotations(nlohmann::json payload);
    void HandlePacket_PlayerAnimationFlags(nlohmann::json payload);
    void HandlePacket_PlayerMotionVars(nlohmann::json payload);
    void HandlePacket_PlayerBowState(nlohmann::json payload);
    void HandlePacket_PlayerHandTypes(nlohmann::json payload);
    void HandlePacket_PlayerVisualState(nlohmann::json payload);
    void HandlePacket_PlayerEquipVisible(nlohmann::json payload);
    void HandlePacket_PlayerFace(nlohmann::json payload);
    void HandlePacket_PlayerScale(nlohmann::json payload);
    void HandlePacket_PlayerTransformation(nlohmann::json payload);
    void HandlePacket_PlayerGoronState(nlohmann::json payload);
    void HandlePacket_PlayerCustomItemState(nlohmann::json payload);
    void HandlePacket_PlayerInvincibility(nlohmann::json payload);
    void HandlePacket_PlayerKill(nlohmann::json payload);
    void HandlePacket_PlayerFullState(nlohmann::json payload);

    // Harpoon v2 — appearance / skin sync
    void HandlePacket_SkinSyncAnnounceCatalog(nlohmann::json payload);
    void HandlePacket_SkinSyncUpdateSlots(nlohmann::json payload);

  public:
    // Announce our list of enabled .o2r global mods (sent once after handshake).
    // Used for divergence notifications only — never affects remote rendering.
    void SendPacket_O2rModList();

    // ============================================================================
    // Wire-protocol primitive names (Harpoon WebSocket v2)
    // ============================================================================
    // Every outgoing message is wrapped in an envelope: {type, seq, payload}.
    // The C++ Send/Handle methods build the inner payload and the transport
    // layer wraps it. The strings below are the `type` field of the envelope.

    // HARPOON.* — connection lifecycle
    inline static const std::string HPN_HANDSHAKE          = "HARPOON.HANDSHAKE";
    inline static const std::string HPN_HANDSHAKE_ACK      = "HARPOON.HANDSHAKE_ACK";
    inline static const std::string HPN_RESUME             = "HARPOON.RESUME";
    inline static const std::string HPN_SERVER_INFO        = "HARPOON.SERVER_INFO";
    inline static const std::string HPN_ERROR              = "HARPOON.ERROR";

    // ROOM.* — lobby
    inline static const std::string HPN_ROOM_CREATE        = "ROOM.CREATE";
    inline static const std::string HPN_ROOM_JOIN          = "ROOM.JOIN";
    inline static const std::string HPN_ROOM_LEAVE         = "ROOM.LEAVE";
    inline static const std::string HPN_ROOM_LIST          = "ROOM.LIST";
    inline static const std::string HPN_ROOM_LIST_RESPONSE = "ROOM.LIST_RESPONSE";
    inline static const std::string HPN_ROOM_JOINED        = "ROOM.JOINED";
    inline static const std::string HPN_ROOM_LEFT          = "ROOM.LEFT";
    inline static const std::string HPN_ROOM_MEMBERS       = "ROOM.MEMBERS_UPDATED";
    inline static const std::string HPN_ROOM_SET_PHASE     = "ROOM.SET_PHASE";
    inline static const std::string HPN_ROOM_PHASE_CHANGED = "ROOM.PHASE_CHANGED";
    inline static const std::string HPN_ROOM_BROADCAST     = "ROOM.BROADCAST_EVENT";
    inline static const std::string HPN_ROOM_EVENT         = "ROOM.EVENT";
    inline static const std::string HPN_ROOM_MANIFEST      = "ROOM.GAMEMODE_MANIFEST";
    inline static const std::string HPN_ROOM_GM_CONFIG     = "ROOM.GAMEMODE_CONFIG";
    inline static const std::string HPN_ROOM_TIMER         = "ROOM.TIMER";
    inline static const std::string HPN_ROOM_START_GAME    = "ROOM.START_GAME";
    inline static const std::string HPN_ROOM_MAP_BEGIN     = "ROOM.MAP_SELECT_BEGIN";
    inline static const std::string HPN_ROOM_MAP_SELECT    = "ROOM.SELECT_MAP";
    inline static const std::string HPN_ROOM_MAP_CONFIRMED = "ROOM.MAP_CONFIRMED";

    // VOTING.* — used by everyone_chooses map mode and other player votes
    inline static const std::string HPN_VOTING_START       = "VOTING.START_VOTE";
    inline static const std::string HPN_VOTING_CAST        = "VOTING.CAST_VOTE";
    inline static const std::string HPN_VOTING_END         = "VOTING.END_VOTE";
    inline static const std::string HPN_VOTING_STARTED     = "VOTING.STARTED";
    inline static const std::string HPN_VOTING_TALLY       = "VOTING.TALLY";
    inline static const std::string HPN_VOTING_RESULT      = "VOTING.RESULT";

    // PLAYER.* — granular per-frame updates
    inline static const std::string HPN_PLAYER_TRANSFORM       = "PLAYER.UPDATE_TRANSFORM";
    inline static const std::string HPN_PLAYER_SKELETON        = "PLAYER.UPDATE_SKELETON";
    inline static const std::string HPN_PLAYER_LIMB_ROT        = "PLAYER.UPDATE_LIMB_ROTATIONS";
    inline static const std::string HPN_PLAYER_ANIM_FLAGS      = "PLAYER.UPDATE_ANIMATION_FLAGS";
    inline static const std::string HPN_PLAYER_MOTION_VARS     = "PLAYER.UPDATE_MOTION_VARS";
    inline static const std::string HPN_PLAYER_BOW_STATE       = "PLAYER.UPDATE_BOW_STATE";
    inline static const std::string HPN_PLAYER_HAND_TYPES      = "PLAYER.UPDATE_HAND_TYPES";
    inline static const std::string HPN_PLAYER_VISUAL_STATE    = "PLAYER.UPDATE_VISUAL_STATE";
    inline static const std::string HPN_PLAYER_EQUIP_VISIBLE   = "PLAYER.UPDATE_EQUIP_VISIBLE";
    inline static const std::string HPN_PLAYER_FACE            = "PLAYER.UPDATE_FACE";
    inline static const std::string HPN_PLAYER_SCALE           = "PLAYER.UPDATE_SCALE";
    inline static const std::string HPN_PLAYER_TRANSFORMATION  = "PLAYER.SET_TRANSFORMATION";
    inline static const std::string HPN_PLAYER_GORON_STATE     = "PLAYER.UPDATE_GORON_STATE";
    inline static const std::string HPN_PLAYER_INVINCIBILITY   = "PLAYER.SET_INVINCIBILITY_TIMER";
    inline static const std::string HPN_PLAYER_CUSTOM_ITEM     = "PLAYER.UPDATE_CUSTOM_ITEM_STATE";
    inline static const std::string HPN_PLAYER_FULL_STATE      = "PLAYER.UPDATE_FULL_STATE";
    inline static const std::string HPN_PLAYER_KILL            = "PLAYER.KILL";

    // COMBAT.* — damage / status / effects
    inline static const std::string HPN_COMBAT_DAMAGE          = "COMBAT.DEAL_DAMAGE";
    inline static const std::string HPN_COMBAT_APPLY_STATUS    = "COMBAT.APPLY_STATUS";
    inline static const std::string HPN_COMBAT_DECOY_HIT       = "COMBAT.DECOY_HIT";
    inline static const std::string HPN_COMBAT_SPAWN_DECOY     = "COMBAT.SPAWN_DECOY";
    inline static const std::string HPN_COMBAT_DESTROY_DECOY   = "COMBAT.DESTROY_DECOY";
    inline static const std::string HPN_COMBAT_CUSTOM_EFFECT   = "COMBAT.CUSTOM_EFFECT";

    // INVENTORY.* + SAVE.*
    inline static const std::string HPN_INV_GIVE_ITEM          = "INVENTORY.GIVE_ITEM";
    inline static const std::string HPN_INV_DUNGEON_ITEMS      = "INVENTORY.SET_DUNGEON_ITEMS";
    inline static const std::string HPN_INV_AMMO               = "INVENTORY.SET_AMMO";
    inline static const std::string HPN_SAVE_SET_FLAG          = "SAVE.SET_FLAG";
    inline static const std::string HPN_SAVE_UNSET_FLAG        = "SAVE.UNSET_FLAG";
    inline static const std::string HPN_SAVE_QUEST_STATE       = "SAVE.SET_QUEST_STATE";
    inline static const std::string HPN_SAVE_TEAM_STATE        = "SAVE.UPDATE_TEAM_STATE";
    inline static const std::string HPN_SAVE_TEAM_REQUEST      = "SAVE.REQUEST_TEAM_STATE";
    inline static const std::string HPN_SAVE_CUTSCENE          = "SAVE.CUTSCENE_TRIGGER";
    inline static const std::string HPN_SAVE_GAME_COMPLETE     = "SAVE.GAME_COMPLETE";
    inline static const std::string HPN_AUDIO_OCARINA          = "AUDIO.OCARINA_SFX";

    // WORLD.*
    inline static const std::string HPN_WORLD_TRANSPORT        = "WORLD.TRANSPORT_SCENE";
    inline static const std::string HPN_WORLD_TELEPORT         = "WORLD.TELEPORT";

    // MAP.*
    inline static const std::string HPN_MAP_ENTRANCE           = "MAP.ENTRANCE_DISCOVERED";

    // AUDIO.*
    inline static const std::string HPN_AUDIO_SFX              = "AUDIO.PLAY_SFX";
    inline static const std::string HPN_AUDIO_BGM              = "AUDIO.PLAY_BGM";

    // UI.*
    inline static const std::string HPN_UI_MESSAGE             = "UI.SHOW_MESSAGE";
    inline static const std::string HPN_UI_BANNER              = "UI.SHOW_BANNER";

    // CHAT.*
    inline static const std::string HPN_CHAT_MESSAGE           = "CHAT.MESSAGE";
    inline static const std::string HPN_CHAT_PING              = "CHAT.PING";

    // APPEARANCE.* — skin sync
    inline static const std::string HPN_SKIN_ANNOUNCE          = "APPEARANCE.SKIN_SYNC.ANNOUNCE_CATALOG";
    inline static const std::string HPN_SKIN_UPDATE_SLOTS      = "APPEARANCE.SKIN_SYNC.UPDATE_SLOTS";
    inline static const std::string HPN_APPEARANCE_TINT        = "APPEARANCE.SET_TINT";
    inline static const std::string HPN_APPEARANCE_SCALE       = "APPEARANCE.SET_SCALE";
    inline static const std::string HPN_APPEARANCE_HIDE_OBS    = "APPEARANCE.HIDE_FROM_OBSERVER";
    inline static const std::string HPN_APPEARANCE_SHOW_OBS    = "APPEARANCE.SHOW_TO_OBSERVER";
    inline static const std::string HPN_APPEARANCE_SPAWN_VFX   = "APPEARANCE.SPAWN_VFX_ACTOR";

    // ADMIN.*
    inline static const std::string HPN_ADMIN_PROMOTE          = "ADMIN.PROMOTE";
    inline static const std::string HPN_ADMIN_DEMOTE           = "ADMIN.DEMOTE";
    inline static const std::string HPN_ADMIN_SET_HOST         = "ADMIN.SET_HOST";
    inline static const std::string HPN_ADMIN_KICK             = "ADMIN.KICK";

    // === Legacy aliases (kept so older parts of the code compile during migration) ===
    inline static const std::string HPN_ALL_CLIENTS         = HPN_ROOM_MEMBERS;
    inline static const std::string HPN_PLAYER_UPDATE       = HPN_PLAYER_FULL_STATE;
    inline static const std::string HPN_DAMAGE              = HPN_COMBAT_DAMAGE;
    inline static const std::string HPN_PLAYER_DIED         = HPN_PLAYER_KILL;
    inline static const std::string HPN_PLAYER_SFX          = HPN_AUDIO_SFX;
    inline static const std::string HPN_SERVER_MSG          = HPN_UI_MESSAGE;
    inline static const std::string HPN_GIVE_ITEM           = HPN_INV_GIVE_ITEM;
    inline static const std::string HPN_UPDATE_TEAM_STATE   = HPN_SAVE_TEAM_STATE;
    inline static const std::string HPN_GAME_STATE          = HPN_ROOM_PHASE_CHANGED;
    inline static const std::string HPN_CHEST_OPENED        = "ROOM.BROADCAST_EVENT";
    inline static const std::string HPN_READY               = "ROOM.BROADCAST_EVENT";
    inline static const std::string HPN_START_GAME          = HPN_ROOM_SET_PHASE;
    inline static const std::string HPN_WINNER              = "ROOM.BROADCAST_EVENT";
    inline static const std::string HPN_MAP_CONFIRM         = "ROOM.BROADCAST_EVENT";
    inline static const std::string HPN_ROLE_CHANGE         = "ROOM.BROADCAST_EVENT";
    inline static const std::string HPN_MAP_VOTE            = "ROOM.BROADCAST_EVENT";
    inline static const std::string HPN_DECOY_HIT           = HPN_COMBAT_DECOY_HIT;
    inline static const std::string HPN_CUSTOM_DAMAGE       = HPN_COMBAT_DAMAGE;
    inline static const std::string HPN_CUSTOM_EFFECT       = HPN_COMBAT_CUSTOM_EFFECT;
    inline static const std::string HPN_SET_FLAG            = HPN_SAVE_SET_FLAG;
    inline static const std::string HPN_UNSET_FLAG          = HPN_SAVE_UNSET_FLAG;
    inline static const std::string HPN_SET_CHECK_STATUS    = HPN_SAVE_QUEST_STATE;
    inline static const std::string HPN_ENTRANCE_DISCOVERED = HPN_MAP_ENTRANCE;
    inline static const std::string HPN_UPDATE_DUNGEON_ITEMS= HPN_INV_DUNGEON_ITEMS;
    inline static const std::string HPN_TELEPORT_TO         = HPN_WORLD_TRANSPORT;
    inline static const std::string HPN_UPDATE_BEANS_COUNT  = HPN_INV_AMMO;
    inline static const std::string HPN_O2R_MOD_LIST        = HPN_SKIN_ANNOUNCE;

    static Harpoon* Instance;
    std::map<uint32_t, HarpoonClient> clients;
    uint32_t ownClientId = 0;

    // Harpoon-specific WebSocket transport (RFC 6455). Used INSTEAD of the
    // base Network class's TCP+\0 framing. Anchor / Sail / CrowdControl still
    // inherit from Network and use TCP — only Harpoon talks WebSocket.
    std::unique_ptr<HarpoonWebSocket> ws;

    // Harpoon WebSocket protocol v2 — session token issued by server in
    // HARPOON.SERVER_INFO. Used by HARPOON.RESUME on reconnect to migrate
    // identity/room without losing state.
    std::string sessionToken;

    // Per-session monotonically increasing sequence counter for envelope.
    uint64_t nextSeq = 1;

    // Cached gamemode manifest received from the server when joining a room.
    nlohmann::json currentGamemodeManifest;

    // Item sync state
    // Default true (Anchor parity) — story / sandbox packs explicitly set
    // sync_items=false in their default_config to opt out. Without a true
    // default, packets sent in the window between connect and manifest-arrival
    // get silently dropped client-side.
    bool syncItems = true;
    bool pvpEnabled = true; // When false: no damage/knockback from other players, status effects still apply
    bool isProcessingIncomingPacket = false;
    bool isHandlingUpdateTeamState = false;
    bool justLoadedSave = false;
    // Defensive guard: track whether we've pushed a VisualState since the
    // last save load. OnSceneSpawnActors normally fires on every scene
    // transition (including the initial load) and triggers SendPacket_PlayerVisualState,
    // but if the chain breaks for any reason the per-frame OnPlayerUpdate
    // hook resends so the server's session.scene_num / is_save_loaded fields
    // get populated and AOI starts working.
    bool visualStateSentSinceLoad = false;

    // Game state (from Scooter)
    HarpoonGameMode activeGameMode = HARPOON_MODE_NONE;
    HarpoonGameState gameState = HARPOON_STATE_DISCONNECTED;
    s32 countdownTimer = 0;
    s32 aliveCount = 0;
    bool isEliminated = false;
    std::vector<std::string> killFeed;

    // Host tracking
    uint32_t hostClientId = 0;

    // Room tracking (from Scooter)
    std::string currentRoomId;
    std::string currentRoomName;
    std::string currentRoomGameMode;

    // Room list (from Scooter)
    struct RoomInfo {
        std::string roomId;
        std::string name;
        std::string gameMode;
        bool hasPassword;
        int playerCount;
        int maxPlayers;
        std::string state;
    };
    std::vector<RoomInfo> roomList;

    // Map selection (from Scooter)
    s32 selectedMapIndex = 0;
    HarpoonMapSelectMode mapSelectMode = MAP_SELECT_HOST_CHOOSES;

    // Prop Hunt state (from Scooter)
    bool pendingPropHuntInit = false;
    bool isPropHuntMode = false;
    s32 localPropCategory = 0;
    s32 localPropIndex = -1;
    s32 localPropState = 0;
    std::string localRole;
    s32 categoryLabelTimer = 0;
    s32 propModeLockoutTimer = 0;
    bool showHudOverlay = true;
    u8 savedButtonItems[8] = {};

    // Prop Hunt gameplay (from Scooter)
    s32 seekerCount = 1;
    s32 confirmedMapIndex = -1;
    s32 confirmedMapEntrance = 0;
    f32 propHuntTimerSeconds = 0.0f;
    bool propHuntTimerRunning = false;
    s32 seekerCountdownSeconds = 0;

    void Enable();
    void Disable();
    void OnIncomingJson(nlohmann::json payload) override;
    void OnConnected() override;
    void OnDisconnected() override;
    void SendJsonToRemote(nlohmann::json packet) override;
    void ProcessIncomingPacketQueue();
    bool IsSaveLoaded();
    uint32_t GetDummyPlayerClientId(const Actor* actor);

    // ============================================================================
    // Send packets — Harpoon v2 protocol (envelope-wrapped)
    // ============================================================================

    // Connection lifecycle
    void SendPacket_Handshake();
    void SendPacket_Resume(const std::string& token);

    // Game lifecycle (host-driven)
    void SendPacket_StartGameNew();                       // ROOM.START_GAME
    void SendPacket_SelectMap(s32 mapIndex);              // ROOM.SELECT_MAP
    void SendPacket_StartMapVote(s32 durationSeconds);    // VOTING.START_VOTE for map
    void SendPacket_CastMapVote(s32 optionIndex);         // VOTING.CAST_VOTE
    void SendPacket_RandomMapPick();                      // host helper for "random" mode

    // Player state — granular per-frame primitives. SendPacket_PlayerUpdate()
    // calls all of these in sequence; you can also call them individually if
    // you only want to broadcast a subset.
    void SendPacket_PlayerUpdate();           // bundles all of below
    void SendPacket_PlayerTransform();         // PLAYER.UPDATE_TRANSFORM
    void SendPacket_PlayerSkeleton();          // PLAYER.UPDATE_SKELETON
    void SendPacket_PlayerLimbRotations();     // PLAYER.UPDATE_LIMB_ROTATIONS
    void SendPacket_PlayerAnimationFlags();    // PLAYER.UPDATE_ANIMATION_FLAGS
    void SendPacket_PlayerMotionVars();        // PLAYER.UPDATE_MOTION_VARS
    void SendPacket_PlayerBowState();          // PLAYER.UPDATE_BOW_STATE
    void SendPacket_PlayerHandTypes();         // PLAYER.UPDATE_HAND_TYPES
    void SendPacket_PlayerVisualState();       // PLAYER.UPDATE_VISUAL_STATE
    void SendPacket_PlayerEquipVisible();      // PLAYER.UPDATE_EQUIP_VISIBLE
    void SendPacket_PlayerFace();              // PLAYER.UPDATE_FACE
    void SendPacket_PlayerScale();             // PLAYER.UPDATE_SCALE
    void SendPacket_PlayerTransformation();    // PLAYER.SET_TRANSFORMATION
    void SendPacket_PlayerGoronState();        // PLAYER.UPDATE_GORON_STATE
    void SendPacket_PlayerCustomItemState();   // PLAYER.UPDATE_CUSTOM_ITEM_STATE
    void SendPacket_PlayerInvincibility();     // PLAYER.SET_INVINCIBILITY_TIMER
    void SendPacket_PlayerKill();              // PLAYER.KILL — alias of SendPacket_PlayerDied
    void SendPacket_PlayerDied();              // legacy name, equivalent

    // Combat
    void SendPacket_Damage(u32 clientId, u8 damageEffect, u8 damage);
    void SendPacket_PlayerSfx(u16 sfxId);

    // Inventory / save
    void SendPacket_GiveItem(u16 modId, s16 getItemId);
    void SendPacket_UpdateTeamState();

    // Send packets (Scooter)
    void SendPacket_ChestOpened(s16 sceneNum, s16 flag);
    void SendPacket_Ready();
    void SendPacket_StartGame(const char* gameMode);
    void SendPacket_RoomCreate(const char* name, const char* gameMode, const char* password);
    void SendPacket_RoomJoin(const char* roomId, const char* password);
    void SendPacket_RoomLeave();
    void SendPacket_RoomList();
    void SendPacket_SetTeam(const char* team);
    void SendPacket_MapConfirm(s32 mapIndex);
    void SendPacket_RoleChange(const char* newRole);
    void SendPacket_MapVote(s32 mapIndex);
    void SendPacket_DecoyHit(u32 targetClientId, u8 decoySlot);
    void UpdateDecoys();

    // Send packets (Anchor rando)
    void SendPacket_SetFlag(s16 sceneNum, s16 flagType, s16 flag);
    void SendPacket_UnsetFlag(s16 sceneNum, s16 flagType, s16 flag);
    void SendPacket_SetCheckStatus(RandomizerCheck rc);
    void SendPacket_EntranceDiscovered(u16 entranceIndex);
    void SendPacket_UpdateDungeonItems();
    void SendPacket_TeleportTo(uint32_t clientId);
    void SendPacket_UpdateBeansCount();

    // Story sync — broadcast a cutscene trigger so other players in the same
    // scene replay the same cutscene (best-effort).
    void SendPacket_CutsceneTrigger(s32 cutsceneIndex, s16 sceneNum);
    void HandlePacket_CutsceneTrigger(nlohmann::json payload);

    // Pull team save state from any connected teammate. Sent on local save
    // load so a late-joiner inherits the team's progression rather than
    // pushing their (probably empty) save and clobbering everyone else.
    void SendPacket_RequestTeamState();
    void HandlePacket_RequestTeamState(nlohmann::json payload);

    // Game complete (Ganon defeat) broadcast.
    void SendPacket_GameComplete();
    void HandlePacket_GameComplete(nlohmann::json payload);

    // Ocarina note SFX. Streams notes so teammates in the same scene hear it.
    void SendPacket_OcarinaSfx(uint8_t note, float modulator, int8_t bend);
    void HandlePacket_OcarinaSfx(nlohmann::json payload);

    // VFX actor spawn — broadcast a fire-and-forget visual actor to teammates.
    // Used for sw97 medallion arrows / spells / FD beam / fin throw / etc.
    // `vfxKind` lets the receiving client filter by category (e.g. clients
    // without the sw97 pack ignore "sw97_*" kinds).
    void SendPacket_SpawnVfxActor(int16_t actorId, float posX, float posY, float posZ,
                                   int16_t rotX, int16_t rotY, int16_t rotZ,
                                   int16_t params, const char* vfxKind,
                                   bool attachedToOwner);
    void HandlePacket_SpawnVfxActor(nlohmann::json payload);

    // Owner registry for spawned VFX actors. Key = Actor*, value = ownerClientId.
    // Used by collision hooks (Phase 4) to route PvP damage through to the
    // actual attacker, and to suppress friendly-fire on the owner's own VFX.
    void  SetVfxActorOwner(const Actor* actor, uint32_t ownerClientId);
    uint32_t GetVfxActorOwner(const Actor* actor);

    // Story sync helpers
    bool syncCutscenes = false;
};

// Damage response types
typedef enum {
    HARPOON_HIT_RESPONSE_STUN = 5,
    HARPOON_HIT_RESPONSE_FIRE,
    HARPOON_HIT_RESPONSE_NORMAL,
    HARPOON_HIT_RESPONSE_WIND_BLOW,  // 8 — Deku Leaf / Gust Jar: zero dmg, big horizontal launch
} HarpoonDamageResponseType;

#endif // __cplusplus
#endif // NETWORK_HARPOON_H
