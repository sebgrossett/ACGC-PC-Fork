#include "m_player_call.h"

#include "m_player.h"
#include "m_player_lib.h"
#include "m_name_table.h"
#include "m_play.h"
#include "m_common_data.h"
#include "m_controller.h"
#include "m_lib.h"

static mActor_proc Player_actor_ct_func;
static mActor_proc Player_actor_dt_func;
static mActor_proc Player_actor_move_func;
static mActor_proc Player_actor_draw_func;

#ifdef TARGET_PC
static void Player_actor_ct_call(ACTOR* actor, GAME* game);
static void Player_actor_dt_call(ACTOR* actor, GAME* game);
static void Player_actor_move_call(ACTOR* actor, GAME* game);
static void Player_actor_draw_call(ACTOR* actor, GAME* game);
#else
void Player_actor_ct_call(ACTOR* actor, GAME* game);
void Player_actor_dt_call(ACTOR* actor, GAME* game);
void Player_actor_move_call(ACTOR* actor, GAME* game);
void Player_actor_draw_call(ACTOR* actor, GAME* game);
#endif

ACTOR_PROFILE Player_Profile = {
    mAc_PROFILE_PLAYER,
    ACTOR_PART_PLAYER,
    ACTOR_STATE_CAN_MOVE_IN_DEMO_SCENES | ACTOR_STATE_26 | ACTOR_STATE_25 | ACTOR_STATE_NO_MOVE_WHILE_CULLED |
        ACTOR_STATE_NO_DRAW_WHILE_CULLED | ACTOR_STATE_2 | ACTOR_STATE_0,
    EMPTY_NO,
    ACTOR_OBJ_BANK_KEEP,
    sizeof(PLAYER_ACTOR),
    Player_actor_ct_call,
    Player_actor_dt_call,
    Player_actor_move_call,
    Player_actor_draw_call,
    NULL,
};

static void initfunc(GAME_PLAY* play) {
    Submenu* submenu = &play->submenu;

    Player_actor_ct_func = mSM_ovlptr_dllcnv(&Player_actor_ct, submenu, 1);
    Player_actor_dt_func = mSM_ovlptr_dllcnv(&Player_actor_dt, submenu, 1);
    Player_actor_move_func = mSM_ovlptr_dllcnv(&Player_actor_move, submenu, 1);
    Player_actor_draw_func = mSM_ovlptr_dllcnv(&Player_actor_draw, submenu, 1);
}

#ifdef TARGET_PC
static MCON s_guest_mcon[MAXCONTROLLERS];
static int s_guest_mcon_ready[MAXCONTROLLERS];
static int s_guest_active_pad = -1;
static MCON s_saved_mcon;
static pad_t s_saved_pad;
static Private_c* s_saved_priv;

static void guest_load_look(GAME* game) {
    if (Now_Private == NULL) {
        return;
    }

    mPlib_change_player_face(game);
    mPlib_change_player_cloth(game, Now_Private->cloth.idx);
}

static void guest_begin(GAME* game, PLAYER_ACTOR* player, int slot) {
    Private_c* priv = mPlib_private_for_slot(slot);
    int pad = mPlib_pad_for_player_slot(slot);
    pad_t* src;

    if ((pad < 0) || (pad >= MAXCONTROLLERS)) {
        pad = 1;
    }

    s_guest_active_pad = pad;
    src = &game->pads[pad];

    s_saved_priv = Now_Private;
    s_saved_mcon = game->mcon;
    s_saved_pad = game->pads[0];
    if (priv != NULL) {
        Common_Set(now_private, priv);
    }

    mPlib_bind_player_actor(player);
    game->pads[0] = *src;
    if (s_guest_mcon_ready[pad] == FALSE) {
        mem_clear((u8*)&s_guest_mcon[pad], sizeof(s_guest_mcon[pad]), 0);
        s_guest_mcon_ready[pad] = TRUE;
    }

    mCon_calc(&s_guest_mcon[pad], (f32)src->now.stick_x, (f32)src->now.stick_y);
    game->mcon = s_guest_mcon[pad];
}

static void guest_end(GAME* game, int restore_look) {
    int pad = s_guest_active_pad;

    if ((pad >= 0) && (pad < MAXCONTROLLERS)) {
        s_guest_mcon[pad] = game->mcon;
    }

    s_guest_active_pad = -1;
    game->mcon = s_saved_mcon;
    game->pads[0] = s_saved_pad;
    Common_Set(now_private, s_saved_priv);
    mPlib_unbind_player_actor();
    if ((restore_look != FALSE) && (Now_Private != NULL)) {
        guest_load_look(game);
    }
}

/* Player context: the submenu update/draw and the talk demo read pads[0],
 * mcon, player_no and the player's private data. While a guest owns one of
 * those, run it with the guest's pad and private data, then restore the
 * selected resident's. */
