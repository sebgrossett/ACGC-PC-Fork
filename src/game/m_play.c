#include "m_play.h"

#include "evw_anime.h"
#include "m_common_data.h"
#include "libultra/libultra.h"
#include "m_fbdemo_wipe1.h"
#include "m_fbdemo_triforce.h"
#include "m_fbdemo_fade.h"
#include "m_debug.h"
#include "boot.h"
#include "version.h"
#include "m_bgm.h"
#include "m_rcp.h"
#include "m_msg.h"
#include "m_banti.h"
#include "m_event.h"
#include "m_npc.h"
#include "m_huusui_room.h"
#include "m_field_make.h"
#include "m_card.h"
#include "m_eappli.h"
#include "famicom_emu.h"
#include "Famicom/famicomPriv.h"
#include "m_malloc.h"
#include "m_field_info.h"
#include "m_time.h"
#include "m_titledemo.h"
#include "sys_matrix.h"
#include "m_fuusen.h"
#include "m_watch_my_step.h"
#include "m_vibctl.h"
#include "m_train_control.h"
#include "gfxalloc.h"
#include "m_debug_display.h"
#include "jsyswrap.h"
#include "m_trademark.h"
#include "zurumode.h"
#include "m_scene_table.h"
#include "m_submenu_ovl.h"
#include "m_player_lib.h"
#include "m_skin_matrix.h"
#include "padmgr.h"
#ifdef TARGET_PC
#include "pc_diag.h"
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_session_menu.h"
#include "m_controller.h"

/* Split-view helpers defined further down; the per-view clock-slide driver in
 * Game_play_move uses them before their definitions. */
#include "pc_pause_menu.h"
#include "pc_menu_util.h"
#include "pc_world.h"
#include "libforest/emu64/emu64_wrapper.h"

/* Split-view helpers defined further down; the per-view clock-slide driver in
 * Game_play_move uses them before their definitions. */
static void play_view_rect(int index, int count, rect* out);
static int play_player_count(void);
static PLAYER_ACTOR* play_view_player(GAME_PLAY* play, int index);
static void play_view_camera(GAME_PLAY* play, int index, xyz_t* eye, xyz_t* center);
#endif

#define Game_play_HYRAL_SIZE 0x3E800 // 256,000 bytes
#define Game_play_IS_PAUSED(play) (ZURUMODE2_ENABLED() ? (!Pause_proc(&(play)->pause, &(play)->game.pads[PAD1])) : FALSE)

static fbdemo_c fbdemo;
static f32 S_back_title_timer = 0.0f;
static f32 S_se_endcheck_timeout = 0.0f;
static u8 gxbuf[0x140] ATTRIBUTE_ALIGN(32);
static u8 prbuf[(2*SCREEN_WIDTH) * (2*SCREEN_HEIGHT) * sizeof(u32)] ATTRIBUTE_ALIGN(32); // 0x12C000

static void Game_play_fbdemo_wipe_init(GAME_PLAY* play);
static void Gameplay_Scene_Read(GAME_PLAY* play, s16 scene_no);
#ifdef TARGET_PC
static void play_guest_camera_reset(int pad);
#endif

static void Game_play_dec_dt_timer(f32* timer, GAME_PLAY* play) {
    if (*timer > 0.0f) {
        *timer -= (f32)play->game.graph->dt_num_60fps_frames;
        if (*timer < 0.0f) {
            *timer = 0.0f;
        }
    }
}

static void Game_play_Reset_destiny() {
    lbRTC_time_c* rtc_time = Common_GetPointer(time.rtc_time);
    mPr_destiny_c* destiny = &Common_Get(now_private)->destiny;

    if (destiny->type == mPr_DESTINY_NORMAL) {
        return;
    }

    if ((rtc_time->year != destiny->received_time.year) || (rtc_time->month != destiny->received_time.month) ||
        (rtc_time->day != destiny->received_time.day)) {
        Common_Set(now_private->destiny.type, mPr_DESTINY_NORMAL);
    }
}

static void event_title_flag_on() {
    if (Common_Get(event_title_flags) != 0) {
        Common_Set(event_notification_active, 1);
    }
}

static void event_title_flag_off() {
    if (Common_Get(event_title_flags) == 2) {
        Common_Set(event_title_flags, 0);
        Common_Set(event_notification_active, 0);
    }
}

static void Game_play_camera_proc(GAME_PLAY* play) {
    Camera2_ClearActorTalking_Cull(play);
    Camera2_process(play);
}

static void Game_play_fbdemo_wipe_destroy(GAME_PLAY* play) {
    play->fbdemo_wipe.wipe_procs.cleanup_proc(&play->fbdemo_wipe.wipe_data);
    play->fb_wipe_mode = WIPE_MODE_NONE;
    play->fbdemo_wipe.wipe_type = -1;
}

static void Game_play_fbdemo_wipe_create_sub(GAME_PLAY* play) {
    static void* wipe_normal_proc[] = {
        fbdemo_wipe1_init,
        none_proc1,
        fbdemo_wipe1_move,
        fbdemo_wipe1_draw,
        fbdemo_wipe1_startup,
        fbdemo_wipe1_settype,
        fbdemo_wipe1_setcolor_rgba8888,
        NULL,
        fbdemo_wipe1_is_finish,
    };
    static void* wipe_triforce_proc[] = {
        fbdemo_triforce_init,
        none_proc1,
        fbdemo_triforce_move,
        fbdemo_triforce_draw,
        fbdemo_triforce_startup,
        fbdemo_triforce_settype,
        fbdemo_triforce_setcolor_rgba8888,
        NULL,
        fbdemo_triforce_is_finish,
    };
    static void* wipe_fade_proc[] = {
        fbdemo_fade_init,
        none_proc1,
        fbdemo_fade_move,
        fbdemo_fade_draw,
        fbdemo_fade_startup,
        fbdemo_fade_settype,
        fbdemo_fade_setcolor_rgba8888,
        NULL,
        fbdemo_fade_is_finish,
    };

    static void* wipe_proc[] = {
        wipe_normal_proc,   wipe_triforce_proc, wipe_fade_proc, wipe_fade_proc,
        wipe_triforce_proc, wipe_triforce_proc, wipe_fade_proc,
    };

    int type = play->fb_wipe_type;
    fbdemo_wipe_proc_c* cur;

    bzero(&play->fbdemo_wipe, sizeof(fbdemo_wipe));

    if (type >= WIPE_TYPE_NUM) {
        type = WIPE_TYPE_TRIFORCE;
    }

    play->fbdemo_wipe.wipe_type = type;
    cur = wipe_proc[type];
    play->fbdemo_wipe.wipe_procs.init_proc = cur->init_proc;
    play->fbdemo_wipe.wipe_procs.cleanup_proc = cur->cleanup_proc;
    play->fbdemo_wipe.wipe_procs.move_proc = cur->move_proc;
    play->fbdemo_wipe.wipe_procs.draw_proc = cur->draw_proc;
    play->fbdemo_wipe.wipe_procs.startup_proc = cur->startup_proc;
    play->fbdemo_wipe.wipe_procs.settype_proc = cur->settype_proc;
    play->fbdemo_wipe.wipe_procs.setcolor_proc = cur->setcolor_proc;
    play->fbdemo_wipe.wipe_procs.isfinished_proc = cur->isfinished_proc;
}

static void Game_play_fbdemo_wipe_create(GAME_PLAY* play) {
    Game_play_fbdemo_wipe_create_sub(play);
    Game_play_fbdemo_wipe_init(play);
}

static void Game_play_fbdemo_wipe_init(GAME_PLAY* play) {
    fbdemo_wipe* wipe = &play->fbdemo_wipe;
    u8 type;

    play->fbdemo_wipe.wipe_procs.init_proc(&wipe->wipe_data);

    Common_Set(transition.wipe_rate, 28);
    Common_Set(transition.fade_rate, 30);

    wipe->wipe_procs.setcolor_proc(&wipe->wipe_data, 0);

    type = play->fb_fade_type;

    if (wipe->wipe_type == WIPE_TYPE_CIRCLE_LEFT) {
        type |= 0x80;
    } else if (wipe->wipe_type == WIPE_TYPE_CIRCLE_RIGHT) {
        type |= 0x40;
    }
    wipe->wipe_procs.settype_proc(&wipe->wipe_data, type);
    wipe->wipe_procs.startup_proc(&wipe->wipe_data);

    play->fb_wipe_mode = WIPE_MODE_MOVE;

    S_back_title_timer = 120.0f;
    S_se_endcheck_timeout = 120.0f;
}

#ifdef TARGET_PC
/* The player whose view plays the current fade-in. NULL means the selected
 * resident, which is the retail house exit. */
static ACTOR* s_fade_player = NULL;
/* Set around Game_play_start_fade_in. Any other IN after the first frame is
 * the entrance wipe starting again. */
static int s_allow_fade_in = FALSE;
#endif

static void Game_play_fbdemo_fade_in_move_end(GAME_PLAY* play) {
#ifdef TARGET_PC
    s_fade_player = NULL;
    /* goto_other_scene clears pads because retail destroys the play. The
     * return fade is this play still on screen. */
    play->game.pad_initialized = TRUE;
#endif
    Game_play_fbdemo_wipe_destroy(play);

    if (play->fb_mode == FBDEMO_MODE_MOVE) {
#ifdef TARGET_PC
        /* Retail frees the one capture and drops the frame rate to 3 because
         * this play is about to be destroyed. Here the town stays. That free
         * sits in the heap while the music thread keeps going, and the frame
         * rate stays at 3, so both players look frozen. */
        play->fb_mode = FBDEMO_MODE_NONE;
#else
        fbdemo_cleanup(&fbdemo);
        play->fb_mode = FBDEMO_MODE_NONE;
        SetGameFrame(3);
#endif
    }
}

static void Game_play_fbdemo_fade_out_start_emu_move_end(GAME_PLAY* play) {
    GAME_GOTO_NEXT(&play->game, famicom_emu, FAMICOM_EMU);
}

static void Game_play_fbdemo_fade_out_game_end_move_end(GAME_PLAY* play) {
#ifdef TARGET_PC
    pc_world_drop_children();
#endif
    GAME_GOTO_NEXT(&play->game, trademark, TRADEMARK);
}

static void Game_play_change_scene_move_end(GAME_PLAY* play) {
#ifdef TARGET_PC
    /* Another player is still in this play, or this play is an interior that
     * should return to the town play. Keep both worlds. */
    if (pc_world_transition(play)) {
        Game_play_fbdemo_wipe_destroy(play);
        play->fb_fade_type = FADE_TYPE_NONE;
        /* goto_other_scene clears pads because retail destroys this play.
         * The play stays, and the other world copies these pads each frame. */
        play->game.pad_initialized = TRUE;
        sAdo_Set_ongenpos_refuse_fg(0);
        return;
    }
#endif
    game_goto_next_game_play(&play->game);
    Common_Set(last_scene_no, Save_Get(scene_no));
    Save_Set(scene_no, play->next_scene_no);
}

#ifdef TARGET_PC
/* The second house leave covers the screen and never finishes. Sound checks
 * can hold an out-wipe there with no fault. Count this play's frames only.
 * The town steps first each frame and must not clear the house count. */
static GAME_PLAY* s_out_wipe_play = NULL;
static int s_out_wipe_ticks = 0;
#endif

