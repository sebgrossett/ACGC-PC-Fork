#include "m_submenu.h"
#include "m_submenu_ovl.h"

#include "m_common_data.h"
#include "m_name_table.h"
#include "m_player_lib.h"
#include "m_msg.h"
#include "m_quest.h"
#include "libultra/libultra.h"

#ifdef PC_ENHANCEMENTS
#include "m_museum_display.h"
#endif

static int s_submenu_cur_slot = 0;

extern int mSM_cur_slot(void) {
    return s_submenu_cur_slot;
}

extern void mSM_set_cur_slot(int slot) {
    s_submenu_cur_slot = (slot >= 0 && slot < mSM_SLOT_NUM) ? slot : 0;
}

/* Side slots: one Submenu per guest Controls row (slots 1-3). A side slot's
 * state is copied into the primary `play->submenu` for its move/draw and
 * copied back afterwards, so the retail code keeps operating on the one
 * working struct. `item_p` points into its own struct's `items` array, so it
 * is re-pointed after every copy. While a side slot runs, the primary's state
 * is held in the backup so a host menu and a guest menu can both be open. */
static Submenu s_submenu_side_slot[mSM_SLOT_NUM];
static Submenu s_submenu_primary_backup;

extern Submenu* mSM_side_slot_p(int slot) {
    if (slot <= 0 || slot >= mSM_SLOT_NUM) {
        return NULL;
    }

    return &s_submenu_side_slot[slot];
}

/* TRUE when this save slot's player has a game menu open or transitioning:
 * their own primary-menu open (they are the slot-0 opener), or their own
 * side-slot menu. The single source for the demo owner guard, the per-view
 * clock fade gate and the session-menu gate. */
extern int mSM_player_menu_open_by_slot(GAME_PLAY* play, int slot) {
    Submenu* side;
    PLAYER_ACTOR* player;
    int pad;

    if (slot < 0 || slot >= PLAYER_NUM) {
        return FALSE;
    }

    player = get_player_actor_by_index(play, slot);

    if ((play->submenu.process_status != mSM_PROCESS_WAIT || play->submenu.menu_type != mSM_OVL_NONE) &&
        player != NULL && mPlib_menu_opener_for_slot(0) == player) {
        return TRUE;
    }

    pad = mPlib_pad_for_player_slot(slot);
    side = (pad > 0 && pad < mSM_SLOT_NUM) ? mSM_side_slot_p(pad) : NULL;
    if (side != NULL && (side->process_status != mSM_PROCESS_WAIT || side->menu_type != mSM_OVL_NONE)) {
        return TRUE;
    }

    return FALSE;
}

extern void mSM_side_slot_begin(int slot) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;

    if (slot <= 0 || slot >= mSM_SLOT_NUM) {
        return; /* slot 0 is the primary and runs in place */
    }

    s_submenu_primary_backup = play->submenu;
    play->submenu = s_submenu_side_slot[slot];
    play->submenu.item_p = &play->submenu.items[0];
    mSM_set_cur_slot(slot);
}

extern void mSM_side_slot_end(int slot) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;

    if (slot <= 0 || slot >= mSM_SLOT_NUM) {
        return;
    }

    s_submenu_side_slot[slot] = play->submenu;
    s_submenu_side_slot[slot].item_p = &s_submenu_side_slot[slot].items[0];
    mSM_set_cur_slot(0);
    play->submenu = s_submenu_primary_backup;
}

#define mSM_DLFTBL_INIT \
    { { NULL, 0, 0, 0, 0, 0, "submenu_ovl" }, { NULL, 0, 0, 0, 0, 0, "player_actor" } }

/* Area link state is per menu slot; the primary menu is slot 0. */
static mSM_dlftbl_c SubmenuArea_dlftbl_slot[mSM_SLOT_NUM][mSM_DLF_NUM] = {
    mSM_DLFTBL_INIT, mSM_DLFTBL_INIT, mSM_DLFTBL_INIT, mSM_DLFTBL_INIT
};
static mSM_dlftbl_c* SubmenuArea_visit_slot[mSM_SLOT_NUM];
static void* SubmenuArea_allocp = NULL;

#define SubmenuArea_dlftbl SubmenuArea_dlftbl_slot[s_submenu_cur_slot]
#define SubmenuArea_visit SubmenuArea_visit_slot[s_submenu_cur_slot]

extern int mSM_COLLECT_INSECT_GET(int idx) {
    u32 ftr_idx = (0x2F2 << 2) + (idx << 2);
    return (Common_Get(now_private)->furniture_collected_bitfield[ftr_idx >> 2 >> 5] &
            (1 << ((ftr_idx >> 2) & 0x1F))) != 0;
}

extern void mSM_COLLECT_INSECT_SET(int idx) {
    u32 ftr_idx = (0x2F2 << 2) + (idx << 2);
    Common_Get(now_private)->furniture_collected_bitfield[ftr_idx >> 2 >> 5] |= (1 << ((ftr_idx >> 2) & 0x1F));
}

