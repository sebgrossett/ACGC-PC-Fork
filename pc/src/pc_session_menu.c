/* pc_session_menu.c - per-player co-op session menu (guest leave) */
#include "pc_session_menu.h"
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_pause_menu.h"
#include "pc_menu_util.h"
#include "pc_text_draw.h"

#include "game.h"
#include "graph.h"
#include "main.h"       /* SCREEN_WIDTH_F */
#include "m_font.h"
#include "m_common_data.h"
#include "m_demo.h"
#include "m_msg.h"
#include "m_player_lib.h"
#include "m_play.h"
#include "m_rcp.h"
#include "pc_world.h"

#include <stdio.h>

typedef struct {
    int open;
    int sel;
    int stick_latch;
} PCSessionMenu;

static PCSessionMenu s_menu[PC_PLAYER_COUNT];
static int s_leave_request[PC_PLAYER_COUNT];

/* The play world this Controls row's guest is in (their interior included).
 * NULL when the row has no live resident. */
static GAME_PLAY* session_guest_world(int pad) {
    int slot;
    GAME_PLAY* world;

    if ((pad <= 0) || (pad >= PC_PLAYER_COUNT)) {
        return NULL;
    }

    slot = g_pc_coop_guest_slot[pad];
    if ((slot < 0) || (slot >= PLAYER_NUM)) {
        return NULL;
    }

    world = pc_world_at(pc_world_slot_world(slot));
    if ((world == NULL) || (world->game.exec != play_main)) {
        return NULL;
    }

    return world;
}

static int guest_row_active(int pad) {
    if (g_pc_coop_session == 0 || pad <= 0 || pad >= PC_PLAYER_COUNT) return 0;
    if (Save_Get(scene_no) != SCENE_FG && session_guest_world(pad) == NULL) return 0;
    return mPlib_pad_in_use(pad);
}

static int message_window_active(mMsg_Window_c* msg) {
    return (msg != NULL) && (msg->draw_flag != FALSE);
}

/* The player actor that owns this Controls row (NULL when none). */
static PLAYER_ACTOR* session_menu_pad_player(int pad) {
    int count = 1 + mPlib_extra_player_count();
    int i;

    for (i = 1; i < count; i++) {
        PLAYER_ACTOR* guest = mPlib_extra_player_at(i - 1);

        if (guest != NULL && mPlib_pad_for_player(guest) == pad) {
            return guest;
        }
    }

    /* This world's table drops a guest who went indoors. The session still
     * knows their slot, and their own world still holds the actor. */
    if ((pad > 0) && (pad < PC_PLAYER_COUNT)) {
        return (PLAYER_ACTOR*)pc_world_slot_player(g_pc_coop_guest_slot[pad]);
    }

    return NULL;
}

static int session_menu_can_open(int pad) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    PLAYER_ACTOR* me;
    PLAYER_ACTOR* talker;
    ACTOR* side_talker;
    Submenu* side;
    int slot;

    if (g_pc_paused || gamePT == NULL || gamePT->exec != play_main) return 0;

    me = session_menu_pad_player(pad);
    if (me == NULL) return 0;

    /* The slot comes from the session, not from this world's table: an indoor
     * guest is off it. */
    slot = ((pad > 0) && (pad < PC_PLAYER_COUNT)) ? g_pc_coop_guest_slot[pad] : -1;

    /* Only this player's own game menu blocks their session menu: their
     * primary-menu open or their own side-slot inventory (the shared helper),
     * plus the mode tails retail keeps after a close. The primary can be busy
     * because another player opened something; that does not hold this session
     * menu hostage. */
    if (mSM_player_menu_open_by_slot(play, slot) ||
        (mPlib_menu_opener_for_slot(0) == me && play->submenu.mode != mSM_MODE_IDLE) ||
        ((side = mSM_side_slot_p(pad)) != NULL && side->mode != mSM_MODE_IDLE)) {
        return 0;
    }

    if (play->fb_wipe_mode != WIPE_MODE_NONE || play->fb_fade_type != FADE_TYPE_NONE) return 0;
    if (play->fb_mode != FBDEMO_MODE_NONE) return 0;
    if (mEv_CheckFirstIntro() != FALSE) return 0;

    /* Only this player's own conversation blocks it. An active demo or window
     * with no attributable owner blocks everyone (conservative). */
    talker = mPlib_demo_talker();
    side_talker = mDemo_side_player();
    if (mDemo_CheckDemo() != FALSE && (talker == me || talker == NULL)) return 0;
    if (side_talker != NULL && (PLAYER_ACTOR*)side_talker == me) return 0;
    if (message_window_active(mMsg_Get_base_window_p()) && (talker == me || talker == NULL)) return 0;
    if (message_window_active(mMsg_side_window_p()) &&
        (side_talker == NULL || (PLAYER_ACTOR*)side_talker == me)) {
        return 0;
    }
    return 1;
}

static void session_menu_open(int pad) {
    if (!guest_row_active(pad) || !session_menu_can_open(pad)) return;
    s_menu[pad].open = 1;
    s_menu[pad].sel = 0;
    s_menu[pad].stick_latch = 0;
}

void pc_session_menu_close(int pad) {
    if (pad < 0 || pad >= PC_PLAYER_COUNT) return;
    s_menu[pad].open = 0;
    s_menu[pad].sel = 0;
    s_menu[pad].stick_latch = 0;
}

