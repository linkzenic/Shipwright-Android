#include "PvPAnchor.h"
#include "soh/Enhancements/nametag.h"
#include "soh/frame_interpolation.h"

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;

void Player_UseItem(PlayState* play, Player* player, s32 item);
void Player_Draw(Actor* actor, PlayState* play);
}

static DamageTable PvPDummyPlayerDamageTable = {
    /* Deku nut      */ DMG_ENTRY(0, PVP_HIT_RESPONSE_STUN),
    /* Deku stick    */ DMG_ENTRY(2, PVP_HIT_RESPONSE_NORMAL),
    /* Slingshot     */ DMG_ENTRY(1, PVP_HIT_RESPONSE_NORMAL),
    /* Explosive     */ DMG_ENTRY(2, PVP_HIT_RESPONSE_NORMAL),
    /* Boomerang     */ DMG_ENTRY(0, PVP_HIT_RESPONSE_STUN),
    /* Normal arrow  */ DMG_ENTRY(2, PVP_HIT_RESPONSE_NORMAL),
    /* Hammer swing  */ DMG_ENTRY(2, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE),
    /* Hookshot      */ DMG_ENTRY(0, PVP_HIT_RESPONSE_STUN),
    /* Kokiri sword  */ DMG_ENTRY(1, PVP_HIT_RESPONSE_NORMAL),
    /* Master sword  */ DMG_ENTRY(2, PVP_HIT_RESPONSE_NORMAL),
    /* Giant's Knife */ DMG_ENTRY(4, PVP_HIT_RESPONSE_NORMAL),
    /* Fire arrow    */ DMG_ENTRY(2, PVP_HIT_RESPONSE_FIRE),
    /* Ice arrow     */ DMG_ENTRY(4, PLAYER_HIT_RESPONSE_ICE_TRAP),
    /* Light arrow   */ DMG_ENTRY(2, PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK),
    /* Unk arrow 1   */ DMG_ENTRY(2, PLAYER_HIT_RESPONSE_NONE),
    /* Unk arrow 2   */ DMG_ENTRY(2, PLAYER_HIT_RESPONSE_NONE),
    /* Unk arrow 3   */ DMG_ENTRY(2, PLAYER_HIT_RESPONSE_NONE),
    /* Fire magic    */ DMG_ENTRY(0, PVP_HIT_RESPONSE_FIRE),
    /* Ice magic     */ DMG_ENTRY(3, PLAYER_HIT_RESPONSE_ICE_TRAP),
    /* Light magic   */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK),
    /* Shield        */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_NONE),
    /* Mirror Ray    */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_NONE),
    /* Kokiri spin   */ DMG_ENTRY(1, PVP_HIT_RESPONSE_NORMAL),
    /* Giant spin    */ DMG_ENTRY(4, PVP_HIT_RESPONSE_NORMAL),
    /* Master spin   */ DMG_ENTRY(2, PVP_HIT_RESPONSE_NORMAL),
    /* Kokiri jump   */ DMG_ENTRY(2, PVP_HIT_RESPONSE_NORMAL),
    /* Giant jump    */ DMG_ENTRY(8, PVP_HIT_RESPONSE_NORMAL),
    /* Master jump   */ DMG_ENTRY(4, PVP_HIT_RESPONSE_NORMAL),
    /* Unknown 1     */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_NONE),
    /* Unblockable   */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_NONE),
    /* Hammer jump   */ DMG_ENTRY(4, PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE),
    /* Unknown 2     */ DMG_ENTRY(0, PLAYER_HIT_RESPONSE_NONE),
};

static void Math_Vec3s_Copy_PvP(Vec3s* dest, Vec3s* src) {
    dest->x = src->x;
    dest->y = src->y;
    dest->z = src->z;
}

