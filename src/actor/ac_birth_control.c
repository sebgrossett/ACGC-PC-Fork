#include "ac_birth_control.h"

#include "m_play.h"
#include "m_field_info.h"
#include "m_common_data.h"
#include "GBA2/gba2.h"
#include "m_player_lib.h"

static void aBC_actor_ct(ACTOR*, GAME*);
static void aBC_actor_move(ACTOR*, GAME*);
#ifdef TARGET_PC
static int aBC_wide_block_is_built(BIRTH_CONTROL_ACTOR*, int, int);
static void aBC_wide_block_set_built(BIRTH_CONTROL_ACTOR*, int, int);
static void aBC_wide_block_clear_built(BIRTH_CONTROL_ACTOR*, int, int);
#endif

ACTOR_PROFILE Birth_Control_Profile = {
  mAc_PROFILE_BIRTH_CONTROL,
  ACTOR_PART_ITEM,
  ACTOR_STATE_NO_MOVE_WHILE_CULLED | ACTOR_STATE_NO_DRAW_WHILE_CULLED,
  EMPTY_NO,
  ACTOR_OBJ_BANK_KEEP,
  sizeof(BIRTH_CONTROL_ACTOR),
  &aBC_actor_ct,
  mActor_NONE_PROC1,
  &aBC_actor_move,
  mActor_NONE_PROC1,
  NULL
};

static f32 aBC_pos_table[UT_BASE_NUM] = {
  mFI_UT_WORLDSIZE_X_F *  0 + mFI_UT_WORLDSIZE_HALF_X_F,  //  20.0f
  mFI_UT_WORLDSIZE_X_F *  1 + mFI_UT_WORLDSIZE_HALF_X_F,  //  60.0f
  mFI_UT_WORLDSIZE_X_F *  2 + mFI_UT_WORLDSIZE_HALF_X_F,  // 100.0f
  mFI_UT_WORLDSIZE_X_F *  3 + mFI_UT_WORLDSIZE_HALF_X_F,  // 140.0f
  mFI_UT_WORLDSIZE_X_F *  4 + mFI_UT_WORLDSIZE_HALF_X_F,  // 180.0f
  mFI_UT_WORLDSIZE_X_F *  5 + mFI_UT_WORLDSIZE_HALF_X_F,  // 220.0f
  mFI_UT_WORLDSIZE_X_F *  6 + mFI_UT_WORLDSIZE_HALF_X_F,  // 260.0f
  mFI_UT_WORLDSIZE_X_F *  7 + mFI_UT_WORLDSIZE_HALF_X_F,  // 300.0f
  mFI_UT_WORLDSIZE_X_F *  8 + mFI_UT_WORLDSIZE_HALF_X_F,  // 340.0f
  mFI_UT_WORLDSIZE_X_F *  9 + mFI_UT_WORLDSIZE_HALF_X_F,  // 380.0f
  mFI_UT_WORLDSIZE_X_F * 10 + mFI_UT_WORLDSIZE_HALF_X_F,  // 420.0f
  mFI_UT_WORLDSIZE_X_F * 11 + mFI_UT_WORLDSIZE_HALF_X_F,  // 460.0f
  mFI_UT_WORLDSIZE_X_F * 12 + mFI_UT_WORLDSIZE_HALF_X_F,  // 500.0f
  mFI_UT_WORLDSIZE_X_F * 13 + mFI_UT_WORLDSIZE_HALF_X_F,  // 540.0f
  mFI_UT_WORLDSIZE_X_F * 14 + mFI_UT_WORLDSIZE_HALF_X_F,  // 580.0f
  mFI_UT_WORLDSIZE_X_F * 15 + mFI_UT_WORLDSIZE_HALF_X_F   // 620.0f
};

