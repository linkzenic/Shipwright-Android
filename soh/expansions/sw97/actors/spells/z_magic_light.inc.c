/**
 * Original: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * File: z_magic_light.c
 * Overlay: ovl_Magic_Light
 * Description: Sun's Song Effect
 */

#include "expansions/sw97/sw97_compat.h"
#include "expansions/sw97/sw97_config.h"

// ============================================================
// Struct (merged from z_magic_light.h)
// ============================================================

struct MagicLight;

typedef void (*MagicLightActionFunc)(struct MagicLight*, PlayState*);

typedef struct MagicLight {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ LightNode* lightNode1;
    /* 0x0150 */ LightInfo lightInfo1;
    /* 0x0160 */ LightNode* lightNode2;
    /* 0x0164 */ LightInfo lightInfo2;
    /* 0x0174 */ f32 unk_174;
    /* 0x0178 */ u16 timer;
    /* 0x017C */ MagicLightActionFunc actionFunc;
    /*        */ ColliderCylinder collider;
    u8 massStunApplied;
} MagicLight;

// Runtime actor ID (assigned by ActorDB in sw97_init.cpp)
extern s16 gSw97ActorId_MagicLight;

// ============================================================
// Forward declarations
// ============================================================

#define FLAGS 0x02000010
#define THIS ((MagicLight*)thisx)

void MagicLight_Init(Actor* thisx, PlayState* play);
void MagicLight_Destroy(Actor* thisx, PlayState* play);
void MagicLight_Update(Actor* thisx, PlayState* play);
void MagicLight_Draw(Actor* thisx, PlayState* play);

void MagicLight_GrowCylinder(MagicLight* this, PlayState* play);

// ============================================================
// Graphics data (merged from z_magic_light_gfx.c)
// ============================================================