static void Game_play_fbdemo_wipe_move(GAME_PLAY* play) {
    fbdemo_wipe* wipe = &play->fbdemo_wipe;
    s16 wipet = FALSE;
    int isDone = TRUE;

#ifdef TARGET_PC
    /* Out only. A return fade that is still opening must not be cut: that
     * leaves the peephole, and the frame that drops the card never presents. */
    if (play->fb_fade_type == FADE_TYPE_OUT) {
        if (s_out_wipe_play != play) {
            s_out_wipe_play = play;
            s_out_wipe_ticks = 0;
        }
        s_out_wipe_ticks++;
        if (s_out_wipe_ticks > 90) {
            s_out_wipe_ticks = 0;
            s_out_wipe_play = NULL;
            Game_play_change_scene_move_end(play);
            play->fb_fade_type = FADE_TYPE_NONE;
            return;
        }
    }
#endif

    if (wipe->wipe_procs.isfinished_proc(&wipe->wipe_data) != 0) {
        if ((play->fb_fade_type != FADE_TYPE_IN) && (play->fb_fade_type != FADE_TYPE_EVENT)) {
            Game_play_dec_dt_timer(&S_se_endcheck_timeout, play);

#ifdef TARGET_PC
            /* The present runs one iteration behind the draw: the frame the
             * switch fires on never reaches the screen, so the last thing
             * shown is the fade at ~96 percent with the old scene faintly
             * visible. Hold full black a few frames so it presents before
             * the switch. */
            if (((sAdo_SeFadeoutCheck() == 0) && (S_se_endcheck_timeout > 0.0f)) ||
                (S_se_endcheck_timeout > 115.0f)) {
                isDone = FALSE;
            } else {
                sAdo_Set_ongenpos_refuse_fg(1);
            }
#else
            if ((sAdo_SeFadeoutCheck() == 0) && (S_se_endcheck_timeout > 0.0f)) {
                isDone = FALSE;
            } else {
                sAdo_Set_ongenpos_refuse_fg(1);
            }
#endif
        }
        if (play->fb_fade_type == FADE_TYPE_EVENT) {
            sAdo_Set_ongenpos_refuse_fg(2);
        }

        if (isDone == TRUE) {
            switch (play->fb_fade_type) {
                case FADE_TYPE_IN:
                    Game_play_fbdemo_fade_in_move_end(play);
                    break;

                case FADE_TYPE_OUT_START_EMU:
                    Game_play_dec_dt_timer(&S_back_title_timer, play);

                    if ((sAdo_BgmFadeoutCheck() == 1) || (S_back_title_timer <= 0.0f)) {
                        Game_play_fbdemo_fade_out_start_emu_move_end(play);
                    } else {
                        wipet = TRUE;
                    }
                    break;

                case FADE_TYPE_OUT_RETURN_TITLE:
                case FADE_TYPE_OUT_GAME_END:
                    if (S_back_title_timer >= 120.0f) {
                        mBGMPsComp_make_ps_wipe(360);
                    }
                case FADE_TYPE_OUT_GAME_END_TRAIN:
                case FADE_TYPE_SELECT_END:
                    Game_play_dec_dt_timer(&S_back_title_timer, play);

                    if ((sAdo_BgmFadeoutCheck() == 1) || (S_back_title_timer <= 0.0f)) {
                        Game_play_fbdemo_fade_out_game_end_move_end(play);
                    } else {
                        wipet = TRUE;
                    }
                    break;

                case FADE_TYPE_SELECT:
                    Game_play_dec_dt_timer(&S_back_title_timer, play);

                    if ((sAdo_BgmFadeoutCheck() == 1) || (S_back_title_timer <= 0.0f)) {
                        if ((Common_Get(clip.animal_logo_clip) != NULL) &&
                            (Common_Get(clip.animal_logo_clip->data_init_proc) != NULL)) {
                            Common_Get(clip.animal_logo_clip->data_init_proc)(play);
                            Game_play_change_scene_move_end(play);
                        }
                    } else {
                        wipet = TRUE;
                    }
                    break;

                case FADE_TYPE_DEMO:
                    Game_play_dec_dt_timer(&S_back_title_timer, play);

                    if ((sAdo_BgmFadeoutCheck() == 1) || (S_back_title_timer <= 0.0f)) {
                        Game_play_change_scene_move_end(play);
                    } else {
                        wipet = TRUE;
                    }
                    break;

                case FADE_TYPE_EVENT:
                case FADE_TYPE_OTHER_ROOM:
                    Game_play_change_scene_move_end(play);
                    break;

                default:
                    Game_play_dec_dt_timer(&S_back_title_timer, play);

                    if ((sAdo_BgmFadeoutCheck() == 1) || (S_back_title_timer <= 0.0f)) {
                        Game_play_change_scene_move_end(play);
                    } else {
                        wipet = TRUE;
                    }
                    break;
            }
            if (!wipet) {
#ifdef TARGET_PC
                /* The handoff frame still draws this play. Clearing the type
                 * here drops the card on every view but the first, so that
                 * half shows the town before trademark. */
                if (play->fb_fade_type != FADE_TYPE_OUT_RETURN_TITLE) {
                    play->fb_fade_type = FADE_TYPE_NONE;
                }
#else
                play->fb_fade_type = FADE_TYPE_NONE;
#endif
            }
        }
    } else {
        wipe->wipe_procs.move_proc(&wipe->wipe_data, game_GameFrame);
    }
}

static void Game_play_fbdemo_wipe_proc(GAME_PLAY* play) {
    static PLAY_WIPE_PROC wipe_proc[] = { (PLAY_WIPE_PROC)none_proc1, Game_play_fbdemo_wipe_create,
                                          Game_play_fbdemo_wipe_init, Game_play_fbdemo_wipe_move };

#ifdef TARGET_PC
    /* The entrance wipe runs while the menu is idle. The first inventory open
     * was starting that same wipe again after the menu had already left idle.
     * Drop that IN wipe only. An exit wipe has to keep running, or the second
     * leave stays on the door walk. */
    if ((play->submenu.process_status != mSM_PROCESS_WAIT) && (s_allow_fade_in == FALSE) &&
        ((play->fb_fade_type == FADE_TYPE_IN) || (play->fb_fade_type == FADE_TYPE_NONE))) {
        if (play->fb_wipe_mode == WIPE_MODE_MOVE) {
            Game_play_fbdemo_wipe_destroy(play);
        }
        play->fb_wipe_mode = WIPE_MODE_NONE;
        play->fb_fade_type = FADE_TYPE_NONE;
    }
#endif

    if ((play->fb_wipe_mode == WIPE_MODE_NONE) && (play->fb_fade_type != FADE_TYPE_NONE)) {
#ifdef TARGET_PC
        /* play_init sets IN while game_frame is still 0, so the real entrance
         * still plays. A later IN that did not come from start_fade_in is the
         * same wipe running again. */
        if ((play->fb_fade_type == FADE_TYPE_IN) && (play->game_frame != 0) && (s_allow_fade_in == FALSE)) {
            play->fb_fade_type = FADE_TYPE_NONE;
        } else
#endif
        {
            play->fb_wipe_mode = WIPE_MODE_CREATE;
        }
    }

    wipe_proc[play->fb_wipe_mode](play);
}

#ifdef TARGET_PC
extern void Game_play_start_fade_in(GAME_PLAY* play, int wipe_type) {
    if (play == NULL) {
        return;
    }

    s_fade_player = NULL;
    play->fb_wipe_type = (u8)wipe_type;
    play->fb_fade_type = FADE_TYPE_IN;
    play->fb_wipe_mode = WIPE_MODE_NONE;
    s_allow_fade_in = TRUE;
    Game_play_fbdemo_wipe_proc(play);
    s_allow_fade_in = FALSE;
}

extern void Game_play_start_fade_in_player(GAME_PLAY* play, int wipe_type, ACTOR* player) {
    Game_play_start_fade_in(play, wipe_type);
    s_fade_player = player;
}
#endif

static Gfx* game_play_set_fog(GAME_PLAY* play, Gfx* gfx) {
    return gfx_set_fog_nosync(gfx, play->global_light.fogColor[0], play->global_light.fogColor[1],
                              play->global_light.fogColor[2], 0, play->global_light.fogNear, play->global_light.fogFar);
}

static void Game_play_fbdemo_proc(GAME_PLAY* play) {
    switch (play->fb_mode) {
        case FBDEMO_MODE_INIT:
            if (fbdemo_init(&fbdemo, 10, 7) == 0) {
                play->fb_mode = FBDEMO_MODE_NONE;
            } else {
                play->fb_mode = FBDEMO_MODE_MOVE;
                SetGameFrame(1);
            }
            break;

        case FBDEMO_MODE_MOVE:
            fbdemo_move(&fbdemo);
            break;
    }
}

extern void play_cleanup(GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;

    mMsg_dt(game);
#ifdef TARGET_PC
    if (pc_world_cleanup_is_child() == FALSE) {
        banti_dt();
        /* The primary play's GAME is freed right after this cleanup. Drop
         * every world reference (the town, the interiors) now, or the
         * submenu's world scan reads the freed play before the next
         * attach. */
        pc_world_detach_primary();
    }
#else
    banti_dt();
#endif

    game->graph->taskEndCallback = NULL;
    game->graph->taskEndData = NULL;
    play->submenu.mode = mSM_MODE_IDLE;

    PreRender_cleanup(&play->prerender);
    CollisionCheck_dt(game, &play->collision_check);

    if (play->fb_mode == FBDEMO_MODE_MOVE) {
        fbdemo_cleanup(&fbdemo);
        play->fb_mode = FBDEMO_MODE_NONE;
    }

    if (play->fb_wipe_mode == WIPE_MODE_MOVE) {
        Game_play_fbdemo_wipe_destroy(play);
    }

    Actor_info_dt(&play->actor_info, play);
    mEv_finish(&play->event);
#ifdef TARGET_PC
    if (pc_world_cleanup_is_child() == FALSE) {
        mNpc_ClearEventNpc();
        mNpc_ClearMaskNpc();
    }
#else
    mNpc_ClearEventNpc();
    mNpc_ClearMaskNpc();
#endif

    mSM_submenu_dt(&play->submenu);
    play->submenu_ground_idx = -1;
    mSM_submenu_ovlptr_cleanup(&play->submenu);

    mPlib_Object_Exchange_keep_Player_dt(play);

#ifdef TARGET_PC
    if (pc_world_cleanup_is_child() == FALSE) {
        mHsRm_GetHuusuiRoom(NULL, Common_Get(player_no));
    }
#else
    mHsRm_GetHuusuiRoom(NULL, Common_Get(player_no));
#endif
    mFM_Field_dt();
#ifdef TARGET_PC
    if (pc_world_cleanup_is_child() == FALSE) {
        mCD_toNextLand();
        mEA_CleanCardDLProgram();
        if (my_malloc_current == &my_malloc_func) {
            my_malloc_current = NULL;
        }
    }
#else
    mCD_toNextLand();
    mEA_CleanCardDLProgram();

    if (my_malloc_current == &my_malloc_func) {
        my_malloc_current = NULL;
    }
#endif

    zelda_CleanupArena();
}

static void VR_Box_ct(GAME_PLAY* play) {
    Global_kankyo_ct((GAME*)play, &play->kankyo);
}

extern void play_init(GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;
    GRAPH* graph = game->graph;
    int type;
    fbdemo_fade* fade;
    int freebytes;
    u32 alloc;
    u32 aligned;
    u32 size;

    game_resize_hyral(game, -Game_play_HYRAL_SIZE); // reserve bytes from gamealloc
    Common_Set(rhythym_updated, 0);

#ifdef TARGET_PC
    if (pc_world_init_is_child() == FALSE) {
#endif
    mFI_ChangeClimate_ForEventNotice();
    mTM_time_init();
    sAdo_Set_ongenpos_refuse_fg(0);
    event_title_flag_on();
    mTD_rtc_set();
    mTM_set_season();
    mPlib_Clear_controller_data_for_title_demo();
#ifdef TARGET_PC
    }
#endif

#ifdef TARGET_PC
    /* The overlay table is the session's. A house play that resets it unlinks
     * the open menu and the next inventory draw never comes back. */
    if (pc_world_init_is_child() == FALSE) {
        mSM_submenu_ovlptr_init(play);
    }
#else
    mSM_submenu_ovlptr_init(play);
#endif
    mDemo_Init(play);
    mEv_init(&play->event);

    initView(&play->view, graph);
    Init_Camera2(play);
    CollisionCheck_ct(game, &play->collision_check);

    mCoBG_InitMoveBgData();
    mCoBG_InitBlockBgCheckMode();
    mCoBG_InitDecalCircle();

    play->submenu_ground_idx = -1;

    Gameplay_Scene_Read(play, Save_Get(scene_no));

    mSM_submenu_ct(&play->submenu);
    play->submenu.mode = mSM_MODE_IDLE;

    PreRender_init(&play->prerender);
    PreRender_setup_savebuf(&play->prerender, SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2, NULL, NULL, NULL);
    PreRender_setup_renderbuf(&play->prerender, SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2, NULL, NULL);

    play->fb_mode = FBDEMO_MODE_NONE;
    type = WIPE_TYPE_TRIFORCE;

    play->fb_wipe_mode = WIPE_MODE_NONE;
    play->fb_fade_type = FADE_TYPE_IN;

    if (Common_Get(transition.wipe_type) != 0xFF) {
        type = Common_Get(transition.wipe_type);
        Common_Set(transition.wipe_type, 0xFF);
    }

    play->fb_wipe_type = type;

#ifdef TARGET_PC
    /* The frame order is draw then move, so the new play's first draw runs
     * before its first wipe move and the old scene shows through for a frame.
     * Start the entrance card here, the same way pc_world_fade_in_town and
     * pc_world_open_scene start theirs. */
    Game_play_start_fade_in(play, type);
#endif

    Pause_ct(&play->pause);
    new_Matrix(game);

    fade = &play->color_fade;

    game->exec = play_main;
    game->cleanup = play_cleanup;
    fbdemo_fade_init(fade);
    fbdemo_fade_settype(fade, 7);
    fbdemo_fade_setcolor_rgba8888(fade, 0xA0A0A0FF);
    fbdemo_fade_startup(fade);

    play->fade_color_value.rgba8888 = 0;

    freebytes = game_getFreeBytes(game);
    alloc = (u32)THA_alloc16(&game->tha, freebytes);
    aligned = ALIGN_NEXT(alloc, 16);
    size = aligned - alloc;

    zelda_InitArena((void*)aligned, freebytes - size);

    if (my_malloc_current == NULL) {
        my_malloc_current = &my_malloc_func;
    }

    mFM_FieldInit(play);
    VR_Box_ct(play);
    mMmd_MakeMuseumDisplayData();
    Actor_info_ct(game, &play->actor_info, play->player_data);
    play->draw_chk_proc = none_proc1;
    mMsg_ct(game);
    mEv_2nd_init(&play->event);
    mTD_player_keydata_init(play);
    Balloon_init(play);
    watch_my_step_ct();
#ifdef TARGET_PC
    if (pc_world_init_is_child() == FALSE) {
        mNtc_set_auto_nwrite_data();
        banti_ct();
        event_title_flag_off();
        mEA_GetCardDLProgram();
        pc_world_attach_primary(game);
    }
#else
    mNtc_set_auto_nwrite_data();
    banti_ct();
    event_title_flag_off();
    mEA_GetCardDLProgram();
#endif
}

