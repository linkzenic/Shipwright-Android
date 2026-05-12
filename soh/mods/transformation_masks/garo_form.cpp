/**
 * garo_form.cpp - Garo Transformation Form (custom .o2r mod)
 *
 * Loads the Garo skeleton from `nei/garo.o2r` (produced by tools/glb_to_o2r.py).
 * Garo uses Link's vanilla idle/walk/run animations, so the only custom piece
 * is the skeleton + per-limb DLs + texture inside garo.o2r.
 *
 * The MM_PLAYER_FORM_GARO entry in sFormProps (mm_player_form.cpp) has skelPath=NULL
 * and animation paths pointing to vanilla link_normal_* anims. MmForm_LoadFormSkeleton
 * dispatches to GaroForm_LoadSkeleton when form == MM_PLAYER_FORM_GARO, then continues
 * the standard anim-load + SkelAnime_InitLink flow.
 */

#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "mods/transformation_masks/transformation_masks.h"
#include "soh/ResourceManagerHelpers.h"

#define GARO_SKEL_PATH "__OTR__objects/garo/gGaroSkel"

extern "C" FlexSkeletonHeader* GaroForm_LoadSkeleton(PlayState* play) {
    SkeletonHeader* hdr = ResourceMgr_LoadSkeletonByName(GARO_SKEL_PATH, NULL);
    if (hdr == NULL) {
        return NULL;
    }
    // The skeleton was emitted as Flex by glb_to_o2r.py (Type="Flex" in the XML),
    // so SkeletonFactory populated FlexSkeletonHeader via skeleton->skeletonData.flexSkeletonHeader.
    // Caller (MmForm_LoadFormSkeleton) logs the failure via MMFORM_LOG.
    return (FlexSkeletonHeader*)hdr;
}