void pc_session_menu_close_all(void) {
    for (int i = 0; i < PC_PLAYER_COUNT; i++) pc_session_menu_close(i);
}

int pc_session_menu_is_open(int pad) {
    if (pad < 0 || pad >= PC_PLAYER_COUNT) return 0;
    return s_menu[pad].open;
}

int pc_session_menu_take_leave_request(int pad) {
    if (pad < 0 || pad >= PC_PLAYER_COUNT) return 0;

    if (s_leave_request[pad]) {
        s_leave_request[pad] = 0;
        return 1;
    }

    return 0;
}

static void session_menu_move(int pad, int dir) {
    s_menu[pad].sel += dir;
    if (s_menu[pad].sel < 0) s_menu[pad].sel = 1;
    if (s_menu[pad].sel > 1) s_menu[pad].sel = 0;
}

static void session_menu_confirm(int pad) {
    if (s_menu[pad].sel == 1) {
        s_leave_request[pad] = 1;
    }

    pc_session_menu_close(pad);
}

/* Handle navigation while this row's menu is open. Returns 1 when consumed. */
static int session_menu_handle_open(int pad, const SDL_Event* e) {
    if (!s_menu[pad].open) return 0;

    switch (e->type) {
        case SDL_KEYDOWN:
            if (e->key.repeat) return 1;
            switch (e->key.keysym.sym) {
                case SDLK_UP:
                case SDLK_w:
                    session_menu_move(pad, -1);
                    return 1;
                case SDLK_DOWN:
                case SDLK_s:
                    session_menu_move(pad, 1);
                    return 1;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    session_menu_confirm(pad);
                    return 1;
                case SDLK_ESCAPE:
                case SDLK_BACKSPACE:
                    pc_session_menu_close(pad);
                    return 1;
            }
            return 0;

        case SDL_CONTROLLERBUTTONDOWN:
            switch (e->cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    session_menu_move(pad, -1);
                    return 1;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    session_menu_move(pad, 1);
                    return 1;
                case SDL_CONTROLLER_BUTTON_A:
                    session_menu_confirm(pad);
                    return 1;
                case SDL_CONTROLLER_BUTTON_B:
                case SDL_CONTROLLER_BUTTON_BACK:
                    pc_session_menu_close(pad);
                    return 1;
            }
            return 1; /* swallow the opener's other buttons while it is open */

        case SDL_CONTROLLERAXISMOTION:
            if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                if (e->caxis.value <= -18000) {
                    if (!s_menu[pad].stick_latch) {
                        s_menu[pad].stick_latch = 1;
                        session_menu_move(pad, -1);
                    }
                } else if (e->caxis.value >= 18000) {
                    if (!s_menu[pad].stick_latch) {
                        s_menu[pad].stick_latch = 1;
                        session_menu_move(pad, 1);
                    }
                } else if (e->caxis.value > -12000 && e->caxis.value < 12000) {
                    s_menu[pad].stick_latch = 0;
                }
                return 1;
            }
            return 0;
    }

    return 0;
}

int pc_session_menu_handle_event(const SDL_Event* e) {
    int pad = -1;

    if (e == NULL || g_pc_paused) return 0;

    switch (e->type) {
        case SDL_KEYDOWN:
            pad = pc_pad_keyboard_row();
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            pad = pc_pad_row_for_instance((SDL_JoystickID)e->cbutton.which);
            break;
        case SDL_CONTROLLERAXISMOTION:
            pad = pc_pad_row_for_instance((SDL_JoystickID)e->caxis.which);
            break;
        default:
            return 0;
    }

    if (pad < 0) return 0;

    /* A menu that is already open consumes its owner's input first. */
    if (session_menu_handle_open(pad, e)) return 1;

    if (!guest_row_active(pad) || !session_menu_can_open(pad)) return 0;

    if ((e->type == SDL_CONTROLLERBUTTONDOWN && e->cbutton.button == SDL_CONTROLLER_BUTTON_BACK) ||
        (e->type == SDL_KEYDOWN && !e->key.repeat && e->key.keysym.sym == SDLK_ESCAPE)) {
        session_menu_open(pad);
        return 1;
    }

    return 0;
}

static void draw_centered_in(struct game_s* game, const char* s, f32 y, int selected) {
    int r, g, b, a;
    f32 scale = selected ? PC_MENU_SCALE_SELECTED : 1.0f;
    f32 x = (SCREEN_WIDTH_F - (f32)pc_text_width(s) * scale) * 0.5f;

    pc_menu_row_colors(selected, &r, &g, &b, &a);
    pc_text_draw(game, s, x, y, r, g, b, a, scale);
}

void pc_session_menu_draw(struct game_s* game, int pad) {
    static const char* const items[2] = { "Resume", "Leave Session" };
    char title[16];

    if (!game || !game->graph || pad < 0 || pad >= PC_PLAYER_COUNT || !s_menu[pad].open) return;

    mFont_SetMatrix(game->graph, mFont_MODE_FONT);
    pc_menu_dim_rect(game->graph, 150);

    snprintf(title, sizeof(title), "Player %d", pad + 1);
    draw_centered_in(game, title, 82.0f, 0);
    draw_centered_in(game, items[0], 116.0f, s_menu[pad].sel == 0);
    draw_centered_in(game, items[1], 138.0f, s_menu[pad].sel == 1);

    mFont_UnSetMatrix(game->graph, mFont_MODE_FONT);
}