/* Every player who must not move this pass: the primary menu's opener (when
 * the primary menu is open), each open side slot's opener, and each guest with
 * an open session menu. */
static void play_menu_wait_actors_add(GAME_PLAY* play) {
    PLAYER_ACTOR* opener0;
    int slot;
    int i;
    int count;

    if (play->submenu.process_status != mSM_PROCESS_WAIT || play->submenu.menu_type != mSM_OVL_NONE) {
        opener0 = mPlib_menu_opener_for_slot(0);
        if (opener0 != NULL) {
            Actor_info_add_wait_actor((ACTOR*)opener0);
        }
    }

    for (slot = 1; slot < mSM_SLOT_NUM; slot++) {
        Submenu* side = mSM_side_slot_p(slot);
        PLAYER_ACTOR* opener;

        if (side->menu_type == mSM_OVL_NONE) {
            continue;
        }

        opener = mPlib_menu_opener_for_slot(slot);
        if (opener != NULL) {
            Actor_info_add_wait_actor((ACTOR*)opener);
        }
    }

    count = mPlib_extra_player_count();
    for (i = 0; i < count; i++) {
        PLAYER_ACTOR* guest = mPlib_extra_player_at(i);
        int pad = mPlib_pad_for_player(guest);

        if ((pad > 0) && (pc_session_menu_is_open(pad) != FALSE)) {
            Actor_info_add_wait_actor((ACTOR*)guest);
        }
    }
}

/* Any co-op menu open: the primary, or a guest's side slot. */
static int play_menu_any_open(GAME_PLAY* play) {
    int slot;

    if (play->submenu.menu_type != mSM_OVL_NONE) {
        return TRUE;
    }

    for (slot = 1; slot < mSM_SLOT_NUM; slot++) {
        if (mSM_side_slot_p(slot)->menu_type != mSM_OVL_NONE) {
            return TRUE;
        }
    }

    return FALSE;
}

#ifdef TARGET_PC
/* FALSE while stepping an interior, so town-wide work stays once per frame. */
static int s_world_move_primary = TRUE;

/* The town's actor loop needs someone in that world, or someone still indoors.
 * The outdoor door is in the town. If the last guest leaves while the other
 * player is inside, stopping this loop leaves that door stuck and the exit
 * never finishes. */
static int play_world_has_player(GAME_PLAY* play) {
    if (get_player_actor_withoutCheck(play) != NULL) {
        return TRUE;
    }

    if (s_world_move_primary == FALSE) {
        return FALSE;
    }

    return (mPlib_extra_player_count() != 0) || (pc_world_live() >= 2);
}
#endif

#ifdef TARGET_PC
/* The demo, message and choice machines are one global set. Every live world
 * used to step the message, so an interior talk typed, scrolled and consumed
 * its continue page twice per frame: the NPC polling the window in the actor
 * pass saw CURSOL and the next page was already up, so a continue code never
 * rested on mMsg_INDEX_NORMAL (police lost-and-found never gave the item) and
 * either player's pad could dismiss the other's window. Step it only in the
 * world the active demo's owner is in, and in this town while no demo runs. */
static int play_steps_message(GAME_PLAY* play) {
    int slot = mDemo_OwnerSlot();
    int world;

    if (slot < 0) {
        return (s_world_move_primary != FALSE);
    }

    world = pc_world_slot_world(slot);
    return (pc_world_at(world) == play);
}
#endif

static void Game_play_move_fbdemo_not_move(GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;

    game->doing_point = 0;
    game->doing_point_specific = 0x8F;
    game->doing_point = 1;

    mSM_submenu_ctrl(play);

    game->doing_point = 2;
#ifdef TARGET_PC
    /* Demos run while the world is live. A co-op menu keeps the world live
     * (only its opener waits), so the demo processor must keep running or a
     * guest could never start a talk while the other player has a menu open.
     * Requests whose owner has a menu open are refused in mDemo_Request. */
    if (play->submenu.process_status == mSM_PROCESS_WAIT ||
        (mPlib_extra_player_count() > 0 && play_menu_any_open(play))) {
        /* The talker is one global. The house move's demo is a different
         * world; binding or clearing here drops the outdoor sign talk. The
         * demo machine has to run in every world: a request from a child is
         * picked up and started with that world bound. */
        if (s_world_move_primary) {
            mPlib_demo_context_begin(play);
            mDemo_side_begin_frame();
        }
        mDemo_Main(play);
        if (s_world_move_primary) {
            mDemo_side_try(play);
            mPlib_demo_context_end(play);
            mDemo_side_sync_player(play);
            if (mDemo_CheckDemo() == FALSE) {
                mPlib_clear_demo_talker();
            }
        }
    }
#else
    if (play->submenu.process_status == mSM_PROCESS_WAIT) {
        mDemo_Main(play);
    }
#endif
    if (play->submenu.process_status == mSM_PROCESS_WAIT) {
        game->doing_point = 3;
        mEv_run(&play->event);
    }

    game->doing_point = 4;
    mDemo_stock_clear();
    game->doing_point = 5;
    mSc_dmacopy_data_bank(&play->object_exchange);
    game->doing_point = 6;
#ifdef TARGET_PC
    mPlib_menu_context_begin(play);
#endif
    mSM_submenu_move(&play->submenu);
#ifdef TARGET_PC
    mPlib_menu_context_end(play);
    /* Side slots are the town's menus. An interior has no copy of those
     * players, and stepping the menus there clears now_private. */
    if (s_world_move_primary) {
    /* Side slots: run each slot's move every frame so its WAIT tick keeps the
     * refuse timers alive after a close (an un-ticked timer would refuse every
     * later open). The opener's player context is only needed while the menu
     * is actually open. */
    {
        int slot;

        for (slot = 1; slot < mSM_SLOT_NUM; slot++) {
            Submenu* side = mSM_side_slot_p(slot);
            int open = side->menu_type != mSM_OVL_NONE;

            if (open == FALSE && mPlib_extra_player_count() == 0) {
                continue; /* single player never touches side slots */
            }

            mSM_side_slot_begin(slot);
            if (open) {
                mPlib_menu_context_begin_slot(play, slot);
            }
            mSM_submenu_move(&play->submenu);
            if (open) {
                mPlib_menu_context_end(play);
            }
            mSM_side_slot_end(slot);
        }
    }
    }
#endif

    if (play->submenu.process_status == mSM_PROCESS_WAIT) {
        play->game_frame++;
        mVibctl_clr_force_stop(2);
        game->doing_point = 7;
        CollisionCheck_OC(game, &play->collision_check);
        game->doing_point = 8;
        CollisionCheck_clear(game, &play->collision_check);
        game->doing_point = 9;
        game->doing_point = 0;
        game->doing_point_specific = 0x90;
        play_menu_wait_actors_add(play);
#ifdef TARGET_PC
        if (play_world_has_player(play))
#endif
        {
            Actor_info_call_actor(play, &play->actor_info);
        }
        Actor_info_clear_wait_actors();
        game->doing_point = 0;
        game->doing_point_specific = 0x91;
        game->doing_point = 1;
        mCoBG_CalcTimerDecalCircle();
        game->doing_point = 2;
#ifdef TARGET_PC
        if (play_steps_message(play)) {
            mPlib_demo_context_begin(play);
            mMsg_Main(game);
            mPlib_demo_context_end(play);
        }
        if (s_world_move_primary) {
            mDemo_side_msg(play);
        }
#else
        mMsg_Main(game);
#endif
    } else {
        mVibctl_set_force_stop(2);
#ifdef TARGET_PC
        if (mPlib_menu_opener_is_guest() || mPlib_extra_player_count() > 0) {
            /* Any co-op menu keeps the world live; only the opener waits.
             * Single player (no extras, host opener) keeps the retail freeze:
             * no actor loop at all. */
            play->game_frame++;
            CollisionCheck_OC(game, &play->collision_check);
            CollisionCheck_clear(game, &play->collision_check);
            play_menu_wait_actors_add(play);
#ifdef TARGET_PC
            if (play_world_has_player(play))
#endif
            {
                Actor_info_call_actor(play, &play->actor_info);
            }
            Actor_info_clear_wait_actors();
            mCoBG_CalcTimerDecalCircle();
#ifdef TARGET_PC
            if (play_steps_message(play)) {
                mPlib_demo_context_begin(play);
                mMsg_Main(game);
                mPlib_demo_context_end(play);
            }
            if (s_world_move_primary) {
                mDemo_side_msg(play);
            }
#endif
        }
#endif
    }

    fbdemo_fade_move(&play->color_fade, game_GameFrame);
}

#ifdef TARGET_PC
#define PLAY_COOP_LEAVE_TIMEOUT 180.0f
static f32 s_coop_leave_wait[PLAYER_NUM];
/* Start presses remembered until a safe frame. Press order, not pad index. */
static s8 s_coop_join_q[PLAYER_NUM];
static int s_coop_join_n;

static void play_coop_join_remove(int pad) {
    int i;
    int j;

    for (i = 0; i < s_coop_join_n; i++) {
        if (s_coop_join_q[i] != pad) {
            continue;
        }

        for (j = i; j < s_coop_join_n - 1; j++) {
            s_coop_join_q[j] = s_coop_join_q[j + 1];
        }

        s_coop_join_n--;
        return;
    }
}

static void play_coop_join_push(int pad) {
    int i;

    for (i = 0; i < s_coop_join_n; i++) {
        if (s_coop_join_q[i] == pad) {
            return;
        }
    }

    if (s_coop_join_n < PLAYER_NUM) {
        s_coop_join_q[s_coop_join_n] = (s8)pad;
        s_coop_join_n++;
    }
}

/* A frame where a player can join or leave without interrupting a menu, demo,
 * fade or scene wipe. A guest's side-slot menu counts: its opener must not be
 * deleted or spawned over while it edits its pockets. An interior menu lives
 * on that play, not this town submenu, and it counts the same way. */
static int play_coop_safe(GAME_PLAY* play) {
    int slot;
    int world;

    for (slot = 1; slot < mSM_SLOT_NUM; slot++) {
        if (mSM_side_slot_p(slot)->menu_type != mSM_OVL_NONE) {
            return FALSE;
        }
    }

    for (world = 1; world < PC_WORLD_MAX; world++) {
        GAME_PLAY* child = pc_world_at(world);

        if ((child != NULL) && (child->submenu.process_status != mSM_PROCESS_WAIT)) {
            return FALSE;
        }
    }

    return (play->submenu.process_status == mSM_PROCESS_WAIT && play->fb_fade_type == FADE_TYPE_NONE &&
            play->fb_wipe_mode == WIPE_MODE_NONE && play->fb_mode == FBDEMO_MODE_NONE &&
            mEv_CheckFirstIntro() == FALSE && mDemo_CheckDemo() == FALSE);
}

/* Co-op join and leave. Start on an idle configured row joins; a guest's
 * session menu sets a leave request, which runs once that actor is idle and
 * the frame is safe. */