static u32 sTexture[] = {
    0x354D5AAC, 0xA5561C01, 0x124688B5, 0x74583231, 0x313D71D6, 0x9F5B3507, 0x104089AD, 0x748F440C, 0x408176AC,
    0xB4661301, 0x2261C0BE, 0x49392533, 0x2C497FC4, 0x80451D06, 0x2466BBD0, 0x9CB85411, 0x40A09DA3, 0xB37F210B,
    0x3D8AEAC7, 0x3F241930, 0x2F5591A7, 0x6441080F, 0x4191E8DB, 0xB7A76B18, 0x4094BAA0, 0x76532D22, 0x60BAFEE8,
    0x8253545F, 0x6078A896, 0x73540723, 0x66C2FEBB, 0x87888723, 0x4B87C79E, 0x33123048, 0x88E3FFFC, 0xD39F7E72,
    0x8DA2AE80, 0xA77B1940, 0x90EBFFA9, 0x6289973E, 0x6F9ACD83, 0x2D104B7F, 0xAEEBFDF3, 0xD8B26772, 0xA7C7AD77,
    0xB1B34A61, 0xB0F8FFD5, 0x9496986B, 0x9EC6D25D, 0x503E85B3, 0xC0C1E1EE, 0xBD9B618E, 0xBFE4A975, 0x86E18B86,
    0xABCFF7F2, 0xBC837E69, 0xB5EFE465, 0x5B66BACB, 0xC794AADC, 0xAC8982AE, 0xD3F59754, 0x65E2AAAD, 0x8C85D0E1,
    0xAF634D39, 0xC7E2EDA3, 0x527ECACF, 0xBF777FC0, 0xA284A5A5, 0xE3FE8A2F, 0x60CCAFBE, 0x7164B8BA, 0x97553A2B,
    0xE8BCBCDA, 0x76A1BBBA, 0x844D83BD, 0x9C6B875B, 0xD9FFAA3F, 0x6AC9B59B, 0x6592BD94, 0x956D3D3B, 0xFFA773D3,
    0xB5B9AB90, 0x4033A0BC, 0x873E3E1D, 0xB5FFE48D, 0x8CC9A761, 0x62CBAF79, 0x98883457, 0xF3766FAF, 0xD5B2A26B,
    0x44549D8B, 0x56232221, 0x7DFFF2DD, 0xC6B68840, 0x4ECCA267, 0x696D296E, 0xA8328AAD, 0xD3998447, 0x5E7D8C5B,
    0x391F1E35, 0x4EF6CDD3, 0xDD9B6C2D, 0x27A09571, 0x37443656, 0x4C1265C9, 0xCA91612E, 0x3B5A7A65, 0x2F4F3421,
    0x36EAC380, 0xB6814A1B, 0x0C607075, 0x5860803B, 0x27102CCD, 0xC9A87A47, 0x16204B80, 0x44B3860F, 0x28DAE042,
    0x7F773710, 0x0235616D, 0xA089BE60, 0x34161EBC, 0xBBAAA972, 0x172A3B91, 0x96F6C319, 0x21B7F02A, 0x618F662A,
    0x02327A8C, 0x97577B5C, 0x48171FB3, 0xA090AF87, 0x1F65609D, 0xD8FFD41E, 0x177CC522, 0x69A8A462, 0x0D3A93B6,
    0x4F0C1821, 0x39151CA7, 0x928EAA9A, 0x33919BAF, 0xEBFBDA23, 0x093B6937, 0x80AEA66C, 0x304491C7, 0x3A031710,
    0x16162197, 0x9B9FB5BA, 0x4C95CED8, 0xF8F0D425, 0x05144066, 0x84B17F5A, 0x725E8DCD, 0x5D376910, 0x19183886,
    0xA3A1C4C6, 0x6584D1F1, 0xFFFABF12, 0x0E123F77, 0x88AF6881, 0xB97F98BB, 0x879BBC23, 0x4A20557C, 0x9BA5D0BB,
    0x7583C6E0, 0xFEFFB322, 0x34283976, 0x95B06EB8, 0xE29B999F, 0xBDE9E582, 0x814C5C82, 0xA1C0ECBE, 0x789ED6B2,
    0xF4FFD78F, 0x73324087, 0xA0C26FAB, 0xE0B1A5B9, 0xF2FFFADE, 0xAC906A89, 0xB3DDFFCB, 0x86B8CD75, 0xDFFFF1D7,
    0x8F31579C, 0xAFDE7B87, 0xC8BAB4EB, 0xF0F2FFE4, 0xB29B798B, 0xC3F4FFD2, 0xA0A17C32, 0xA9FFE8BD, 0x784D7A91,
    0xC7F5AB9B, 0xBEBB86D9, 0xB0BCF9D4, 0x835C4C70, 0xD8FFFFD7, 0xA2682907, 0x68FFE29F, 0x61707B7A, 0xE5FFE7C8,
    0xCCC33C8E, 0x768CE0C7, 0x51281940, 0xCBFFFFCB, 0x87450C00, 0x4BF3D58E, 0x6D767B92, 0xFAFFFFC8, 0xC7BF175B,
    0x708BBA9A, 0x3A161542, 0xB4FFF5A9, 0x6D39090C, 0x50D8BC86, 0x897AA1C8, 0xFFFFF4A1, 0x8C880B50, 0x91A49762,
    0x2D0C1E78, 0xD4FFD97F, 0x58351E23, 0x6BB49283, 0x9B95C9EE, 0xFFFFD377, 0x4336075E, 0xBFB27641, 0x1B0224B5,
    0xFCF7AC55, 0x41384344, 0x8FA06468, 0x9FB5DBFD, 0xFFF1A553, 0x190B1275, 0xCCA75D3A, 0x0C042ED9, 0xFFD97C33,
    0x28405669, 0xB0914C3A, 0x9199C6FF, 0xFECF7835, 0x080C308B, 0xB38F6D47, 0x0A0D3DDB, 0xF3A75519, 0x1341558E,
    0xBE7B4321, 0x6B4D94F8, 0xE4AC591A, 0x02175096, 0x8E7A9145, 0x1D224BBF, 0xC1723508, 0x0D3E5FA5, 0xA568351E,
    0x422B6EE0, 0xBB85480B, 0x04256799, 0x716D6D21,
};