static int mSM_CHECK_ALL_INSECT_GET_SUB() {
    int i;
    int n = 0;

    for (i = 0; i < INSECT_ONLY_NUM; i++) {
        if (mSM_COLLECT_INSECT_GET(i) != FALSE) {
            n++;
        }
    }

    return n;
}

extern int mSM_CHECK_ALL_INSECT_GET() {
    if (mSM_CHECK_ALL_INSECT_GET_SUB() == INSECT_ONLY_NUM) {
        return TRUE;
    }

    return FALSE;
}

extern int mSM_CHECK_LAST_INSECT_GET(int idx) {
    int res = FALSE;

    if (mSM_CHECK_ALL_INSECT_GET_SUB() == (INSECT_ONLY_NUM - 1) && mSM_COLLECT_INSECT_GET(idx) == FALSE) {
        res = TRUE;
    }

    return res;
}

extern int mSM_COLLECT_FISH_GET(int idx) {
    u32 ftr_idx = (0x31A << 2) + (idx << 2);
    return (Common_Get(now_private)->furniture_collected_bitfield[ftr_idx >> 2 >> 5] &
            (1 << ((ftr_idx >> 2) & 0x1F))) != 0;
}

extern void mSM_COLLECT_FISH_SET(int idx) {
    u32 ftr_idx = (0x31A << 2) + (idx << 2);
    Common_Get(now_private)->furniture_collected_bitfield[ftr_idx >> 2 >> 5] |= (1 << ((ftr_idx >> 2) & 0x1F));
}

static int mSM_CHECK_ALL_FISH_GET_SUB() {
    int i;
    int n = 0;

    for (i = 0; i < FISH_NUM; i++) {
        if (mSM_COLLECT_FISH_GET(i) != FALSE) {
            n++;
        }
    }

    return n;
}

extern int mSM_CHECK_ALL_FISH_GET() {
    if (mSM_CHECK_ALL_FISH_GET_SUB() == FISH_NUM) {
        return TRUE;
    }

    return FALSE;
}

extern int mSM_CHECK_LAST_FISH_GET(int idx) {
    int res = FALSE;

    if (mSM_CHECK_ALL_FISH_GET_SUB() == (FISH_NUM - 1) && mSM_COLLECT_FISH_GET(idx) == FALSE) {
        res = TRUE;
    }

    return res;
}

extern int SubmenuArea_IsPlayer() {
    return SubmenuArea_visit == &SubmenuArea_dlftbl[mSM_DLF_PLAYER_ACTOR];
}

static void mSM_load_player_anime(GAME_PLAY* play) {
    // stubbed
}

static void SubmenuArea_DoLink(mSM_dlftbl_c* dlftbl, Submenu* submenu, int dlf_idx) {
    dlftbl->_00 = SubmenuArea_allocp;
    dlftbl->_14 = 0;
    SubmenuArea_visit = dlftbl;
    submenu->overlay_address = dlftbl->_00;
    submenu->next_overlay_address = dlftbl->_00;
}

static void SubmenuArea_DoUnlink(mSM_dlftbl_c* dlftbl, Submenu* submenu) {
    if (dlftbl->_00 != NULL) {
        submenu->overlay_address = dlftbl->_00;
        submenu->next_overlay_address = dlftbl->_00;
        dlftbl->_14 = 0;
        dlftbl->_00 = NULL;
        SubmenuArea_visit = NULL;
    }
}

static int mSM_ovlptr_dllcnv_sub(void* proc, mSM_dlftbl_c* dlftbl, Submenu* submenu, int dlf_idx) {
    if (dlf_idx == mSM_DLF_SUBMENU_OVL || dlf_idx == mSM_DLF_PLAYER_ACTOR) {
        SubmenuArea_DoLink(dlftbl, submenu, dlf_idx);
        return TRUE;
    }

    return FALSE;
}

extern void* mSM_ovlptr_dllcnv(void* proc, Submenu* submenu, int dlf_idx) {
    if (SubmenuArea_visit != NULL) {
        return proc; /* overlay already loaded, pointer is valid */
    } else if (mSM_ovlptr_dllcnv_sub(proc, SubmenuArea_dlftbl, submenu, dlf_idx) == FALSE) {
        return NULL;
    }

    return proc;
}

extern void mSM_submenu_ovlptr_init(GAME_PLAY* play) {
    int i;

    SubmenuArea_allocp = (void*)1;
    s_submenu_cur_slot = 0;
    for (i = 0; i < mSM_SLOT_NUM; i++) {
        SubmenuArea_visit_slot[i] = NULL;
    }
}

/* Side slots keep their state across the copy-in/copy-out swap; reset them so
 * a scene change cannot leave a stale open menu. This deliberately does not go
 * through mSM_submenu_ct: the submenu_disabled one-shot belongs to the
 * primary's ct at play_init, not to a side-slot reset here. */
static void mSM_side_slot_reset(Submenu* submenu) {
    bzero(submenu, sizeof(Submenu));
    submenu->process_status = mSM_PROCESS_WAIT;
    submenu->wait_timer = 0;
    submenu->move_proc = (SUBMENU_PROC)&none_proc1;
    submenu->draw_proc = (SUBMENU_GAME_PROC)&none_proc1;
}

