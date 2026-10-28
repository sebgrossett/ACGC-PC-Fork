#include "ac_museum.h"

#include "m_name_table.h"
#include "bg_item_h.h"
#include "m_common_data.h"
#include "m_house.h"
#include "m_player_lib.h"
#include "m_demo.h"
#include "ac_intro_demo.h"
#include "m_bgm.h"
#include "sys_matrix.h"
#include "m_rcp.h"
#include "libforest/gbi_extensions.h"

static void aMsm_actor_ct(ACTOR* actor, GAME* game);
static void aMsm_actor_init(ACTOR* actor, GAME* game);
static void aMsm_actor_draw(ACTOR* actor, GAME* game);

// clang-format off
ACTOR_PROFILE Museum_Profile = {
    mAc_PROFILE_MUSEUM,
    ACTOR_PART_ITEM,
    ACTOR_STATE_TA_SET,
    MUSEUM,
    ACTOR_OBJ_BANK_KEEP,
    sizeof(STRUCTURE_ACTOR),
    &aMsm_actor_ct,
    mActor_NONE_PROC1,
    &aMsm_actor_init,
    &aMsm_actor_draw,
    NULL
};
// clang-format on

// clang-format off
static u8 aMsm_shadow_vtx_fix_flg_table[] = {
    TRUE, FALSE, FALSE, TRUE,
    FALSE, TRUE, TRUE, FALSE
};
// clang-format on

extern Vtx obj_museum_shadow_v[];
extern Gfx obj_museum_shadowT_model[];

// clang-format off
static bIT_ShadowData_c aMsm_shadow_data = {
    8,
    aMsm_shadow_vtx_fix_flg_table,
    60.0f,
    obj_museum_shadow_v,
    obj_museum_shadowT_model
};
// clang-format on

// clang-format off
static Door_data_c aMsm_museum_enter_data = {
    SCENE_MUSEUM_ENTRANCE,
    mSc_DIRECT_NORTH,
    FALSE,
    0,
    {240,0,440},
    EMPTY_NO,
    1,
    {0,0,0},
};
// clang-format on

extern Gfx obj_s_museum_lightT_model[];
extern Gfx obj_s_museum_t1_model[];
extern Gfx obj_s_museum_t2_model[];
extern Gfx obj_s_museum_t3T_model[];
extern Gfx obj_s_museum_windowT_model[];

extern Gfx obj_w_museum_lightT_model[];
extern Gfx obj_w_museum_t1_model[];
extern Gfx obj_w_museum_t2_model[];
extern Gfx obj_w_museum_t3T_model[];
extern Gfx obj_w_museum_windowT_model[];

static void aMsm_set_bgOffset(STRUCTURE_ACTOR* kamakura);
static int aMsm_ctrl_light();

static void aMsm_actor_ct(ACTOR* actor, GAME* game) {
    STRUCTURE_ACTOR* museum;

    museum = (STRUCTURE_ACTOR*)actor;
    museum->season = Common_Get(time.season);

    aMsm_set_bgOffset(museum);

    actor->cull_width = 1000.0f;
    actor->cull_radius = 1600.0f;

    museum->arg0_f = aMsm_ctrl_light() ? 1.0f : 0.0f;
}

static void aMsm_set_bgOffset(STRUCTURE_ACTOR* museum) {
    // clang-format off
    static s_xz unit_offset[35] = {
        { -3, -2 },
        { -2, -2 },
        { -1, -2 },
        { 0, -2 },
        { 1, -2 },
        { 2, -2 },
        { 3, -2 },
        { -3, -1 },
        { -2, -1 },
        { -1, -1 },
        { 0, -1 },
        { 1, -1 },
        { 2, -1 },
        { 3, -1 },
        { -3, 0 },
        { -2, 0 },
        { -1, 0 },
        { 0, 0 },
        { 1, 0 },
        { 2, 0 },
        { 3, 0 },
        { -3, 1 },
        { -2, 1 },
        { -1, 1 },
        { 0, 1 },
        { 1, 1 },
        { 2, 1 },
        { 3, 1 },
        { -3, 2 },
        { -2, 2 },
        { -1, 2 },
        { 0, 2 },
        { 1, 2 },
        { 2, 2 },
        { 3, 2 }
    };
    // clang-format on

    static const mCoBG_OffsetTable_c rewrite_data = {mCoBG_ATTRIBUTE_NONE, 10, 10, 10, 10, 10, 0 };

    xyz_t pos = { 0.0f, 0.0f, 0.0f };
    int i;

    for (i = 0; i < 35; i++) {
        pos.x = museum->actor_class.world.position.x + ((mFI_UT_WORLDSIZE_X_F * (f32)unit_offset[i].x));
        pos.z = museum->actor_class.world.position.z + ((mFI_UT_WORLDSIZE_Z_F * (f32)unit_offset[i].z));

        mCoBG_SetPluss5PointOffset_file(pos, rewrite_data, __FILE__, 291);
    }
}

