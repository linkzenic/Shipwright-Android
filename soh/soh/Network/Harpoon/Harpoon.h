#ifndef NETWORK_HARPOON_H
#define NETWORK_HARPOON_H
#ifdef __cplusplus

#include "soh/Network/Network.h"
#include <libultraship/libultraship.h>
#include <queue>
#include <mutex>
#include <vector>

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

// Game mode enum (from Scooter)
typedef enum {
    HARPOON_MODE_NONE = 0,
    HARPOON_MODE_HUNGER_GAMES,
    HARPOON_MODE_PROP_HUNT,
    HARPOON_MODE_RANDOMIZER,
} HarpoonGameMode;

// Game states (from Scooter)
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

// Map selection modes (from Scooter)
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
    s8 invincibilityTimer;
    f32 unk_85C;
    s16 unk_862;
    s8 actionVar1;

    // Transformation data
    u8 transformation;     // MM_PLAYER_FORM_GORON, etc. (0 = human/no transform)
    s16 cylRadius;         // Form-specific collider radius
    s16 cylHeight;         // Form-specific collider height
    s16 cylYShift;         // Form-specific collider Y offset
    u32 mmStateFlags3;     // MM stateFlags3 (spike mode, roll active, etc.)
    f32 mmSpeedXZ;         // MM horizontal speed

    // Model type for rendering (transformation masks, prop hunt)
    u8 modelType;          // 0=Link, 1=Goron, 2=Zora, 3=Deku, 4=FierceDeity, 5+=props
    u16 propObjectId;      // Object ID for prop hunt (0 = no prop)

    // OOT visual state
    u8 currentMask;        // PlayerMask enum (0=none, 1-8=masks)
    s32 wornMask;          // MM worn mask item ID (ITEM_NONE=no mask, from MmMaskWear system)
    s16 face;              // actor.shape.face (eye/mouth texture index)
    f32 scaleX, scaleY, scaleZ; // actor.scale

    // MM form visual state (for remote rendering)
    s32 goronAction;       // GoronActionId - determines ball vs standing draw
    u8 eyeIndex;           // Blink state (0=open, 1=half, 2=closed)
    f32 rollSquash;        // Ball deformation factor
    s16 rollSpikeActive;   // Spike mode counter (0=off, >0=active)
    s16 rollChargeLevel;   // Charge level for energy effects

    // Custom item visual state (for remote rendering)
    u32 customItemFlags;   // CI_FLAG_* bitfield
    // Beetle
    Vec3f ciBeetlePos;
    Vec3s ciBeetleRot;
    f32 ciBeetleWingScale;
    u8 ciBeetleState;
    // Gust Jar
    Vec3f ciGustJarProjPos;
    u8 ciGustJarProjActive;
    u8 ciGustJarAmmoType;
    u8 ciGustJarMode;
    s16 ciGustJarProjYaw;
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

    // Prop Hunt state (from Scooter)
    std::string role;      // "seeker" or "hider" (empty = no game)
    s32 propCategory;      // 0=env, 1=enemies, 2=npcs
    s32 propIndex;         // 0-9 within category (-1 = no prop / Link)
    s32 propState;         // Current state/variant within prop

    // Somaria Decoy state (Prop Hunt, from Scooter)
    Vec3f somariaDecoyPos[3];
    s16   somariaDecoyRotY[3];
    s32   somariaDecoyPropIdx[3];
    s32   somariaDecoyPropCat[3];
    s32   somariaDecoyPropState[3];
    u8    somariaDecoyActive[3];
    u8    somariaDecoyCount;

    // Map selection state (from Scooter)
    s32 mapSelectIndex;
    bool hasVoted;

    // Game mode state (from Scooter)
    bool isAlive;
    bool isReady;
    bool isAdmin;
    s16 kills;
    std::string team;

    // Remote somaria cubes
    struct {
        Vec3f pos;
        u8 state;   // 0=none, 1=spawn, 2=idle, 3=held, 4=thrown
        u8 form;    // Elegy form (ELEGY_FORM_*)
        f32 scale;
        s16 rotY;
    } remoteCubes[3];
    u8 remoteCubeCount;
    Actor* remoteCubeActors[3];

    // Ptr to the dummy player actor
    Player* player;
} HarpoonClient;

class Harpoon : public Network {
  private:
    uint32_t spawningDummyPlayerForClientId = 0;
    bool shouldRefreshActors = false;