extern void mSM_submenu_ovlptr_cleanup(Submenu* submenu) {
    int i;

    for (i = 0; i < mSM_SLOT_NUM; i++) {
        mSM_dlftbl_c* visit = SubmenuArea_visit_slot[i];

        if (visit != NULL) {
            s_submenu_cur_slot = i;
            SubmenuArea_DoUnlink(visit, submenu);
            SubmenuArea_visit_slot[i] = NULL;
        }
    }

    /* Side slots keep their state across the copy-in/copy-out swap; reset them
     * with the primary so a scene change cannot leave a stale open menu. */
    for (i = 1; i < mSM_SLOT_NUM; i++) {
        mSM_side_slot_reset(&s_submenu_side_slot[i]);
    }

    s_submenu_cur_slot = 0;
    SubmenuArea_allocp = NULL;
}

extern void load_player(Submenu* submenu) {
    mSM_dlftbl_c* dlftbl = &SubmenuArea_dlftbl[mSM_DLF_PLAYER_ACTOR];

    /* A guest keeps moving while the submenu is open, and its move call lands
     * here every frame. The menu links its own area and the panel draw gate
     * requires it, so do not swap the area back until the menu has closed.
     * mSM_move_End calls this after setting mSM_PROCESS_WAIT. */
    if (submenu->process_status != mSM_PROCESS_WAIT) {
        return;
    }

    if (SubmenuArea_visit != dlftbl) {
        if (SubmenuArea_visit != NULL) {
            SubmenuArea_DoUnlink(SubmenuArea_visit, submenu);
        }

        SubmenuArea_DoLink(dlftbl, submenu, mSM_DLF_PLAYER_ACTOR);
    }
}

extern void mSM_submenu_ct(Submenu* submenu) {
    bzero(submenu, sizeof(Submenu));
    submenu->process_status = mSM_PROCESS_WAIT;
    submenu->wait_timer = 0;

    if (Common_Get(submenu_disabled) == TRUE) {
        submenu->start_refuse = TRUE;
        Common_Set(submenu_disabled, FALSE);
    }

    submenu->move_proc = (SUBMENU_PROC)&none_proc1;
    submenu->draw_proc = (SUBMENU_GAME_PROC)&none_proc1;
}

extern void mSM_submenu_dt(Submenu* submenu) {
    // stubbed
}

extern void mSM_open_submenu(Submenu* submenu, int type, int arg0, int arg1) {
    mSM_open_submenu_new2(submenu, type, arg0, arg1, NULL, 0);
}

extern void mSM_open_submenu_new(Submenu* submenu, int type, int arg0, int arg1, void* arg2) {
    mSM_open_submenu_new2(submenu, type, arg0, arg1, arg2, 0);
}

extern void mSM_open_submenu_new2(Submenu* submenu, int type, int arg0, int arg1, void* arg2, int arg3) {
    submenu->menu_type = type;
    submenu->param0 = arg0;
    submenu->param1 = arg1;
    submenu->param2 = arg2;
    submenu->param3 = arg3;
}

static void mSM_Reset_player_btn(PLAYER_ACTOR* player) {
    if (player != NULL) {
        player->a_btn_pressed = FALSE;
        player->a_btn_triggers_submenu = TRUE;
    }
}

static void mSM_Reset_player_btn_type2(GAME_PLAY* play) {
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);

    if (player != NULL) {
        player->a_btn_triggers_submenu = TRUE;
    }
}

/* Map-open position checks for one player: not a cottage scene, not standing
 * on ocean-block ground. */
static int mSM_check_map_position(PLAYER_ACTOR* player) {
    int bx;
    int bz;

    if (player == NULL) {
        return FALSE;
    }

    if (Save_Get(scene_no) == SCENE_COTTAGE_MY || Save_Get(scene_no) == SCENE_COTTAGE_NPC) {
        return FALSE;
    }

    mFI_Wpos2BlockNum(&bx, &bz, player->actor_class.world.position);
    return mFI_CheckBlockKind_OR(bx, bz, mRF_BLOCKKIND_OCEAN) == FALSE;
}

static int mSM_check_open_map_new(GAME_PLAY* play) {
    if (mSM_CHECK_OPEN_MAP() == FALSE) {
        return FALSE;
    }

    return mSM_check_map_position(GET_PLAYER_ACTOR(play));
}

/* First active player pressing Start/Y, in controller-index order (ties go to
 * the lower index). */
