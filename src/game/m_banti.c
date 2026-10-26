#include "m_banti.h"

#include "m_play.h"
#include "libultra/libultra.h"
#include "m_player_lib.h"
#include "m_demo.h"
#include "sys_matrix.h"
#include "m_font.h"
#include "m_common_data.h"
#ifdef TARGET_PC
#include "pc_platform.h"
#endif

static Banti_c banti;

static void banti_animation_init_hiduke(Banti_anim_c* anim);
static void banti_animation_init_jikan(Banti_anim_c* anim);

extern cKF_Skeleton_R_c cKF_bs_r_clk_hiniti;
extern cKF_Skeleton_R_c cKF_bs_r_clk_jikan;

extern void banti_ct() {
    int i;

    bzero(&banti, sizeof(Banti_c));

    for (i = 0; i < banti_VIEW_NUM; i++) {
        banti.view[i].addressable_type = 0;
        banti.view[i].disabled = 0;
        banti.view[i].timer = 0.0f;
        banti.view[i].alpha = 0.0f;
        banti.view[i].disp_left = 0;
        banti.view[i].move_left = 0;
        banti.view[i].move_timer = 0.0f;
    }

    cKF_SkeletonInfo_R_ct(&banti.month_anim.keyframe, &cKF_bs_r_clk_hiniti, NULL, banti.month_anim.work,
                          banti.month_anim.morph);
    banti_animation_init_hiduke(&banti.month_anim);
    banti.month_anim.keyframe.frame_control.speed = 0.0f;

    cKF_SkeletonInfo_R_ct(&banti.day_anim.keyframe, &cKF_bs_r_clk_hiniti, NULL, banti.day_anim.work,
                          banti.day_anim.morph);
    banti_animation_init_hiduke(&banti.day_anim);
    banti.day_anim.keyframe.frame_control.speed = 0.0f;

    cKF_SkeletonInfo_R_ct(&banti.hour_upper_anim.keyframe, &cKF_bs_r_clk_jikan, NULL, banti.hour_upper_anim.work,
                          banti.hour_upper_anim.morph);
    banti_animation_init_jikan(&banti.hour_upper_anim);
    banti.hour_upper_anim.keyframe.frame_control.speed = 0.0f;

    cKF_SkeletonInfo_R_ct(&banti.hour_lower_anim.keyframe, &cKF_bs_r_clk_jikan, NULL, banti.hour_lower_anim.work,
                          banti.hour_lower_anim.morph);
    banti_animation_init_jikan(&banti.hour_lower_anim);
    banti.hour_lower_anim.keyframe.frame_control.speed = 0.0f;

    cKF_SkeletonInfo_R_ct(&banti.min_upper_anim.keyframe, &cKF_bs_r_clk_jikan, NULL, banti.min_upper_anim.work,
                          banti.min_upper_anim.morph);
    banti_animation_init_jikan(&banti.min_upper_anim);
    banti.min_upper_anim.keyframe.frame_control.speed = 0.0f;

    cKF_SkeletonInfo_R_ct(&banti.min_lower_anim.keyframe, &cKF_bs_r_clk_jikan, NULL, banti.min_lower_anim.work,
                          banti.min_lower_anim.morph);
    banti_animation_init_jikan(&banti.min_lower_anim);
    banti.min_lower_anim.keyframe.frame_control.speed = 0.0f;

    lbRTC_TimeCopy(&banti.next_time, Common_GetPointer(time.rtc_time));
    lbRTC_TimeCopy(&banti.displayed_time, Common_GetPointer(time.rtc_time));
}

extern void banti_dt() {
    cKF_SkeletonInfo_R_dt(&banti.month_anim.keyframe);
    cKF_SkeletonInfo_R_dt(&banti.day_anim.keyframe);
    cKF_SkeletonInfo_R_dt(&banti.hour_upper_anim.keyframe);
    cKF_SkeletonInfo_R_dt(&banti.hour_lower_anim.keyframe);
    cKF_SkeletonInfo_R_dt(&banti.min_upper_anim.keyframe);
    cKF_SkeletonInfo_R_dt(&banti.min_lower_anim.keyframe);
}

/* Per-view slide state machine. `screen_pos` is the view player's position in
 * 320x240 view space (retail thresholds); the PC driver projects through that
 * view's own camera, the retail path projects through the one camera. */