static int s_player_ctx_active = FALSE;
static pad_t s_player_ctx_saved_pad;
static MCON s_player_ctx_saved_mcon;
static Private_c* s_player_ctx_saved_priv;
static u32 s_player_ctx_saved_player_no;

static void player_context_begin(GAME_PLAY* play, PLAYER_ACTOR* player, int pad, int slot) {
    if (s_player_ctx_active || player == NULL || pad <= 0 || pad >= MAXCONTROLLERS || slot < 0) {
        return;
    }

    s_player_ctx_saved_pad = play->game.pads[0];
    s_player_ctx_saved_mcon = play->game.mcon;
    s_player_ctx_saved_priv = Common_Get(now_private);
    s_player_ctx_saved_player_no = Common_Get(player_no);

    Common_Set(now_private, mPlib_private_for_slot(slot));
    Common_Set(player_no, slot);
    play->game.pads[0] = play->game.pads[pad];
    mCon_calc(&s_guest_mcon[pad], (f32)play->game.pads[pad].now.stick_x, (f32)play->game.pads[pad].now.stick_y);
    play->game.mcon = s_guest_mcon[pad];
    s_player_ctx_active = TRUE;
}

static void player_context_end(GAME_PLAY* play) {
    if (s_player_ctx_active == FALSE) {
        return;
    }

    play->game.pads[0] = s_player_ctx_saved_pad;
    play->game.mcon = s_player_ctx_saved_mcon;
    Common_Set(now_private, s_player_ctx_saved_priv);
    Common_Set(player_no, s_player_ctx_saved_player_no);
    s_player_ctx_active = FALSE;
}

extern void mPlib_menu_context_begin(GAME_PLAY* play) {
    player_context_begin(play, mPlib_menu_opener(), mPlib_menu_opener_pad(), mPlib_menu_opener_slot());
}

extern void mPlib_menu_context_end(GAME_PLAY* play) {
    player_context_end(play);
}

extern void mPlib_demo_context_begin(GAME_PLAY* play) {
    player_context_begin(play, mPlib_demo_talker(), mPlib_demo_talker_pad(), mPlib_demo_talker_slot());
}

extern void mPlib_demo_context_end(GAME_PLAY* play) {
    player_context_end(play);
}

extern void mPlib_slot_context_begin(GAME_PLAY* play, int slot, int pad) {
    player_context_begin(play, get_player_actor_by_index(play, slot), pad, slot);
}

extern void mPlib_slot_context_end(GAME_PLAY* play) {
    player_context_end(play);
}

static int guest_slot_for_actor(ACTOR* actor) {
    int slot = mPlib_player_slot_of((PLAYER_ACTOR*)actor);

    if ((slot < 0) || (slot == (int)Common_Get(player_no))) {
        return -1;
    }

    return slot;
}
#endif

static void Player_actor_ct_call(ACTOR* actor, GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;

    load_player(&play->submenu);
    initfunc(play);
#ifdef TARGET_PC
    {
        int guest_slot = mPlib_guest_slot_ready();

        if (guest_slot >= 0) {
            guest_begin(game, (PLAYER_ACTOR*)actor, guest_slot);
            guest_load_look(game);
            Player_actor_ct_func(actor, game);
            guest_end(game, TRUE);
            return;
        }
    }
#endif
    Player_actor_ct_func(actor, game);
}

static void Player_actor_dt_call(ACTOR* actor, GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;
#ifdef TARGET_PC
    int guest_slot = guest_slot_for_actor(actor);
#endif

    load_player(&play->submenu);
#ifdef TARGET_PC
    if (guest_slot >= 0) {
        guest_begin(game, (PLAYER_ACTOR*)actor, guest_slot);
        Player_actor_dt_func(actor, game);
        guest_end(game, TRUE);
        return;
    }
#endif
    Player_actor_dt_func(actor, game);
}

static void Player_actor_move_call(ACTOR* actor, GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;
#ifdef TARGET_PC
    int guest_slot = guest_slot_for_actor(actor);
#endif

    load_player(&play->submenu);
#ifdef TARGET_PC
    if (guest_slot >= 0) {
        guest_begin(game, (PLAYER_ACTOR*)actor, guest_slot);
        Player_actor_move_func(actor, game);
        guest_end(game, FALSE);
        return;
    }
#endif
    Player_actor_move_func(actor, game);
}

static void Player_actor_draw_call(ACTOR* actor, GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;
#ifdef TARGET_PC
    int guest_slot = guest_slot_for_actor(actor);
#endif

    load_player(&play->submenu);
#ifdef TARGET_PC
    if (guest_slot >= 0) {
        guest_begin(game, (PLAYER_ACTOR*)actor, guest_slot);
        guest_load_look(game);
        Player_actor_draw_func(actor, game);
        guest_end(game, TRUE);
        return;
    }
#endif
    Player_actor_draw_func(actor, game);
}
