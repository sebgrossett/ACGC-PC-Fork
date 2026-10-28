#ifndef AC_INSECT_H
#define AC_INSECT_H

#include "types.h"
#include "m_actor.h"
#include "ac_insect_h.h"

#ifdef __cplusplus
extern "C" {
#endif

extern ACTOR_PROFILE Insect_Profile;

#ifdef TARGET_PC
extern void aINS_world_store(aINS_CTRL_ACTOR** ctrl, aINS_Clip_c* clip);
extern void aINS_world_load(aINS_CTRL_ACTOR* ctrl, const aINS_Clip_c* clip);
#endif

#ifdef __cplusplus
}
#endif

#endif
