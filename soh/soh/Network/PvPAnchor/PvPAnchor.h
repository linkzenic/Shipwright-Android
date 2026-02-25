#ifndef NETWORK_PVP_ANCHOR_H
#define NETWORK_PVP_ANCHOR_H
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

// Forward declarations for PvP dummy player
void PvPDummyPlayer_Init(Actor* actor, PlayState* play);
void PvPDummyPlayer_Update(Actor* actor, PlayState* play);
void PvPDummyPlayer_Draw(Actor* actor, PlayState* play);
void PvPDummyPlayer_Destroy(Actor* actor, PlayState* play);

// CVar prefix for PvP Anchor settings
#define CVAR_PVP_ANCHOR(var) "Remote.PvPAnchor." var

// Hunger Games states
typedef enum {
    PVP_HG_DISCONNECTED,
    PVP_HG_LOBBY,
    PVP_HG_COUNTDOWN,
    PVP_HG_PLAYING,
    PVP_HG_SPECTATING,
    PVP_HG_FINISHED,
} PvPHungerGamesState;

// Extended client struct with transformation data
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

    // Player visual state (same as AnchorClient)
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

    // === NEW: Transformation data ===
    u8 transformation;     // MM_PLAYER_FORM_GORON, etc. (0 = human/no transform)
    s16 cylRadius;         // Form-specific collider radius
    s16 cylHeight;         // Form-specific collider height
    s16 cylYShift;         // Form-specific collider Y offset
    u32 mmStateFlags3;     // MM stateFlags3 (spike mode, roll active, etc.)
    f32 mmSpeedXZ;         // MM horizontal speed

    // === Hunger Games state ===
    bool isAlive;
    bool isReady;
    s16 kills;

    // Ptr to the dummy player actor
    Player* player;
} PvPClient;

class PvPAnchor : public Network {
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
    void HandlePacket_GameState(nlohmann::json payload);
    void HandlePacket_Winner(nlohmann::json payload);
    void HandlePacket_ServerMsg(nlohmann::json payload);
    void HandlePacket_PlayerSfx(nlohmann::json payload);

  public:
    // Packet type strings
    inline static const std::string PVP_HANDSHAKE = "PVP_HANDSHAKE";
    inline static const std::string PVP_ALL_CLIENTS = "PVP_ALL_CLIENTS";
    inline static const std::string PVP_PLAYER_UPDATE = "PVP_PLAYER_UPDATE";
    inline static const std::string PVP_DAMAGE = "PVP_DAMAGE";
    inline static const std::string PVP_PLAYER_DIED = "PVP_PLAYER_DIED";
    inline static const std::string PVP_PLAYER_SFX = "PVP_PLAYER_SFX";
    inline static const std::string PVP_GAME_STATE = "PVP_GAME_STATE";
    inline static const std::string PVP_CHEST_OPENED = "PVP_CHEST_OPENED";
    inline static const std::string PVP_READY = "PVP_READY";
    inline static const std::string PVP_START_GAME = "PVP_START_GAME";
    inline static const std::string PVP_WINNER = "PVP_WINNER";
    inline static const std::string PVP_SERVER_MSG = "PVP_SERVER_MSG";

    static PvPAnchor* Instance;
    std::map<uint32_t, PvPClient> clients;
    uint32_t ownClientId = 0;

    // Hunger Games state
    PvPHungerGamesState gameState = PVP_HG_DISCONNECTED;
    s32 countdownTimer = 0;
    s32 aliveCount = 0;
    bool isEliminated = false;
    std::vector<std::string> killFeed; // Last N kill messages

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

    // Send packets
    void SendPacket_Handshake();
    void SendPacket_PlayerUpdate();
    void SendPacket_Damage(u32 clientId, u8 damageEffect, u8 damage);
    void SendPacket_PlayerDied();
    void SendPacket_PlayerSfx(u16 sfxId);
    void SendPacket_ChestOpened(s16 sceneNum, s16 flag);
    void SendPacket_Ready();
    void SendPacket_StartGame();
};

// Damage response types (same as Anchor's DummyPlayer)
typedef enum {
    PVP_HIT_RESPONSE_STUN = 5,
    PVP_HIT_RESPONSE_FIRE,
    PVP_HIT_RESPONSE_NORMAL,
} PvPDamageResponseType;

#endif // __cplusplus
#endif // NETWORK_PVP_ANCHOR_H