static void play_coop_session_proc(GAME_PLAY* play) {
    int safe = play_coop_safe(play);
    int count = mPlib_extra_player_count();
    int pad;
    int i;

    if (g_pc_coop_session == 0) {
        s_coop_join_n = 0;
        return;
    }

    for (pad = 1; pad < PLAYER_NUM; pad++) {
        if ((g_pc_settings.player_input[pad] == PC_PLAYER_INPUT_NONE) || (mPlib_pad_in_use(pad) == TRUE)) {
            play_coop_join_remove(pad);
        } else if (chkTrigger_pad(pad, BUTTON_START) != FALSE) {
            /* Remember the press during a talk, menu, or fade. The join runs
             * on the next safe frame, in the order the presses arrived. */
            play_coop_join_push(pad);
        }

        if (mPlib_pad_in_use(pad) == FALSE) {
            s_coop_leave_wait[pad] = 0.0f;
            pc_session_menu_close(pad);
            pc_session_menu_take_leave_request(pad); /* drop a stale request */
            play_guest_camera_reset(pad);
        }
    }

    for (i = 0; i < count; i++) {
        PLAYER_ACTOR* guest = mPlib_extra_player_at(i);
        int guest_pad = mPlib_pad_for_player(guest);

        if ((guest_pad > 0) && (pc_session_menu_take_leave_request(guest_pad) != FALSE)) {
            s_coop_leave_wait[guest_pad] = PLAY_COOP_LEAVE_TIMEOUT;
        }
    }

    for (i = 0; i < count; i++) {
        PLAYER_ACTOR* guest = mPlib_extra_player_at(i);
        int guest_pad = mPlib_pad_for_player(guest);

        if ((guest_pad <= 0) || (s_coop_leave_wait[guest_pad] <= 0.0f)) {
            continue;
        }

        s_coop_leave_wait[guest_pad] -= play->game.graph->dt_num_60fps_frames;
        if (s_coop_leave_wait[guest_pad] <= 0.0f) {
            s_coop_leave_wait[guest_pad] = 0.0f;
            continue;
        }

        /* A conversation still owns this actor's pointer. Let the talk finish
         * (or the request time out) instead of freeing it underneath the demo. */
        if ((guest == mPlib_demo_talker()) || ((ACTOR*)guest == mDemo_side_player())) {
            continue;
        }

        if ((safe == FALSE) || (guest->now_main_index != mPlayer_INDEX_WAIT)) {
            continue;
        }

        s_coop_leave_wait[guest_pad] = 0.0f;
        pc_session_menu_close(guest_pad);
        Actor_info_delete(&play->actor_info, (ACTOR*)guest, (GAME*)play);
        g_pc_coop_joined[guest_pad] = 0;
        g_pc_coop_guest_slot[guest_pad] = -1;
        return;
    }

    /* A guest who asked to leave from inside a building is not in this world's
     * actor list, so the loop above never sees their request. Their interior is
     * theirs alone: drop it and their body with it. */
    for (pad = 1; pad < PLAYER_NUM; pad++) {
        int slot;

        if ((g_pc_coop_joined[pad] == 0) || (pc_session_menu_take_leave_request(pad) == FALSE)) {
            continue;
        }

        slot = g_pc_coop_guest_slot[pad];
        pc_world_leave_slot(slot);
        g_pc_coop_joined[pad] = 0;
        g_pc_coop_guest_slot[pad] = -1;
        s_coop_leave_wait[pad] = 0.0f;
        pc_session_menu_close(pad);
        play_guest_camera_reset(pad);
    }

    if ((safe != FALSE) && (s_coop_join_n > 0)) {
        int join_pad = s_coop_join_q[0];

        if (Actor_info_join_guest_pad((GAME*)play, join_pad) != FALSE) {
            play_coop_join_remove(join_pad);
        }
    }
}
#endif

#ifdef TARGET_PC
static void play_move_child(GAME* child);
#endif

static void Game_play_move(GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;
    int pause;
#ifdef TARGET_PC
    {
        extern int g_pc_paused;
        if (g_pc_paused) return;
    }
#endif

    game->doing_point = 0;
    game->doing_point_specific = 0x8D;
    game->doing_point = 1;
    Game_play_Reset_destiny();
    game->doing_point = 2;

    if (ZURUMODE2_ENABLED()) {
        pause = !Pause_proc(&play->pause, &game->pads[PAD1]);
    } else {
        pause = FALSE;
    }

    game->doing_point = 0;
    game->doing_point_specific = 0x8E;

    if (!pause) {
#ifdef TARGET_PC
        /* A world swap can leave a joined guest on pad 0. Their mailbox close
         * then opens the town submenu, which is the other resident's mail. */
        if (s_world_move_primary) {
            mPlib_repair_guest_pads();
        }
#endif
        game->doing_point = 1;
        Game_play_fbdemo_wipe_proc(play);
        game->doing_point = 2;
        Game_play_fbdemo_proc(play);
        game->doing_point = 3;

        if (play->fb_mode != FBDEMO_MODE_MOVE) {
            Game_play_move_fbdemo_not_move(game);
        }
    }

    if (play->submenu.process_status == mSM_PROCESS_WAIT
#ifdef TARGET_PC
        || mPlib_menu_opener_is_guest() || mPlib_extra_player_count() > 0
#endif
    ) {
        evw_anime_add_scroll_phase(&play->game);
        game->doing_point = 0;
        game->doing_point_specific = 0x92;
        game->doing_point = 1;
#ifdef TARGET_PC
        /* The selected resident is in another world, or that world was just
         * dropped on the way to the title. This play has no camera for them.
         * Guest cameras are built at draw time. */
        if (get_player_actor_withoutCheck(play) != NULL) {
            Game_play_camera_proc(play);
        }
#else
        Game_play_camera_proc(play);
#endif
        game->doing_point = 2;
#ifdef TARGET_PC
        if (s_world_move_primary) {
#endif
        mPO_business_proc(play);
        game->doing_point = 3;
        mTRC_move(game);
        game->doing_point = 4;
        Balloon_move(play);
#ifdef TARGET_PC
        }
#endif
        mEnv_ManagePointLight(play, &play->kankyo, &play->global_light);
    }

    game->doing_point = 0;
    game->doing_point_specific = 0x93;
    game->doing_point = 1;
    Global_kankyo_set(play, &play->kankyo, &play->global_light);
    game->doing_point = 2;
#ifdef TARGET_PC
    if (s_world_move_primary) {
        mEnv_WindMove();
    }
#else
    mEnv_WindMove();
#endif
    game->doing_point = 3;
#ifdef TARGET_PC
    if (s_world_move_primary) {
        watch_my_step_move(play);
    } else {
        watch_my_step_other_world(play);
    }
#else
    watch_my_step_move(play);
#endif
    game->doing_point = 4;
#ifdef TARGET_PC
    if (s_world_move_primary) {
    /* Each view's clock slide needs its own camera: project that view's player
     * through a rebuilt per-view matrix (the stored play->projection_matrix is
     * the last-drawn view's at move time). */
    {
        int count = play_player_count();
        int i;

        for (i = 0; i < count; i++) {
            PLAYER_ACTOR* player = play_view_player(play, i);
            xyz_t eye, center, screen_pos;
            Mtx proj_m;
            Mtx look_m;
            MtxF proj;
            MtxF look;
            MtxF combined;
            u16 normal;
            rect vrect;
            f32 aspect;

            if (player == NULL) {
                continue;
            }

            play_view_camera(play, i, &eye, &center);
            play_view_rect(i, count, &vrect);
            aspect = (f32)(vrect.r - vrect.l) / (f32)(vrect.bottom - vrect.top);
            guPerspective(&proj_m, &normal, play->view.fovY, aspect, play->view.near, play->view.far,
                          play->view.scale);
            guLookAt(&look_m, eye.x, eye.y, eye.z, center.x, center.y, center.z, play->view.up.x, play->view.up.y,
                     play->view.up.z);
            Matrix_MtxtoMtxF(&proj_m, &proj);
            Matrix_MtxtoMtxF(&look_m, &look);
            Skin_Matrix_MulMatrix(&proj, &look, &combined);
            Game_play_Projection_Trans_m(&combined, &player->actor_class.world.position, &screen_pos);
            banti_view_slide(i, &screen_pos, (f32)play->game.graph->dt_num_60fps_frames);
        }
    }
    }
#endif
#ifdef TARGET_PC
    if (s_world_move_primary) {
#endif
    banti_move(play);
#ifdef TARGET_PC
    }
#endif
    game->doing_point = 5;
#ifdef TARGET_PC
    if (s_world_move_primary) {
        title_demo_move(play);
        play_coop_session_proc(play);
    }
#else
    title_demo_move(play);
#endif
    game->doing_point = 0;
    game->doing_point_specific = 0x95;
}

// @BUG - this function works properly in N64, but not in GC.
// The devs moved blocks (acres) to new display list buffers in GC,
// but forgot to apply fog to those new display list buffers.
static void setupFog(GAME_PLAY* play, GRAPH* graph) {
    OPEN_DISP(graph);

    SET_POLY_OPA_DISP(game_play_set_fog(play, NOW_POLY_OPA_DISP));
    SET_POLY_XLU_DISP(game_play_set_fog(play, NOW_POLY_XLU_DISP));

    #if defined(BUGFIXES) || defined(TARGET_PC)
    // Apply fog to blocks
    SET_BG_OPA_DISP(game_play_set_fog(play, NOW_BG_OPA_DISP));
    SET_BG_XLU_DISP(game_play_set_fog(play, NOW_BG_XLU_DISP));
    #endif

    CLOSE_DISP(graph);
}

static void setupViewer(GAME_PLAY* play) {
    showView(&play->view, VIEW_UPDATE_ALL);
}

static void setupViewMatrix(GAME_PLAY* play, GRAPH* graph1, GRAPH* graph2) {
    OPEN_DISP(graph1);

    Matrix_MtxtoMtxF(&play->view.mtx_viewing, &play->billboard_matrix);
    Matrix_MtxtoMtxF(&play->view.mtx_projection, &play->projection_matrix);
    Skin_Matrix_MulMatrix(&play->projection_matrix, &play->billboard_matrix, &play->projection_matrix);

    play->billboard_matrix.mf[0][3] = 0.0f;
    play->billboard_matrix.mf[1][3] = 0.0f;
    play->billboard_matrix.mf[2][3] = 0.0f;
    play->billboard_matrix.mf[3][0] = 0.0f;
    play->billboard_matrix.mf[3][1] = 0.0f;
    play->billboard_matrix.mf[3][2] = 0.0f;

    Matrix_reverse(&play->billboard_matrix);
    play->billboard_mtx_p = _MtxF_to_Mtx(&play->billboard_matrix, GRAPH_ALLOC_TYPE(graph2, MtxF, 1));
    gSPSegment(NEXT_BG_OPA_DISP, 7, play->billboard_mtx_p);
    gSPSegment(NEXT_POLY_OPA_DISP, 7, play->billboard_mtx_p);

    CLOSE_DISP(graph1);
}

static void copy_efb_to_texture(Gfx** graph, void* buf) {
    u32 dl;

    GXBeginDisplayList(gxbuf, sizeof(gxbuf));
    GXSetTexCopySrc(0, 0, SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2);
    GXSetTexCopyDst(SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2, GX_TF_RGB565, 0);
    GXSetCopyFilter(0, 0, 0, 0);
    GXCopyTex(buf, 0);
    dl = GXEndDisplayList();
    gGXCallDisplayList((*graph)++, gxbuf, dl);
}

#ifdef TARGET_PC
/* One view per player. One player fills the window, two are halves, and three
 * or four are quadrants in view order: P1 top-left, guests left to right then
 * down. With three players the bottom-right quadrant stays empty. */
static void play_view_rect(int index, int count, rect* out) {
    out->top = 0;
    out->bottom = SCREEN_HEIGHT;
    out->l = 0;
    out->r = SCREEN_WIDTH;

    if (count <= 1) {
        return;
    }

    if (count == 2) {
        if (index <= 0) {
            out->r = SCREEN_WIDTH / 2;
        } else {
            out->l = SCREEN_WIDTH / 2;
        }
        return;
    }

    out->l = (index & 1) ? (SCREEN_WIDTH / 2) : 0;
    out->r = out->l + (SCREEN_WIDTH / 2);
    out->top = (index >= 2) ? (SCREEN_HEIGHT / 2) : 0;
    out->bottom = out->top + (SCREEN_HEIGHT / 2);
}

static void play_set_view_player(GAME_PLAY* play, int index, int count) {
    rect view_rect;

    play_view_rect(index, count, &view_rect);
    setScissorView(&play->view, &view_rect);
}

/* View slot of an actor (0 = selected resident). */
static int play_actor_view_index(GAME_PLAY* play, PLAYER_ACTOR* actor) {
    int count = 1 + mPlib_extra_player_count();
    int i;

    (void)play;

    if (actor == NULL) {
        return 0;
    }

    for (i = 1; i < count; i++) {
        if (mPlib_extra_player_at(i - 1) == actor) {
            return i;
        }
    }

    return 0;
}

/* Menu rect: the opener's view. At two players that is its half with a 4:3
 * band so the panel and its 3D hand share one undistorted space; a quadrant at
 * three or four players is already 4:3. */
static void play_set_view_menu(GAME_PLAY* play, int count, int index) {
    rect band;

    play_view_rect(index, count, &band);

    if (count == 2) {
        band.top = (SCREEN_HEIGHT - (SCREEN_HEIGHT / 2)) / 2;
        band.bottom = band.top + (SCREEN_HEIGHT / 2);
    }

    setScissorView(&play->view, &band);
}

/* Talk message: the retail window is almost the full 320 width and sits on the
 * bottom. Map that whole layout into the bottom 4:3 of the talker's half so it
 * stays in their view instead of crossing the split. A quadrant is already
 * 4:3. */