    std::queue<nlohmann::json> incomingPacketQueue;
    std::mutex incomingPacketQueueMutex;
    std::queue<nlohmann::json> outgoingPacketQueue;
    std::mutex outgoingPacketQueueMutex;

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

  public:
    // Packet type strings (wire values kept for server compatibility)
    inline static const std::string HPN_HANDSHAKE = "PVP_HANDSHAKE";
    inline static const std::string HPN_ALL_CLIENTS = "PVP_ALL_CLIENTS";
    inline static const std::string HPN_PLAYER_UPDATE = "PVP_PLAYER_UPDATE";
    inline static const std::string HPN_DAMAGE = "PVP_DAMAGE";
    inline static const std::string HPN_PLAYER_DIED = "PVP_PLAYER_DIED";
    inline static const std::string HPN_PLAYER_SFX = "PVP_PLAYER_SFX";
    inline static const std::string HPN_SERVER_MSG = "PVP_SERVER_MSG";
    inline static const std::string HPN_GIVE_ITEM = "PVP_GIVE_ITEM";
    inline static const std::string HPN_UPDATE_TEAM_STATE = "PVP_UPDATE_TEAM_STATE";

    // Scooter packet types
    inline static const std::string HPN_GAME_STATE = "PVP_GAME_STATE";
    inline static const std::string HPN_CHEST_OPENED = "PVP_CHEST_OPENED";
    inline static const std::string HPN_READY = "PVP_READY";
    inline static const std::string HPN_START_GAME = "PVP_START_GAME";
    inline static const std::string HPN_WINNER = "PVP_WINNER";
    inline static const std::string HPN_MAP_CONFIRM = "PVP_MAP_CONFIRM";
    inline static const std::string HPN_ROLE_CHANGE = "PVP_ROLE_CHANGE";
    inline static const std::string HPN_MAP_VOTE = "PVP_MAP_VOTE";
    inline static const std::string HPN_DECOY_HIT = "PVP_DECOY_HIT";
    inline static const std::string HPN_CUSTOM_DAMAGE = "PVP_CUSTOM_DAMAGE";
    inline static const std::string HPN_CUSTOM_EFFECT = "PVP_CUSTOM_EFFECT";

    // Anchor rando packet types
    inline static const std::string HPN_SET_FLAG = "PVP_SET_FLAG";
    inline static const std::string HPN_UNSET_FLAG = "PVP_UNSET_FLAG";
    inline static const std::string HPN_SET_CHECK_STATUS = "PVP_SET_CHECK_STATUS";
    inline static const std::string HPN_ENTRANCE_DISCOVERED = "PVP_ENTRANCE_DISCOVERED";
    inline static const std::string HPN_UPDATE_DUNGEON_ITEMS = "PVP_UPDATE_DUNGEON_ITEMS";
    inline static const std::string HPN_TELEPORT_TO = "PVP_TELEPORT_TO";
    inline static const std::string HPN_UPDATE_BEANS_COUNT = "PVP_UPDATE_BEANS_COUNT";

    static Harpoon* Instance;
    std::map<uint32_t, HarpoonClient> clients;
    uint32_t ownClientId = 0;

    // Item sync state
    bool syncItems = false;
    bool pvpEnabled = true;  // When false: no damage/knockback from other players, status effects still apply
    bool isProcessingIncomingPacket = false;
    bool isHandlingUpdateTeamState = false;
    bool justLoadedSave = false;

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
    void ProcessOutgoingPackets() override;
    void SendJsonToRemote(nlohmann::json packet) override;
    void ProcessIncomingPacketQueue();
    bool IsSaveLoaded();
    uint32_t GetDummyPlayerClientId(const Actor* actor);

    // Send packets (existing)
    void SendPacket_Handshake();
    void SendPacket_PlayerUpdate();
    void SendPacket_Damage(u32 clientId, u8 damageEffect, u8 damage);
    void SendPacket_PlayerDied();
    void SendPacket_PlayerSfx(u16 sfxId);
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
};

// Damage response types
typedef enum {
    HARPOON_HIT_RESPONSE_STUN = 5,
    HARPOON_HIT_RESPONSE_FIRE,
    HARPOON_HIT_RESPONSE_NORMAL,
} HarpoonDamageResponseType;

#endif // __cplusplus
#endif // NETWORK_HARPOON_H