static void banti_view_slide_disp(Banti_view_c* view, f32 dt, xyz_t* screen_pos) {
    if (screen_pos->x >= 180.0f && screen_pos->y >= 150.0f) {
        if (view->move_left == FALSE) {
            view->move_left = TRUE;
            view->move_timer = 0.0f;
        }

        view->move_timer += dt;

        if (view->move_timer > 10.0f) {
            view->move_timer = 10.0f;

            if (view->alpha < 0.01f) {
                view->disp_left = TRUE;
                view->disabled = FALSE;
            } else if (view->disp_left == FALSE) {
                view->disabled = TRUE;
            }
        }
    } else {
        if (view->move_left == TRUE) {
            view->move_left = FALSE;
            view->move_timer = 0.0f;
        }

        view->move_timer += dt;

        if (view->move_timer > 10.0f) {
            view->move_timer = 10.0f;

            if (view->alpha < 0.01f) {
                view->disp_left = FALSE;
                view->disabled = FALSE;
            } else if (view->disp_left == TRUE) {
                view->disabled = TRUE;
            }
        }
    }
}

/* PC entry: m_play.c projects each view's player through that view's camera. */
extern void banti_view_slide(int view_idx, xyz_t* screen_pos, f32 dt) {
    if (view_idx < 0 || view_idx >= banti_VIEW_NUM) {
        return;
    }

    banti_view_slide_disp(&banti.view[view_idx], dt, screen_pos);
}

static void banti_calc_disp_alpha_rate(GAME_PLAY* play, Banti_view_c* view, PLAYER_ACTOR* player) {
    /* The retail forced fade was global: any demo faded the one clock. With
     * per-player demos, only the demo's own player (or an unattributable one)
     * forces the fade; the other player's clock follows their own state. */
    int demo_mine = FALSE;

    if (mDemo_CheckDemo()) {
        PLAYER_ACTOR* talker = mPlib_demo_talker();
        ACTOR* side_talker = mDemo_side_player();

        demo_mine = (talker == player) || (talker == NULL) ||
                    (side_talker != NULL && (PLAYER_ACTOR*)side_talker == player);
    }

    if (demo_mine || mEv_IsTitleDemo() || view->disabled) {
        view->addressable_type = mPlayer_ADDRESSABLE_FALSE_MOVEMENT;
        add_calc(&view->alpha, 0.0f, 1.0f - sqrtf(0.8), 0.1f,
                 0.005f); /* quickly fade out on demo screen or title or force disabled */
    } else if (view->addressable_type == mPlayer_ADDRESSABLE_TRUE &&
               mSM_player_menu_open_by_slot(play, mPlib_player_slot_of(player)) == FALSE) {
        add_calc(&view->alpha, 1.0f, 1.0f - sqrtf(0.8), 0.0425f, 0.0005f); /* fade in */
    } else {
        add_calc(&view->alpha, 0.0f, 1.0f - sqrtf(0.8), 0.0425f, 0.0005f); /* fade out */
    }
}

extern cKF_Animation_R_c cKF_ba_r_clk_hiniti;

static void banti_animation_init_hiduke(Banti_anim_c* anim) {
    cKF_SkeletonInfo_R_init(&anim->keyframe, anim->keyframe.skeleton, &cKF_ba_r_clk_hiniti, 1.0f, 15.0f, 1.0f, 0.5f,
                            0.0f, cKF_FRAMECONTROL_STOP, NULL);
}

extern cKF_Animation_R_c cKF_ba_r_clk_jikan;

static void banti_animation_init_jikan(Banti_anim_c* anim) {
    cKF_SkeletonInfo_R_init(&anim->keyframe, anim->keyframe.skeleton, &cKF_ba_r_clk_jikan, 1.0f, 15.0f, 1.0f, 0.5f,
                            0.0f, cKF_FRAMECONTROL_STOP, NULL);
}