static void aMsm_rewrite_out_data(ACTOR* actor, GAME_PLAY* play) {
    Door_data_c* door_data;
    xyz_t pos;

    door_data = Common_GetPointer(structure_exit_door_data);
    if (play->fb_wipe_mode != WIPE_MODE_NONE) {
        return;
    }

    door_data->next_scene_id = Save_Get(scene_no);
    door_data->exit_orientation = mSc_DIRECT_SOUTH;
    door_data->exit_type = 0;
    door_data->extra_data = 3;

    pos.x = actor->home.position.x;
    pos.z = actor->home.position.z + 120.0f;
    pos.y = mCoBG_GetBgY_OnlyCenter_FromWpos2(pos, 0.0f);

    door_data->exit_position.x = pos.x;
    door_data->exit_position.y = pos.y;
    door_data->exit_position.z = pos.z;

    door_data->door_actor_name = MUSEUM;

    door_data->wipe_type = WIPE_TYPE_TRIFORCE;

    mBGMPsComp_make_ps_wipe(0x2168);
}

static int aMsm_ctrl_light() {
    int now_sec = Common_Get(time.now_sec);
    return !(now_sec < (64800) && now_sec >= (21600));
}

static int aMsm_player_at_door(ACTOR* actor, PLAYER_ACTOR* player) {
    u16 y;
    f32 t;
    f32 xOffs;
    f32 zOffs;

    if (player == NULL) {
        return 0;
    }

    y = player->actor_class.shape_info.rotation.y;

    xOffs = actor->world.position.x;
    xOffs = SQ(player->actor_class.world.position.x - xOffs);

    zOffs = (actor->world.position.z + 100.0f);
    zOffs = SQ(player->actor_class.world.position.z - zOffs);

    t = (xOffs) + (zOffs);

    if ((y > DEG2SHORT_ANGLE2(135.0f)) && (y < DEG2SHORT_ANGLE2(225.0f)) && (t < 1100.0f)) { // 0x6000 && 0xA000
        return 2;
    }

    return 1;
}

static int aMsm_check_player(ACTOR* actor, GAME_PLAY* play) {
    return aMsm_player_at_door(actor, GET_PLAYER_ACTOR(play));
}

static int aMsm_request_enter_door(ACTOR* actor, GAME_PLAY* play) {
    PLAYER_ACTOR* player;
    int i;
    int count;
    xyz_t pos;

    player = GET_PLAYER_ACTOR(play);
    if (aMsm_player_at_door(actor, player) == 2) {
        pos.x = actor->world.position.x;
        pos.y = player->actor_class.world.position.y;
        pos.z = actor->world.position.z + 100.0f;
        mPlib_request_main_door_type1((GAME*)play, &pos, -DEG2SHORT_ANGLE(180.0f), TRUE, actor); // 0x8000
        return TRUE;
    }

#ifdef TARGET_PC
    /* The request proc reads the selected resident's context no matter which
     * actor it is called on, so a guest's request must run bound to the
     * guest, or the selected resident takes the walk-in instead. */
    count = mPlib_extra_player_count();
    for (i = 0; i < count; i++) {
        int pad;
        int slot;

        player = mPlib_extra_player_at(i);
        if (aMsm_player_at_door(actor, player) != 2) {
            continue;
        }

        slot = mPlib_player_slot_of(player);
        pad = mPlib_pad_for_player(player);
        if (pad > 0) {
            mPlib_slot_context_begin(play, slot, pad);
        }
        pos.x = actor->world.position.x;
        pos.y = player->actor_class.world.position.y;
        pos.z = actor->world.position.z + 100.0f;
        mPlib_request_main_door_type1((GAME*)play, &pos, -DEG2SHORT_ANGLE(180.0f), TRUE, actor); // 0x8000
        if (pad > 0) {
            mPlib_slot_context_end(play);
        }
        return TRUE;
    }
#endif

    return FALSE;
}

#ifdef TARGET_PC
/* The walk-out starts on the exit point (home + 120 z, aMsm_rewrite_out_data),
 * which is outside the door-walk box. */