static void aBC_deleteActor_part(BIRTH_CONTROL_ACTOR* birth_control, GAME_PLAY* play, int part) {
  mFI_block_tbl_c* last_block_table = &play->last_block_table;
  s8 last_bx = last_block_table->block_x;
  s8 last_bz = last_block_table->block_z;
  s8 now_bx = play->block_table.block_x;
  s8 now_bz = play->block_table.block_z;
  s8 check_bx;
  s8 check_bz;
  ACTOR* actor = play->actor_info.list[part].actor;
  int dx;
  int dz;

  while (TRUE) {
    if (actor == NULL) {
      break;
    }

    check_bx = actor->block_x;
    check_bz = actor->block_z;

#ifdef TARGET_PC
    /* PC draws a 5x5 window around every player (terrain and props), so
     * actors inside it must survive too, single player included — the retail
     * rule (current + last acre only) pops structures at the screen edge. */
    if (Actor_is_near_active_player(play, actor, 2) == TRUE) {
      actor = actor->next_actor;
      continue;
    }
#endif

    if (g_mPlib_wade_disabled) {
      dx = ABS(check_bx - now_bx);
      dz = ABS(check_bz - now_bz);

      /* Keep actors in adjacent acres alive so borderless camera movement does not cause pop-in. */
      if (check_bx >= 0 && check_bz >= 0) {
        if (dx > 1 || dz > 1) {
#ifdef TARGET_PC
          aBC_wide_block_clear_built(birth_control, check_bx, check_bz);
#endif
          Actor_delete(actor);
        }
      }
    } else {
      /* Delete any actors which aren't in the current block or the last block */
      if (
        (check_bx >= 0 && check_bx != last_bx && check_bx != now_bx) &&
        (check_bz >= 0 && check_bz != last_bz && check_bz != now_bz)
      ) {
#ifdef TARGET_PC
        aBC_wide_block_clear_built(birth_control, check_bx, check_bz);
#endif
        Actor_delete(actor);
      }
    }

    actor = actor->next_actor;
  }
}

#ifdef TARGET_PC
/* The player rescan walks an acre that already spawned this prop. A second
 * actor at the same spot is the doubled gyroid. Clear the name and move on.
 * ITEM and BG are the two parts props live in: the gyroid (haniwa) profile is
 * ACTOR_PART_BG, so an ITEM-only search never saw the first one and a rescan
 * that regained the FG name spawned a second. The wide window rescan runs this
 * for every candidate item every frame, so this stays at those two parts
 * instead of the full ten-part walk. */
static int aBC_actor_already_at(GAME_PLAY* play, s16 profile, mActor_name_t actor_id, f32 pos_x, f32 pos_z) {
  static const int part_table[2] = { ACTOR_PART_ITEM, ACTOR_PART_BG };
  int i;

  for (i = 0; i < 2; i++) {
    ACTOR* actor = play->actor_info.list[part_table[i]].actor;

    while (actor != NULL) {
      f32 dx;
      f32 dz;

      if ((actor->id == profile) && (actor->npc_id == actor_id)) {
        dx = actor->world.position.x - pos_x;
        dz = actor->world.position.z - pos_z;
        if ((dx * dx + dz * dz) < (36.0f * 36.0f)) {
          return TRUE;
        }
      }

      actor = actor->next_actor;
    }
  }

  return FALSE;
}
#endif

static int aBC_setupOtherActor(GAME_PLAY* play, mActor_name_t actor_id, s16 profile, f32 pos_x, f32 pos_z,
                               mActor_name_t clear_item, int bx, int bz, int* budget) {
  ACTOR* actor;
  xyz_t pos;
  int res = FALSE;

  pos.x = pos_x;
  pos.z = pos_z;
  pos.y = mCoBG_GetBgY_OnlyCenter_FromWpos2(pos, 0.0f);
#ifdef TARGET_PC
  if (aBC_actor_already_at(play, profile, actor_id, pos_x, pos_z) != FALSE) {
    /* Init writes the dummy once. A later scan must not replace it with RSV,
     * or the A check no longer sees a mailbox. */
    if ((actor_id >= ACTOR_PROP_MAILBOX0) && (actor_id <= ACTOR_PROP_MAILBOX3)) {
      clear_item = (mActor_name_t)(DUMMY_MAILBOX0 + (actor_id - ACTOR_PROP_MAILBOX0));
    }
    mFI_SetFG_common(clear_item, pos, FALSE);
    return FALSE;
  }
#endif
  actor = Actor_info_make_actor(
    &play->actor_info,
    (GAME*)play,
    profile,
    pos.x, pos.y, pos.z,
    0, 0, 0,
    bx, bz,
    -1,
    actor_id,
    actor_id,
    -1,
    -1
  );

  if (actor != NULL) {
    actor->restore_fg = TRUE;
    mFI_SetFG_common(clear_item, pos, FALSE);
    if (budget != NULL) {
      (*budget)--;
    }
  }
  else {
    res = TRUE;
  }

  return res;
}