static int mSM_find_inventory_press(GAME_PLAY* play, PLAYER_ACTOR** out_player, int* out_pad) {
    int count = 1 + mPlib_extra_player_count();
    int i;

    for (i = 0; i < count; i++) {
        PLAYER_ACTOR* player = (i == 0) ? GET_PLAYER_ACTOR(play) : mPlib_extra_player_at(i - 1);
        int pad = (i == 0) ? 0 : mPlib_pad_for_player_slot(mPlib_player_slot_of(player));

        if ((player == NULL) || (pad < 0) || (pad >= MAXCONTROLLERS)) {
            continue;
        }

        if (chkTrigger_pad(pad, mSM_INV_BUTTON_0) || chkTrigger_pad(pad, mSM_INV_BUTTON_1)) {
            *out_player = player;
            *out_pad = pad;
            return TRUE;
        }
    }

    return FALSE;
}

extern void mSM_submenu_ctrl(GAME_PLAY* play) {
    Submenu* submenu = &play->submenu;
    PLAYER_ACTOR* press_player = NULL;
    int press_pad = -1;
    int open_inventory;
    int i;

    if (play->fb_fade_type != FADE_TYPE_NONE) {
        return;
    }

    if (play->fb_wipe_mode != WIPE_MODE_NONE) {
        return;
    }

    /* Default opener: the selected resident. Skip it while an actor is already
     * opening a menu (menu_type is set but process_status is still WAIT for one
     * frame), otherwise it overwrites the actor's opener. */
    if (submenu->process_status == mSM_PROCESS_WAIT && submenu->menu_type == mSM_OVL_NONE) {
        mPlib_set_menu_opener(GET_PLAYER_ACTOR(play), 0, (int)Common_Get(player_no));
    }

    open_inventory = mSM_find_inventory_press(play, &press_player, &press_pad);

    if (open_inventory && press_pad > 0) {
        /* A guest opens its own side slot. Only that slot's state and that
         * player's own request state gate it; the primary being busy, or the
         * host's refuse flags, do not. */
        Submenu* side = mSM_side_slot_p(press_pad);

        if (side != NULL && side->process_status == mSM_PROCESS_WAIT && side->menu_type == mSM_OVL_NONE &&
            Common_Get(reset_flag) == FALSE && side->start_refuse == FALSE && side->start_refuse_timer == 0 &&
            mEv_CheckFirstIntro() == FALSE && mPlib_able_submenu_type1_player((GAME*)play, press_player)) {
            mPlib_set_menu_opener_for_slot(press_pad, press_player, press_pad, mPlib_player_slot_of(press_player));
            mSM_open_submenu(side, mSM_OVL_INVENTORY, 0, 0);
            press_player->a_btn_triggers_submenu = TRUE;
            side->process_status = mSM_PROCESS_PREWAIT;
            side->mode = mSM_MODE_PRERENDER_INIT;
            SetGameFrame(1);
        }

        /* A guest press never opens the primary inventory; fall through to the
         * board loop, like a refused press used to. */
        open_inventory = FALSE;
    }

    /* A guest's map button (X) routes to their side slot, with the same gates
     * as their inventory plus the map's own position checks at their position.
     * Start/Y wins when both are pressed the same frame, like retail. */
    if (open_inventory == FALSE) {
        int count = 1 + mPlib_extra_player_count();

        for (i = 1; i < count; i++) {
            PLAYER_ACTOR* player = mPlib_extra_player_at(i - 1);
            int pad = (player != NULL) ? mPlib_pad_for_player_slot(mPlib_player_slot_of(player)) : -1;
            Submenu* side;

            if ((player == NULL) || (pad <= 0) || (pad >= MAXCONTROLLERS)) {
                continue;
            }

            if (chkTrigger_pad(pad, mSM_MAP_BUTTON) == FALSE) {
                continue;
            }

            side = mSM_side_slot_p(pad);
            if (side != NULL && side->process_status == mSM_PROCESS_WAIT && side->menu_type == mSM_OVL_NONE &&
                Common_Get(reset_flag) == FALSE && Common_Get(map_flag) == TRUE &&
                side->start_refuse == FALSE && side->start_refuse_timer == 0 &&
                mEv_CheckFirstIntro() == FALSE && mPlib_able_submenu_type1_player((GAME*)play, player) &&
                mSM_check_map_position(player)) {
                mPlib_set_menu_opener_for_slot(pad, player, pad, mPlib_player_slot_of(player));
                mSM_open_submenu(side, mSM_OVL_MAP, 1, 0);
                player->a_btn_triggers_submenu = TRUE;
                side->process_status = mSM_PROCESS_PREWAIT;
                side->mode = mSM_MODE_PRERENDER_INIT;
                SetGameFrame(1);
            }

            break; /* one guest map attempt per frame */
        }
    }

    if (((open_inventory && Common_Get(reset_flag) == FALSE) ||
         (mSM_check_open_map_new(play) == TRUE && Common_Get(reset_flag) == FALSE)) &&
        submenu->process_status == mSM_PROCESS_WAIT &&
        submenu->start_refuse == FALSE && submenu->start_refuse_timer == 0 && mPlib_able_submenu_type1((GAME*)play) &&
        mEv_CheckFirstIntro() == FALSE) {
        if (open_inventory) {
            mPlib_set_menu_opener(press_player, press_pad, mPlib_player_slot_of(press_player));
            mSM_open_submenu(submenu, mSM_OVL_INVENTORY, 0, 0);
        } else {
            mSM_open_submenu(submenu, mSM_OVL_MAP, 1, 0);
        }

        mSM_Reset_player_btn_type2(play);
    } else {
        int count = 1 + mPlib_extra_player_count();

        for (i = 0; i < count; i++) {
            PLAYER_ACTOR* player =
                (i == 0) ? GET_PLAYER_ACTOR(play) : mPlib_extra_player_at(i - 1);
            int pad = (i == 0) ? 0 : mPlib_pad_for_player_slot(mPlib_player_slot_of(player));
            Submenu* target = (pad == 0) ? submenu : mSM_side_slot_p(pad);
            int menu_slot = (pad == 0) ? 0 : pad;
            s16 y_dir;
            int dir;
            int opened = FALSE;

            if ((player == NULL) || (pad < 0) || (pad >= MAXCONTROLLERS) || (target == NULL)) {
                continue;
            }

            if ((player->a_btn_pressed == FALSE) && (chkTrigger_pad(pad, BUTTON_A) == FALSE)) {
                continue;
            }

            if ((Common_Get(reset_flag) != FALSE) || (target->process_status != mSM_PROCESS_WAIT) ||
                (target->start_refuse != FALSE) ||
                (target->start_refuse_timer != 0) ||
                (mPlib_able_submenu_type1_player((GAME*)play, player) == FALSE)) {
                continue;
            }

            y_dir = player->actor_class.shape_info.rotation.y;
            y_dir -= -0x8000;
            dir = ABS(y_dir);

            if (dir < DEG2SHORT_ANGLE(45.0f)) {
                switch (player->item_in_front) {
                    case MESSAGE_BOARD1:
                    case MESSAGE_BOARD0:
                        mPlib_set_menu_opener_for_slot(menu_slot, player, pad, mPlib_player_slot_of(player));
                        mSM_open_submenu(target, mSM_OVL_NOTICE, 0, 0);
                        mSM_Reset_player_btn(player);
                        opened = TRUE;
                        break;
                    case MAP_BOARD1:
                    case MAP_BOARD0:
                        mPlib_set_menu_opener_for_slot(menu_slot, player, pad, mPlib_player_slot_of(player));
                        mSM_open_submenu(target, mSM_OVL_MAP, 0, 0);
                        mSM_Reset_player_btn(player);
                        opened = TRUE;
                        break;
                }
            }

            if (opened != FALSE) {
                /* Side slots promote here; the primary is promoted by the
                 * trailing block (which also handles the actor-open window). */
                if (menu_slot != 0) {
                    target->process_status = mSM_PROCESS_PREWAIT;
                    target->mode = mSM_MODE_PRERENDER_INIT;
                    SetGameFrame(1);
                }
                break;
            }
        }
    }

    /* Promote a menu that an actor just opened (it set menu_type during the
     * actor move while process_status was still WAIT). The WAIT check keeps
     * this from re-promoting a menu that is already running. */
    if (submenu->process_status == mSM_PROCESS_WAIT && submenu->menu_type != mSM_OVL_NONE) {
        submenu->process_status = mSM_PROCESS_PREWAIT;
        submenu->mode = mSM_MODE_PRERENDER_INIT;
        SetGameFrame(1);
    }
}