static void banti_anime_proc() {
    if (cKF_SkeletonInfo_R_play(&banti.month_anim.keyframe) == cKF_STATE_STOPPED) {
        banti.month_anim.keyframe.frame_control.current_frame = 1.0f;
        banti.month_anim.keyframe.frame_control.speed = 0.0f;
        banti.month_anim.state = banti_ANIM_STATE_STOPPED;
    }

    if (cKF_SkeletonInfo_R_play(&banti.day_anim.keyframe) == cKF_STATE_STOPPED) {
        banti.day_anim.keyframe.frame_control.current_frame = 1.0f;
        banti.day_anim.keyframe.frame_control.speed = 0.0f;
        banti.day_anim.state = banti_ANIM_STATE_STOPPED;
    }

    if (cKF_SkeletonInfo_R_play(&banti.hour_upper_anim.keyframe) == cKF_STATE_STOPPED) {
        banti.hour_upper_anim.keyframe.frame_control.current_frame = 1.0f;
        banti.hour_upper_anim.keyframe.frame_control.speed = 0.0f;
        banti.hour_upper_anim.state = banti_ANIM_STATE_STOPPED;
    }

    if (cKF_SkeletonInfo_R_play(&banti.hour_lower_anim.keyframe) == cKF_STATE_STOPPED) {
        banti.hour_lower_anim.keyframe.frame_control.current_frame = 1.0f;
        banti.hour_lower_anim.keyframe.frame_control.speed = 0.0f;
        banti.hour_lower_anim.state = banti_ANIM_STATE_STOPPED;
    }

    if (cKF_SkeletonInfo_R_play(&banti.min_upper_anim.keyframe) == cKF_STATE_STOPPED) {
        banti.min_upper_anim.keyframe.frame_control.current_frame = 1.0f;
        banti.min_upper_anim.keyframe.frame_control.speed = 0.0f;
        banti.min_upper_anim.state = banti_ANIM_STATE_STOPPED;
    }

    if (cKF_SkeletonInfo_R_play(&banti.min_lower_anim.keyframe) == cKF_STATE_STOPPED) {
        banti.min_lower_anim.keyframe.frame_control.current_frame = 1.0f;
        banti.min_lower_anim.keyframe.frame_control.speed = 0.0f;
        banti.min_lower_anim.state = banti_ANIM_STATE_STOPPED;
    }
}

static void banti_chk_anime_start() {
    lbRTC_time_c* rtc_time = Common_GetPointer(time.rtc_time);
    u8 banti_hour;
    u8 rtc_hour;
    u8 banti_min;
    u8 rtc_min;

    int disp0;
    int disp1;
    int work0;
    int work1;

    if (banti.month_anim.state == banti_ANIM_STATE_STOPPED &&
        banti.displayed_time.month != Common_GetPointer(time.rtc_time)->month) {
        banti_animation_init_hiduke(&banti.month_anim);
        banti.month_anim.state = banti_ANIM_STATE_PLAYING;
    }

    if (banti.day_anim.state == banti_ANIM_STATE_STOPPED &&
        banti.displayed_time.day != Common_GetPointer(time.rtc_time)->day) {
        banti_animation_init_hiduke(&banti.day_anim);
        banti.day_anim.state = banti_ANIM_STATE_PLAYING;
    }

    if (banti.displayed_time.hour == 0 || banti.displayed_time.hour == 12) {
        work0 = 12;
    } else if (banti.displayed_time.hour > 11) {
        work0 = banti.displayed_time.hour - 12;
    } else {
        work0 = banti.displayed_time.hour;
    }

    if (Common_Get(time.rtc_time.hour) == 0 || Common_Get(time.rtc_time.hour) == 12) {
        work1 = 12;
    } else if (Common_Get(time.rtc_time.hour) > 11) {
        work1 = Common_Get(time.rtc_time.hour) - 12;
    } else {
        work1 = Common_Get(time.rtc_time.hour);
    }

    disp0 = work0 / 10;
    disp1 = work1 / 10;
    if (banti.hour_upper_anim.state == banti_ANIM_STATE_STOPPED && disp0 != disp1) {
        banti_animation_init_jikan(&banti.hour_upper_anim);
        banti.hour_upper_anim.state = banti_ANIM_STATE_PLAYING;
    }

    disp0 = work0 % 10;
    disp1 = work1 % 10;
    if (banti.hour_lower_anim.state == banti_ANIM_STATE_STOPPED && disp0 != disp1) {
        banti_animation_init_jikan(&banti.hour_lower_anim);
        banti.hour_lower_anim.state = banti_ANIM_STATE_PLAYING;
    }

    disp0 = banti.displayed_time.min / 10;
    disp1 = Common_GetPointer(time.rtc_time)->min / 10;
    if (banti.min_upper_anim.state == banti_ANIM_STATE_STOPPED && disp0 != disp1) {
        banti_animation_init_jikan(&banti.min_upper_anim);
        banti.min_upper_anim.state = banti_ANIM_STATE_PLAYING;
    }

    disp0 = banti.displayed_time.min % 10;
    disp1 = Common_GetPointer(time.rtc_time)->min % 10;
    if (banti.min_lower_anim.state == banti_ANIM_STATE_STOPPED && disp0 != disp1) {
        banti_animation_init_jikan(&banti.min_lower_anim);
        banti.min_lower_anim.state = banti_ANIM_STATE_PLAYING;
    }
}