static int aMsm_player_at_exit(PLAYER_ACTOR* player, ACTOR* actor) {
    f32 x;
    f32 z;

    if (player == NULL) {
        return FALSE;
    }

    x = player->actor_class.world.position.x - actor->home.position.x;
    z = player->actor_class.world.position.z - (actor->home.position.z + 120.0f);
    return ((x * x) + (z * z)) < (80.0f * 80.0f);
}

static int aMsm_walking_out_slot(GAME_PLAY* play, ACTOR* actor) {
    int session = (int)Common_Get(player_no);
    PLAYER_ACTOR* host = get_player_actor_by_index(play, session);
    int i;
    int count;

    if ((host != NULL) && (host->now_main_index == mPlayer_INDEX_OUTDOOR) &&
        (pc_world_door_return_pending(session) != FALSE) && (aMsm_player_at_exit(host, actor) != FALSE)) {
        return session;
    }

    count = mPlib_extra_player_count();
    for (i = 0; i < count; i++) {
        PLAYER_ACTOR* guest = mPlib_extra_player_at(i);
        int slot = mPlib_player_slot_of(guest);

        if ((guest == NULL) || (slot < 0) || (guest->now_main_index != mPlayer_INDEX_OUTDOOR) ||
            (pc_world_door_return_pending(slot) == FALSE) || (aMsm_player_at_exit(guest, actor) == FALSE)) {
            continue;
        }

        return slot;
    }

    return -1;
}

/* get_door_label_proc reads the selected resident, and the demo context is
 * already over by the time this building moves. The label is on the walker. */
static int aMsm_this_door(PLAYER_ACTOR* player, STRUCTURE_ACTOR* museum) {
    if ((player == NULL) || (player->now_main_index != mPlayer_INDEX_DOOR)) {
        return FALSE;
    }

    return museum == (STRUCTURE_ACTOR*)player->main_data.door.label;
}

static PLAYER_ACTOR* aMsm_player_in_door(STRUCTURE_ACTOR* museum) {
    PLAYER_ACTOR* player;
    int i;
    int count;

    player = mPlib_demo_talker();
    if (aMsm_this_door(player, museum) != FALSE) {
        return player;
    }

    player = GET_PLAYER_ACTOR_NOW();
    if ((player != NULL) && (museum == (STRUCTURE_ACTOR*)player->get_door_label_proc(gamePT))) {
        return player;
    }

    count = mPlib_extra_player_count();
    for (i = 0; i < count; i++) {
        player = mPlib_extra_player_at(i);
        if (aMsm_this_door(player, museum) != FALSE) {
            return player;
        }
    }

    return NULL;
}
#endif

static void aMsm_actor_move(ACTOR* actor, GAME* game) {
    STRUCTURE_ACTOR* museum;
    GAME_PLAY* play;
    f32 target;
#ifdef TARGET_PC
    PLAYER_ACTOR* in_door;
#endif

    play = (GAME_PLAY*)game;
    museum = (STRUCTURE_ACTOR*)actor;

    target = aMsm_ctrl_light() ? 1.0f : 0.0f;
    chase_f(&museum->arg0_f, target, 0.019532442f * (f32)game->graph->dt_num_60fps_frames);

#ifdef TARGET_PC
    /* GET_PLAYER_ACTOR_NOW() is a raw deref and the town play keeps running
     * without a selected resident, so check every player and the demo
     * talker instead of just the selected one. */
    in_door = aMsm_player_in_door(museum);
    if (in_door != NULL) {
        /* Name the walker before the scene change, or the interior spawns
         * the selected resident (aSHOP_pl_into_wait does the same). */
        mPlib_set_demo_talker(in_door);
        aMsm_rewrite_out_data(&museum->actor_class, play);
        goto_other_scene(play, &aMsm_museum_enter_data, 0);
        return;
    }

    /* The town stays, so nothing re-names this door after a return; clear
     * the walk-out mark once the returner's clip is done. The museum door
     * has no keyframe, so there is nothing to animate here. */
    {
        static int s_exit_slot = -1;

        if (s_exit_slot < 0) {
            s_exit_slot = aMsm_walking_out_slot(play, actor);
        } else {
            PLAYER_ACTOR* leaving = get_player_actor_by_index(play, s_exit_slot);

            if ((leaving == NULL) || (leaving->now_main_index != mPlayer_INDEX_OUTDOOR)) {
                pc_world_clear_door_return(s_exit_slot);
                s_exit_slot = -1;
            }
        }
    }

    aMsm_request_enter_door(actor, play);
#else
    if (museum == GET_PLAYER_ACTOR_NOW()->get_door_label_proc(gamePT)) {
        aMsm_rewrite_out_data(&museum->actor_class, play);
        goto_other_scene(play, &aMsm_museum_enter_data, 0);
        return;
    }

    if (aMsm_check_player(actor, play) != 2) {
        return;
    }

    pos.x = actor->world.position.x;
    pos.y = GET_PLAYER_ACTOR(play)->actor_class.world.position.y;
    pos.z = actor->world.position.z + 100.0f;
    mPlib_request_main_door_type1(game, &pos, -DEG2SHORT_ANGLE(180.0f), TRUE, actor); // 0x8000
#endif
}

