#include "ac_intro_demo.h"

#include "m_name_table.h"
#include "m_common_data.h"
#include "libultra/libultra.h"
#include "m_event.h"
#include "m_play.h"
#include "m_bgm.h"
#include "m_player_lib.h"
#include "m_house.h"

enum {
    aID_ACT_FIRST_SET,
    aID_ACT_TRAIN_BIRTH_WAIT,
    aID_ACT_RIDE_TRAIN,
    aID_ACT_RIDE_OFF_PLAYER,
    aID_ACT_WALK_ONE_UNIT,
    aID_ACT_GO_OUT_OF_STATION,
    aID_ACT_BIRTH_RCN_GUIDE,
    aID_ACT_FIRST_TALK_END_WAIT,
    aID_ACT_WALK_AFTER_RCN_GUIDE,
    aID_ACT_IN_HOUSE,
    aID_ACT_DECIDE_HOUSE,
    aID_ACT_RETIRE_RCN_GUIDE_WAIT,
    aID_ACT_FIRST_JOB,

    aID_ACT_NUM
};

static void aID_actor_ct(ACTOR* actorx, GAME* game);
static void aID_actor_dt(ACTOR* actorx, GAME* game);
static void aID_actor_move(ACTOR* actorx, GAME* game);

// clang-format off
ACTOR_PROFILE Intro_Demo_Profile = {
    mAc_PROFILE_INTRO_DEMO,
    ACTOR_PART_CONTROL,
    ACTOR_STATE_CAN_MOVE_IN_DEMO_SCENES | ACTOR_STATE_NO_MOVE_WHILE_CULLED,
    EMPTY_NO,
    ACTOR_OBJ_BANK_KEEP,
    sizeof(INTRO_DEMO_ACTOR),
    &aID_actor_ct,
    &aID_actor_dt,
    &aID_actor_move,
    mActor_NONE_PROC1,
    NULL,
};
// clang-format on

static mDemo_Clip_c aID_clip;

static void aID_setupAction(INTRO_DEMO_ACTOR* intro_demo, GAME_PLAY* play, int action);

static void aID_actor_ct(ACTOR* actorx, GAME* game) {
    INTRO_DEMO_ACTOR* intro_demo = (INTRO_DEMO_ACTOR*)actorx;
    GAME_PLAY* play = (GAME_PLAY*)game;

    Common_Get(clip).demo_clip = &aID_clip;
    bzero(&aID_clip, sizeof(aID_clip));
    Common_Get(clip).demo_clip->demo_class = actorx;
    Common_Get(clip).demo_clip->type = mDemo_CLIP_TYPE_INTRO_DEMO;

    if (mEv_CheckFirstIntro() == TRUE) {
        if (Common_Get(field_type) == mFI_FIELDTYPE_FG) {
            switch (mFI_GET_TYPE(Common_Get(last_field_id))) {
                case mFI_FIELD_PLAYER0_ROOM:
                    intro_demo->_1A4 = TRUE;
                    intro_demo->next_action = aID_ACT_DECIDE_HOUSE;
                    aID_setupAction(intro_demo, play, aID_ACT_BIRTH_RCN_GUIDE);
                    mBGMPsComp_make_ps_demo(BGM_INTRO_RCN_GUIDE, 0x168);
                    break;
                default:
                    aID_setupAction(intro_demo, play, 0);
                    break;
            }
        } else {
            aID_setupAction(intro_demo, play, aID_ACT_IN_HOUSE);
        }
    } else {
        aID_setupAction(intro_demo, play, aID_ACT_FIRST_JOB);
    }
}

#ifdef TARGET_PC
void aID_world_store(mDemo_Clip_c* dst) {
    if (dst != NULL) {
        *dst = aID_clip;
    }
}

void aID_world_load(const mDemo_Clip_c* src) {
    if (src != NULL) {
        aID_clip = *src;
    }
}

/* The house scene spawns its own intro demo into this same static. After that
 * actor is freed, demo_class still holds the old address. Only a class that is
 * still on this play may be written. */
int aID_class_is_live(struct game_play_s* play, void* demo_class) {
    ACTOR* cursor;

    if ((play == NULL) || (demo_class == NULL) || (aID_clip.demo_class != demo_class)) {
        return FALSE;
    }

    cursor = play->actor_info.list[ACTOR_PART_CONTROL].actor;

    while (cursor != NULL) {
        if (cursor == (ACTOR*)demo_class) {
            return TRUE;
        }

        cursor = cursor->next_actor;
    }

    return FALSE;
}
#endif

static void aID_actor_dt(ACTOR* actorx, GAME* game) {
    INTRO_DEMO_ACTOR* intro_demo = (INTRO_DEMO_ACTOR*)actorx;

    if (aID_clip.demo_class == actorx) {
        aID_clip.demo_class = NULL;
        aID_clip.type = mDemo_CLIP_TYPE_NONE;
    }

    if (Common_Get(clip).demo_clip != NULL) {
        Common_Get(clip).demo_clip = NULL;
    }

    if (intro_demo->first_field_bgm_state == 2) {
        mBGMPsComp_delete_ps_fieldSuddenEv(BGM_INTRO_FIND_SHOP, 0x168);
    }
}

#include "../src/actor/ac_intro_demo_move.c_inc"