/* Scan one acre and spawn what it holds. budget (may be NULL) limits how many
 * new actors this call creates; when it runs out the scan stops early and a
 * later call resumes — spawned items have their FG name cleared, so the
 * resume never makes a second copy. Returns the retry flag: TRUE when
 * something failed and the retail current-acre rescan should run again. */
static int aBC_setupActor(BIRTH_CONTROL_ACTOR* birth_control, GAME_PLAY* play, int bx, int bz, int* budget) {
  mActor_name_t* item_p = mFI_BkNumtoUtFGTop(bx, bz);
  f32 base_x = bx * mFI_BK_WORLDSIZE_X_F;
  f32 base_z = bz * mFI_BK_WORLDSIZE_Z_F;
  int setup_actor_flag = FALSE;
  mActor_name_t clear_item;
  int ut_z;
  int ut_x;

  if (item_p == NULL) {
    return FALSE;
  }

  for (ut_z = 0; ut_z < UT_Z_NUM; ut_z++) {
    for (ut_x = 0; ut_x < UT_X_NUM; ut_x++) {
      if (budget != NULL && *budget <= 0) {
        return setup_actor_flag;
      }
#ifdef TARGET_PC
      /* The live shrine replaces its FG name with DUMMY_SHRINE. Deleting the
       * actor does not always put WISHING_WELL back, and a later scan then
       * walks past the spot. Put the structure name back when no shrine actor
       * is alive so this same pass can spawn it. */
      if ((*item_p == DUMMY_SHRINE) &&
          (Actor_info_name_search(&play->actor_info, mAc_PROFILE_SHRINE, ACTOR_PART_ITEM) == NULL)) {
        *item_p = WISHING_WELL;
      }
#endif
      switch (ITEM_NAME_GET_TYPE(*item_p)) {
        case NAME_TYPE_ITEM2:
        {
          int idx = *item_p - ETC_START;

          setup_actor_flag |= aBC_setupOtherActor(play, *item_p, move_obj_profile_table[idx],
                                                  base_x + aBC_pos_table[ut_x],
                                                  base_z + aBC_pos_table[ut_z], EMPTY_NO, bx, bz, budget);
          break;
        }

        case NAME_TYPE_PROPS:
        {
          int idx;

          if (*item_p >= SNOWMAN0 && *item_p <= SNOWMAN8) {
            clear_item = EMPTY_NO;
          }
          else {
            clear_item = RSV_NO;
          }

          idx = *item_p - ACTOR_PROP_START;
          setup_actor_flag |= aBC_setupOtherActor(play, *item_p, props_profile_table[idx],
                                                  base_x + aBC_pos_table[ut_x],
                                                  base_z + aBC_pos_table[ut_z], clear_item, bx, bz, budget);
          break;
        }

        case NAME_TYPE_STRUCT:
#ifdef TARGET_PC
          /* An existing structure "succeeds" on every rescan (the setup proc
           * returns the actor it finds), and that success used to eat the
           * wide-window spawn budget before it ever reached the other
           * players' acres. Dedupe here instead: actor already up -> clear
           * the fg name and move on, no spawn, no budget. */
          if (*item_p == DUMMY_SHRINE) {
            /* The fixup above only leaves DUMMY_SHRINE in place when the
             * live shrine actor is up; keep the name, spawn nothing. */
            break;
          }
          if (Actor_info_fgName_search(&play->actor_info, *item_p, ACTOR_PART_ITEM) != NULL) {
            xyz_t struct_pos;

            struct_pos.x = base_x + aBC_pos_table[ut_x];
            struct_pos.z = base_z + aBC_pos_table[ut_z];
            struct_pos.y = mCoBG_GetBgY_OnlyCenter_FromWpos2(struct_pos, 0.0f);
            mFI_SetFG_common(RSV_NO, struct_pos, FALSE);
            break;
          }
          if (budget != NULL && *budget <= 0) {
            break;
          }
#endif
          if (CLIP(structure_clip) != NULL) {
            STRUCTURE_ACTOR* actor = CLIP(structure_clip)->setup_actor_proc((GAME*)play, *item_p, -1,
                                                                            base_x + aBC_pos_table[ut_x],
                                                                            base_z + aBC_pos_table[ut_z]);
            setup_actor_flag |= actor == NULL;
            if (actor != NULL && budget != NULL) {
              (*budget)--;
            }
          }

          break;
      }

      item_p++;
    }
  }

  return setup_actor_flag;
}