static void aMsm_actor_init(ACTOR* actor, GAME* game) {
    xyz_t pos;

    pos = actor->home.position;
    mFI_SetFG_common(DUMMY_MUSEUM, pos, FALSE);
    aMsm_actor_move(actor, game);
    actor->mv_proc = aMsm_actor_move;
}

// clang-format off
static Gfx* aMsm_summer_disp_table[] = {
    obj_s_museum_lightT_model,
    obj_s_museum_t1_model,
    obj_s_museum_t2_model,
    obj_s_museum_t3T_model,
    obj_s_museum_windowT_model
};
// clang-format on

// clang-format off
static Gfx* aMsm_winter_disp_table[] = {
    obj_w_museum_lightT_model,
    obj_w_museum_t1_model,
    obj_w_museum_t2_model,
    obj_w_museum_t3T_model,
    obj_w_museum_windowT_model
};
// clang-format on

static void aMsm_actor_draw(ACTOR* actor, GAME* game) {
    static Gfx** disp_info[] = { aMsm_summer_disp_table, aMsm_winter_disp_table };

    STRUCTURE_ACTOR* museum;
    GRAPH* graph;
    Mtx* mtx;
    u16* pal;
    Gfx** disp;
    f32 color;
    int r1;
    int g1;
    int b1;
    int l2;
    int r2;
    int g2;
    int b2;

    museum = (STRUCTURE_ACTOR*)actor;
    graph = game->graph;
    pal = Common_Get(clip).structure_clip->get_pal_segment_proc(aSTR_PAL_MUSEUM);
    disp = disp_info[museum->season == mTM_SEASON_WINTER];

    _texture_z_light_fog_prim_xlu(graph);
    _texture_z_light_fog_prim_npc(graph);
    _texture_z_light_fog_prim_shadow(graph);

    OPEN_DISP(graph);
    {
        gSPMatrix(NEXT_POLY_OPA_DISP, _Matrix_to_Mtx_new(graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPMatrix(NEXT_POLY_XLU_DISP, _Matrix_to_Mtx_new(graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPMatrix(NEXT_SHADOW_DISP, _Matrix_to_Mtx_new(graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

        gSPSegment(NEXT_POLY_OPA_DISP, G_MWO_SEGMENT_8, pal);
        gSPSegment(NEXT_POLY_XLU_DISP, G_MWO_SEGMENT_8, pal);
        gSPSegment(NEXT_SHADOW_DISP, G_MWO_SEGMENT_8, pal);

        gDPPipeSync(NEXT_POLY_OPA_DISP);

        color = museum->arg0_f;
        r1 = ((color * 255.0f) + 0.5f);
        g1 = ((color * 255.0f) + 0.5f);
        b1 = ((color * 150.0f) + 0.5f);
        gDPSetPrimColor(NEXT_POLY_OPA_DISP, 0, 0, r1, g1, b1, 255);
        gSPDisplayList(NEXT_POLY_OPA_DISP, disp[0]);
        gSPDisplayList(NEXT_POLY_OPA_DISP, disp[1]);
        gSPDisplayList(NEXT_POLY_OPA_DISP, disp[2]);
        gSPDisplayList(NEXT_POLY_OPA_DISP, disp[3]);

        gDPPipeSync(NEXT_POLY_XLU_DISP);

        if (museum->arg0_f > 0.0f) {
            r2 = 255;
            g2 = 255;
            b2 = 150;
            l2 = (museum->arg0_f * 120.0f) + 0.5f;
        } else {
            l2 = 0;
            r2 = 0;
            g2 = 0;
            b2 = 0;
        }

        gDPSetPrimColor(NEXT_POLY_XLU_DISP, 0, l2, r2, g2, b2, 0);
        gSPDisplayList(NEXT_POLY_XLU_DISP, disp[4]);
    }
    CLOSE_DISP(graph);

    (*Common_Get(clip).bg_item_clip->draw_shadow_proc)(game, &aMsm_shadow_data, TRUE);
}
