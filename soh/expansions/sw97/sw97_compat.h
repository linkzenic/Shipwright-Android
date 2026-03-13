/**
 * sw97_compat.h - Compatibility layer for SW97 code running in SOH
 *
 * Original: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 */
#ifndef SW97_COMPAT_H
#define SW97_COMPAT_H

#include "global.h"
#include "z64.h"
#include "macros.h"

// SW97 decomp uses old name "GlobalContext" — SOH uses "PlayState"
typedef PlayState GlobalContext;

// SW97 uses func_800D20CC — SOH renamed to Matrix_MtxFToYXZRotS
#define func_800D20CC Matrix_MtxFToYXZRotS

// SW97 uses Effect_GetGlobalCtx — SOH renamed to Effect_GetPlayState
#define Effect_GetGlobalCtx Effect_GetPlayState

// PLAYER macro — SW97 uses the same as SOH
#ifndef PLAYER
#define PLAYER GET_PLAYER(Effect_GetPlayState())
#endif

// Matrix_MultZ — SW97 custom function, not in SOH
static inline void Matrix_MultZ(f32 scale, Vec3f* dst) {
    MtxF* cmf = Matrix_GetCurrent();
    dst->x = cmf->wx + cmf->zx * scale;
    dst->y = cmf->wy + cmf->zy * scale;
    dst->z = cmf->wz + cmf->zz * scale;
}

// Matrix rotation helpers using s16 (binang) — SW97 defines these in z64physics.h
// but they may conflict with SOH's existing macros, so we prefix them
#ifndef SW97_Matrix_RotateY_s
#define SW97_Matrix_RotateY_s(binang, A) Matrix_RotateY(BINANG_TO_RAD(binang), A)
#define SW97_Matrix_RotateX_s(binang, A) Matrix_RotateX(BINANG_TO_RAD(binang), A)
#define SW97_Matrix_RotateZ_s(binang, A) Matrix_RotateZ(BINANG_TO_RAD(binang), A)
#define SW97_Matrix_RotateY_f(degf, A) Matrix_RotateY(DEG_TO_RAD(degf), A)
#define SW97_Matrix_RotateX_f(degf, A) Matrix_RotateX(DEG_TO_RAD(degf), A)
#define SW97_Matrix_RotateZ_f(degf, A) Matrix_RotateZ(DEG_TO_RAD(degf), A)
#endif

// RADF_TO_BINANG / BINANG_TO_RADF — may not exist in SOH
#ifndef RADF_TO_BINANG
#define RADF_TO_BINANG(rad) (s16)((rad) * (32768.0f / M_PI))
#endif
#ifndef BINANG_TO_RADF
#define BINANG_TO_RADF(binang) ((f32)(binang) * (M_PI / 32768.0f))
#endif
#ifndef DEGF_TO_BINANG
#define DEGF_TO_BINANG(degf) (s16)((degf) * (32768.0f / 180.0f))
#endif
#ifndef DEGF_TO_RADF
#define DEGF_TO_RADF(degf) ((degf) * (M_PI / 180.0f))
#endif

// Hat physics constants (from z64player.h in SW97)
#define HAT_LIMBS 4
#define HAT_SCALE_CHILD 1.2f
#define HAT_SCALE_ADULT 1.43f

// SW97 NPC compat — SW97 decomp uses older/different function names
typedef NpcInteractInfo NpcInfo;
#define Npc_TurnTowardsFocus Npc_TrackPoint
#define Actor_IsTalking Actor_ProcessTalkRequest
#define Actor_RequestToTalkInRange(actor, play, range) func_8002F2CC(actor, play, range)
#define Actor_MoveForwardXZ Actor_MoveXZGravity

// SW97 decomp uses old decompiled names for these functions
#ifndef func_80093D84
#define func_80093D84 Gfx_SetupDL_25Opa
#endif
#ifndef func_80093D18
#define func_80093D18 Gfx_SetupDL_25Opa
#endif
#ifndef func_8010BDBC
#define func_8010BDBC Message_GetState
#endif
#ifndef func_80106BC8
#define func_80106BC8 Message_ShouldAdvance
#endif
#ifndef func_8010B720
#define func_8010B720 Message_ContinueTextbox
#endif
#ifndef func_800876C8
#define func_800876C8 Magic_Reset
#endif
#ifndef func_800937C0
#define func_800937C0 Gfx_SetupDL_57
#endif
#ifndef func_800773A8
#define func_800773A8 Environment_AdjustLights
#endif
#ifndef func_8002F7DC
#define func_8002F7DC Player_PlaySfx
#endif
#ifndef func_8004356C
#define func_8004356C DynaPolyActor_IsPlayerOnTop
#endif

// SW97 uses old/different names for these functions
#define Matrix_RotateRPY Matrix_RotateZYX
#define Gfx_CallSetupDL Gfx_SetupDL
#define Audio_PlaySoundAtPosition SoundSource_PlaySfxAtFixedWorldPos

// MATRIX_TO_MTX — SW97 macro for Matrix_ToMtx
#ifndef MATRIX_TO_MTX
#define MATRIX_TO_MTX(dest, file, line) Matrix_ToMtx(dest, file, line)
#endif

// GRAPH_ALLOC — SW97 macro for Graph_Alloc
#ifndef GRAPH_ALLOC
#define GRAPH_ALLOC(gfxCtx, size) Graph_Alloc(gfxCtx, size)
#endif

// alloca — ensure it's available (SOH defines it as malloc in alloca.h)
#include "alloca.h"

#endif // SW97_COMPAT_H