static void mSM_move_Wait(Submenu* submenu) {
    static float wait_accum[mSM_SLOT_NUM];
    int ticks = graph_dt_60hz_ticks(gamePT, &wait_accum[s_submenu_cur_slot]);

    if (submenu->wait_timer != 0) {
        submenu->wait_timer -= ticks;
        if (submenu->wait_timer < 0) {
            submenu->wait_timer = 0;
        }
    }

    if (submenu->start_refuse_timer != 0) {
        submenu->start_refuse_timer = ticks >= submenu->start_refuse_timer ? 0 : submenu->start_refuse_timer - ticks;
    }
}

static void mSM_move_PREWait(Submenu* submenu) {
    if (submenu->mode >= mSM_MODE_PRERENDER_DONE) {
        submenu->process_status = mSM_PROCESS_LINKWAIT;
    }
}

static void mSM_move_LINKWait(Submenu* submenu) {
    Submenu_Item_c* item;
    mSM_dlftbl_c* dlftbl = &SubmenuArea_dlftbl[mSM_DLF_SUBMENU_OVL];
    int i;

    if (SubmenuArea_visit != dlftbl) {
        if (SubmenuArea_visit != NULL) {
            SubmenuArea_DoUnlink(SubmenuArea_visit, submenu);
        }

        SubmenuArea_DoLink(dlftbl, submenu, mSM_DLF_SUBMENU_OVL);
        submenu->move_proc = (SUBMENU_PROC)mSM_ovlptr_dllcnv(&mSM_menu_ovl_init, submenu, mSM_DLF_SUBMENU_OVL);
        submenu->draw_proc = (SUBMENU_GAME_PROC)&none_proc1;
        submenu->process_status = mSM_PROCESS_PLAY;
        submenu->open_flag = TRUE;
        submenu->after_mode = 7;
        submenu->unk_164 = 0;
        mMl_clear_mail(&submenu->mail);
        submenu->item_p = &submenu->items[0];
        submenu->item_num = mPr_POCKETS_SLOT_COUNT;

        item = submenu->item_p;
        for (i = 0; i < submenu->item_num; i++) {
            item->item = EMPTY_NO;
            item->slot_no = mPr_POCKETS_SLOT_COUNT;
            item++;
        }

        if (submenu->mode != mSM_MODE_OTHER) {
            if ((submenu->menu_type == mSM_OVL_LEDIT && submenu->param0 == 0) ||
                Common_Get(now_private)->gender == mPr_SEX_MALE) {
                sAdo_SpecChange(5);
            } else {
                sAdo_SpecChange(6);
            }

            sAdo_SetVoiceMode(0);
        }
    }
}