static Vtx sLightCylinderVtx[] = {
    VTX(35, 0, -35, 1280, 1024, 0xFF, 0xFF, 0xFF, 0x00),   VTX(35, 150, -35, 1280, 512, 0xFF, 0xFF, 0xFF, 0x7E),
    VTX(50, 150, 0, 1024, 512, 0xFF, 0xFF, 0xFF, 0x7E),    VTX(50, 0, 0, 1024, 1024, 0xFF, 0xFF, 0xFF, 0x00),
    VTX(35, 500, -35, 1280, 0, 0xFF, 0xFF, 0xFF, 0xFF),    VTX(50, 500, 0, 1024, 0, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(0, 0, -50, 1536, 1024, 0xFF, 0xFF, 0xFF, 0x00),    VTX(0, 150, -50, 1536, 512, 0xFF, 0xFF, 0xFF, 0x7E),
    VTX(0, 500, -50, 1536, 0, 0xFF, 0xFF, 0xFF, 0xFF),     VTX(-35, 0, -35, 1792, 1024, 0xFF, 0xFF, 0xFF, 0x00),
    VTX(-35, 150, -35, 1792, 512, 0xFF, 0xFF, 0xFF, 0x7E), VTX(-35, 500, -35, 1792, 0, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(-50, 0, 0, 2048, 1024, 0xFF, 0xFF, 0xFF, 0x00),    VTX(-50, 150, 0, 2048, 512, 0xFF, 0xFF, 0xFF, 0x7E),
    VTX(-50, 500, 0, 2048, 0, 0xFF, 0xFF, 0xFF, 0xFF),     VTX(-35, 0, 35, 256, 1024, 0xFF, 0xFF, 0xFF, 0x00),
    VTX(-35, 150, 35, 256, 512, 0xFF, 0xFF, 0xFF, 0x7E),   VTX(-50, 150, 0, 0, 512, 0xFF, 0xFF, 0xFF, 0x7E),
    VTX(-50, 0, 0, 0, 1024, 0xFF, 0xFF, 0xFF, 0x00),       VTX(-35, 500, 35, 256, 0, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(-50, 500, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF),        VTX(0, 0, 50, 512, 1024, 0xFF, 0xFF, 0xFF, 0x00),
    VTX(0, 150, 50, 512, 512, 0xFF, 0xFF, 0xFF, 0x7E),     VTX(0, 500, 50, 512, 0, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(35, 0, 35, 768, 1024, 0xFF, 0xFF, 0xFF, 0x00),     VTX(35, 150, 35, 768, 512, 0xFF, 0xFF, 0xFF, 0x7E),
    VTX(35, 500, 35, 768, 0, 0xFF, 0xFF, 0xFF, 0xFF),
};

static Gfx sLightTextureDL[] = {
    gsDPPipeSync(),
    gsDPSetTextureLUT(G_TT_NONE),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPLoadTextureBlock(sTexture, G_IM_FMT_I, G_IM_SIZ_8b, 32, 32, 0, G_TX_NOMIRROR | G_TX_WRAP,
                         G_TX_NOMIRROR | G_TX_WRAP, 5, 5, 1, 0),
    gsDPTileSync(),
    gsDPSetTile(G_IM_FMT_I, G_IM_SIZ_8b, 4, 0x0, 1, 0, G_TX_NOMIRROR | G_TX_WRAP, 5, 15, G_TX_NOMIRROR | G_TX_WRAP, 5,
                0),
    gsDPSetTileSize(1, 0, 0, (31 << 2), (31 << 2)),
    gsDPSetCombineLERP(TEXEL1, PRIMITIVE, ENV_ALPHA, TEXEL0, TEXEL1, TEXEL0, ENVIRONMENT, TEXEL0, PRIMITIVE,
                       ENVIRONMENT, COMBINED, ENVIRONMENT, COMBINED, 0, SHADE, 0),
    gsDPSetRenderMode(AA_EN | Z_CMP | IM_RD | CLR_ON_CVG | CVG_DST_WRAP | ZMODE_XLU | FORCE_BL |
                          GBL_c1(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1),
                      G_RM_AA_ZB_XLU_SURF2),
    gsSPClearGeometryMode(G_TEXTURE_ENABLE | G_CULL_BACK | G_FOG | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR),
    gsDPSetPrimColor(0x00, 0x00, 255, 255, 255, 255),
    gsDPSetEnvColor(170, 170, 170, 128),
    gsSPEndDisplayList(),
};

static Gfx sCylinderDL[] = {
    gsSPVertex(sLightCylinderVtx, 27, 0),         gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsSP2Triangles(1, 4, 5, 0, 1, 5, 2, 0),       gsSP2Triangles(6, 7, 1, 0, 6, 1, 0, 0),
    gsSP2Triangles(7, 8, 4, 0, 7, 4, 1, 0),       gsSP2Triangles(9, 10, 7, 0, 9, 7, 6, 0),
    gsSP2Triangles(10, 11, 8, 0, 10, 8, 7, 0),    gsSP2Triangles(12, 13, 10, 0, 12, 10, 9, 0),
    gsSP2Triangles(13, 14, 11, 0, 13, 11, 10, 0), gsSP2Triangles(15, 16, 17, 0, 15, 17, 18, 0),
    gsSP2Triangles(16, 19, 20, 0, 16, 20, 17, 0), gsSP2Triangles(21, 22, 16, 0, 21, 16, 15, 0),
    gsSP2Triangles(22, 23, 19, 0, 22, 19, 16, 0), gsSP2Triangles(24, 25, 22, 0, 24, 22, 21, 0),
    gsSP2Triangles(25, 26, 23, 0, 25, 23, 22, 0), gsSP2Triangles(3, 2, 25, 0, 3, 25, 24, 0),
    gsSP2Triangles(2, 5, 26, 0, 2, 26, 25, 0),    gsSPEndDisplayList(),
};

// ============================================================
// Collider
// ============================================================

static ColliderCylinderInit sLightCylinderInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_1,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { DMG_MAGIC_LIGHT, 0x00, 0x7F },
        { 0x00000000, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NONE,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { 3250.0f, 4500.0f, -2250.0f, { 0, 0, 0 } },
};

static InitChainEntry sLightInitChain[] = {
    ICHAIN_VEC3F_DIV1000(scale, 0, ICHAIN_CONTINUE),
    ICHAIN_F32(uncullZoneForward, 1500, ICHAIN_STOP),
};

// ============================================================
// Undead stun helpers
// ============================================================

static u8 MagicLight_IsUndeadActor(Actor* actor) {
    switch (actor->id) {
        case ACTOR_EN_RD:         // ReDead / Gibdo
        case ACTOR_EN_POH:        // Poe
        case ACTOR_EN_PO_SISTERS: // Poe Sisters
        case ACTOR_EN_DH:         // Dead Hand
        case ACTOR_EN_BB:         // Bubble
        case ACTOR_EN_FLOORMAS:   // Floormaster
        case ACTOR_EN_WALLMAS:    // Wallmaster
        case ACTOR_EN_RR:         // Like Like
        case ACTOR_EN_SKB:        // Stalchild
        case ACTOR_EN_TEST:       // Stalfos
        case ACTOR_EN_TORCH2:     // Dark Link
            return 1;
        default:
            return 0;
    }
}

static void MagicLight_StunAllUndead(PlayState* play) {
    Actor* enemy = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
    while (enemy != NULL) {
        Actor* next = enemy->next;
        if (MagicLight_IsUndeadActor(enemy)) {
            enemy->freezeTimer = 600;                          // 30 sec (same as Sun's Song)
            Actor_SetColorFilter(enemy, -0x8000, 200, 0, 255); // white flash
        }
        enemy = next;
    }
}

// ============================================================
// Action setup
// ============================================================

void MagicLight_SetupAction(MagicLight* this, MagicLightActionFunc actionFunc) {
    this->actionFunc = actionFunc;
}

// ============================================================
// Init / Destroy
// ============================================================

void MagicLight_Init(Actor* thisx, PlayState* play) {
    s32 pad;
    MagicLight* this = THIS;

    Actor_ProcessInitChain(&this->actor, sLightInitChain);
    MagicLight_SetupAction(this, MagicLight_GrowCylinder);

    Lights_PointNoGlowSetInfo(&this->lightInfo1, this->actor.world.pos.x, this->actor.world.pos.y,
                              this->actor.world.pos.z, 0, 0, 0, 0);
    this->lightNode1 = LightContext_InsertLight(play, &play->lightCtx, &this->lightInfo1);

    Lights_PointNoGlowSetInfo(&this->lightInfo2, this->actor.world.pos.x, this->actor.world.pos.y,
                              this->actor.world.pos.z, 0, 0, 0, 0);
    this->lightNode2 = LightContext_InsertLight(play, &play->lightCtx, &this->lightInfo2);
    if (YREG(15)) {
        this->actor.scale.y = 2.4f;
    } else {
        this->actor.scale.y = 0.3f;
    }

    this->unk_174 = -1.0f;

    Collider_InitCylinder(play, &this->collider);
    Collider_SetCylinder(play, &this->collider, &this->actor, &sLightCylinderInit);
    Collider_UpdateCylinder(&this->actor, &this->collider);
    this->massStunApplied = 0;
}

void MagicLight_Destroy(Actor* thisx, PlayState* play) {
    s32 pad;
    MagicLight* this = THIS;
    Player* player = PLAYER;

    LightContext_RemoveLight(play, &play->lightCtx, this->lightNode1);
    LightContext_RemoveLight(play, &play->lightCtx, this->lightNode2);
    func_800876C8(play);
    if ((gSaveContext.nayrusLoveTimer != 0) && (player != NULL)) {
        player->stateFlags3 |= 0x40;
    }
}

// ============================================================
// Action functions
// ============================================================

void MagicLight_End(MagicLight* this, PlayState* play) {
    if (this->unk_174 > 0) {
        this->unk_174 -= 0.05f;
    } else {
        Actor_Kill(&this->actor);
    }
}

void MagicLight_Wait(MagicLight* this, PlayState* play) {
    if (this->timer > 0) {
        this->timer--;
    } else {
        MagicLight_SetupAction(this, MagicLight_End);
    }
}

void MagicLight_GrowCylinder(MagicLight* this, PlayState* play) {
    if (this->unk_174 < 1.0f) {
        this->unk_174 += 0.05f;
    } else {
        MagicLight_SetupAction(this, MagicLight_Wait);
        this->timer = 20;
        // Mass stun all undead when light reaches full size
        if (!this->massStunApplied) {
            MagicLight_StunAllUndead(play);
            this->massStunApplied = 1;
        }
    }
}

// ============================================================
// Update
// ============================================================

void MagicLight_Update(Actor* thisx, PlayState* play) {
    MagicLight* this = THIS;
    s32 pad;
    Player* player = PLAYER;
    f32 temp;

    if (this->unk_174 >= 0.0f) {
        func_8002F974(&this->actor, NA_SE_PL_ARROW_CHARGE_LIGHT - SFX_FLAG);

        Collider_UpdateCylinder(&this->actor, &this->collider);
        CollisionCheck_SetAT(play, &play->colChkCtx, &this->collider.base);

        // Undead stun + double damage on AT hit
        if (this->collider.base.atFlags & AT_HIT) {
            Actor* hitActor = this->collider.base.at;
            if (hitActor != NULL && hitActor->update != NULL && MagicLight_IsUndeadActor(hitActor)) {
                // Stun (catches enemies spawned after mass stun)
                hitActor->freezeTimer = 600;
                Actor_SetColorFilter(hitActor, -0x8000, 200, 0, 255);
                // Double damage: subtract extra HP
                if (hitActor->colChkInfo.health > 0) {
                    u8 dmg = this->collider.info.toucher.damage;
                    if (hitActor->colChkInfo.health > dmg) {
                        hitActor->colChkInfo.health -= dmg;
                    } else {
                        hitActor->colChkInfo.health = 0;
                    }
                }
            }
            this->collider.base.atFlags &= ~AT_HIT;
        }

        temp = (1.0f - cosf(this->unk_174 * M_PI)) * 0.5f;
    } else {
        temp = 0.0f;
    }
    this->actionFunc(this, play);

    this->actor.scale.z = 0.42f * temp * 2.0f;
    this->actor.scale.x = 0.42f * temp * 2.0f;

    this->actor.world.pos = player->actor.world.pos;
    this->actor.world.pos.y += 5.0f;

    if (this->unk_174 >= 0.0f) {
        temp = (2.0f - this->unk_174) * this->unk_174;
    }
    func_800773A8(play, temp * 0.5F, 880.0f, 0.2f, 0.9f);

    Lights_PointNoGlowSetInfo(&this->lightInfo1, (s16)this->actor.world.pos.x, (s16)this->actor.world.pos.y + 55.0f,
                              (s16)this->actor.world.pos.z, (s32)(255.0f * temp), (s32)(255.0f * temp),
                              (s32)(200.0f * temp), (s16)(100.0f * temp));

    Lights_PointNoGlowSetInfo(&this->lightInfo2,
                              (s16)this->actor.world.pos.x + Math_SinS(player->actor.shape.rot.y) * 20.0f,
                              (s16)this->actor.world.pos.y + 20.0f,
                              (s16)this->actor.world.pos.z + Math_CosS(player->actor.shape.rot.y) * 20.0f,
                              (s32)(255.0f * temp), (s32)(255.0f * temp), (s32)(200.0f * temp), (s16)(100.0f * temp));
}

// ============================================================
// Draw
// ============================================================

void MagicLight_Draw(Actor* thisx, PlayState* play) {
    MagicLight* this = THIS;
    u32 scroll = play->state.frames & 0xFFFF;

    OPEN_DISPS(play->state.gfxCtx);

    func_80093D84(play->state.gfxCtx);

    gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, "../z_magic_light.c", 469),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, sLightTextureDL);
    gSPDisplayList(POLY_XLU_DISP++, Gfx_TwoTexScroll(play->state.gfxCtx, 0, scroll * 2, scroll * (-2), 32, 32, 1, 0,
                                                     scroll * (-8), 32, 32));
    gSPDisplayList(POLY_XLU_DISP++, sCylinderDL);

    CLOSE_DISPS(play->state.gfxCtx);
}