static int aBC_setupCommonMvActor(GAME_PLAY* play, mFM_move_actor_c* mv_actor_list, int mv_actor_list_no, s16 profile,
                                  f32 pos_x, f32 pos_z, int bx, int bz) {
  Actor_info* actor_info = &play->actor_info;
  xyz_t pos;
  f32 y;
  int res = FALSE;

  pos.x = pos_x;
  pos.z = pos_z;
  pos.y = mCoBG_GetBgY_OnlyCenter_FromWpos(pos, 0.0f);

  if (Actor_info_make_actor(
    actor_info,
    (GAME*)play,
    profile,
    pos.x, pos.y, pos.z,
    0, 0, 0,
    bx, bz,
    mv_actor_list_no,
    mv_actor_list->name_id,
    mv_actor_list->arg,
    mv_actor_list->npc_info_idx,
    -1
  ) != NULL) {
    res = TRUE;
  }

  return res;
}

static u16 aBC_setupMvActor(BIRTH_CONTROL_ACTOR* birth_control, GAME_PLAY* play, u16 mv_actor_bitfield,
                            mFM_move_actor_c* mv_actor_list_p, int bx, int bz, f32 base_x, f32 base_z) {
  
  if (mv_actor_list_p != NULL) {
    int was_born;
    int i;

    for (i = 0; i < mFM_MOVE_ACTOR_NUM; i++) {
      if (((mv_actor_bitfield >> i) & 1) == 1) {
        mActor_name_t mv_actor_name = mv_actor_list_p->name_id;

        switch (ITEM_NAME_GET_TYPE(mv_actor_name)) {
          case NAME_TYPE_ITEM2:
            was_born = aBC_setupCommonMvActor(play, mv_actor_list_p, i, move_obj_profile_table[mv_actor_name - ETC_START],
                                              base_x + aBC_pos_table[mv_actor_list_p->ut_x],
                                              base_z + aBC_pos_table[mv_actor_list_p->ut_z], bx, bz);
            break;
          case NAME_TYPE_ACTOR:
            was_born = aBC_setupCommonMvActor(play, mv_actor_list_p, i, actor_profile_table[mv_actor_name - MISC_ACTOR_START],
                                              base_x + aBC_pos_table[mv_actor_list_p->ut_x],
                                              base_z + aBC_pos_table[mv_actor_list_p->ut_z], bx, bz);
            break;
          case NAME_TYPE_SPNPC:
          case NAME_TYPE_NPC:
            if (Common_Get(clip).npc_clip != NULL && Common_Get(clip).npc_clip->setupActor_proc != NULL) {
              was_born = (*Common_Get(clip).npc_clip->setupActor_proc)(play, mv_actor_name, mv_actor_list_p->npc_info_idx,
                                                                       i, mv_actor_list_p->arg, bx, bz,
                                                                       mv_actor_list_p->ut_x, mv_actor_list_p->ut_z);
            }
            else {
              was_born = FALSE;
            }
            break;
          default:
            was_born = FALSE;
            break;
        }

        if (was_born == TRUE) {
          mv_actor_bitfield = ~(1 << i) & mv_actor_bitfield;
        }
      }

      mv_actor_list_p++;
    }

  }

  return mv_actor_bitfield;
}