void PvPDummyPlayer_Init(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;

    uint32_t clientId = PvPAnchor::Instance->GetDummyPlayerClientId(actor);

    if (!PvPAnchor::Instance->clients.contains(clientId)) {
        Actor_Kill(actor);
        return;
    }

    PvPClient& client = PvPAnchor::Instance->clients[clientId];

    s32 originalAge = gSaveContext.linkAge;
    gSaveContext.linkAge = client.linkAge;

    actor->room = -1;
    player->itemAction = player->heldItemAction = -1;
    player->heldItemId = ITEM_NONE;
    Player_UseItem(play, player, ITEM_NONE);
    Player_SetModelGroup(player, Player_ActionToModelGroup(player, player->heldItemAction));
    play->playerInit(player, play, gPlayerSkelHeaders[client.linkAge]);
    play->func_11D54(player, play);

    player->cylinder.base.acFlags = AC_ON | AC_TYPE_PLAYER;
    player->cylinder.base.ocFlags2 = OC2_TYPE_1;
    player->cylinder.info.bumperFlags = BUMP_ON | BUMP_HOOKABLE | BUMP_NO_HITMARK;
    player->actor.flags |= ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER;
    player->cylinder.dim.radius = 30;
    player->actor.colChkInfo.damageTable = &PvPDummyPlayerDamageTable;

    gSaveContext.linkAge = originalAge;

    NameTag_RegisterForActorWithOptions(actor, client.name.c_str(), {});
}

