#include "pc_world.h"

#include "game.h"
#include "m_play.h"
#include "m_player_lib.h"
#include "m_field_make.h"
#include "m_field_info.h"
#include "m_collision_bg.h"
#include "m_collision_obj.h"
#include "m_malloc.h"
#include "m_demo.h"
#include "m_common_data.h"
#include "m_scene.h"
#include "m_scene_table.h"
#include "m_actor.h"
#include "m_actor_dlftbls.h"
#include "m_lib.h"
#include "m_camera2.h"
#include "m_bgm.h"
#include "m_kankyo.h"
#include "m_lights.h"
#include "sys_matrix.h"
#include "ac_insect.h"
#include "ac_quest_manager.h"
#include "ac_intro_demo.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PC_DLFTBL_CAP 256

typedef struct {
    GAME* game;
    OSArena arena;
    mFM_fdinfo_c* fdinfo;
    Clip_c clip;
    mDemo_Data_c demo;
    ACTOR_DLFTBL dlftbls[PC_DLFTBL_CAP];
    int dlftbl_num;
    PLAYER_ACTOR* players[PLAYER_NUM];
    Private_c* privates[PLAYER_NUM];
    int pads[PLAYER_NUM];
    int guest_slot;
    int scene_no;
    int last_scene_no;
    u8 field_type;
    u8 field_draw_type;
    u8 game_started;
    u8 in_initial_block;
    u8 sunlight_flag;
    u8 player_no;
    Private_c* now_private;
    Door_data_c door_data;
    Door_data_c structure_exit;
    u8 mfi_state[PC_MFI_STATE_BYTES];
    u8 cobg_state[PC_COBG_STATE_BYTES];
    u8 env_light[PC_MENV_LIGHT_BYTES];
    Light_buffer lights;
    int look_bank[MPLIB_LOOK_BANK_NUM];
    MtxF* matrix_now;
    MtxF* matrix_stack;
    aINS_CTRL_ACTOR* insect_ctrl;
    aINS_Clip_c insect_clip;
    aQMgr_world_c quest;
    mDemo_Clip_c intro_clip;
    int destroy;
} PcWorld;

static PcWorld s_world[PC_WORLD_MAX];
static int s_bound = -1;
static int s_live = 0;
static int s_child_init = FALSE;
static int s_child_cleanup = FALSE;
/* Resident slot -> world index. 0 is the town. */
static s8 s_slot_world[PLAYER_NUM];
static s8 s_door_return[PLAYER_NUM];
/* Pad 0's resident. Cleared from the town table once they are indoors. */
static int s_pad0_slot = 0;

/* TEMP DIAG: stage trace for the indoors transition. Remove once confirmed. */
static void pc_world_trace(const char* text, int a, int b) {
    FILE* f = fopen("transition_log.txt", "a");

    if (f != NULL) {
        fprintf(f, "pw: %s (%d, %d)\n", text, a, b);
        fclose(f);
    }
}

static int pc_scene_splits(int scene) {
    /* One list for every interior. The house proved the switch; the other
     * buildings use the same enter and leave. */
    if (mSc_IS_SCENE_PLAYER_ROOM(scene)) {
        return TRUE;
    }

    switch (scene) {
        case SCENE_NPC_HOUSE:
        case SCENE_COTTAGE_NPC:
        case SCENE_SHOP0:
        case SCENE_CONVENI:
        case SCENE_SUPER:
        case SCENE_DEPART:
        case SCENE_DEPART_2:
        case SCENE_BROKER_SHOP:
        case SCENE_POST_OFFICE:
        case SCENE_NEEDLEWORK:
        case SCENE_POLICE_BOX:
        case SCENE_MUSEUM_ENTRANCE:
        case SCENE_MUSEUM_ROOM_PAINTING:
        case SCENE_MUSEUM_ROOM_FOSSIL:
        case SCENE_MUSEUM_ROOM_INSECT:
        case SCENE_MUSEUM_ROOM_FISH:
            return TRUE;
        default:
            return FALSE;
    }
}