static int aBC_chk_near_boat_block(BIRTH_CONTROL_ACTOR* birth_control, GAME_PLAY* play) {
  /* Check to spawn boat while in E-5 or F-4 */
  static int chk_bx[] = { 5, 4 }; // 4 & 5 column
  static int chk_bz[] = { 5, 6 }; // E & F row
  int res = FALSE;
  int i;

  for (i = 0; i < 2; i++) {
    if (play->block_table.block_x == chk_bx[i] && play->block_table.block_z == chk_bz[i]) {
      if (birth_control->boat_spawned == FALSE) {
        mGcgba_InitVar();
        birth_control->boat_spawned = TRUE;
      }
      
      res = TRUE;
      break;
    }
  }

  return res;
}

static void aBC_set_boat(BIRTH_CONTROL_ACTOR* birth_control, GAME_PLAY* play) {
  if (mEv_IsNotTitleDemo() && aBC_chk_near_boat_block(birth_control, play) == TRUE) {
    mActor_name_t* boat_ut_p = mFI_UtNum2UtFG(5 * UT_X_NUM + 5, 6 * UT_Z_NUM + 10); // Set boat at F-5, unit 5-10 (x-z)

    if (boat_ut_p != NULL) {
      mActor_name_t boat_item = *boat_ut_p;

      switch (mGcgba_ConnectEnabled()) {
        case GBA2_GBA_STATE_SUCCESS:
          /* Successfully connected to the GBA */
          mGcgba_InitVar();
          boat_item = BOAT; // set boat
          break;
        default:
          /* Failed to connect to the GBA */
          mGcgba_InitVar();
          boat_item = EMPTY_NO; // clear boat
          break;
        case GBA2_GBA_STATE_TRANSMITTING:
          /* Still transmitting */
          break;
      }

      *boat_ut_p = boat_item;
    }
  }
  else {
    /* We're not in a boat acre, so allow initial communication again */
    birth_control->boat_spawned = FALSE;
  }
}

static const BlockOrUnit_c l_near_block_lookup_table[][3] = {
  { { 0, -1 }, { -1, -1 }, { -1, 0 } },
  { { 0, 1 }, { -1, 1 }, { -1, 0 } },
  { { 0, -1 }, { 1, -1 }, { 1, 0 } },
  { { 0, 1 }, { 1, 1 }, { 1, 0 } },
};

static void aBC_spawn_actors_in_nearby_blocks(BIRTH_CONTROL_ACTOR* birth_control, GAME_PLAY* play) {
  const BlockOrUnit_c* ofs_p = l_near_block_lookup_table[birth_control->last_quadrant];
  int i;

  for (i = 0; i < 3; i++) {
    int bx = play->block_table.block_x + ofs_p[i].x;
    int bz = play->block_table.block_z + ofs_p[i].z;

    if (mFI_BlockCheck(bx, bz) == TRUE) {
      f32 pos_x;
      f32 pos_z;
      u16 bit_data;

      aBC_setupActor(birth_control, play, bx, bz, NULL);

      birth_control->move_actor_data[1 + i] = mFI_MoveActorListDma(bx, bz);
      mNpc_AddActor_inBlock(birth_control->move_actor_data[1 + i], bx, bz);

      mFI_BkNum2WposXZ(&pos_x, &pos_z, bx, bz);
      bit_data = aBC_setupMvActor(birth_control, play, mFI_GetMoveActorBitData(bx, bz),
                                  birth_control->move_actor_data[1 + i], bx, bz, pos_x, pos_z);
      mFI_SetMoveActorBitData(bx, bz, bit_data);
    }
  }
}

static int aBC_nearby_refresh_request = FALSE;