void PvPDummyPlayer_Update(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;

    uint32_t clientId = PvPAnchor::Instance->GetDummyPlayerClientId(actor);

    if (!PvPAnchor::Instance->clients.contains(clientId)) {
        Actor_Kill(actor);
        return;
    }

    PvPClient& client = PvPAnchor::Instance->clients[clientId];

    if (client.sceneNum != gPlayState->sceneNum || !client.online || !client.isSaveLoaded) {
        actor->world.pos.x = -9999.0f;
        actor->world.pos.y = -9999.0f;
        actor->world.pos.z = -9999.0f;
        actor->shape.shadowAlpha = 0;
        return;
    }

    // If eliminated in HG, hide
    if (!client.isAlive && PvPAnchor::Instance->gameState == PVP_HG_PLAYING) {
        actor->world.pos.x = -9999.0f;
        actor->world.pos.y = -9999.0f;
        actor->world.pos.z = -9999.0f;
        actor->shape.shadowAlpha = 0;
        return;
    }

    actor->shape.shadowAlpha = 255;
    Math_Vec3s_Copy_PvP(&player->upperLimbRot, &client.upperLimbRot);
    Math_Vec3s_Copy_PvP(&actor->shape.rot, &client.posRot.rot);
    Math_Vec3f_Copy(&actor->world.pos, &client.posRot.pos);
    player->skelAnime.jointTable = client.jointTable;
    player->skelAnime.movementFlags = client.movementFlags;
    Math_Vec3s_Copy_PvP(&player->skelAnime.prevTransl, &client.prevTransl);
    player->currentBoots = client.currentBoots;
    player->currentShield = client.currentShield;
    player->currentTunic = client.currentTunic;
    player->stateFlags1 = client.stateFlags1;
    player->stateFlags2 = client.stateFlags2;
    player->itemAction = client.itemAction;
    player->heldItemAction = client.heldItemAction;
    player->invincibilityTimer = client.invincibilityTimer;
    player->unk_862 = client.unk_862;
    player->unk_85C = client.unk_85C;
    player->av1.actionVar1 = client.actionVar1;

    // Update collider dimensions from transformation data
    if (client.cylRadius > 0) {
        player->cylinder.dim.radius = client.cylRadius;
    }
    if (client.cylHeight > 0) {
        player->cylinder.dim.height = client.cylHeight;
    }
    player->cylinder.dim.yShift = client.cylYShift;

    // Apply animation movement
    Vec3f diff;
    SkelAnime_UpdateTranslation(&player->skelAnime, &diff, player->actor.shape.rot.y);

    if (player->skelAnime.movementFlags & 1) {
        if (!LINK_IS_ADULT) {
            diff.x *= 0.64f;
            diff.z *= 0.64f;
        }
        player->actor.world.pos.x += diff.x * player->actor.scale.x;
        player->actor.world.pos.z += diff.z * player->actor.scale.z;
    }

    if (player->skelAnime.movementFlags & 2) {
        if (!(player->skelAnime.movementFlags & 4)) {
            diff.y *= player->ageProperties->unk_08;
        }
        player->actor.world.pos.y += diff.y * player->actor.scale.y;
    }

    if (player->modelGroup != client.modelGroup) {
        s32 originalAge = gSaveContext.linkAge;
        gSaveContext.linkAge = client.linkAge;
        u8 originalButtonItem0 = gSaveContext.equips.buttonItems[0];
        gSaveContext.equips.buttonItems[0] = client.buttonItem0;
        Player_SetModelGroup(player, client.modelGroup);
        gSaveContext.linkAge = originalAge;
        gSaveContext.equips.buttonItems[0] = originalButtonItem0;
    }

    // PvP is always enabled in PvP Anchor (no team check)
    // During lobby or countdown, disable PvP hits
    if (PvPAnchor::Instance->gameState != PVP_HG_PLAYING) {
        actor->flags |= ACTOR_FLAG_LOCK_ON_DISABLED;
        return;
    }

    actor->flags &= ~ACTOR_FLAG_LOCK_ON_DISABLED;

    if (player->cylinder.base.acFlags & AC_HIT && player->invincibilityTimer == 0) {
        PvPAnchor::Instance->SendPacket_Damage(client.clientId, player->actor.colChkInfo.damageEffect,
                                                player->actor.colChkInfo.damage);
        if (player->actor.colChkInfo.damageEffect == PVP_HIT_RESPONSE_STUN) {
            Actor_SetColorFilter(&player->actor, 0, 0xFF, 0, 24);
        } else {
            player->invincibilityTimer = 20;
        }
    }

    Collider_UpdateCylinder(&player->actor, &player->cylinder);

    if (!(player->stateFlags2 & PLAYER_STATE2_FROZEN)) {
        if (!(player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_HANGING_OFF_LEDGE |
                                     PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_ON_HORSE))) {
            CollisionCheck_SetOC(play, &play->colChkCtx, &player->cylinder.base);
        }

        if (!(player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_DAMAGED)) &&
            (player->invincibilityTimer <= 0)) {
            CollisionCheck_SetAC(play, &play->colChkCtx, &player->cylinder.base);

            if (player->invincibilityTimer < 0) {
                CollisionCheck_SetAT(play, &play->colChkCtx, &player->cylinder.base);
            }
        }
    }

    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE)) {
        player->actor.colChkInfo.mass = MASS_IMMOVABLE;
    } else {
        player->actor.colChkInfo.mass = 50;
    }

    Collider_ResetCylinderAC(play, &player->cylinder.base);
}

void PvPDummyPlayer_Draw(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;

    uint32_t clientId = PvPAnchor::Instance->GetDummyPlayerClientId(actor);

    if (!PvPAnchor::Instance->clients.contains(clientId)) {
        Actor_Kill(actor);
        return;
    }

    PvPClient& client = PvPAnchor::Instance->clients[clientId];

    if (client.sceneNum != gPlayState->sceneNum || !client.online || !client.isSaveLoaded) {
        return;
    }

    if (!client.isAlive && PvPAnchor::Instance->gameState == PVP_HG_PLAYING) {
        return;
    }

    // TODO: If client.transformation != 0, use MmPlayer_Draw for MM form rendering
    // For now, always use standard Player_Draw
    s32 originalAge = gSaveContext.linkAge;
    gSaveContext.linkAge = client.linkAge;
    u8 originalButtonItem0 = gSaveContext.equips.buttonItems[0];
    gSaveContext.equips.buttonItems[0] = client.buttonItem0;

    Player_Draw((Actor*)player, play);

    gSaveContext.linkAge = originalAge;
    gSaveContext.equips.buttonItems[0] = originalButtonItem0;
}

void PvPDummyPlayer_Destroy(Actor* actor, PlayState* play) {
}
