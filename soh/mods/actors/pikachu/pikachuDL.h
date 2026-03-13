#ifndef PIKACHUDL_H
#define PIKACHUDL_H

#include "z64.h"

extern u64 pika_001_yellow_ci4[];
extern u64 pika_001_yellow_pal_rgba16[];
extern u64 pika_001_eyes_happy_ci4[];
extern u64 pika_001_eyes_happy_pal_rgba16[];
extern u64 pika_001_mouth_happy_ci4[];
extern u64 pika_001_mouth_happy_pal_rgba16[];
extern u64 pika_001_back_ci4[];
extern u64 pika_001_back_pal_rgba16[];
extern u64 pika_001_tail_ci4[];
extern u64 pika_001_tail_pal_rgba16[];
extern Vtx pika_001_pika_001_mesh_layer_Opaque_vtx_cull[8];
extern Vtx pika_001_pika_001_mesh_layer_Opaque_vtx_0[170];
extern Gfx pika_001_pika_001_mesh_layer_Opaque_tri_0[];
extern Vtx pika_001_pika_001_mesh_layer_Opaque_vtx_1[11];
extern Gfx pika_001_pika_001_mesh_layer_Opaque_tri_1[];
extern Vtx pika_001_pika_001_mesh_layer_Opaque_vtx_2[22];
extern Gfx pika_001_pika_001_mesh_layer_Opaque_tri_2[];
extern Vtx pika_001_pika_001_mesh_layer_Opaque_vtx_3[40];
extern Gfx pika_001_pika_001_mesh_layer_Opaque_tri_3[];
extern Vtx pika_001_pika_001_mesh_layer_Opaque_vtx_4[20];
extern Gfx pika_001_pika_001_mesh_layer_Opaque_tri_4[];
extern Vtx pika_001_pika_001_mesh_layer_Opaque_vtx_5[18];
extern Gfx pika_001_pika_001_mesh_layer_Opaque_tri_5[];
extern Gfx mat_pika_001_f3dlite_material_004_layerOpaque[];
extern Gfx mat_pika_001_black_layerOpaque[];
extern Gfx pika_mat_back[];
extern Gfx pika_mat_tail[];
extern Gfx pika_mat_tail_iron[];
extern Gfx pika_mat_eyes_happy[];
extern Gfx pika_mat_eyes_angry[];
extern Gfx pika_mat_eyes_closed[];
extern Gfx pika_mat_eyes_neutral[];
extern Gfx pika_mat_mouth_happy[];
extern Gfx pika_mat_mouth_smile[];
extern Gfx pika_mat_mouth_attack[];
extern Gfx pika_mat_mouth_attack_charge[];
extern Gfx pika_mat_mouth_attack_discharge[];
extern Gfx pika_mat_mouth_surprised[];
extern Gfx pika_001_opaque_dl[];

// ── face / tail variant tables (added by convert_textures.py) ─────────────────
extern Gfx* pika_eyes_mats[];
extern Gfx* pika_mouth_mats[];
extern Gfx* pika_tail_mats[];

#define PIKA_EYES_HAPPY 0
#define PIKA_EYES_ANGRY 1
#define PIKA_EYES_CLOSED 2
#define PIKA_EYES_NEUTRAL 3
#define PIKA_EYES_MAX 4

#define PIKA_MOUTH_HAPPY 0
#define PIKA_MOUTH_SMILE 1
#define PIKA_MOUTH_ATTACK 2
#define PIKA_MOUTH_CHARGE 3
#define PIKA_MOUTH_DISCHARGE 4
#define PIKA_MOUTH_SURPRISED 5
#define PIKA_MOUTH_MAX 6

#define PIKA_TAIL_NORMAL 0
#define PIKA_TAIL_IRON 1
#define PIKA_TAIL_MAX 2

#endif