void aBC_RequestNearbyRefresh(void) {
  aBC_nearby_refresh_request = TRUE;
}

static int aBC_check_update_actors_in_nearby_blocks(BIRTH_CONTROL_ACTOR* birth_control, GAME_PLAY* play) {
  int quadrant;
  int block_ux;
  int block_uz;
  ACTOR* player = GET_PLAYER_ACTOR_ACTOR(play);

#ifdef TARGET_PC
  /* The selected resident can already be indoors. Follow whoever is still here. */
  if (player == NULL) {
    player = (ACTOR*)mPlib_extra_player_at(0);
  }

  if (player == NULL) {
    return FALSE;
  }
#endif

  if (mFI_Wpos2UtNum_inBlock(&block_ux, &block_uz, player->world.position) == FALSE) {
    return FALSE;
  }

  quadrant = block_ux >= 8;
  quadrant |= (block_uz >= 8) << 1;

  if (quadrant != birth_control->last_quadrant) {
    birth_control->last_quadrant = quadrant;
    return TRUE;
  }

  return FALSE;
}

static void aBC_actor_ct(ACTOR* actorx, GAME* game) {
  BIRTH_CONTROL_ACTOR* birth_control = (BIRTH_CONTROL_ACTOR*)actorx;

  birth_control->last_quadrant = -1;
#ifdef TARGET_PC
  bzero(birth_control->built_bits, sizeof(birth_control->built_bits));
#endif
}

#ifdef TARGET_PC
/* How many new actors the wide window may create per frame. The 5x5 window
 * holds up to 25 acres per player; draining it over frames hides the burst
 * that used to land on the scene-enter frame (the enter stutter), and the
 * nearest-first order means anything visible spawns first. */
#define aBC_WIDE_SPAWN_BUDGET 6

typedef struct {
    int bx;
    int bz;
    int dist;
} aBC_wide_block_c;

/* Built-acre bit helpers. A set bit means the acre scanned clean (nothing to
 * spawn, nothing failed), so the every-frame rescan skips its 256-unit walk.
 * A crossing clears every bit: fg can be written into a built acre after it
 * was built (the GBA boat write, event structures), and the re-drain is
 * budgeted so nothing pops. */
static int aBC_wide_block_is_built(BIRTH_CONTROL_ACTOR* birth_control, int bx, int bz) {
    int bit = bz * BLOCK_X_NUM + bx;

    return (birth_control->built_bits[bit >> 3] >> (bit & 7)) & 1;
}

static void aBC_wide_block_set_built(BIRTH_CONTROL_ACTOR* birth_control, int bx, int bz) {
    int bit = bz * BLOCK_X_NUM + bx;

    birth_control->built_bits[bit >> 3] |= (u8)(1 << (bit & 7));
}

static void aBC_wide_block_clear_built(BIRTH_CONTROL_ACTOR* birth_control, int bx, int bz) {
    int bit = bz * BLOCK_X_NUM + bx;

    birth_control->built_bits[bit >> 3] &= (u8)~(1 << (bit & 7));
}

/* An actor in acre (bx,bz) was deleted from outside the birth control pass
 * (the draw-time cull delete). The acre must rescan, or its deleted
 * structures never respawn: the acre re-enters a window with its built bit
 * still set and the pass skips it. */
extern void aBC_ClearBuiltBitForBlock(GAME_PLAY* play, int bx, int bz) {
    BIRTH_CONTROL_ACTOR* birth_control =
        (BIRTH_CONTROL_ACTOR*)Actor_info_name_search(&play->actor_info, mAc_PROFILE_BIRTH_CONTROL, ACTOR_PART_ITEM);

    if (birth_control != NULL && mFI_BlockCheck(bx, bz) == TRUE) {
        aBC_wide_block_clear_built(birth_control, bx, bz);
    }
}

static void aBC_wide_clear_built_bits(BIRTH_CONTROL_ACTOR* birth_control) {
    bzero(birth_control->built_bits, sizeof(birth_control->built_bits));
}