static void banti_time_check() {
    int pre_state;
    int post_state;

    pre_state = banti.month_anim.state | banti.day_anim.state | banti.hour_upper_anim.state |
                banti.hour_lower_anim.state | banti.min_upper_anim.state | banti.min_lower_anim.state;

    if (pre_state == banti_ANIM_STATE_STOPPED) {
        banti_chk_anime_start();
    }

    banti_anime_proc();

    post_state = banti.month_anim.state | banti.day_anim.state | banti.hour_upper_anim.state |
                 banti.hour_lower_anim.state | banti.min_upper_anim.state | banti.min_lower_anim.state;

    if (pre_state != post_state) {
        if (pre_state == banti_ANIM_STATE_STOPPED) {
            lbRTC_TimeCopy(&banti.next_time, Common_GetPointer(time.rtc_time));
        } else {
            lbRTC_TimeCopy(&banti.displayed_time, &banti.next_time);
        }
    }
}

extern void banti_move(GAME_PLAY* play) {
    int count = 1;
    int i;

#ifdef TARGET_PC
    count = 1 + mPlib_extra_player_count();
#else
    /* Retail path: one view, the slide projects through the one camera. */
    {
        xyz_t player_pos_screen;

        Game_play_Projection_Trans(play, &get_player_actor_withoutCheck(play)->actor_class.world.position,
                                   &player_pos_screen);
        banti_view_slide(0, &player_pos_screen, (f32)play->game.graph->dt_num_60fps_frames);
    }
#endif

    /* Per-view fade: each clock follows its own player's addressable state and
     * menu. The retail clock read only the selected resident, so both clocks
     * faded together. The PC slide is driven per view from m_play.c before
     * this call, because each view needs its own camera. */
    for (i = 0; i < count; i++) {
        PLAYER_ACTOR* player = (i == 0) ? GET_PLAYER_ACTOR_NOW() : mPlib_extra_player_at(i - 1);
        int addressable_type =
            (player != NULL) ? player->address_able_display : mPlayer_ADDRESSABLE_FALSE_MOVEMENT;

        if (banti.view[i].addressable_type != addressable_type) {
            int update = FALSE;

            banti.view[i].timer += (f32)play->game.graph->dt_num_60fps_frames;

            if (addressable_type == mPlayer_ADDRESSABLE_FALSE_READY_NET) {
                if (banti.view[i].timer > 50.0f) {
                    update = TRUE;
                }
            } else if (banti.view[i].addressable_type != mPlayer_ADDRESSABLE_TRUE) {
                if (banti.view[i].timer > 30.0f || addressable_type == mPlayer_ADDRESSABLE_FALSE_TALKING) {
                    update = TRUE;
                }
            } else if (banti.view[i].timer > 50.0f || addressable_type == mPlayer_ADDRESSABLE_FALSE_TALKING) {
                update = TRUE;
            }

            if (update == TRUE) {
                banti.view[i].timer = 0.0f;
                banti.view[i].addressable_type = addressable_type;
            }
        } else {
            banti.view[i].timer = 0.0f;
        }

        banti_calc_disp_alpha_rate(play, &banti.view[i], player);
    }

    banti_time_check();
}