static void mSM_move_Play(Submenu* submenu) {
    (*submenu->move_proc)(submenu);
}

static void mSM_move_End(Submenu* submenu) {
    mSM_dlftbl_c* dlftbl = &SubmenuArea_dlftbl[mSM_DLF_SUBMENU_OVL];
    GAME_PLAY* play = (GAME_PLAY*)gamePT;

    (*submenu->move_proc)(submenu);
    submenu->process_status = mSM_PROCESS_WAIT;
    submenu->menu_type = mSM_OVL_NONE;
    submenu->wait_timer = 2;
    submenu->open_flag = FALSE;
    SetGameFrame(1);

    if (submenu->mode != mSM_MODE_OTHER) {
        mMsg_Window_c* msg_win = mMsg_Get_base_window_p();
        submenu->mode = mSM_MODE_IDLE;
        mSc_dmacopy_all_exchange_bank(&play->object_exchange);
        SubmenuArea_DoUnlink(dlftbl, submenu);
        load_player(submenu);
        mSM_load_player_anime(play);
        submenu->start_refuse_timer = 1;

        if (submenu->after_mode == 12) {
            mPlib_request_main_demo_wait_from_submenu((ACTOR*)submenu->overlay->menu_info[mSM_OVL_MSCORE].data2);
        }

        if (mMsg_Check_main_hide(msg_win) == FALSE && mMsg_Check_not_series_main_wait(msg_win)) {
            mMsg_sound_spec_change_voice(msg_win);
        }
    }
}

extern void mSM_submenu_move(Submenu* submenu) {
    static SUBMENU_PROC move_proc[mSM_PROCESS_NUM] = { &mSM_move_Wait, &mSM_move_PREWait, &mSM_move_LINKWait,
                                                       &mSM_move_Play, &mSM_move_End };

    (*move_proc[submenu->process_status])(submenu);
}

extern void mSM_submenu_draw(Submenu* submenu, GAME* game) {
    if (submenu->mode >= mSM_MODE_PRERENDER_DONE && submenu->process_status == mSM_PROCESS_PLAY &&
        SubmenuArea_visit == &SubmenuArea_dlftbl[mSM_DLF_SUBMENU_OVL]) {
        (*submenu->draw_proc)(submenu, game);
    }
}

static int mSM_check_item_for_furniture(int slot_no, int param_2) {
    Private_c* priv = Common_Get(now_private);
    mActor_name_t item = priv->inventory.pockets[slot_no];
    int cat = ITEM_NAME_GET_CAT(item);
    int res = FALSE;

    if (item != EMPTY_NO && mPr_GET_ITEM_COND(priv->inventory.item_conditions, slot_no) == mPr_ITEM_COND_NORMAL &&
        ITEM_NAME_GET_TYPE(item) == NAME_TYPE_ITEM1 && cat != ITEM1_CAT_FISH && cat != ITEM1_CAT_KABU &&
        cat != ITEM1_CAT_INSECT && item != ITM_KNIFE_AND_FORK &&
        !(item >= ITM_EXCERCISE_CARD00 && item <= ITM_EXCERCISE_CARD12)) {
        res = TRUE;
    }

    return res;
}

static int mSM_check_item_for_quest(int slot_no, int param_2) {
    Private_c* priv = Common_Get(now_private);
    mActor_name_t item = priv->inventory.pockets[slot_no];
    int res = FALSE;

    if (item != EMPTY_NO && !(item >= ITM_EXCERCISE_CARD00 && item <= ITM_EXCERCISE_CARD12) &&
        item != ITM_KNIFE_AND_FORK && !(item >= ITM_SPIRIT0 && item <= ITM_SPIRIT4)) {
        res = TRUE;
    }

    return res;
}

static int mSM_check_item_for_sell(int slot_no, int param_2) {
    Private_c* priv = Common_Get(now_private);
    mActor_name_t item = priv->inventory.pockets[slot_no];
    int res = FALSE;

    if (item != EMPTY_NO && mPr_GET_ITEM_COND(priv->inventory.item_conditions, slot_no) == mPr_ITEM_COND_NORMAL &&
        (ITEM_NAME_GET_TYPE(item) != NAME_TYPE_ITEM1 || ITEM_NAME_GET_CAT(item) != ITEM1_CAT_MONEY) &&
        !(item >= ITM_EXCERCISE_CARD00 && item <= ITM_EXCERCISE_CARD12) && item != ITM_KNIFE_AND_FORK &&
        !(item >= ITM_SPIRIT0 && item <= ITM_SPIRIT4)) {
        res = TRUE;
    }

    return res;
}