/* Scan one acre's structures and props against the budget. Returns TRUE when
 * the acre scanned clean (nothing spawned, nothing failed) and can be marked
 * built; a partial scan (budget ran out mid-acre) stays unbuilt and resumes
 * next frame — spawned items have their FG name cleared, so the resume never
 * makes a second copy. */
static int aBC_wide_scan_block(BIRTH_CONTROL_ACTOR* birth_control, GAME_PLAY* play, int bx, int bz, int* budget) {
    int retry;
    int budget_before = *budget;

    retry = aBC_setupActor(birth_control, play, bx, bz, budget);

    return retry == FALSE && *budget == budget_before;
}

/* Spawn one acre's move actors (villagers) from the per-block bit data the
 * retail current-acre path uses — deleting an actor turns its bit back on,
 * so re-entering the window respawns it. Runs for every window acre every
 * frame (the bit data changes as the schedule moves villagers around), so it
 * must stay cheap; the budget still throttles how many NPCs are born. */
static void aBC_wide_spawn_mv(BIRTH_CONTROL_ACTOR* birth_control, GAME_PLAY* play, int bx, int bz, int* budget) {
    static mFM_move_actor_c l_wide_aclist[mFM_MOVE_ACTOR_NUM];
    mFM_move_actor_c* move_actor_list;
    f32 pos_x;
    f32 pos_z;
    u16 before;
    u16 after;
    u16 spawned;

    move_actor_list = mFI_MoveActorListDmaTo(l_wide_aclist, bx, bz);
    mNpc_AddActor_inBlock(move_actor_list, (u8)bx, (u8)bz);

    mFI_BkNum2WposXZ(&pos_x, &pos_z, bx, bz);
    before = mFI_GetMoveActorBitData(bx, bz);
    after = aBC_setupMvActor(birth_control, play, before, move_actor_list, bx, bz, pos_x, pos_z);
    mFI_SetMoveActorBitData(bx, bz, after);

    spawned = before & ~after;
    while (spawned != 0) {
        spawned &= spawned - 1;
        (*budget)--;
    }
}

/* Spawn every acre in each player's 5x5 draw window (the same pattern as the
 * terrain and prop tables), nearest acre first, at most
 * aBC_WIDE_SPAWN_BUDGET new actors per frame. Retail builds only the crossed
 * acre, and the widescreen view sees past it, so structures, gates and signs
 * popped in at the screen edge. Every acre in the window rescans every frame
 * — a built acre is free (its fg names are cleared), so a spawn that failed
 * on a busy frame retries a frame or two later instead of never, and the
 * budget flows toward the acres that still need work. */
static void aBC_setup_wide_blocks(BIRTH_CONTROL_ACTOR* birth_control, GAME_PLAY* play) {
    aBC_wide_block_c blocks[PLAYER_NUM * 25];
    ACTOR* actor = play->actor_info.list[ACTOR_PART_PLAYER].actor;
    int count = 0;
    int budget = aBC_WIDE_SPAWN_BUDGET;
    int i;

    while (actor != NULL) {
        int px;
        int pz;

        if (mFI_Wpos2BlockNum(&px, &pz, actor->world.position) != FALSE) {
            int dx;
            int dz;

            for (dz = -2; dz <= 2; dz++) {
                for (dx = -2; dx <= 2; dx++) {
                    int bx = px + dx;
                    int bz = pz + dz;
                    int dist = ABS(dx) > ABS(dz) ? ABS(dx) : ABS(dz);
                    int j;

                    if (mFI_BlockCheck(bx, bz) == FALSE) {
                        continue;
                    }

                    for (j = 0; j < count; j++) {
                        if (blocks[j].bx == bx && blocks[j].bz == bz) {
                            if (dist < blocks[j].dist) {
                                blocks[j].dist = dist;
                            }
                            break;
                        }
                    }

                    if (j == count) {
                        blocks[count].bx = bx;
                        blocks[count].bz = bz;
                        blocks[count].dist = dist;
                        count++;
                    }
                }
            }
        }

        actor = actor->next_actor;
    }

    /* Nearest acre first: insertion sort (count <= 50). */
    for (i = 1; i < count; i++) {
        aBC_wide_block_c key = blocks[i];
        int j = i - 1;

        while (j >= 0 && blocks[j].dist > key.dist) {
            blocks[j + 1] = blocks[j];
            j--;
        }
        blocks[j + 1] = key;
    }

    for (i = 0; i < count && budget > 0; i++) {
        int bx = blocks[i].bx;
        int bz = blocks[i].bz;

        if (aBC_wide_block_is_built(birth_control, bx, bz) == FALSE) {
            if (aBC_wide_scan_block(birth_control, play, bx, bz, &budget) == TRUE) {
                aBC_wide_block_set_built(birth_control, bx, bz);
            }
        }

        aBC_wide_spawn_mv(birth_control, play, bx, bz, &budget);
    }
}
#endif