static void pc_world_capture(int index) {
    PcWorld* world = &s_world[index];
    int n = actor_dlftbls_num;

    if (n > PC_DLFTBL_CAP) {
        n = PC_DLFTBL_CAP;
    }

    world->arena = zelda_arena;
    world->fdinfo = g_fdinfo;
    world->clip = Common_Get(clip);
    mDemo_world_store(&world->demo);
    world->dlftbl_num = n;
    if (n > 0) {
        memcpy(world->dlftbls, actor_dlftbls, (size_t)n * sizeof(ACTOR_DLFTBL));
    }
    mPlib_world_store(world->players, world->privates, world->pads, &world->guest_slot);
    world->scene_no = Save_Get(scene_no);
    world->last_scene_no = Common_Get(last_scene_no);
    world->field_type = Common_Get(field_type);
    world->field_draw_type = Common_Get(field_draw_type);
    world->game_started = Common_Get(game_started);
    world->in_initial_block = Common_Get(in_initial_block);
    world->sunlight_flag = Common_Get(sunlight_flag);
    world->player_no = Common_Get(player_no);
    world->now_private = Common_Get(now_private);
    world->door_data = Common_Get(door_data);
    world->structure_exit = Common_Get(structure_exit_door_data);
    mFI_world_store(world->mfi_state);
    mCoBG_world_store(world->cobg_state);
    mEnv_light_store(world->env_light);
    world->lights = light_list_buf;
    mPlib_look_bank_store(world->look_bank);
    Matrix_world_store(&world->matrix_now, &world->matrix_stack);
    aINS_world_store(&world->insect_ctrl, &world->insect_clip);
    aQMgr_world_store(&world->quest);
    aID_world_store(&world->intro_clip);
}

static void pc_world_restore(int index) {
    PcWorld* world = &s_world[index];

    zelda_arena = world->arena;
    g_fdinfo = world->fdinfo;
    Common_Set(clip, world->clip);
    mDemo_world_load(&world->demo);
    if (world->dlftbl_num > 0) {
        memcpy(actor_dlftbls, world->dlftbls, (size_t)world->dlftbl_num * sizeof(ACTOR_DLFTBL));
    }
    mPlib_world_load(world->players, world->privates, world->pads, world->guest_slot);
    Save_Set(scene_no, world->scene_no);
    Common_Set(last_scene_no, world->last_scene_no);
    Common_Set(field_type, world->field_type);
    Common_Set(field_draw_type, world->field_draw_type);
    Common_Set(game_started, world->game_started);
    Common_Set(in_initial_block, world->in_initial_block);
    Common_Set(sunlight_flag, world->sunlight_flag);
    Common_Set(player_no, world->player_no);
    Common_Set(now_private, world->now_private);
    Common_Set(door_data, world->door_data);
    Common_Set(structure_exit_door_data, world->structure_exit);
    mFI_world_load(world->mfi_state);
    mCoBG_world_load(world->cobg_state);
    mEnv_light_load(world->env_light);
    light_list_buf = world->lights;
    mPlib_look_bank_load(world->look_bank);
    Matrix_world_load(world->matrix_now, world->matrix_stack);
    aINS_world_load(world->insect_ctrl, &world->insect_clip);
    aQMgr_world_load(&world->quest);
    aID_world_load(&world->intro_clip);
    gamePT = world->game;
    mPlib_world_clear_bind();
}

void pc_world_bind(int index) {
    if ((index < 0) || (index >= PC_WORLD_MAX) || (s_world[index].game == NULL)) {
        return;
    }

    if (s_bound == index) {
        return;
    }

    if ((s_bound >= 0) && (s_world[s_bound].game != NULL)) {
        pc_world_capture(s_bound);
    }

    pc_world_restore(index);
    s_bound = index;
}

void pc_world_attach_primary(GAME* game) {
    int i;

    if (s_world[0].game == game) {
        return;
    }

    memset(s_world, 0, sizeof(s_world));
    for (i = 0; i < PLAYER_NUM; i++) {
        s_slot_world[i] = 0;
        s_door_return[i] = FALSE;
    }

    s_world[0].game = game;
    s_bound = 0;
    s_live = 1;
    /* TEMP DIAG */
    {
        FILE* f = fopen("transition_log.txt", "w");

        if (f != NULL) {
            fprintf(f, "pw: attach\n");
            fclose(f);
        }
    }
    pc_world_capture(0);
}