static int mSM_check_item_for_give(int slot_no, int param_2) {
    Private_c* priv = Common_Get(now_private);
    mActor_name_t item = priv->inventory.pockets[slot_no];
    int res = FALSE;

    if (item != EMPTY_NO && mPr_GET_ITEM_COND(priv->inventory.item_conditions, slot_no) == mPr_ITEM_COND_NORMAL &&
        !(item >= ITM_EXCERCISE_CARD00 && item <= ITM_EXCERCISE_CARD12) && item != ITM_KNIFE_AND_FORK &&
        !(item >= ITM_SPIRIT0 && item <= ITM_SPIRIT4)) {
        res = TRUE;
    }

    return res;
}

static int mSM_check_item_for_take(int slot_no, int param_2) {
    Private_c* priv = Common_Get(now_private);
    mActor_name_t item = priv->inventory.pockets[slot_no];
    int cat = ITEM_NAME_GET_CAT(item);
    int res = FALSE;

    if (item != EMPTY_NO && mPr_GET_ITEM_COND(priv->inventory.item_conditions, slot_no) == mPr_ITEM_COND_NORMAL &&
        !(item >= ITM_EXCERCISE_CARD00 && item <= ITM_EXCERCISE_CARD12) && item != ITM_KNIFE_AND_FORK &&
        !(item >= ITM_SPIRIT0 && item <= ITM_SPIRIT4) &&
        (param_2 == 0 || (ITEM_NAME_GET_TYPE(item) == NAME_TYPE_ITEM1 &&
                          ((cat == ITEM1_CAT_FISH && param_2 == 1) || (cat == ITEM1_CAT_INSECT && param_2 == 2))))) {
        res = TRUE;
    }

    return res;
}

static int mSM_check_item_for_minidisk(int slot_no, int param_2) {
    Private_c* priv = Common_Get(now_private);
    mActor_name_t item = priv->inventory.pockets[slot_no];

    if (mPr_GET_ITEM_COND(priv->inventory.item_conditions, slot_no) == mPr_ITEM_COND_NORMAL &&
        ITEM_NAME_GET_TYPE(item) == NAME_TYPE_ITEM1 && ITEM_NAME_GET_CAT(item) == ITEM1_CAT_MINIDISK) {
        return TRUE;
    }

    return FALSE;
}

static int mSM_check_item_for_shrine(int slot_no, int param_2) {
    Private_c* priv = Common_Get(now_private);
    if (mPr_GET_ITEM_COND(priv->inventory.item_conditions, slot_no) == mPr_ITEM_COND_QUEST &&
        mQst_CheckLimitbyPossessionIdx(slot_no)) {
        return TRUE;
    }

    return FALSE;
}

static int mSM_check_item_for_entrust(int slot_no, int param_2) {
    Private_c* priv = Common_Get(now_private);
    mActor_name_t item = priv->inventory.pockets[slot_no];
    int res = FALSE;

    if (item == EMPTY_NO ||
        mPr_GET_ITEM_COND(priv->inventory.item_conditions, slot_no) == mPr_ITEM_COND_NORMAL &&
            (ITEM_NAME_GET_TYPE(item) != NAME_TYPE_ITEM1 || ITEM_NAME_GET_CAT(item) != ITEM1_CAT_MONEY) &&
            !(item >= ITM_EXCERCISE_CARD00 && item <= ITM_EXCERCISE_CARD12) && item != ITM_KNIFE_AND_FORK &&
            !(item >= ITM_SPIRIT0 && item <= ITM_SPIRIT4)) {
        res = TRUE;
    }

    return res;
}

static int mSM_check_item_for_exchange(int slot_no, int exchange_id) {
    int res = FALSE;
    Private_c* priv = Common_Get(now_private);
    mActor_name_t item = priv->inventory.pockets[slot_no];

    if (item != EMPTY_NO && mPr_GET_ITEM_COND(priv->inventory.item_conditions, slot_no) == mPr_ITEM_COND_NORMAL &&
        item != ITM_SIGNBOARD && item != ITM_KNIFE_AND_FORK &&
        !(item >= ITM_EXCERCISE_CARD00 && item <= ITM_EXCERCISE_CARD12)) {
        if ((ITEM_NAME_GET_TYPE(item) == NAME_TYPE_ITEM1 && ITEM_NAME_GET_CAT(item) == ITEM1_CAT_FISH) &&
            (ITEM_NAME_GET_TYPE(exchange_id) != NAME_TYPE_ITEM1 || ITEM_NAME_GET_CAT(exchange_id) != ITEM1_CAT_FISH)) {
            PLAYER_ACTOR* player = GET_PLAYER_ACTOR_NOW();
            xyz_t water_pos;

            if (mCoBG_SearchWaterLimitDistN(&water_pos, player->actor_class.world.position,
                                            player->actor_class.shape_info.rotation.y, 120.0f, 12) == FALSE) {
                return FALSE;
            }

            xyz_t_move(&((GAME_PLAY*)gamePT)->submenu.water_pos, &water_pos);
        }

        res = TRUE;
    }

    return res;
}