static void aBC_actor_move(ACTOR* actorx, GAME* game) {
  BIRTH_CONTROL_ACTOR* birth_control = (BIRTH_CONTROL_ACTOR*)actorx;
  GAME_PLAY* play = (GAME_PLAY*)game;

  if (Common_Get(bg_item_type) == 0) {
    birth_control->setup_actor_flag |= mFI_ActorisBorn() == TRUE;
    aBC_set_boat(birth_control, play);
  }

  if (mFI_ActorisBorn() == TRUE) {
    int bx = play->block_table.block_x;
    int bz = play->block_table.block_z;

#ifdef TARGET_PC
    /* An acre crossing can coincide with fg writes (GBA boat, event
     * structures) — re-scan the window over the next frames. */
    aBC_wide_clear_built_bits(birth_control);
#endif

    birth_control->move_actor_data[0] = mFI_MoveActorListDma(bx, bz);
    mNpc_AddActor_inBlock(birth_control->move_actor_data[0], bx, bz);

    if (birth_control->move_actor_data[0] != NULL) {
      birth_control->move_actor_bitfield = mFI_GetMoveActorBitData(bx, bz);
      birth_control->move_actor_list_exists_flag = TRUE;
    }
    else {
      birth_control->move_actor_bitfield = 0;
    }
  }

  g_fdinfo->born_actor = FALSE;
  
  if (play->game.pad_initialized == TRUE) {
    if (birth_control->setup_actor_flag) {
      aBC_deleteActor_part(birth_control, play, ACTOR_PART_ITEM);
      birth_control->setup_actor_flag =
        aBC_setupActor(birth_control, play, play->block_table.block_x, play->block_table.block_z, NULL);
    }

    if (birth_control->move_actor_list_exists_flag == TRUE && birth_control->move_actor_bitfield != 0) {
      int bx = play->block_table.block_x;
      int bz = play->block_table.block_z;

      aBC_deleteActor_part(birth_control, play, ACTOR_PART_NPC);
      birth_control->move_actor_bitfield = aBC_setupMvActor(birth_control, play, birth_control->move_actor_bitfield,
                                                            birth_control->move_actor_data[0],
                                                            play->block_table.block_x, play->block_table.block_z,
                                                            play->block_table.pos_x, play->block_table.pos_z);
      mFI_SetMoveActorBitData(bx, bz, birth_control->move_actor_bitfield);
    }

    if (aBC_nearby_refresh_request) {
      aBC_nearby_refresh_request = FALSE;
      birth_control->last_quadrant = -1;
    }

    if (g_mPlib_wade_disabled && aBC_check_update_actors_in_nearby_blocks(birth_control, play)) {
      aBC_spawn_actors_in_nearby_blocks(birth_control, play);
    }

#ifdef TARGET_PC
    aBC_setup_wide_blocks(birth_control, play);
#endif
  }

  /* Only refresh the list when transitioning between acres or transitioning between scenes */
  if (mFI_CheckPlayerWade(mFI_WADE_NONE) == TRUE && play->fb_fade_type == FADE_TYPE_NONE) {
    birth_control->move_actor_list_exists_flag = FALSE;
  }
}
