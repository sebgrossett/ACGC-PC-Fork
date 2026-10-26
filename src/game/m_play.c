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
#include "pc_pause_menu.h"
#include "pc_menu_util.h"
#include "libforest/emu64/emu64_wrapper.h"
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

static void Game_play_fbdemo_fade_in_move_end(GAME_PLAY* play) {
    Game_play_fbdemo_wipe_destroy(play);

    if (play->fb_mode == FBDEMO_MODE_MOVE) {
        fbdemo_cleanup(&fbdemo);
        play->fb_mode = FBDEMO_MODE_NONE;
        SetGameFrame(3);
    }
}

static void Game_play_fbdemo_fade_out_start_emu_move_end(GAME_PLAY* play) {
    GAME_GOTO_NEXT(&play->game, famicom_emu, FAMICOM_EMU);
}

static void Game_play_fbdemo_fade_out_game_end_move_end(GAME_PLAY* play) {
    GAME_GOTO_NEXT(&play->game, trademark, TRADEMARK);
}

static void Game_play_change_scene_move_end(GAME_PLAY* play) {
    game_goto_next_game_play(&play->game);
    Common_Set(last_scene_no, Save_Get(scene_no));
    Save_Set(scene_no, play->next_scene_no);
}

static void Game_play_fbdemo_wipe_move(GAME_PLAY* play) {
    fbdemo_wipe* wipe = &play->fbdemo_wipe;
    s16 wipet = FALSE;
    int isDone = TRUE;

    if (wipe->wipe_procs.isfinished_proc(&wipe->wipe_data) != 0) {
        if ((play->fb_fade_type != FADE_TYPE_IN) && (play->fb_fade_type != FADE_TYPE_EVENT)) {
            Game_play_dec_dt_timer(&S_se_endcheck_timeout, play);

            if ((sAdo_SeFadeoutCheck() == 0) && (S_se_endcheck_timeout > 0.0f)) {
                isDone = FALSE;
            } else {
                sAdo_Set_ongenpos_refuse_fg(1);
            }
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
                play->fb_fade_type = FADE_TYPE_NONE;
            }
        }
    } else {
        wipe->wipe_procs.move_proc(&wipe->wipe_data, game_GameFrame);
    }
}

static void Game_play_fbdemo_wipe_proc(GAME_PLAY* play) {
    static PLAY_WIPE_PROC wipe_proc[] = { (PLAY_WIPE_PROC)none_proc1, Game_play_fbdemo_wipe_create,
                                          Game_play_fbdemo_wipe_init, Game_play_fbdemo_wipe_move };

    if ((play->fb_wipe_mode == WIPE_MODE_NONE) && (play->fb_fade_type != FADE_TYPE_NONE)) {
        play->fb_wipe_mode = WIPE_MODE_CREATE;
    }

    wipe_proc[play->fb_wipe_mode](play);
}

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
    banti_dt();

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
    mNpc_ClearEventNpc();
    mNpc_ClearMaskNpc();

    mSM_submenu_dt(&play->submenu);
    play->submenu_ground_idx = -1;
    mSM_submenu_ovlptr_cleanup(&play->submenu);

    mPlib_Object_Exchange_keep_Player_dt(play);

    mHsRm_GetHuusuiRoom(NULL, Common_Get(player_no));
    mFM_Field_dt();
    mCD_toNextLand();
    mEA_CleanCardDLProgram();

    if (my_malloc_current == &my_malloc_func) {
        my_malloc_current = NULL;
    }

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

    mFI_ChangeClimate_ForEventNotice();
    mTM_time_init();
    sAdo_Set_ongenpos_refuse_fg(0);
    event_title_flag_on();
    mTD_rtc_set();
    mTM_set_season();
    mPlib_Clear_controller_data_for_title_demo();

    mSM_submenu_ovlptr_init(play);
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
    mNtc_set_auto_nwrite_data();
    banti_ct();
    watch_my_step_ct();
    event_title_flag_off();
    mEA_GetCardDLProgram();
}

static void play_session_wait_actors_add(GAME_PLAY* play) {
    int count = mPlib_extra_player_count();
    int i;

    for (i = 0; i < count; i++) {
        PLAYER_ACTOR* guest = mPlib_extra_player_at(i);
        int pad = mPlib_pad_for_player(guest);

        if ((pad > 0) && (pc_session_menu_is_open(pad) != FALSE)) {
            Actor_info_add_wait_actor((ACTOR*)guest);
        }
    }
}

