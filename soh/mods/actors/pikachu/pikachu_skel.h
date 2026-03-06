#ifndef PIKACHU_SKEL_H
#define PIKACHU_SKEL_H
extern u64 Armature_mouth_rgba32[];
extern u64 Armature_mouth_ci8[];
extern u64 Armature_mouth_pal_rgba16[];
extern Vtx Armature_hand_left_mesh_layer_Opaque_vtx_0[45];
extern Gfx Armature_hand_left_mesh_layer_Opaque_tri_0[];
extern Vtx Armature_hand_right_mesh_layer_Opaque_vtx_0[45];
extern Gfx Armature_hand_right_mesh_layer_Opaque_tri_0[];
extern Vtx Armature_head_mesh_layer_Opaque_vtx_0[36];
extern Gfx Armature_head_mesh_layer_Opaque_tri_0[];
extern Vtx Armature_head_mesh_layer_Opaque_vtx_1[181];
extern Gfx Armature_head_mesh_layer_Opaque_tri_1[];
extern Vtx Armature_head_mesh_layer_Opaque_vtx_2[10];
extern Gfx Armature_head_mesh_layer_Opaque_tri_2[];
extern Vtx Armature_head_mesh_layer_Opaque_vtx_3[12];
extern Gfx Armature_head_mesh_layer_Opaque_tri_3[];
extern Gfx mat_Armature_body_layerOpaque[];
extern Gfx mat_Armature_hears_tip_layerOpaque[];
extern Gfx mat_Armature_mouth_layerOpaque[];
extern Gfx mat_Armature_eyes_layerOpaque[];
extern Gfx Armature_hand_left_mesh_layer_Opaque[];
extern Gfx Armature_hand_right_mesh_layer_Opaque[];
extern Gfx Armature_head_mesh_layer_Opaque[];
extern FlexSkeletonHeader Armature;
#define ARMATURE_ROOT_POS_LIMB 0
#define ARMATURE_ROOT_ROT_LIMB 1
#define ARMATURE_HAND_LEFT_LIMB 2
#define ARMATURE_HAND_RIGHT_LIMB 3
#define ARMATURE_HEAD_LIMB 4
#define ARMATURE_NUM_LIMBS 5

#endif
