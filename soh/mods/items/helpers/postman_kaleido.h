/**
 * postman_kaleido.h - Standalone overlay for the Postman's Hat warp map.
 */

#ifndef POSTMAN_KALEIDO_H
#define POSTMAN_KALEIDO_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

void PostmanKaleido_Update(PlayState* play);
void PostmanKaleido_Draw(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // POSTMAN_KALEIDO_H