static void play_set_view_talk(GAME_PLAY* play, int count, int index) {
    rect band;

    play_view_rect(index, count, &band);

    if (count <= 2) {
        band.top = SCREEN_HEIGHT / 2;
        band.bottom = SCREEN_HEIGHT;
    }

    setScissorView(&play->view, &band);
}

#ifdef TARGET_PC
/* Live guest session menus and the host pause overlay, each in its owner's
 * view. The host pause still freezes the game; guest menus do not. */
static void play_draw_coop_overlays(GAME_PLAY* play) {
    rect saved = play->view.screen;
    rect full;
    int count = play_player_count();
    int has_overlay = FALSE;
    int pad;

    /* By pad, not by this world's actor list: a guest inside a building has no
     * town body, and their menu still has to draw in their own view. */
    for (pad = 1; pad < PLAYER_NUM; pad++) {
        if (pc_session_menu_is_open(pad) != FALSE) {
            has_overlay = TRUE;
        }
    }

    if (g_pc_paused) {
        has_overlay = TRUE;
    }

    if (has_overlay == FALSE) {
        return;
    }

    /* These are 2D overlays over the finished world. The menu pass uses the
     * same depth bypass; without it the backdrop is rejected by the town's
     * depth buffer while the font text (depthless) still shows. */
    OPEN_DISP(play->game.graph);
    gDPNoOpTag(NOW_POLY_OPA_DISP++, PC_NOOP_MENU_DEPTH_OFF);
    CLOSE_DISP(play->game.graph);

    for (pad = 1; pad < PLAYER_NUM; pad++) {
        int view_index;

        if (pc_session_menu_is_open(pad) == FALSE) {
            continue;
        }

        view_index = pc_world_slot_view(g_pc_coop_guest_slot[pad]);
        if ((view_index <= 0) || (view_index >= count)) {
            continue;
        }

        play_set_view_menu(play, count, view_index);
        showSplitHudView(&play->view);
        pc_menu_band_l = play->view.screen.l;
        pc_menu_band_t = play->view.screen.top;
        pc_menu_band_r = play->view.screen.r;
        pc_menu_band_b = play->view.screen.bottom;
        pc_session_menu_draw(&play->game, pad);
    }

    if (g_pc_paused) {
        play_set_view_menu(play, count, 0);
        showSplitHudView(&play->view);
        pc_menu_band_l = play->view.screen.l;
        pc_menu_band_t = play->view.screen.top;
        pc_menu_band_r = play->view.screen.r;
        pc_menu_band_b = play->view.screen.bottom;
        pc_pause_menu_draw(&play->game);
    }

    OPEN_DISP(play->game.graph);
    gDPNoOpTag(NOW_POLY_OPA_DISP++, PC_NOOP_MENU_DEPTH_ON);
    CLOSE_DISP(play->game.graph);

    pc_menu_band_l = -1;
    pc_menu_band_t = -1;
    pc_menu_band_r = -1;
    pc_menu_band_b = -1;

    full.top = 0;
    full.bottom = SCREEN_HEIGHT;
    full.l = 0;
    full.r = SCREEN_WIDTH;
    play->view.screen = saved;
    setScissorView(&play->view, &full);
    showSplitHudView(&play->view);
}
#endif

#ifdef TARGET_PC
/* One message window into one player's bottom band. */
static void play_draw_talk_msg(GAME_PLAY* play, mMsg_Window_c* msg, PLAYER_ACTOR* talker) {
    rect saved;
    rect full;
    int count;
    int index;

    if ((msg == NULL) || (msg->draw_flag == FALSE)) {
        return;
    }

    saved = play->view.screen;
    /* An indoor resident is off the town actor list, so the extra count is 1
     * and the retail window spans the whole screen. The session still has
     * their half. */
    count = play_player_count();
    index = play_actor_view_index(play, talker);
    {
        /* The indoor actor is not in the town table, so the actor lookup is
         * P1's half. The demo owner slot is the resident who is talking. */
        int slot = mPlib_demo_talker_slot();
        int view;

        if ((slot < 0) && (talker != NULL)) {
            slot = mPlib_player_slot_of(talker);
        }
        view = pc_world_slot_view(slot);
        if (view >= 0) {
            index = view;
        }
    }
    play_set_view_talk(play, count, index);
    showSplitHudView(&play->view);
    mMsg_set_use_window(msg);
    mMsg_Draw((GAME*)play);
    mMsg_set_use_window(NULL);
    full.top = 0;
    full.bottom = SCREEN_HEIGHT;
    full.l = 0;
    full.r = SCREEN_WIDTH;
    play->view.screen = saved;
    setScissorView(&play->view, &full);
    showSplitHudView(&play->view);
}
#endif

/* pc_gx_menu_mode for the opener's view: 1/2 = two-player left/right band,
 * 3-6 = quadrant top-left, top-right, bottom-left, bottom-right. */
static int play_menu_mode(int count, int index) {
    if (count <= 1) {
        return 0;
    }

    if (count == 2) {
        return (index == 0) ? 1 : 2;
    }

    return 3 + index;
}

/* One player, or one view per resident still in the session. An indoor guest
 * is no longer in the town actor list, and still keeps their half. */
static int play_player_count(void) {
#ifdef TARGET_PC
    return pc_world_session_count();
#else
    return 1 + mPlib_extra_player_count();
#endif
}

/* The actor a split view belongs to: view 0 is the selected resident. */
static PLAYER_ACTOR* play_view_player(GAME_PLAY* play, int index) {
    PLAYER_ACTOR* player;

    if (index > 0) {
        player = mPlib_extra_player_at(index - 1);

        if (player != NULL) {
            return player;
        }
    }

    return get_player_actor_withoutCheck(play);
}