/* A guest's side-slot menu: its opener waits like a session-menu opener. */
static void play_side_menu_wait_actors_add(GAME_PLAY* play) {
    int slot;

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
        mPlib_demo_context_begin(play);
        mDemo_Main(play);
        mPlib_demo_context_end(play);
        mDemo_side_sync_player(play);
        if (mDemo_CheckDemo() == FALSE) {
            mPlib_clear_demo_talker();
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
        play_session_wait_actors_add(play);
        play_side_menu_wait_actors_add(play);
        Actor_info_call_actor(play, &play->actor_info);
        Actor_info_clear_wait_actors();
        game->doing_point = 0;
        game->doing_point_specific = 0x91;
        game->doing_point = 1;
        mCoBG_CalcTimerDecalCircle();
        game->doing_point = 2;
#ifdef TARGET_PC
        mPlib_demo_context_begin(play);
#endif
        mMsg_Main(game);
#ifdef TARGET_PC
        mPlib_demo_context_end(play);
        mDemo_side_msg(play);
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
            if (mPlib_menu_opener() != NULL) {
                Actor_info_set_wait_actor((ACTOR*)mPlib_menu_opener());
            }
            play_session_wait_actors_add(play);
            play_side_menu_wait_actors_add(play);
            Actor_info_call_actor(play, &play->actor_info);
            Actor_info_clear_wait_actors();
            Actor_info_set_wait_actor(NULL);
            mCoBG_CalcTimerDecalCircle();
#ifdef TARGET_PC
            mPlib_demo_context_begin(play);
#endif
            mMsg_Main(game);
#ifdef TARGET_PC
            mPlib_demo_context_end(play);
            mDemo_side_msg(play);
#endif
        }
#endif
    }

    fbdemo_fade_move(&play->color_fade, game_GameFrame);
}

#ifdef TARGET_PC
#define PLAY_COOP_LEAVE_TIMEOUT 180.0f
static f32 s_coop_leave_wait[PLAYER_NUM];

/* A frame where a player can join or leave without interrupting a menu, demo,
 * fade or scene wipe. A guest's side-slot menu counts: its opener must not be
 * deleted or spawned over while it edits its pockets. */