void pc_world_mark_door_return(int slot) {
    if ((slot >= 0) && (slot < PLAYER_NUM)) {
        s_door_return[slot] = TRUE;
    }
}

int pc_world_door_return_pending(int slot) {
    if ((slot < 0) || (slot >= PLAYER_NUM)) {
        return FALSE;
    }

    return s_door_return[slot] != FALSE;
}

void pc_world_clear_door_return(int slot) {
    if ((slot >= 0) && (slot < PLAYER_NUM)) {
        s_door_return[slot] = FALSE;
    }
}

int pc_world_any_player(void) {
    int i;
    int slot;

    for (i = 0; i < PC_WORLD_MAX; i++) {
        if (s_world[i].game == NULL) {
            continue;
        }

        /* The bound world's snapshot is stale. The live table is the one
         * Player_actor_dt just edited. */
        if (i == s_bound) {
            if (mPlib_any_player_actor() != FALSE) {
                return TRUE;
            }
            continue;
        }

        for (slot = 0; slot < PLAYER_NUM; slot++) {
            if (s_world[i].players[slot] != NULL) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

int pc_world_live(void) {
    return s_live;
}

int pc_world_init_is_child(void) {
    return s_child_init;
}

int pc_world_cleanup_is_child(void) {
    return s_child_cleanup;
}

GAME* pc_world_primary(void) {
    return s_world[0].game;
}

GAME_PLAY* pc_world_at(int index) {
    if ((index < 0) || (index >= PC_WORLD_MAX)) {
        return NULL;
    }

    return (GAME_PLAY*)s_world[index].game;
}

static int pc_world_free_slot(void) {
    int i;

    for (i = 1; i < PC_WORLD_MAX; i++) {
        if (s_world[i].game == NULL) {
            return i;
        }
    }

    return -1;
}

static void pc_world_drop_player(GAME_PLAY* play) {
    PLAYER_ACTOR* host = get_player_actor_withoutCheck(play);

    if (host == NULL) {
        return;
    }

    if (host->balloon_actor != NULL) {
        ACTOR* balloon = host->balloon_actor;

        host->balloon_actor = NULL;
        Actor_info_delete(&play->actor_info, balloon, (GAME*)play);
    }

    Actor_info_delete(&play->actor_info, (ACTOR*)host, (GAME*)play);
}

static int pc_world_open_scene(GAME_PLAY* primary, int scene, int resident_slot) {
    GAME* child;
    int index;

    index = pc_world_free_slot();
    if (index < 0) {
        pc_world_trace("open no slot", scene, resident_slot);
        return FALSE;
    }

    child = (GAME*)malloc(sizeof(GAME_PLAY));
    if (child == NULL) {
        pc_world_trace("open no malloc", scene, resident_slot);
        return FALSE;
    }

    pc_world_trace("open begin", scene, resident_slot);
    pc_world_bind(0);
    pc_world_capture(0);
    Common_Set(last_scene_no, Save_Get(scene_no));
    Save_Set(scene_no, scene);

    memset(child, 0, sizeof(GAME_PLAY));
    s_child_init = TRUE;
    game_child_ct(child, play_init, primary->game.graph);
    s_child_init = FALSE;
    pc_world_trace("open init", index, scene);

    /* The child play registered its own player into the shared slot table.
     * Keep that player only, then snapshot the interior. */
    mPlib_world_keep_slot(resident_slot);
    s_world[index].game = child;
    pc_world_capture(index);
    pc_world_trace("open capture", index, scene);
    pc_world_restore(0);
    s_bound = 0;
    s_live++;
    pc_world_trace("open restore", index, scene);

    if ((resident_slot >= 0) && (resident_slot < PLAYER_NUM)) {
        s_slot_world[resident_slot] = (s8)index;
        s_pad0_slot = resident_slot;
    }

    /* Retail throws this play away, so the door zoom dies with it. This play
     * stays for the player still outside. Priority 9 beats the door request. */
    Camera2_request_main_normal(primary, 1, 9);
    Camera2_process(primary);
    pc_world_trace("open camera", index, scene);
    /* The door queued a silent BGM wipe. Retail clears it in game_ct of the
     * new play. This play is still the town, so outdoor music comes back.
     * One stream: the indoor player hears it too. */
    mBGM_init();
    pc_world_drop_player(primary);
    pc_world_trace("open drop", index, scene);
    return TRUE;
}

static void pc_world_destroy_child(int index) {
    GAME* child;

    if ((index <= 0) || (s_world[index].game == NULL)) {
        return;
    }

    child = s_world[index].game;
    pc_world_trace("destroy begin", index, 0);
    pc_world_bind(index);
    s_child_cleanup = TRUE;
    play_cleanup(child);
    s_child_cleanup = FALSE;
    pc_world_bind(0);
    THA_dt(&child->tha);
    gamealloc_cleanup(&child->gamealloc);
    free(child);
    memset(&s_world[index], 0, sizeof(s_world[index]));
    pc_world_trace("destroy end", index, 0);
    if (s_live > 1) {
        s_live--;
    }

    /* The last interior move left its colliders in this global. The next town
     * pass reads them. The actors are already freed. */
    mco_work.count = 0;
    memset(mco_work.colliders, 0, sizeof(mco_work.colliders));
}

void pc_world_drop_children(void) {
    int i;

    for (i = 1; i < PC_WORLD_MAX; i++) {
        s_world[i].destroy = FALSE;
        pc_world_destroy_child(i);
    }

    for (i = 0; i < PLAYER_NUM; i++) {
        s_slot_world[i] = 0;
    }

    s_live = (s_world[0].game != NULL) ? 1 : 0;
    if (s_world[0].game != NULL) {
        pc_world_bind(0);
    }
}

static s16 pc_exit_angle(u8 orientation) {
    static const s16 angle_table[mSc_DIRECT_NUM] = {
        DEG2SHORT_ANGLE(0.0f),    DEG2SHORT_ANGLE(45.0f),  DEG2SHORT_ANGLE(90.0f),
        DEG2SHORT_ANGLE(135.0f),  DEG2SHORT_ANGLE(-180.0f), DEG2SHORT_ANGLE(-135.0f),
        DEG2SHORT_ANGLE(-90.0f),  DEG2SHORT_ANGLE(-45.0f),
    };

    if (orientation >= mSc_DIRECT_NUM) {
        orientation = 0;
    }

    return angle_table[orientation];
}

static void pc_world_fade_in_town(GAME_PLAY* leaving, ACTOR* who) {
    GAME_PLAY* town = (GAME_PLAY*)s_world[0].game;

    if ((town == NULL) || (leaving == NULL)) {
        return;
    }

    /* Entering already does this. The door camera stays on the town while the
     * resident is inside, so the exit view is that camera: black, in the door.
     * Priority 9 is what beats it on the way in. */
    Camera2_request_main_normal(town, 1, 9);
    if (get_player_actor_withoutCheck(town) != NULL) {
        Camera2_process(town);
    }

    /* A join fade is already closed over the other player. Restarting it
     * moves that closed card onto the resident who just came out. */
    if ((town->fb_fade_type == FADE_TYPE_IN) && (town->fb_wipe_mode != WIPE_MODE_NONE)) {
        return;
    }

    /* The house wipe just finished. Retail then fades the new scene in, on
     * the view of the player who just walked out. */
    Game_play_start_fade_in_player(town, leaving->fb_wipe_type, who);
}

static ACTOR* pc_world_spawn_return(GAME_PLAY* primary, Door_data_c* door, int resident_slot) {
    xyz_t pos;
    ACTOR* actor;
    s16 angle;

    xyz_t_move_s_xyz(&pos, &door->exit_position);
    angle = pc_exit_angle(door->exit_orientation);
    actor = Actor_info_make_actor(&primary->actor_info, (GAME*)primary, mAc_PROFILE_PLAYER, pos.x, pos.y, pos.z, 0,
                                  angle, 0, -1, -1, -1, EMPTY_NO, (s16)door->extra_data, -1, -1);
    if (actor != NULL) {
        actor->world.position.y = mCoBG_GetBgY_OnlyCenter_FromWpos2(actor->world.position, 0.0f);
    }

    if ((resident_slot >= 0) && (resident_slot < PLAYER_NUM)) {
        s_slot_world[resident_slot] = 0;
        s_door_return[resident_slot] = TRUE;
    }

    return actor;
}

static int pc_world_child_index(GAME_PLAY* play) {
    int i;

    for (i = 1; i < PC_WORLD_MAX; i++) {
        if (s_world[i].game == (GAME*)play) {
            return i;
        }
    }

    return -1;
}

static int pc_world_slot_in(int index) {
    int slot;

    for (slot = 0; slot < PLAYER_NUM; slot++) {
        if (s_slot_world[slot] == (s8)index) {
            return slot;
        }
    }

    return -1;
}

int pc_world_transition(GAME_PLAY* play) {
    int child;
    int scene;
    int slot;
    Door_data_c door;

    if ((s_live <= 0) || (s_world[0].game == NULL)) {
        return FALSE;
    }

    scene = play->next_scene_no;
    child = pc_world_child_index(play);
    pc_world_trace("transition", child, scene);

    if (child < 0) {
        if (play != (GAME_PLAY*)s_world[0].game) {
            return FALSE;
        }

        if (mPlib_extra_player_count() <= 0) {
            return FALSE;
        }

        if (pc_scene_splits(scene) == FALSE) {
            return FALSE;
        }

        slot = (int)Common_Get(player_no);
        return pc_world_open_scene(play, scene, slot);
    }

    /* Leaving an interior. Back to town, or across into another building.
     * The child's Game_play_move is still on the stack and keeps running this
     * frame, so it is only marked here and freed after that move returns.
     * Spawning the returning actor needs the town's globals, so bind the town
     * for it and put the child's state back before this transition returns. */
    slot = pc_world_slot_in(child);
    door = Common_Get(door_data);
    pc_world_trace("exit begin", child, scene);
    pc_world_bind(0);
    pc_world_trace("exit town", child, slot);

    if ((scene == s_world[0].scene_no) || (pc_scene_splits(scene) == FALSE)) {
        pc_world_fade_in_town(play, pc_world_spawn_return((GAME_PLAY*)s_world[0].game, &door, slot));
        pc_world_trace("exit spawn", child, slot);
        mBGM_init();
    } else if (pc_world_open_scene((GAME_PLAY*)s_world[0].game, scene, slot) == FALSE) {
        pc_world_fade_in_town(play, pc_world_spawn_return((GAME_PLAY*)s_world[0].game, &door, slot));
        pc_world_trace("exit reopen failed", child, slot);
        mBGM_init();
    } else {
        pc_world_trace("exit reopen", child, slot);
    }

    s_world[child].destroy = TRUE;
    pc_world_bind(child);
    pc_world_trace("exit pending", child, slot);
    return TRUE;
}

void pc_world_move_rest(void (*move)(GAME*)) {
    int i;
    int saved;

    if ((s_live < 2) || (move == NULL)) {
        return;
    }

    saved = s_bound;
    for (i = 1; i < PC_WORLD_MAX; i++) {
        if ((s_world[i].game == NULL) || (s_world[i].destroy != FALSE)) {
            continue;
        }

        pc_world_bind(i);
        pc_world_trace("move child", i, 0);
        move(s_world[i].game);
        pc_world_trace("move child done", i, 0);
    }

    if (saved >= 0) {
        pc_world_bind(saved);
    }

    /* A world that left its scene during the pass is freed only now, after its
     * Game_play_move has returned. */
    for (i = 1; i < PC_WORLD_MAX; i++) {
        if (s_world[i].destroy != FALSE) {
            pc_world_destroy_child(i);
        }
    }
}

void pc_world_fill_views(int* view_world, int count) {
    int i;
    int host_slot = -1;

    for (i = 0; i < count && i < PLAYER_NUM; i++) {
        view_world[i] = 0;
    }

    if (s_live < 2) {
        return;
    }

    for (i = 0; i < PLAYER_NUM; i++) {
        if (mPlib_pad_for_player_slot(i) == 0) {
            host_slot = i;
            break;
        }
    }

    if (host_slot < 0) {
        host_slot = s_pad0_slot;
    }

    if ((host_slot >= 0) && (count > 0)) {
        view_world[0] = s_slot_world[host_slot];
    }

    for (i = 1; i < count && i < PLAYER_NUM; i++) {
        PLAYER_ACTOR* guest = mPlib_extra_player_at(i - 1);
        int slot = mPlib_player_slot_of(guest);

        if (slot >= 0) {
            view_world[i] = s_slot_world[slot];
        }
    }
}
