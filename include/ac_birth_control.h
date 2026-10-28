#ifndef AC_BIRTH_CONTROL_H
#define AC_BIRTH_CONTROL_H

#include "types.h"
#include "m_actor.h"
#include "m_field_make.h"
#include "m_personal_id.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct birth_control_s BIRTH_CONTROL_ACTOR;

struct birth_control_s {
  ACTOR actor_class;
  int setup_actor_flag;
  mFM_move_actor_c* move_actor_data[mFM_VISIBLE_BLOCK_NUM];
  u16 move_actor_bitfield;
  s16 move_actor_list_exists_flag;
  int boat_spawned;
  int last_quadrant;
#ifdef TARGET_PC
  /* Wide-window bookkeeping: a set bit means the acre's structures and props
   * were scanned and everything spawned clean, so the every-frame rescan can
   * skip it. Bits clear on every acre crossing (a fg write can land in a
   * built acre, e.g. the GBA boat) and the window re-drains under the spawn
   * budget. */
  u8 built_bits[(BLOCK_TOTAL_NUM + 7) / 8];
#endif
};

extern ACTOR_PROFILE Birth_Control_Profile;

void aBC_RequestNearbyRefresh(void);

#ifdef __cplusplus
}
#endif

#endif