static void play_draw_split_side(GAME_PLAY* play, GRAPH* graph, int index, int count, xyz_t* eye, xyz_t* center) {
#ifdef TARGET_PC
    /* The indoor view's room prim stays in GL after its list is flushed.
     * Outdoor acres use that prim and never set it back. */
    OPEN_DISP(graph);
    gDPSetPrimColor(NOW_POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
    gDPSetPrimColor(NOW_POLY_XLU_DISP++, 0, 0, 255, 255, 255, 255);
    gDPSetPrimColor(NOW_BG_OPA_DISP++, 0, 0, 255, 255, 255, 255);
    CLOSE_DISP(graph);
#endif
    play_set_view_player(play, index, count);
    if ((eye != NULL) && (center != NULL)) {
        setLookAtView(&play->view, eye, center, &play->view.up);
    }

    setupFog(play, graph);
    setupViewer(play);
    setupViewMatrix(play, graph, graph);
    /* Field draw follows the view's own player instead of the selected resident. */
    mPlib_set_view_player_actor(play_view_player(play, index));
    mPlib_set_view_player_index(index);
    /* TEMP draw-time fog/light probe: what this view is actually drawn with. */
    {
        static int last_view = -1, last_scene = -1, last_amb = -1, last_fog = -1;
        int scene = Save_Get(scene_no);
        int amb = (play->global_light.ambientColor[0] << 16) | (play->global_light.ambientColor[1] << 8) |
                  play->global_light.ambientColor[2];
        int fog = (play->global_light.fogColor[0] << 16) | (play->global_light.fogColor[1] << 8) |
                  play->global_light.fogColor[2];

        if (index != last_view || scene != last_scene || amb != last_amb || fog != last_fog) {
            FILE* fp = fopen("draw_light_probe.txt", "a");

            last_view = index;
            last_scene = scene;
            last_amb = amb;
            last_fog = fog;
            if (fp != NULL) {
                fprintf(fp, "DRAW view=%d scene=%d amb=%06x fog=%06x near=%d far=%d\n", index, scene, amb, fog,
                        play->global_light.fogNear, play->global_light.fogFar);
                fclose(fp);
            }
        }
    }
    Actor_info_draw_actor(play, &play->actor_info);
    mPlib_set_view_player_actor(NULL);
}

#ifdef TARGET_PC
static int s_draw_view_world[PLAYER_NUM];
static GAME_PLAY* s_draw_culled;

/* Draw one view from the world that player is in. A shared world is drawn
 * once per view, and an empty world is not drawn. */
static void play_draw_bound_view(GAME_PLAY* play, GRAPH* graph, int index, int count, xyz_t* eye, xyz_t* center) {
    int world = s_draw_view_world[index];
    GAME_PLAY* view_play = pc_world_at(world);
    int foreign;

    if (view_play == NULL) {
        view_play = play;
        world = 0;
    }

    foreign = (view_play != play);
    if (foreign) {
        pc_world_bind(world);
    }

    if (s_draw_culled != view_play) {
        Actor_info_cull_begin(view_play);
        s_draw_culled = view_play;
    }

    play_draw_split_side(view_play, graph, index, count, foreign ? NULL : eye, foreign ? NULL : center);

    if (foreign) {
        pc_world_bind(0);
    }
}
#endif

/* The guest camera runs the same normal follow as the host (retail spring and
 * constants) but from the guest's own eye position, direction and distance, so
 * nothing the selected resident does moves this view. Door and talk cameras
 * keep the last normal offset relative to this guest. */
static xyz_t s_eye_ofs[PLAYER_NUM];
static xyz_t s_center_ofs[PLAYER_NUM];
static xyz_t s_guest_center[PLAYER_NUM];
static s_xyz s_guest_dir[PLAYER_NUM];
static f32 s_guest_dist[PLAYER_NUM];
static int s_ofs_ready[PLAYER_NUM];
static int s_follow_ready[PLAYER_NUM];
static int s_outdoor_snap[PLAYER_NUM];
static int s_cam_hold[PLAYER_NUM];

static void play_guest_camera_reset(int pad) {
    if ((pad < 0) || (pad >= PLAYER_NUM)) {
        return;
    }

    s_follow_ready[pad] = FALSE;
    s_ofs_ready[pad] = FALSE;
    s_cam_hold[pad] = FALSE;
    /* The guest camera does not run while they are indoors, so the latch
     * would still be set on the next leave and that view would spring. */
    s_outdoor_snap[pad] = FALSE;
}

extern void pc_play_guest_camera_reset(int pad) {
    play_guest_camera_reset(pad);
}

static void play_guest_camera(GAME_PLAY* play, PLAYER_ACTOR* guest, int pad, xyz_t* eye, xyz_t* center) {
    xyz_t* guest_pos = &guest->actor_class.world.position;
    xyz_t goal_center;
    s_xyz goal_dir;
    f32 goal_dist;
    xyz_t guest_eye;
    xyz_t guest_center;

    if ((pad < 0) || (pad >= PLAYER_NUM)) {
        return;
    }

#ifdef TARGET_PC
    /* Every building walks out through this index. The first frame starts on
     * the door. The latch re-arms when the walk ends, and when the return
     * body is created, because this camera does not run indoors. The generic
     * structure exit (m_player_other_func) walks out through INVADE, which
     * never passes through OUTDOOR. */
    if ((guest->now_main_index == mPlayer_INDEX_OUTDOOR) ||
        (guest->now_main_index == mPlayer_INDEX_INVADE)) {
        if (s_outdoor_snap[pad] == FALSE) {
            s_follow_ready[pad] = FALSE;
            s_ofs_ready[pad] = FALSE;
            s_outdoor_snap[pad] = TRUE;
        }
    } else {
        s_outdoor_snap[pad] = FALSE;
    }
#endif

    if (play->camera.now_main_index == CAMERA2_PROCESS_NORMAL) {
        if (s_cam_hold[pad] != FALSE) {
            /* The play camera was on a door or a talk, and this view held its
             * last offset through it. The follow goals are stale by whatever
             * the guest walked meanwhile, so start them again from here
             * instead of sweeping the camera across to them. */
            s_cam_hold[pad] = FALSE;
            s_follow_ready[pad] = FALSE;
        }

        Camera2_GetNormalCenterGoal(play, guest, &goal_center);
        Camera2_GetNormalFollowGoal(play, guest, &goal_dist, &goal_dir);

        if (s_follow_ready[pad] == FALSE) {
            s_guest_center[pad] = goal_center;
            s_guest_dist[pad] = goal_dist;
            s_guest_dir[pad] = goal_dir;
            s_follow_ready[pad] = TRUE;
        } else {
            add_calc(&s_guest_center[pad].x, goal_center.x, 0.13397461f, 8.75f, 0.25f);
            add_calc(&s_guest_center[pad].y, goal_center.y, 0.13397461f, 8.75f, 0.25f);
            add_calc(&s_guest_center[pad].z, goal_center.z, 0.13397461f, 8.75f, 0.25f);
            add_calc(&s_guest_dist[pad], goal_dist, 0.13397461f, 8.75f, 0.25f);
            add_calc_short_angle2(&s_guest_dir[pad].x, goal_dir.x, 0.051316679f, 11000, 100);
            add_calc_short_angle2(&s_guest_dir[pad].y, goal_dir.y, 0.051316679f, 11000, 75);
            add_calc_short_angle2(&s_guest_dir[pad].z, goal_dir.z, 0.051316679f, 11000, 150);
        }

        guest_center = s_guest_center[pad];
        Camera2_SetEyePos_fromFollow(play, &s_guest_dir[pad], s_guest_dist[pad], &guest_center, &guest_eye);

        xyz_t_sub(&guest_eye, guest_pos, &s_eye_ofs[pad]);
        xyz_t_sub(&guest_center, guest_pos, &s_center_ofs[pad]);
        s_ofs_ready[pad] = TRUE;
    } else if (s_ofs_ready[pad] == TRUE) {
        s_cam_hold[pad] = TRUE;
        xyz_t_add(guest_pos, &s_eye_ofs[pad], &guest_eye);
        xyz_t_add(guest_pos, &s_center_ofs[pad], &guest_center);
    } else {
        s_cam_hold[pad] = TRUE;
        Camera2_GetNormalCenterGoal(play, guest, &guest_center);
        Camera2_GetNormalFollowGoal(play, guest, &goal_dist, &goal_dir);
        Camera2_SetEyePos_fromFollow(play, &goal_dir, goal_dist, &guest_center, &guest_eye);
        s_guest_center[pad] = guest_center;
        s_guest_dist[pad] = goal_dist;
        s_guest_dir[pad] = goal_dir;
        s_follow_ready[pad] = TRUE;
        xyz_t_sub(&guest_eye, guest_pos, &s_eye_ofs[pad]);
        xyz_t_sub(&guest_center, guest_pos, &s_center_ofs[pad]);
        s_ofs_ready[pad] = TRUE;
    }

    *eye = guest_eye;
    *center = guest_center;
}

/* View 0 is the selected resident, whose camera is already in play->view, so
 * only guests need this. */
static void play_view_camera(GAME_PLAY* play, int index, xyz_t* eye, xyz_t* center) {
    PLAYER_ACTOR* guest;
    int pad;

    if (index <= 0) {
        return;
    }

    guest = mPlib_extra_player_at(index - 1);
    if (guest == NULL) {
        int i;

        /* The return body is not in the extra list yet. This half would keep
         * the plaza camera and then chase the door. */
        for (i = 0; i < PLAYER_NUM; i++) {
            PLAYER_ACTOR* who = get_player_actor_by_index(play, i);

            if ((who == NULL) || (mPlib_pad_for_player_slot(i) <= 0)) {
                continue;
            }

            if ((pc_world_door_return_pending(i) == FALSE) && (who->now_main_index != mPlayer_INDEX_OUTDOOR)) {
                continue;
            }

            guest = who;
            break;
        }
    }

    if (guest == NULL) {
        return;
    }

    pad = mPlib_pad_for_player_slot(mPlib_player_slot_of(guest));
    if ((pad < 0) || (pad >= PLAYER_NUM)) {
        pad = index;
    }

    play_guest_camera(play, guest, pad, eye, center);
}

extern void pc_graph_flush_world(GRAPH* graph);

/* Each earlier view has already been shown. Start a new list for the next
 * view and the HUD without changing the clear color. */
static void play_restart_disp(GAME_PLAY* play, GRAPH* graph) {
    u8 fill_r = 0;
    u8 fill_g = 0;
    u8 fill_b = 0;

    if (Common_Get(field_type) == mFI_FIELDTYPE2_FG && mEv_CheckTitleDemo() != mEv_TITLEDEMO_STAFFROLL) {
        fill_r = play->kankyo.base_light.background_color[0];
        fill_g = play->kankyo.base_light.background_color[1];
        fill_b = play->kankyo.base_light.background_color[2];
    }

    DisplayList_initialize(graph, fill_r, fill_g, fill_b, (GAME*)play);
}

/* Drawing a second play into the wipe list is what froze the zoom. Each view
 * now draws the world its player is in, flushed per view, so a wipe in one
 * world no longer replaces the other player's half with this play. Return to
 * Title fades every view in place, so each world keeps its own picture under
 * that fade. */
static int play_view_world(int index) {
    int world = s_draw_view_world[index];

    if ((world <= 0) || (world >= PC_WORLD_MAX) || (pc_world_at(world) == NULL)) {
        return 0;
    }

    return world;
}

static int play_split_live(GAME_PLAY* play) {
    PLAYER_ACTOR* host = get_player_actor_withoutCheck(play);

    /* Someone is still in another scene. Keep their view when the last
     * outdoor player leaves. */
    if (pc_world_live() >= 2) {
        return TRUE;
    }

    if ((mPlib_extra_player_count() == 0) || (host == NULL)) {
        return FALSE;
    }

    return TRUE;
}

static GAME_PLAY* play_fading_play(GAME_PLAY* town) {
    int i;

    for (i = 0; i < PC_WORLD_MAX; i++) {
        GAME_PLAY* world = pc_world_at(i);

        if (world == NULL) {
            continue;
        }

        if ((world->fb_wipe_mode != WIPE_MODE_NONE) || (world->fb_fade_type != FADE_TYPE_NONE) ||
            (world->color_fade.color.a != 0)) {
            return world;
        }
    }

    return town;
}

static int play_fade_is_split(GAME_PLAY* play) {
    GAME_PLAY* fading = play_fading_play(play);
    int split = (mPlib_extra_player_count() > 0) || (pc_world_live() >= 2);

    /* Leaving the house runs the wipe on the house play. The town play stays
     * idle, so a check of this play never sees it. */
    return (split != FALSE) &&
           ((fading->fb_wipe_mode != WIPE_MODE_NONE) || (fading->fb_fade_type != FADE_TYPE_NONE) ||
            (fading->color_fade.color.a != 0));
}

static int play_fade_every_view(GAME_PLAY* fading) {
    return fading->fb_fade_type == FADE_TYPE_OUT_RETURN_TITLE;
}

static int play_fade_view(GAME_PLAY* play) {
    PLAYER_ACTOR* host = get_player_actor_withoutCheck(play);
    GAME_PLAY* fading = play_fading_play(play);
    int slot = -1;
    int view;

    /* Join and the walk back outside name the actor. That wins over the
     * door slot, which is only set for a scene change. */
    if (s_fade_player != NULL) {
        view = play_actor_view_index(play, (PLAYER_ACTOR*)s_fade_player);
        if (view > 0) {
            return view;
        }
    }

    /* A door demo names its walker. The town is the primary play, so
     * pc_world_play_slot() has no slot for it and the fallback below would put
     * the fade on the selected resident's half - the other player's. */
    {
        PLAYER_ACTOR* talker = mPlib_demo_talker();

        if (talker != NULL) {
            return play_actor_view_index(play, talker);
        }
    }

    /* The out-wipe runs on the town before that resident is removed. The
     * in-wipe runs on the interior after. Either one belongs to their half. */
    if (fading != NULL) {
        slot = pc_world_play_slot(fading);
    }
    if (slot < 0) {
        slot = pc_world_entering_slot();
    }

    if (slot >= 0) {
        view = pc_world_slot_view(slot);
        if (view >= 0) {
            return view;
        }
    }

    if (s_fade_player != NULL) {
        return 0;
    }

    if (host == NULL) {
        return 0;
    }

    view = play_actor_view_index(play, host);
    return (view < 0) ? 0 : view;
}

static int play_menu_blocks_fade(GAME_PLAY* fading, int view) {
    GAME_PLAY* view_play;

    if ((fading != NULL) && (fading->submenu.process_status != mSM_PROCESS_WAIT)) {
        return TRUE;
    }

    if ((view >= 0) && (view < PLAYER_NUM)) {
        view_play = pc_world_at(s_draw_view_world[view]);
        if ((view_play != NULL) && (view_play->submenu.process_status != mSM_PROCESS_WAIT)) {
            return TRUE;
        }
    }

    return FALSE;
}

static void play_draw_fade_overlay(GAME_PLAY* play, GRAPH* graph, int view, int count) {
    Gfx* newdisp;
    Gfx* polydisp;
    rect rect;
    int full;

    if ((GETREG(HREG, 80) == 10) && (GETREG(HREG, 92) == 0)) {
        return;
    }

    /* Same frame the menu leaves idle, the wipe proc has already run. Do not
     * draw that card onto an open menu. */
    if (play_menu_blocks_fade(play, view) != FALSE) {
        return;
    }

    OPEN_DISP(graph);

    newdisp = NOW_POLY_OPA_DISP;
    polydisp = gfxopen(newdisp);
    gSPDisplayList(NEXT_OVERLAY_DISP, polydisp);

    full = (count <= 1);
    if (full) {
        rect.top = 0;
        rect.bottom = SCREEN_HEIGHT;
        rect.l = 0;
        rect.r = SCREEN_WIDTH;
        gDPNoOpTag(polydisp++, PC_NOOP_WIDESCREEN_STRETCH);
    } else {
        /* fbdemo_wipe1 then replaces the view projection with its own 4:3
         * matrix. Stretch lets that card fill the window; the scissor below
         * keeps it inside this view. */
        gDPNoOpTag(polydisp++, PC_NOOP_WIDESCREEN_STRETCH);
        play_view_rect(view, count, &rect);
    }

    if (full == FALSE) {
        View overlay_view;

        /* The fade card is replayed at the view's flush, long after the spot
         * scissor was set, so it must carry its own. A plain fade used to skip
         * this and cover whatever scissor was current (both halves for the last
         * view), which washed the other player's half out for the transition. */
        initView(&overlay_view, graph);
        overlay_view.flag = VIEW_UPDATE_ORTHOGRAPHIC | VIEW_UPDATE_SCISSOR;
        setScissorView(&overlay_view, &rect);
        showView1(&overlay_view, VIEW_UPDATE_ALL, &polydisp);
    }

    if (play->fb_wipe_mode == WIPE_MODE_MOVE) {
        play->fbdemo_wipe.wipe_procs.draw_proc(&play->fbdemo_wipe.wipe_data, &polydisp);
    }

    fbdemo_fade_draw(&play->color_fade, &polydisp);
    fade_rgba8888_draw(&polydisp, play->fade_color_value.rgba8888);
#ifdef TARGET_PC
    /* Some wipe textures do not read fully dark at their end position. The
     * post-finish hold exists so the last presented frame is covered; make
     * that certain. */
    if (S_se_endcheck_timeout > 115.0f) {
        fade_rgba8888_draw(&polydisp, 0xFF000000);
    }
#endif

    if (full == FALSE) {
        gDPNoOpTag(polydisp++, PC_NOOP_WIDESCREEN_STRETCH_OFF);
    }

    gSPEndDisplayList(polydisp++);
    gfxclose(newdisp, polydisp);
    SET_POLY_OPA_DISP(polydisp);

    CLOSE_DISP(graph);
}
#endif

static int makeBumpTexture(GAME_PLAY* play, GRAPH* graph1, GRAPH* graph2) {
    Gfx* newdisp;
    Gfx* polydisp;
    Gfx* rect_disp;
    Gfx* fbdemo_disp;
    Gfx* fb_disp;
    Gfx* old_fb;
    Gfx* poly;
#ifdef TARGET_PC
    int fade_drawn = FALSE;
    GAME_PLAY* fade_play = play_fading_play(play);
    int clip_fade = play_fade_is_split(play);
    int fade_view = play_fade_view(play);
#endif

    PC_DIAG(3, "makeBumpTexture: enter fb_mode=%d fb_wipe_mode=%d submenu.mode=%d\n",
            play->fb_mode, play->fb_wipe_mode, play->submenu.mode);
    OPEN_DISP(graph1);

#ifdef TARGET_PC
    /* One world fills the display-list buffers. Draw every view but the last,
     * showing each and restarting the lists, then build the last view where
     * the right half used to go. A wipe is drawn into the view that is
     * changing scenes, before that view is shown. */
    /* Visibility accumulates over every view: the next move pass runs once for
     * all players, so an actor seen by any of them must move. */
    Actor_info_cull_begin(play);
    s_draw_culled = play;
    if (play_split_live(play)) {
        int count = play_player_count();
        xyz_t host_eye = play->view.eye;
        xyz_t host_center = play->view.center;
        xyz_t host_up = play->view.up;
        int i;

        pc_world_fill_views(s_draw_view_world, count);
        for (i = 0; i < count - 1; i++) {
            xyz_t eye = host_eye;
            xyz_t center = host_center;

            play_view_camera(play, i, &eye, &center);
            /* A wipe only changes one world. Keep every view on the world its
             * player is in: collapsing the split onto this play showed the
             * town, and the other player's door walk, on an indoor half. */
            if (play_view_world(i) != 0) {
                play_draw_bound_view(play, graph1, i, count, (i > 0) ? &eye : NULL, (i > 0) ? &center : NULL);
            } else {
                play_draw_split_side(play, graph1, i, count, (i > 0) ? &eye : NULL, (i > 0) ? &center : NULL);
            }
            if ((clip_fade != FALSE) &&
                ((play_fade_every_view(fade_play) != FALSE) || (i == fade_view))) {
                play_draw_fade_overlay(fade_play, graph1, i, count);
                if (play_fade_every_view(fade_play) == FALSE) {
                    fade_drawn = TRUE;
                }
            }
            pc_world_settle_palette(play_view_world(i));
            pc_graph_flush_world(graph1);
            play_restart_disp(play, graph1);
        }

        /* Guest views write play->view. Put the host camera back: the last view
         * offsets from it, and the rest of the frame expects it. */
        setLookAtView(&play->view, &host_eye, &host_center, &host_up);
    }
#endif

#ifdef TARGET_PC
    if (clip_fade != FALSE) {
        if ((play_fade_every_view(fade_play) != FALSE) && (play_split_live(play) != FALSE)) {
            int count = play_player_count();

            if (count > 1) {
                play_draw_fade_overlay(fade_play, graph1, count - 1, count);
            } else if (fade_drawn == FALSE) {
                play_draw_fade_overlay(fade_play, graph1, 0, 1);
            }
        } else if (fade_drawn == FALSE) {
            int count = play_player_count();

            if (play_fade_every_view(fade_play) != FALSE) {
                count = 1;
            }
            play_draw_fade_overlay(fade_play, graph1, fade_view, count);
        }
    } else
#endif
    if ((GETREG(HREG, 80) != 10) || (GETREG(HREG, 92) != 0)) {
        newdisp = NOW_POLY_OPA_DISP;
        polydisp = gfxopen(newdisp);

        gSPDisplayList(NEXT_OVERLAY_DISP, polydisp);

#ifdef TARGET_PC
        /* Tell GXSetProjection to skip widescreen aspect correction for
         * transitions (wipes, fades) so they fill the entire screen.
         * See pc_platform.h for how the marker/flag mechanism works. */
        gDPNoOpTag(polydisp++, PC_NOOP_WIDESCREEN_STRETCH);
#endif

        if (play->fb_wipe_mode == WIPE_MODE_MOVE) {
            View view;
            rect rect;

            initView(&view, graph2);
            view.flag = VIEW_UPDATE_ORTHOGRAPHIC | VIEW_UPDATE_SCISSOR;
            rect.top = 0;
            rect.bottom = SCREEN_HEIGHT;
            rect.l = 0;
            rect.r = SCREEN_WIDTH;
            setScissorView(&view, &rect);
            showView1(&view, VIEW_UPDATE_ALL, &polydisp);
            play->fbdemo_wipe.wipe_procs.draw_proc(&play->fbdemo_wipe.wipe_data, &polydisp);
        }

        fbdemo_fade_draw(&play->color_fade, &polydisp);
        fade_rgba8888_draw(&polydisp, play->fade_color_value.rgba8888);
#ifdef TARGET_PC
        /* Same guarantee as the split path: the hold's frames must present
         * fully dark, whatever the wipe texture looks like at its end. */
        if (S_se_endcheck_timeout > 115.0f) {
            fade_rgba8888_draw(&polydisp, 0xFF000000);
        }
#endif

        gSPEndDisplayList(polydisp++);

        gfxclose(newdisp, polydisp);

        SET_POLY_OPA_DISP(polydisp);
    }

    if (play->fb_mode == FBDEMO_MODE_MOVE) {
        fbdemo_disp = NOW_POLY_OPA_DISP;
        fbdemo_draw(&fbdemo, &fbdemo_disp);
        SET_POLY_OPA_DISP(fbdemo_disp);
        return 1;
    }

    if (play->submenu.mode == mSM_MODE_PRERENDER_WAIT) {
        play->submenu.mode = mSM_MODE_PRERENDER_DONE;
        SETREG(SREG, 3, -(GETREG(SREG, 3)));
    } else if (play->submenu.mode >= mSM_MODE_NUM) {
        play->submenu.mode = mSM_MODE_IDLE;
    }

#ifdef TARGET_PC
    /* Side-slot menus follow the same mode chain without the prerender blit
     * (split never displays it): INIT -> WAIT on the open frame's draw, then
     * WAIT -> DONE on the next, matching the primary's cadence. The primary
     * keeps its own EFB-copy path above. */
    {
        int slot;

        for (slot = 1; slot < mSM_SLOT_NUM; slot++) {
            Submenu* menu = mSM_side_slot_p(slot);

            if (menu->menu_type == mSM_OVL_NONE) {
                continue;
            }

            if (menu->mode == mSM_MODE_PRERENDER_WAIT) {
                menu->mode = mSM_MODE_PRERENDER_DONE;
            } else if (menu->mode == mSM_MODE_PRERENDER_INIT) {
                menu->mode = mSM_MODE_PRERENDER_WAIT;
            } else if (menu->mode >= mSM_MODE_NUM) {
                menu->mode = mSM_MODE_IDLE;
            }
        }
    }

    /* An interior's menu lives on that play. This draw is the town's, so the
     * house map would stay in PRERENDER_INIT and the actor loop would never
     * come back. Same two-frame cadence as a side slot, no fullscreen blit. */
    {
        int world;

        for (world = 1; world < PC_WORLD_MAX; world++) {
            GAME_PLAY* child = pc_world_at(world);
            Submenu* menu;

            if (child == NULL) {
                continue;
            }

            menu = &child->submenu;
            if (menu->menu_type == mSM_OVL_NONE) {
                continue;
            }

            if (menu->mode == mSM_MODE_PRERENDER_WAIT) {
                menu->mode = mSM_MODE_PRERENDER_DONE;
            } else if (menu->mode == mSM_MODE_PRERENDER_INIT) {
                menu->mode = mSM_MODE_PRERENDER_WAIT;
            } else if (menu->mode >= mSM_MODE_NUM) {
                menu->mode = mSM_MODE_IDLE;
            }
        }
    }
#endif

#ifdef TARGET_PC
    /* With anyone else still on screen, keep drawing every view. The fullscreen
     * prerender blit is what blacked out the other view and hid the menu. An
     * indoor resident is already off the town actor list, so the extra count
     * is the wrong test. */
    if (play->submenu.mode == mSM_MODE_PRERENDER_DONE && play_player_count() <= 1) {
#else
    if (play->submenu.mode == mSM_MODE_PRERENDER_DONE) {
#endif
        poly = NOW_POLY_OPA_DISP;

#ifdef TARGET_PC
        /* The prerendered background is a fullscreen textured rect that goes
         * through ortho projection — enable stretch so it fills widescreen. */
        gDPNoOpTag(poly++, PC_NOOP_WIDESCREEN_STRETCH);
#endif
        gDPPipeSync(poly++);
        rect_disp = gfx_rect_moji(poly++);
        gDPSetOtherMode(rect_disp++,
                        G_AD_PATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_CONV | G_TF_POINT | G_TT_NONE | G_TL_TILE |
                            G_TD_CLAMP | G_TP_NONE | G_CYC_COPY | G_PM_NPRIMITIVE,
                        G_AC_NONE | G_ZS_PIXEL | G_RM_NOOP | G_RM_NOOP2);
        gDPSetTextureImage_Dolphin(rect_disp++, G_IM_FMT_I, G_IM_SIZ_16b, (2*SCREEN_HEIGHT), (2*SCREEN_WIDTH), prbuf);
        gDPSetTile_Dolphin(rect_disp++, G_DOLPHIN_TLUT_DEFAULT_MODE, 0, 0, 0, 0, 0, 0);
        gSPTextureRectangle(rect_disp++, 0, 0, ((2*SCREEN_WIDTH) - 1) << 2, ((2*SCREEN_HEIGHT) - 1) << 2, G_TX_RENDERTILE, 0, 0, (256) << G_TEXTURE_IMAGE_FRAC,
                            (256) << G_TEXTURE_IMAGE_FRAC);
#ifdef TARGET_PC
        /* Disable stretch so the inventory overlay UI gets 4:3 correction. */
        gDPNoOpTag(rect_disp++, PC_NOOP_WIDESCREEN_STRETCH_OFF);
#endif

        SET_POLY_OPA_DISP(rect_disp);
        return 1;
    }

    if ((GETREG(HREG, 80) != 10) || (GETREG(HREG, 85) != 0)) {
        PC_DIAG(3, "makeBumpTexture: before Actor_info_draw_actor\n");
#ifdef TARGET_PC
        {
            if (play_split_live(play)) {
                int count = play_player_count();
                int last = count - 1;
                xyz_t eye = play->view.eye;
                xyz_t center = play->view.center;
                xyz_t guest_eye = eye;
                xyz_t guest_center = center;
                xyz_t up = play->view.up;
                rect full;

                full.top = 0;
                full.bottom = SCREEN_HEIGHT;
                full.l = 0;
                full.r = SCREEN_WIDTH;
                play_view_camera(play, last, &guest_eye, &guest_center);

                if (play_view_world(last) != 0) {
                    play_draw_bound_view(play, graph1, last, count, &guest_eye, &guest_center);
                } else {
                    play_draw_split_side(play, graph1, last, count, &guest_eye, &guest_center);
                }
                pc_world_settle_palette(play_view_world(last));
                setLookAtView(&play->view, &eye, &center, &up);
                setScissorView(&play->view, &full);
                setupViewer(play);
            } else {
                pc_world_settle_palette(0);
                Actor_info_draw_actor(play, &play->actor_info);
            }
        }
#else
        Actor_info_draw_actor(play, &play->actor_info);
#endif
        PC_DIAG(3, "makeBumpTexture: Actor draw done, Camera2...\n");
        Camera2_draw(play);
#ifdef TARGET_PC
        /* The message is one full-screen font ortho. In a split that draws it
         * across both views. Put it in the talker's bottom band instead. */
        if (play_split_live(play)) {
            play_draw_talk_msg(play, mMsg_Get_base_window_p(), mPlib_demo_talker());
            play_draw_talk_msg(play, mMsg_side_window_p(), (PLAYER_ACTOR*)mDemo_side_player());
        } else
#endif
        {
            mMsg_Draw((GAME*)play);
        }
    }

    if ((GETREG(HREG, 80) != 10) || (GETREG(HREG, 93) != 0)) {
        Debug_Display_output(play);
    }

    if ((play->submenu.mode == mSM_MODE_PRERENDER_INIT) || (play->fb_mode == FBDEMO_MODE_CREATE)) {
        old_fb = NOW_POLY_OPA_DISP;
        fb_disp = gfxopen(old_fb);

        gSPDisplayList(NEXT_LIGHT_DISP, fb_disp);
        copy_efb_to_texture(&fb_disp, &prbuf);
        gSPEndDisplayList(fb_disp++);

        gfxclose(old_fb, fb_disp);

        SET_POLY_OPA_DISP(fb_disp);

        if (play->submenu.mode == mSM_MODE_PRERENDER_INIT) {
            play->submenu.mode = mSM_MODE_PRERENDER_WAIT;
            SETREG(SREG, 3, -(GETREG(SREG, 3)));
        } else {
            play->fb_mode = FBDEMO_MODE_INIT;
        }
        return 1;
    }

    CLOSE_DISP(graph1);

    return 1;
}

static void draw_version(GRAPH* graph) {
    JW_JUTReport(0x82, 0x172, 1, "[CopyDate:%.19s]", boot_copyDate);
    JW_JUTReport(0x82, 0x180, 1, "[Date:%s]", &__DateTime__);
    JW_JUTReport(0x82, 0x18E, 1, "[Creator:%s]", &__Creator__);
}

static void Game_play_draw(GAME_PLAY* play) {
    GRAPH* graph = play->game.graph;
    u8 fill_r = 0;
    u8 fill_g = 0;
    u8 fill_b = 0;


    // @BUG - when removing display list segment function present in N64, the devs went slightly too far
    // and removed setting the background fill color while in the main field area.
#if defined(BUGFIXES) || defined(TARGET_PC)
    if (Common_Get(field_type) == mFI_FIELDTYPE2_FG && mEv_CheckTitleDemo() != mEv_TITLEDEMO_STAFFROLL) {
        fill_r = play->kankyo.base_light.background_color[0];
        fill_g = play->kankyo.base_light.background_color[1];
        fill_b = play->kankyo.base_light.background_color[2];
    }
#endif

    DisplayList_initialize(graph, fill_r, fill_g, fill_b, &play->game);
    PC_DIAG(3, "Game_play_draw: DL_init done\n");

    if ((GETREG(HREG, 80) != 10) || (GETREG(HREG, 82) != 0)) {
        setupFog(play, graph);
        PC_DIAG(3, "Game_play_draw: fog done\n");
#ifdef TARGET_PC
        /* The last split frame leaves the scissor on the right half. One
         * player again draws the inventory figure in that half. */
        if (play_split_live(play) == FALSE) {
            play_set_view_player(play, 0, 1);
        }
#endif
        setupViewer(play);
        PC_DIAG(3, "Game_play_draw: viewer done\n");
        setupViewMatrix(play, graph, graph);
        PC_DIAG(3, "Game_play_draw: viewMtx done\n");

        if ((makeBumpTexture(play, graph, graph) == 1) && ((GETREG(HREG, 80) != 10) || (GETREG(HREG, 89) != 0))) {
            PC_DIAG(3, "Game_play_draw: bump done, drawing\n");
            watch_my_step_draw(play);
#ifdef TARGET_PC
            if (play_split_live(play)) {
                rect saved = play->view.screen;
                int count = play_player_count();
                int i;
                int indoor_menu = FALSE;
                GAME_PLAY* indoor_play = NULL;
                Submenu indoor_saved;

                for (i = 0; i < count; i++) {
                    play_set_view_player(play, i, count);
                    showSplitHudView(&play->view);
                    /* A half is 320x480 EFB for content x 0..160, so it needs the
                     * half ortho. A quadrant is 320x240 EFB, the retail mapping. */
                    pc_gx_hud_half = (count == 2);
                    banti_draw(play, i);
                    /* Indoor camera and light prompt. Same half ortho and view
                     * as the clock, and only the view that is inside. */
                    if (s_draw_view_world[i] != 0) {
                        navigate_camera_draw(play);
                    }
                    pc_gx_hud_half = FALSE;
                }

                /* The indoor menu lives on the house play. The draw that
                 * already works is the town slot loop, which reads this
                 * play's view. Park the house submenu there for that pass. */
                {
                    int world;

                    for (world = 1; world < PC_WORLD_MAX && indoor_menu == FALSE; world++) {
                        GAME_PLAY* child = pc_world_at(world);

                        if (child == NULL || play->submenu.process_status != mSM_PROCESS_WAIT) {
                            continue;
                        }
                        if (child->submenu.process_status == mSM_PROCESS_WAIT ||
                            child->submenu.mode < mSM_MODE_PRERENDER_DONE) {
                            continue;
                        }

                        indoor_saved = play->submenu;
                        play->submenu = child->submenu;
                        play->submenu.item_p = &play->submenu.items[0];
                        indoor_play = child;
                        indoor_menu = TRUE;
                    }
                }

                /* Per-slot menus: the primary (slot 0) and any open guest
                 * slots, each drawn in its opener's view. Each panel gets its
                 * own list: flush the previous pass, restart, fresh texture
                 * cache, band view, depth clear. */
                {
                    int slot;

                    for (slot = 0; slot < mSM_SLOT_NUM; slot++) {
                        Submenu* menu = (slot == 0) ? &play->submenu : mSM_side_slot_p(slot);
                        int opener_index;

                        if (menu->process_status == mSM_PROCESS_WAIT ||
                            menu->mode < mSM_MODE_PRERENDER_DONE) {
                            continue;
                        }

                        opener_index = play_actor_view_index(play, mPlib_menu_opener_for_slot(slot));
                        /* An indoor resident is not in the town actor list, so
                         * the actor lookup is P1's half. The save slot is theirs.
                         * A parked shop menu keeps that resident even when the
                         * opener slot is still the player outside. */
                        {
                            int resident = mPlib_menu_opener_priv_for_slot(slot);
                            int view;

                            if ((indoor_menu != FALSE) && (slot == 0) && (indoor_play != NULL)) {
                                int indoor_slot = pc_world_play_slot(indoor_play);

                                if (indoor_slot >= 0) {
                                    resident = indoor_slot;
                                }
                            }
                            view = pc_world_slot_view(resident);
                            if (view >= 0) {
                                opener_index = view;
                            }
                        }

                        pc_graph_flush_world(graph);
                        play_restart_disp(play, graph);
                        /* The town pass leaves the texture cache full. The panel's
                         * tex-edge test rejects the default 0x88 texture (136 < 144). */
                        emu64_refresh();
                        /* Menu geometry and its 3D hand both read this flag while
                         * the lists are built, so it is a host flag, not a noop. */
                        play_set_view_menu(play, count, opener_index);
                        showSplitHudView(&play->view);
                        OPEN_DISP(graph);
                        gDPNoOpTag(NOW_POLY_OPA_DISP++, PC_NOOP_CLEAR_DEPTH);
                        gDPNoOpTag(NOW_POLY_OPA_DISP++, PC_NOOP_MENU_DEPTH_OFF);
                        /* One player with a shop still loaded stays on this
                         * split draw. The outdoor inventory turns stretch off
                         * so the outfit viewport tracks the panel. Two players
                         * keep the half-band squeeze instead. */
                        if (count <= 1) {
                            gDPNoOpTag(NOW_POLY_OPA_DISP++, PC_NOOP_WIDESCREEN_STRETCH_OFF);
                        }
                        CLOSE_DISP(graph);
                        pc_gx_menu_mode = play_menu_mode(count, opener_index);
                        /* Side slots: swap the slot's Submenu in so the overlay
                         * draw sees its own state and its own per-slot
                         * statics; slot 0 runs in place. */
                        mSM_side_slot_begin(slot);
                        mPlib_menu_context_begin_slot(play, slot);
                        mSM_submenu_draw(&play->submenu, (GAME*)play);
                        mPlib_menu_context_end(play);
                        mSM_side_slot_end(slot);
                        pc_gx_menu_mode = 0;
                        OPEN_DISP(graph);
                        gDPNoOpTag(NOW_POLY_OPA_DISP++, PC_NOOP_MENU_DEPTH_ON);
                        CLOSE_DISP(graph);
                    }
                }

                if (indoor_menu != FALSE) {
                    indoor_play->submenu = play->submenu;
                    indoor_play->submenu.item_p = &indoor_play->submenu.items[0];
                    play->submenu = indoor_saved;
                    play->submenu.item_p = &play->submenu.items[0];
                }
                play->view.screen = saved;
            } else
#endif
            {
                banti_draw(play, 0);
#ifdef TARGET_PC
                mPlib_menu_context_begin(play);
#endif
                mSM_submenu_draw(&play->submenu, (GAME*)play);
#ifdef TARGET_PC
                mPlib_menu_context_end(play);
#endif
            }
        }
    }
#ifdef TARGET_PC
    play_draw_coop_overlays(play);
#endif
    if (zurumode_flag != 0) {
        switch (mEv_CheckTitleDemo()) {
            case mEv_TITLEDEMO_STAFFROLL:
            case mEv_TITLEDEMO_LOGO:
            default:
                draw_version(graph);
                break;

            case mEv_TITLEDEMO_NONE:
                break;
        }
    }
}

#ifdef TARGET_PC
static void play_move_child(GAME* child) {
    GAME* primary = pc_world_primary();

    if (primary != NULL) {
        int i;

        for (i = 0; i < MAXCONTROLLERS; i++) {
            child->pads[i] = primary->pads[i];
        }

        /* The interior's resident is not pad 0. The camera reads mcon before
         * the actor move borrows pads[0]. */
        {
            int slot = pc_world_play_slot((GAME_PLAY*)child);
            int pad = (slot >= 0) ? mPlib_pad_for_player_slot(slot) : 0;

            if ((pad > 0) && (pad < MAXCONTROLLERS)) {
                /* Talk and the shop read pad 0. This play's resident is the
                 * other controller. */
                child->pads[0] = child->pads[pad];
                mCon_calc(&child->mcon, (f32)child->pads[pad].now.stick_x, (f32)child->pads[pad].now.stick_y);
            } else {
                child->mcon = primary->mcon;
            }
        }
    }

    s_world_move_primary = FALSE;
    Game_play_move(child);
    /* The shop demo borrows this resident through the message. Leaving that
     * borrow up makes the next town menu read them instead of P1. */
    mPlib_demo_context_end((GAME_PLAY*)child);
    s_world_move_primary = TRUE;
}
#endif

extern void play_main(GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;

    PC_DIAG(5, "play_main: enter scene=%d frame=%d\n", play->scene_id, play->game_frame);
    game->doing_point = 0;
    game->doing_point_specific = 0x6E;
    fqrand();
    game->doing_point = 0;
    game->doing_point_specific = 0x78;
    mCon_main(game);
    game->doing_point = 0;
    game->doing_point_specific = 0x82;
    Debug_Display_init();

    if ((GETREG(HREG, 80) == 10) && (GETREG(HREG, 94) != 10)) {
        SETREG(HREG, 81, 1);
        SETREG(HREG, 82, 1);
        SETREG(HREG, 83, 1);
        SETREG(HREG, 84, 3);
        SETREG(HREG, 85, 1);
        SETREG(HREG, 86, 1);
        SETREG(HREG, 87, 1);
        SETREG(HREG, 88, 1);
        SETREG(HREG, 89, 1);
        SETREG(HREG, 90, 15);
        SETREG(HREG, 91, 1);
        SETREG(HREG, 92, 1);
        SETREG(HREG, 93, 1);
        SETREG(HREG, 94, 10);
    }

    game->doing_point = 0;
    game->doing_point_specific = 0x8C;

    if ((GETREG(HREG, 80) != 10) || (GETREG(HREG, 81) != 0)) {
        Game_play_move(game);
#ifdef TARGET_PC
        pc_world_move_rest(play_move_child);
#endif
    }
    game->doing_point = 0;
    game->doing_point_specific = 0xAA;
    Game_play_draw(play);

    game->doing_point = 0;
    game->doing_point_specific = 0xB4;

    {
        GRAPH* graph = game->graph;
        game_debug_draw_last(game, graph);
        game_draw_last(graph);
    }

    game->doing_point = 0;
    game->doing_point_specific = 0XBe;
}

static void Gameplay_Scene_Init(GAME_PLAY* play) {
    play->player_data = NULL;
    play->actor_data_num = 0;
    play->ctrl_actor_data_num = 0;
    play->obj_bank_data_num = 0;
    play->_20A8 = 0;

    mSc_data_bank_ct(play, &play->object_exchange);
    Global_light_ct(&play->global_light);
    Door_info_ct(&play->door_info);
    common_data_clear();
    Scene_ct(play, play->current_scene_data);
    mSc_decide_exchange_bank(&play->object_exchange);
}

static u8 mPl_SceneNo2SoundRoomType(int scene) {
    switch (scene) {
        case SCENE_MY_ROOM_S:
            return 1;

        case SCENE_NPC_HOUSE:
        case SCENE_SHOP0:
        case SCENE_BROKER_SHOP:
        case SCENE_POST_OFFICE:
        case SCENE_BUGGY:
        case SCENE_MY_ROOM_M:
        case SCENE_KAMAKURA:
        case SCENE_MY_ROOM_LL2:
        case SCENE_TENT:
            return 2;

        case SCENE_MY_ROOM_L:
        case SCENE_CONVENI:
        case SCENE_SUPER:
        case SCENE_DEPART:
        case SCENE_DEPART_2:
        case SCENE_MY_ROOM_LL1:
        case SCENE_COTTAGE_MY:
        case SCENE_POLICE_BOX:
            return 3;

        default:
            return 0;
    }
}

#ifdef TARGET_PC
extern void Game_play_set_sound_room(int scene) {
    sAdo_RoomType(mPl_SceneNo2SoundRoomType(scene));
}
#endif

static void Gameplay_Scene_Read(GAME_PLAY* play, s16 idx) {
    static Scene_Word_u* scene_word_data[SCENE_NUM] = {
        test01_info,
        test02_info,
        test03_info,
        water_test_info,
        test_step01_info,
        test04_info,
        npc_room01_info,
        test_fd_npc_land_info,
        field_tool_field_info,
        shop01_info,
        BG_TEST01_info,
        BG_TEST01_XLU_info,
        broker_shop_info,
        fg_tool_in_info,
        post_office_info,
        start_demo1_info,
        start_demo2_info,
        police_box_info,
        buggy_info,
        player_select_info,
        player_room_s_info,
        player_room_m_info,
        player_room_l_info,
        shop02_info,
        shop03_info,
        shop04_1f_info,
        test05_info,
        PLAYER_SELECT2_info,
        PLAYER_SELECT3_info,
        shop04_2f_info,
        event_notification_info,
        kamakura_info,
        field_tool_field_info,
        title_demo_info,
        PLAYER_SELECT4_info,
        museum_entrance_info,
        museum_picture_info,
        museum_fossil_info,
        museum_insect_info,
        museum_fish_info,
        player_room_ll1_info,
        player_room_ll2_info,
        p_room_bm_s_info,
        p_room_bm_m_info,
        p_room_bm_l_info,
        p_room_bm_ll1_info,
        NEEDLEWORK_info,
        player_room_island_info,
        npc_room_island_info,
        start_demo3_info,
        lighthouse_info,
        tent_info,
    };

    Scene_data_status_c* current = &scene_data_status[idx];

    current->unk13 = 0;
    play->scene_data_status = current;
    play->scene_id = idx;
    play->current_scene_data = scene_word_data[idx];
    current->unk13 = 0;
    Gameplay_Scene_Init(play);
    sAdo_RoomType(mPl_SceneNo2SoundRoomType(Save_Get(scene_no)));
}