static int banti_draw_before(GAME* game, cKF_SkeletonInfo_R_c* keyframe, int joint_idx, Gfx** joint_shape,
                             u8* joint_flags, void* arg, s_xyz* joint_rot, xyz_t* joint_pos) {
    Gfx** gfx_pp;
    Mtx* mtx;
    Banti_callback_c* callback_data = (Banti_callback_c*)arg;

    gfx_pp = callback_data->gfx_pp;
    mtx = callback_data->mtx;

    Matrix_softcv3_mult(joint_pos, joint_rot);
    if (*joint_shape != NULL) {
        _Matrix_to_Mtx(mtx);

        gSPMatrix(gfx_pp[0]++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

        if (callback_data->state == banti_ANIM_STATE_STOPPED) {
            if (joint_idx != 2) {
                gSPDisplayList(gfx_pp[0]++, *joint_shape);
            }
        } else {
            gSPDisplayList(gfx_pp[0]++, *joint_shape);
        }

        callback_data->mtx++;
    }

    return 0;
}

extern u8 clk_win_suuji1_TA_tex_txt[];
extern u8 clk_win_suuji2_TA_tex_txt[];
extern u8 clk_win_suuji3_TA_tex_txt[];
extern u8 clk_win_suuji4_TA_tex_txt[];
extern u8 clk_win_suuji5_TA_tex_txt[];
extern u8 clk_win_suuji6_TA_tex_txt[];
extern u8 clk_win_suuji7_TA_tex_txt[];
extern u8 clk_win_suuji8_TA_tex_txt[];
extern u8 clk_win_suuji9_TA_tex_txt[];
extern u8 clk_win_suuji10_TA_tex_txt[];
extern u8 clk_win_suuji11_TA_tex_txt[];
extern u8 clk_win_suuji12_TA_tex_txt[];
extern u8 clk_win_suuji13_TA_tex_txt[];
extern u8 clk_win_suuji14_TA_tex_txt[];
extern u8 clk_win_suuji15_TA_tex_txt[];
extern u8 clk_win_suuji16_TA_tex_txt[];
extern u8 clk_win_suuji17_TA_tex_txt[];
extern u8 clk_win_suuji18_TA_tex_txt[];
extern u8 clk_win_suuji19_TA_tex_txt[];
extern u8 clk_win_suuji20_TA_tex_txt[];
extern u8 clk_win_suuji21_TA_tex_txt[];
extern u8 clk_win_suuji22_TA_tex_txt[];
extern u8 clk_win_suuji23_TA_tex_txt[];
extern u8 clk_win_suuji24_TA_tex_txt[];
extern u8 clk_win_suuji25_TA_tex_txt[];
extern u8 clk_win_suuji26_TA_tex_txt[];
extern u8 clk_win_suuji27_TA_tex_txt[];
extern u8 clk_win_suuji28_TA_tex_txt[];
extern u8 clk_win_suuji29_TA_tex_txt[];
extern u8 clk_win_suuji30_TA_tex_txt[];
extern u8 clk_win_suuji31_TA_tex_txt[];

static void banti_draw_hiduke_sub(Gfx** gfx_pp, GAME* game, Banti_anim_c* anim, int tex0_idx, int tex1_idx,
                                  int pos_idx) {
    static u8* hiniti_tex_table[31] = {
        clk_win_suuji1_TA_tex_txt,  clk_win_suuji2_TA_tex_txt,  clk_win_suuji3_TA_tex_txt,  clk_win_suuji4_TA_tex_txt,
        clk_win_suuji5_TA_tex_txt,  clk_win_suuji6_TA_tex_txt,  clk_win_suuji7_TA_tex_txt,  clk_win_suuji8_TA_tex_txt,
        clk_win_suuji9_TA_tex_txt,  clk_win_suuji10_TA_tex_txt, clk_win_suuji11_TA_tex_txt, clk_win_suuji12_TA_tex_txt,
        clk_win_suuji13_TA_tex_txt, clk_win_suuji14_TA_tex_txt, clk_win_suuji15_TA_tex_txt, clk_win_suuji16_TA_tex_txt,
        clk_win_suuji17_TA_tex_txt, clk_win_suuji18_TA_tex_txt, clk_win_suuji19_TA_tex_txt, clk_win_suuji20_TA_tex_txt,
        clk_win_suuji21_TA_tex_txt, clk_win_suuji22_TA_tex_txt, clk_win_suuji23_TA_tex_txt, clk_win_suuji24_TA_tex_txt,
        clk_win_suuji25_TA_tex_txt, clk_win_suuji26_TA_tex_txt, clk_win_suuji27_TA_tex_txt, clk_win_suuji28_TA_tex_txt,
        clk_win_suuji29_TA_tex_txt, clk_win_suuji30_TA_tex_txt, clk_win_suuji31_TA_tex_txt
    };

    static xyz_t set_pos_table[2] = { { 64.0f, -70.0f, 0.0f }, { 94.0f, -70.0f, 0.0f } };

    GRAPH* g = game->graph;
    xyz_t* set_pos = &set_pos_table[pos_idx];
    Mtx* m;
    int joint;
    Banti_callback_c callback_data;

    Matrix_push();
    Matrix_translate(set_pos->x, set_pos->y, set_pos->z, MTX_MULT);

    m = (Mtx*)GRAPH_ALLOC_TYPE(g, Mtx, anim->keyframe.skeleton->num_shown_joints);
    callback_data.mtx = m;

    if (m != NULL) {
        joint = 0;
        callback_data.gfx_pp = gfx_pp;
        callback_data.state = anim->state;

        gSPSegment(gfx_pp[0]++, G_MWO_SEGMENT_8, hiniti_tex_table[tex0_idx]);
        gSPSegment(gfx_pp[0]++, G_MWO_SEGMENT_9, hiniti_tex_table[tex1_idx]);

        cKF_Si3_draw_SV_R_child(game, &anim->keyframe, &joint, &banti_draw_before, NULL, &callback_data, NULL);
    }

    Matrix_pull();
}

static void banti_draw_hiduke(Gfx** gfx_pp, GAME_PLAY* play, int alpha) {
    u8 a = alpha;

    gDPSetPrimColor(gfx_pp[0]++, 0, a, 235, 255, 120, a);
    gDPSetEnvColor(gfx_pp[0]++, 80, 40, 40, a);

    banti_draw_hiduke_sub(gfx_pp, (GAME*)play, &banti.month_anim, banti.displayed_time.month - 1,
                          banti.next_time.month - 1, 0);
    banti_draw_hiduke_sub(gfx_pp, (GAME*)play, &banti.day_anim, banti.displayed_time.day - 1, banti.next_time.day - 1,
                          1);
}

extern u8 clk_win_jikan0_TA_tex_txt[];
extern u8 clk_win_jikan1_TA_tex_txt[];
extern u8 clk_win_jikan2_TA_tex_txt[];
extern u8 clk_win_jikan3_TA_tex_txt[];
extern u8 clk_win_jikan4_TA_tex_txt[];
extern u8 clk_win_jikan5_TA_tex_txt[];
extern u8 clk_win_jikan6_TA_tex_txt[];
extern u8 clk_win_jikan7_TA_tex_txt[];
extern u8 clk_win_jikan8_TA_tex_txt[];
extern u8 clk_win_jikan9_TA_tex_txt[];
extern u8 clk_win_jikan_TA_tex_txt[];

static void banti_draw_jikan_sub(Gfx** gfx_pp, GAME* game, Banti_anim_c* anim, int idx0, int idx1, int hide_zero,
                                 int pos_idx) {
    static u8* jikan_tex_table[11] = { clk_win_jikan0_TA_tex_txt, clk_win_jikan1_TA_tex_txt, clk_win_jikan2_TA_tex_txt,
                                       clk_win_jikan3_TA_tex_txt, clk_win_jikan4_TA_tex_txt, clk_win_jikan5_TA_tex_txt,
                                       clk_win_jikan6_TA_tex_txt, clk_win_jikan7_TA_tex_txt, clk_win_jikan8_TA_tex_txt,
                                       clk_win_jikan9_TA_tex_txt, clk_win_jikan_TA_tex_txt };

    static xyz_t set_pos_table[4] = {
        { 60.0f, -91.0f, 0.0f }, { 76.0f, -91.0f, 0.0f }, { 98.0f, -91.0f, 0.0f }, { 113.0f, -91.0f, 0.0f }
    };

    GRAPH* g = game->graph;
    xyz_t* set_pos = &set_pos_table[pos_idx];
    Mtx* m;
    int joint;
    Banti_callback_c callback_data;

    Matrix_push();
    Matrix_translate(set_pos->x, set_pos->y, set_pos->z, MTX_MULT);

    if (hide_zero == TRUE) {
        if (idx0 == 0) {
            idx0 = 10;
        }

        if (idx1 == 0) {
            idx1 = 10;
        }
    }

    m = (Mtx*)GRAPH_ALLOC_TYPE(g, Mtx, anim->keyframe.skeleton->num_shown_joints);
    callback_data.mtx = m;

    if (m != NULL) {
        joint = 0;
        callback_data.gfx_pp = gfx_pp;
        callback_data.state = anim->state;

        gSPSegment(gfx_pp[0]++, G_MWO_SEGMENT_8, jikan_tex_table[idx0]);
        gSPSegment(gfx_pp[0]++, G_MWO_SEGMENT_9, jikan_tex_table[idx1]);

        cKF_Si3_draw_SV_R_child(game, &anim->keyframe, &joint, &banti_draw_before, NULL, &callback_data, NULL);
    }

    Matrix_pull();
}

static void banti_draw_jikan(Gfx** gfx_pp, GAME_PLAY* play, int alpha) {
    u8 a = alpha;
    int now_hour;
    int next_hour;

    gDPSetPrimColor(gfx_pp[0]++, 0, a, 255, 255, 255, a);
    gDPSetEnvColor(gfx_pp[0]++, 60, 25, 10, a);

    now_hour = banti.displayed_time.hour;
    if (now_hour == 0 || now_hour == 12) {
        now_hour = 12;
    } else if (now_hour > 11) {
        now_hour -= 12;
    }

    next_hour = banti.next_time.hour;
    if (next_hour == 0 || next_hour == 12) {
        next_hour = 12;
    } else if (next_hour > 11) {
        next_hour -= 12;
    }

    banti_draw_jikan_sub(gfx_pp, (GAME*)play, &banti.hour_upper_anim, now_hour / 10, next_hour / 10, TRUE, 0);
    banti_draw_jikan_sub(gfx_pp, (GAME*)play, &banti.hour_lower_anim, now_hour % 10, next_hour % 10, FALSE, 1);

    banti_draw_jikan_sub(gfx_pp, (GAME*)play, &banti.min_upper_anim, banti.displayed_time.min / 10,
                         banti.next_time.min / 10, FALSE, 2);
    banti_draw_jikan_sub(gfx_pp, (GAME*)play, &banti.min_lower_anim, banti.displayed_time.min % 10,
                         banti.next_time.min % 10, FALSE, 3);
}

extern u8 clk_win_pm_tex_rgb_ia8[];
extern u8 clk_win_am_tex_rgb_ia8[];

extern Gfx clk_win_mode[];

extern u8 clk_win_sun_tex_rgb_ia8[];
extern u8 clk_win_mon_tex_rgb_ia8[];
extern u8 clk_win_tue_tex_rgb_ia8[];
extern u8 clk_win_wed_tex_rgb_ia8[];
extern u8 clk_win_thu_tex_rgb_ia8[];
extern u8 clk_win_fri_tex_rgb_ia8[];
extern u8 clk_win_sat_tex_rgb_ia8[];

extern Gfx clk_win_youbiT_model[];
extern Gfx clk_win_maruT_model[];
extern Gfx clk_win_maru2T_model[];
extern Gfx clk_win_ampmT_model[];

extern void banti_draw(GAME_PLAY* play, int view_idx) {
    static u8* week_tex_table[lbRTC_WEEK] = { clk_win_sun_tex_rgb_ia8, clk_win_mon_tex_rgb_ia8, clk_win_tue_tex_rgb_ia8,
                                              clk_win_wed_tex_rgb_ia8, clk_win_thu_tex_rgb_ia8, clk_win_fri_tex_rgb_ia8,
                                              clk_win_sat_tex_rgb_ia8 };
    int vidx = (view_idx >= 0 && view_idx < banti_VIEW_NUM) ? view_idx : 0;
    f32 alpha_f = banti.view[vidx].alpha;

    if (mFI_GET_TYPE(mFI_GetFieldId()) == mFI_FIELDTYPE_FG && mEv_CheckFirstIntro() != TRUE && alpha_f > 0.01f) {
        GRAPH* g = play->game.graph;
        int poly_render = FALSE;
        Mtx* m = (Mtx*)GRAPH_ALLOC_TYPE(g, Mtx, 1);

        /* TODO: submenu mode enums */
        if (play->submenu.process_status != mSM_PROCESS_WAIT && play->submenu.mode > 2) {
            poly_render = TRUE;
        }

        OPEN_DISP(g);

        if (m != NULL) {
#ifdef TARGET_PC
            if (pc_gx_hud_half) {
                mFont_CulcHudHalfOrthoMatrix(m);
            } else {
                mFont_CulcOrthoMatrix(m);
            }
#else
            mFont_CulcOrthoMatrix(m);
#endif

            if (poly_render == TRUE) {
                gSPMatrix(NOW_POLY_OPA_DISP++, m, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
            } else {
                gSPMatrix(NOW_FONT_DISP++, m, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
            }
        }

        Matrix_scale(16.0f, 16.0f, 16.0f, MTX_LOAD);

        if (banti.view[vidx].disp_left == TRUE) {
#ifdef TARGET_PC
            /* A half's ortho maps only content x 0..160 (the retail right
             * half), so the retail -184 slide would carry the clock off the
             * half's left edge (it sits at about x +40..+150 there). Slide it
             * flush to the half's left edge instead. A quadrant uses the full
             * retail mapping, so it keeps the retail slide. */
            Matrix_translate(pc_gx_hud_half ? -40.0f : -184.0f, 0.0f, 0.0f, MTX_MULT);
#else
            Matrix_translate(-184.0f, 0.0f, 0.0f, MTX_MULT);
#endif
        }

        {
            Gfx** gfx_pp;
            u8* am_pm_tex = banti.displayed_time.hour < 12 ? clk_win_am_tex_rgb_ia8 : clk_win_pm_tex_rgb_ia8;
            int alpha;
            u8 a;
            Gfx* gfx;
            u32 rendermode0;
            u32 rendermode1;

            if (poly_render == TRUE) {
                gfx = NOW_POLY_OPA_DISP;
            } else {
                gfx = NOW_FONT_DISP;
            }

            gfx_pp = &gfx;

            gSPMatrix(gfx_pp[0]++, _Matrix_to_Mtx_new(g), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

            alpha = (int)(alpha_f * 255.0f);
            if (alpha_f >= 0.4f) {
                rendermode0 = G_RM_PASS;
                rendermode1 = G_RM_XLU_SURF2;
            } else {
                rendermode0 = G_RM_PASS;
                rendermode1 = G_RM_CLD_SURF2;
            }

            gSPDisplayList(gfx_pp[0]++, clk_win_mode);
            gDPSetRenderMode(gfx_pp[0]++, rendermode0, rendermode1);

            if (banti.displayed_time.weekday == lbRTC_SUNDAY) {
                u8 a = alpha;

                gDPSetPrimColor(gfx_pp[0]++, 0, a, 255, 255, 225, a);
                gDPSetEnvColor(gfx_pp[0]++, 165, 30, 30, a);
            } else {
                u8 a = alpha;

                gDPSetPrimColor(gfx_pp[0]++, 0, a, 255, 255, 225, a);
                gDPSetEnvColor(gfx_pp[0]++, 80, 40, 0, a);
            }

            gSPSegment(gfx_pp[0]++, G_MWO_SEGMENT_9, week_tex_table[banti.displayed_time.weekday]);
            gSPDisplayList(gfx_pp[0]++, clk_win_youbiT_model);

            if ((Common_Get(time.rtc_time).sec & 1) == 1) {
                u8 a = alpha;

                gDPSetPrimColor(gfx_pp[0]++, 0, a, 215, 120, 0, a);
                gDPSetEnvColor(gfx_pp[0]++, 70, 50, 50, a);
                gSPDisplayList(gfx_pp[0]++, clk_win_maru2T_model);
            }

            a = alpha;
            gDPSetPrimColor(gfx_pp[0]++, 0, a, 255, 255, 120, a);
            gDPSetEnvColor(gfx_pp[0]++, 70, 10, 10, a);
            gSPSegment(gfx_pp[0]++, G_MWO_SEGMENT_A, am_pm_tex);
            gSPDisplayList(gfx_pp[0]++, clk_win_ampmT_model);

            gDPSetPrimColor(gfx_pp[0]++, 0, a, 255, 255, 0, a);
            gDPSetEnvColor(gfx_pp[0]++, 70, 50, 50, a);
            gSPDisplayList(gfx_pp[0]++, clk_win_maruT_model);

            banti_draw_hiduke(&gfx, play, alpha);
            banti_draw_jikan(&gfx, play, alpha);

            if (poly_render == TRUE) {
                SET_POLY_OPA_DISP(*gfx_pp);
            } else {
                SET_FONT_DISP(*gfx_pp);
            }
        }

        CLOSE_DISP(g);
    }
}