static int play_coop_safe(GAME_PLAY* play) {
    int slot;

    for (slot = 1; slot < mSM_SLOT_NUM; slot++) {
        if (mSM_side_slot_p(slot)->menu_type != mSM_OVL_NONE) {
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

    for (pad = 1; pad < PLAYER_NUM; pad++) {
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
        if (Actor_info_save_game((GAME*)play) == FALSE) {
            OSReport("[PC] co-op leave: save write failed\n");
        }
        return;
    }

    if (safe != FALSE) {
        Actor_info_join_guest((GAME*)play);
    }
}
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
        Game_play_camera_proc(play);
        game->doing_point = 2;
        mPO_business_proc(play);
        game->doing_point = 3;
        mTRC_move(game);
        game->doing_point = 4;
        Balloon_move(play);
        mEnv_ManagePointLight(play, &play->kankyo, &play->global_light);
    }

    game->doing_point = 0;
    game->doing_point_specific = 0x93;
    game->doing_point = 1;
    Global_kankyo_set(play, &play->kankyo, &play->global_light);
    game->doing_point = 2;
    mEnv_WindMove();
    game->doing_point = 3;
    watch_my_step_move(play);
    game->doing_point = 4;
    banti_move(play);
    game->doing_point = 5;
    title_demo_move(play);
#ifdef TARGET_PC
    play_coop_session_proc(play);
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
    int count = 1 + mPlib_extra_player_count();
    int has_overlay = FALSE;
    int i;

    for (i = 1; i < count; i++) {
        PLAYER_ACTOR* guest = mPlib_extra_player_at(i - 1);
        int pad = mPlib_pad_for_player(guest);

        if ((pad > 0) && (pc_session_menu_is_open(pad) != FALSE)) {
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

    for (i = 1; i < count; i++) {
        PLAYER_ACTOR* guest = mPlib_extra_player_at(i - 1);
        int pad = mPlib_pad_for_player(guest);

        if ((pad > 0) && (pc_session_menu_is_open(pad) != FALSE)) {
            int view_index = play_actor_view_index(play, guest);

            play_set_view_menu(play, count, view_index);
            showSplitHudView(&play->view);
            pc_menu_band_l = play->view.screen.l;
            pc_menu_band_t = play->view.screen.top;
            pc_menu_band_r = play->view.screen.r;
            pc_menu_band_b = play->view.screen.bottom;
            pc_session_menu_draw(&play->game, pad);
        }
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
    count = 1 + mPlib_extra_player_count();
    index = play_actor_view_index(play, talker);
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
    if (count <= 2) {
        return (index == 0) ? 1 : 2;
    }

    return 3 + index;
}

/* One player, or the selected resident plus one per extra pad. */
static int play_player_count(void) {
    return 1 + mPlib_extra_player_count();
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
    play_set_view_player(play, index, count);
    if ((eye != NULL) && (center != NULL)) {
        setLookAtView(&play->view, eye, center, &play->view.up);
    }

    setupFog(play, graph);
    setupViewer(play);
    setupViewMatrix(play, graph, graph);
    /* Field draw follows the view's own player instead of the selected resident. */
    mPlib_set_view_player_actor(play_view_player(play, index));
    Actor_info_draw_actor(play, &play->actor_info);
    mPlib_set_view_player_actor(NULL);
}

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

static void play_guest_camera_reset(int pad) {
    if ((pad < 0) || (pad >= PLAYER_NUM)) {
        return;
    }

    s_follow_ready[pad] = FALSE;
    s_ofs_ready[pad] = FALSE;
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

    if (play->camera.now_main_index == CAMERA2_PROCESS_NORMAL) {
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
        xyz_t_add(guest_pos, &s_eye_ofs[pad], &guest_eye);
        xyz_t_add(guest_pos, &s_center_ofs[pad], &guest_center);
    } else {
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

/* A door wipe uses one full-screen list. A second town during that wipe is what froze the zoom. */
static int play_split_live(GAME_PLAY* play) {
    PLAYER_ACTOR* host = get_player_actor_withoutCheck(play);

    if ((host == NULL) || (mPlib_extra_player_count() == 0)) {
        return FALSE;
    }

    if ((play->fb_wipe_mode != WIPE_MODE_NONE) || (play->fb_mode != FBDEMO_MODE_NONE)) {
        return FALSE;
    }

    return TRUE;
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

    PC_DIAG(3, "makeBumpTexture: enter fb_mode=%d fb_wipe_mode=%d submenu.mode=%d\n",
            play->fb_mode, play->fb_wipe_mode, play->submenu.mode);
    OPEN_DISP(graph1);

#ifdef TARGET_PC
    /* One world fills the display-list buffers. Draw every view but the last,
     * showing each and restarting the lists, then build the last view where
     * the right half used to go. A wipe stays one full list. */
    /* Visibility accumulates over every view: the next move pass runs once for
     * all players, so an actor seen by any of them must move. */
    Actor_info_cull_begin(play);
    if (play_split_live(play)) {
        int count = play_player_count();
        xyz_t host_eye = play->view.eye;
        xyz_t host_center = play->view.center;
        xyz_t host_up = play->view.up;
        int i;

        for (i = 0; i < count - 1; i++) {
            xyz_t eye = host_eye;
            xyz_t center = host_center;

            play_view_camera(play, i, &eye, &center);
            play_draw_split_side(play, graph1, i, count, (i > 0) ? &eye : NULL, (i > 0) ? &center : NULL);
            pc_graph_flush_world(graph1);
            play_restart_disp(play, graph1);
        }

        /* Guest views write play->view. Put the host camera back: the last view
         * offsets from it, and the rest of the frame expects it. */
        setLookAtView(&play->view, &host_eye, &host_center, &host_up);
    }
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
#endif

#ifdef TARGET_PC
    /* With extra players, keep drawing every view. The fullscreen prerender
     * blit is what blacked out the other view and hid the menu. */
    if (play->submenu.mode == mSM_MODE_PRERENDER_DONE && mPlib_extra_player_count() == 0) {
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

                play_draw_split_side(play, graph1, last, count, &guest_eye, &guest_center);
                setLookAtView(&play->view, &eye, &center, &up);
                setScissorView(&play->view, &full);
                setupViewer(play);
            } else {
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
        setupViewer(play);
        PC_DIAG(3, "Game_play_draw: viewer done\n");
        setupViewMatrix(play, graph, graph);
        PC_DIAG(3, "Game_play_draw: viewMtx done\n");

        if ((makeBumpTexture(play, graph, graph) == 1) && ((GETREG(HREG, 80) != 10) || (GETREG(HREG, 89) != 0))) {
            PC_DIAG(3, "Game_play_draw: bump done, drawing\n");
            watch_my_step_draw(play);
#ifdef TARGET_PC
            if (mPlib_extra_player_count() != 0) {
                rect saved = play->view.screen;
                int count = play_player_count();
                int i;

                for (i = 0; i < count; i++) {
                    play_set_view_player(play, i, count);
                    showSplitHudView(&play->view);
                    /* A half is 320x480 EFB for content x 0..160, so it needs the
                     * half ortho. A quadrant is 320x240 EFB, the retail mapping. */
                    pc_gx_hud_half = (count == 2);
                    banti_draw(play, i);
                    pc_gx_hud_half = FALSE;
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