static int mSM_check_item_for_curator(int slot_no, int param_2) {
    Private_c* priv = Common_Get(now_private);
    mActor_name_t item = priv->inventory.pockets[slot_no];
    int res = FALSE;

    if (item != EMPTY_NO && mPr_GET_ITEM_COND(priv->inventory.item_conditions, slot_no) == mPr_ITEM_COND_NORMAL &&
        item != ITM_KNIFE_AND_FORK && !(item >= ITM_EXCERCISE_CARD00 && item <= ITM_EXCERCISE_CARD12)) {
#ifdef PC_ENHANCEMENTS
        /* only show items the curator can accept */
        if (item == ITM_FOSSIL) {
            res = TRUE; /* fossils are always selectable */
        } else if (mMmd_GetDisplayInfo(item) == mMmd_DISPLAY_CAN_DONATE) {
            res = TRUE;
        }
#else
        res = TRUE;
#endif
    }

    return res;
}

typedef int (*mSM_INVENTORY_CHECK_PROC)(int, int);

extern int mSM_check_open_inventory_itemlist(int type, int param_2) {
    static mSM_INVENTORY_CHECK_PROC check_process[mSM_IV_OPEN_NUM] = { NULL,
                                                                       NULL,
                                                                       &mSM_check_item_for_entrust,
                                                                       NULL,
                                                                       &mSM_check_item_for_quest,
                                                                       &mSM_check_item_for_sell,
                                                                       &mSM_check_item_for_give,
                                                                       NULL,
                                                                       &mSM_check_item_for_take,
                                                                       &mSM_check_item_for_furniture,
                                                                       &mSM_check_item_for_minidisk,
                                                                       &mSM_check_item_for_shrine,
                                                                       NULL,
                                                                       &mSM_check_item_for_exchange,
                                                                       NULL,
                                                                       &mSM_check_item_for_curator,
                                                                       NULL };

    mSM_INVENTORY_CHECK_PROC check_proc = check_process[type];
    int i;
    int res = 0;

    if (check_proc == NULL) {
        return 0xFFFF; /* all slots are valid */
    }

    for (i = 0; i < mPr_POCKETS_SLOT_COUNT; i++) {
        if ((*check_proc)(i, param_2) != FALSE) {
            res |= (1 << i); // slot is valid
        }
    }

    return res;
}

static char* mSM_Object_Exchange_keep_new(GAME_PLAY* play, s16 bank_id, size_t size) {
    Object_Exchange_c* exchange = &play->object_exchange;
    Object_Bank_c* bank = &exchange->banks[exchange->bank_idx];

    bank->bank_id = bank_id;
    bank->dma_start = exchange->next_bank_ram_address;
    bank->ram_start = exchange->next_bank_ram_address;
    bank->size = size;

    if (exchange->bank_idx < mSc_OBJECT_BANK_NUM - 1) {
        exchange->next_bank_ram_address = (char*)ALIGN_NEXT((u32)(exchange->next_bank_ram_address + size), 16);
        exchange->bank_idx++;
    }

    return bank->dma_start;
}

static void mSM_Object_Exchange_keep_new_MenuTexAndPallet(GAME_PLAY* play, int idx) {
    char* tex_p = mSM_Object_Exchange_keep_new(play, ACTOR_OBJ_BANK_14, (32 * 32) / 2);
    char* pal_p = mSM_Object_Exchange_keep_new(play, ACTOR_OBJ_BANK_15, 16 * sizeof(u16));

    play->submenu_ground_tex[idx] = tex_p;
    play->submenu_ground_pallet[idx] = pal_p;
    mPlib_Load_PlayerTexAndPallet(tex_p, pal_p, Common_Get(now_private)->backgound_texture - ITM_CLOTH_START);
}

extern void mSM_Object_Exchange_keep_new_Menu(GAME_PLAY* play) {
    play->submenu_ground_idx = 0;
    mSM_Object_Exchange_keep_new_MenuTexAndPallet(play, 0);
    mSM_Object_Exchange_keep_new_MenuTexAndPallet(play, 1);
}

extern u8* mSM_Get_ground_tex_p(GAME_PLAY* play) {
    int idx = play->submenu_ground_idx;

    if (idx < 0) {
        return NULL;
    }

    return (u8*)play->submenu_ground_tex[idx];
}

extern u16* mSM_Get_ground_pallet_p(GAME_PLAY* play) {
    int idx = play->submenu_ground_idx;

    if (idx < 0) {
        return NULL;
    }

    return (u16*)play->submenu_ground_pallet[idx];
}
