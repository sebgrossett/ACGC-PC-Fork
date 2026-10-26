# Animal Crossing (GameCube) PC Port: Local Co-op Plan

**Status:** Two residents walk outdoors in a left/right split. The clock is once per half, at single-player size. Mailbox close no longer freezes Player 1. Player 1 can enter their own house, and Player 2's camera stays put during that zoom. The mailbox and notice-board panels draw in the opener's half; alpha, blend, and cull bypasses were playtested and are not the fix. Player 2's half still disappears while Player 1 is indoors, which is deferred behind dual/multi-world instancing. The P1-shaped model on load was the guest replaying P1's door-exit state; fixed and playtested. Both players can talk to different villagers at the same time (playtested 2026-10-26). The title offers Single Player / Co-op / Settings / Quit (playtested 2026-10-26). Co-op starts full screen and guests join with Start; per-player session menus, boards, input isolation, the per-view host pause and menu backdrops are playtested. Houses survive join/leave and reload after the mid-game save fix. Remaining: simultaneous-menus slice; the 3-4 player pass is deferred by the operator (2026-10-26).

## 1. Project overview

- **Goal (primary):** Offline local split-screen co-op for 2-4 players on a shared map.
- **Goal (later):** Multi-town joins on one screen, offline travel between towns, and online visits (friend codes, host-authoritative).
- **Base:** `flyngmt/ACGC-PC-Port` (unofficial native PC port, 1.4k stars, MIT/LICENSE in repo) built on `ACreTeam/ac-decomp`.
- **Rendering:** GX calls are replaced with OpenGL 3.3. Game C code runs natively on x86.
- **Assets:** The repo contains no game assets. The user supplies a legally dumped USA disc image (GAFE01_00 Rev 0), read at runtime from `rom/`.
- **Work tree:** this repo. Disc image stays in the repo root and is gitignored.

## 2. Feasibility summary

| Feature | Difficulty | Notes |
|---|---|---|
| Menu rework (Single / Co-op / Settings) | Moderate | Reuse `player_select` screen; add town layer above it |
| Local shared-map co-op, 2-4 players | Hard (core work) | Single-player assumptions throughout; see section 5 |
| Join/leave mid-game (current town) | Moderate | Depends on the per-player refactor |
| Multi-town on one screen (instancing) | Hard | Deferred; global state is singletons |
| Offline visits (snapshot + merge) | Moderate | Deferred |
| Online visits (host-authoritative) | Hard | Long-term |

## 3. Design decisions (locked)

**Menu flow**
- Title → Single Player / Co-op / Settings
- Single Player: Town Select → Player Select (existing screen, reused)
- Co-op: P1 selects town → P1 selects character. Other controllers press a join button, then select a character. Start begins the session when at least one player has joined.
- Settings: unchanged

**Co-op scope (v1)**
- Local only, one active town per session, 2-4 players on one shared map
- Split-screen: one viewport per active player
- Co-op characters are residents of the chosen town (the 4 save slots are residents of one town, not four towns)

**Joining and leaving**
- Players can join mid-game into the current town. Joining from a different town is deferred.
- Players can leave at any time.
- Leave is queued until safe:
  - Finish the current action first
  - Cap the wait (force-finish or cancel after a timeout)
  - Block exit during group events until those events finish
  - Close open menus (shops, inventory) first
- On leave: character disappears from the map. Items, money, house, and save data remain with the player. Save is written.

**Saves**
- Co-op writes to the same save file as single player when it's the same town.
- Save on join and on leave.

**Conflicts**
- First-come-first-served. Same-frame ties broken by controller index (lower index wins).

**Input (v1)**
- Controls tab: Player 1–4. Choices are Key and Pad, Keyboard, Pad 1–4, or None.
- Default is Player 1 on Key and Pad (keyboard plus the first connected gamepad). Players 2–4 are None.
- The row being changed keeps the device. Key and Pad gives up only the device the other row took.
- Saved as `player1_input` … `player4_input` in `settings.ini`. Apply commits them. Pad channel 0 is player 1, so single-player follows that row.
- A join pressed during a host or group event waits until that event is safe to enter. Same rule as leave.

**Deferred (not v1)**
- Multi-town joins, offline travel, online visits
- AI "wandering" for absent players (would require an AFK/bot indicator)

## 4. Code points of interest

**Player count and save structure**
- `include/m_personal_id.h:13` — `PLAYER_NUM 4`, `TOTAL_PLAYER_NUM` includes foreigners
- `include/m_common_data.h:90-143` — per-player arrays (`private_data`, `homes`, `mother_mail`, `keep_house_size`)
- `include/m_card.h:139` — `pid[PLAYER_NUM]`
- `include/m_map_ovl.h:109` — `player_info[PLAYER_NUM]`

**Single-player assumptions (core refactor)**
- `GET_PLAYER_ACTOR*` calls `get_player_actor_withoutCheck`. That follows `Common_Get(player_no)` through `s_player_by_slot` in `src/game/m_player_lib.c`. One registered actor in the wrong slot is moved to `player_no`. No registered actor falls back to the player-list head. `mPlib_get_player_actor_main_index` is action state (`now_main_index`), not a save slot.
- `include/m_actor.h` — `Actor_list` is `num_actors` plus an `ACTOR*` head. `Actor_info_part_new` prepends, so the head is the newest actor. There is no fixed slot array.
- Scenes spawn one `mAc_PROFILE_PLAYER` from `Actor_info_ct`. `Actor_info_make_actor` calls `Actor_ct` before it returns (`src/game/m_actor.c`, `Actor_data_bank_dma_end_check` is always true on PC). `Actor_info_call_actor` still reads the player pointer before any leftover `ct_proc` and uses it with no null check.
- `player_actor_exists` is one flag, set in `Player_actor_ct` and cleared in `Player_actor_dt`. `Now_Private` is one `Private_c*` (`include/m_common_data.h`, `Common_Get(now_private)`). `Save_Get(private_data[n])` is the per-resident record.
- `include/m_player.h` — `player_actor_s` starts with `ACTOR actor_class` (size 0x13A8). Do not add fields; offsets are load-bearing. Per-player extras stay in side tables.
- `Actor_info_call_actor` measures every actor against that one looked-up player, then moves parts in enum order (player part is before NPC). Player move code calls `GET_PLAYER_ACTOR_GAME` again (~130 sites in `src/game/m_player_*.c_inc`). The actor argument is not the player those sites use.

**Player data paths (reuse; do not re-search)**

Session pointer and save record:
- `Now_Private` is the only private the game mutates. Cloth, face, gender, pockets, and wallet reads go through it (`mPlib_Get_UseFaceTexRom_p`, `mPlib_get_player_Object_Bank`, `mPr_SetNowPrivateCloth`, shop and inventory overlays).
- Title Start Game (`src/actor/ac_animal_logo_misc.c` `title_action_data_init_start_select`) sets `now_private` to `private_data[0]` and `player_no` to 0, then `decide_next_scene_no`. An existing town goes to `SCENE_PLAYERSELECT_2`.
- Retail select NPC is `src/actor/npc/ac_npc_p_sel2.c` plus `ac_npc_p_sel2_talk.c_inc`. "I'm new" is `aNPS2_select_player`: free slot, message 19, talk `aNPS2_TALK_START_TYPE3` (talk index 26). Loading an existing resident calls `aNPS2_set_str_ct_player_name`, which sets `player_no` and `now_private` immediately.
- `aNPS2_game_start_wait` calls `aNPS2_setup_load_data` → `mCD_InitGameStart_bg` (`src/game/m_card.c`). New resident uses `mCD_START_COND_2`. `init_mode_table[2]` is 1, `mSDI_INIT_MODE_NEW_PLAYER`.
- `mSDI_StartInitNewPlayer` (`src/game/m_start_data_init.c`) inits that `private_data[player_no]`, sets `now_private`, `player_no`, and `scene_from_title_demo` to `SCENE_START_DEMO2`. It does not call `mHS_set_use`. `mSDI_StartInitAfter` then sets `last_field_id` to -1 and `mHm_SetNowHome` (house from `mHS_get_arrange_idx`).
- Card write is one step per call of `mCD_InitGameStart_bg`: `make_data` (the init above), then `set_data` (copy `Save_t`), then `write_main`. The NPC calls it from its move, which is after `Actor_info_call_actor` has already taken the player pointer. Changing `player_no` here shows up on the next frame.

Actor binding:
- `PLAYER_ACTOR` proc members ignore their receiver: `check_request_main_priority_proc`, `able_submenu_request_main_index_proc`, and `check_request_change_item_proc` all read `GET_PLAYER_ACTOR_GAME(game)` (the selected resident / context-bound player), so calling them "on" a guest still evaluates the host. Player-explicit forms live in `m_player_common.c_inc` (`..._for(PLAYER_ACTOR*)`, declared in `m_player.h`). `mPlib_CheckScene_AbleSubmenu()` adds `SubmenuArea_IsPlayer()` on top of the scene table, which on PC just means "no primary menu is open" — a side-slot gate must use the member proc (scene table only).

Timing/frame counters (do not relearn):
- `game_GameFrame` is the frame RATE (`SetGameFrame(1)`), not a frame counter. `sAdo_GameFrame`'s pump cadence is not per game frame (audio ticks accumulate `dt_num_60fps_frames`, which can be < 1 per loop). The only reliable per-frame counter is `play->game_frame` (u32, `m_play.h` 0x2090), incremented once per normal play frame.
- Any `graph_dt_period_elapsed`/`graph_dt_60hz_ticks` accumulator inside a DRAW function is a co-op hazard: an actor visible in any view draws in every view, so a shared/static accumulator advances several times per frame. Keep such state per actor instance (or per home/owner key) and guard one advance per game frame with `play->game_frame`. The mailbox mail ping (`0x43B`, `aMBX_actor_draw`) was the first found this way.

Actor binding (retail):
- `Player_actor_ct` registers `s_player_by_slot[player_no]`. `Player_actor_dt` clears every slot holding that pointer. Profile wrappers are `Player_actor_*_call` in `src/game/m_player_call.c`.
- Select scene still has the slot-0 actor when new-resident init changes `player_no`. The one-actor rebind in `get_player_actor_withoutCheck` is what keeps that frame from a null deref. A second registered actor must not use that rebind.
- Pad row: `s_pad_by_slot[]` (`src/game/m_player_lib.c`) maps resident slot → Controls row (0 host, 1-3 guests). `mPlib_queue_guest_slot(slot, pad)` registers it at spawn, because `Player_actor_ct_call` runs `guest_begin` before `Player_actor_ct` registers the slot. The host registers row 0 in `Player_actor_ct`. `guest_begin` reads `pads[pad]`, and the per-pad `mcon` homes (`s_guest_mcon[]`) live in `src/game/m_player_call.c`. Camera follow offsets in `m_play.c` are `[PLAYER_NUM]` arrays indexed by that pad.

New resident after the select NPC:
- Train scene data: `src/data/scene/start_demo2.c`. One player, door to `SCENE_FG` at `{1979, 200, 760}`. First-town train is `start_demo1.c` and `ac_npc_guide_move.c_inc` (`aNGD_scene_change_wait_init`), which also inits island, shop, post office, and calendar.
- Second-resident train end is `aNG2_scene_change_wait_init` in `src/actor/npc/ac_npc_guide2_move.c_inc`: `goto_next_scene`, `mEv_SetFirstJob`, `mEv_SetFirstIntro`, `aNG2_set_pl_face_type` (writes `Now_Private->face` from `answer_flags`), `mPr_SetNowPrivateCloth`.
- Town arrival with `mEv_CheckFirstIntro` spawns `mAc_PROFILE_INTRO_DEMO` from `Actor_info_ct`. House claim is `aID_retire_rcn_guide_wait` in `src/actor/ac_intro_demo_move.c_inc` (`mHS_set_use`, then `Now_Private` loan and collect bits). `house_idx` is set only on the restart branch (`_1A4`).
- `play_cleanup` calls `mHsRm_GetHuusuiRoom`. That overlay is skipped while `mEv_CheckFirstJob` is set (`src/game/m_huusui_room.c`).

Look and input (shared globals, not per actor):
- Body mesh bank is chosen once in `mPlib_Object_Exchange_keep_new_PlayerMdl` from `Now_Private->gender` (boy `ACTOR_OBJ_BANK_8`, girl `ACTOR_OBJ_BANK_51`) and stored on the player profile.
- Cloth uses two banks toggled by `mPlib_change_player_cloth`. Face tex is one bank (`Player_face_tex_bank_ID`); face pallet is two. Both load from `Now_Private`.
- `pc/src/pc_pad.c` `PADRead` writes `status[0..3]` from `player_input[n]` (`0` Key and Pad, `1` Keyboard, `2..5` Pad 1..4, `6` None). `padmgr_UpdatePC` (`src/padmgr.c`) copies those into `cur_pads`, then `padmgr_HandleDoneReadPadMsg` keeps a channel only when `device_type[i] == PADMGR_TYPE_CONTROLLER`. All four channels are marked connected there. `padmgr_RequestPadData_NonLock` then copies `num_controllers` pads into `game->pads`.
- Move and menus read `game->pads[0]` and `game->mcon` only (`src/game/m_controller.c`). `mCon_main` runs once per frame in `play_main`, before actors. Player 1's Controls row is that channel. Player 2's row is `pads[1]`.
- Guest spawn (`Actor_info_spawn_guest` in `src/game/m_actor.c`): outdoors (`SCENE_FG`, enum 7), not during `mEv_CheckFirstIntro`, and only when `g_pc_settings.player_input[1]` is not `PC_PLAYER_INPUT_NONE` (6). Spawned after the selected actor, because `make_actor` runs `ct` immediately and `mPlib_guest_slot_ready` (`src/game/m_player_lib.c`) waits for the session slot. Spawn `arg` is 0 on purpose: `Player_actor_request_proc_index_fromDma` (`src/game/m_player_main_dma.c_inc:32`) turns `actor_specific & 7` 1-3 into `mPlayer_INDEX_OUTDOOR`, the host's own door-exit animation, so copying `player_data->arg` made the guest walk out of P1's door. `mPlib_queue_guest_slot` is consumed by `Player_actor_ct`. `Player_actor_*_call` binds that actor, points `Now_Private` at `private_data[slot]`, and copies `pads[1]` onto `pads[0]` for that call. Face and cloth reload around the guest draw, then reload the selected resident. Body mesh stays the selected resident's gender bank. A created guest is drawn at its own `world.position` (`Actor_draw` in `m_actor.c`, `Player_actor_draw_Normal` in `m_player_draw.c_inc`). Cull does not delete it (`block` is -1, `npc_id` is `EMPTY_NO`).

Guest spawn trace (do not re-search):
- Scene id is already the destination when `Actor_info_ct` runs. `Game_play_change_scene_move_end` (`src/game/m_play.c`) writes `scene_no` from `play->next_scene_no`, then `graph_proc` (`src/graph.c`) leaves `while (game_is_doing)` and the next loop calls `play_init`. `Gameplay_Scene_Read` sets `play->scene_id` before `Actor_info_ct`.
- Select doors (`aNPS2_make_door_data` in `src/actor/npc/ac_npc_p_sel2_talk.c_inc`): home and both station doors are `SCENE_FG`. Talk 26 is `SCENE_START_DEMO2`.
- `mEv_SetFirstJob` / `mEv_SetFirstIntro` run together at the end of `ac_npc_guide_move.c_inc` and `aNG2_scene_change_wait_init` in `ac_npc_guide2_move.c_inc`. `aID_retire_rcn_guide_wait` (`src/actor/ac_intro_demo_move.c_inc`) clears First Intro when the guide actor is gone, then the player can walk. `aID_first_job` clears First Job only when `_1A8` is set (`aNRG2_all_job_end_init` in `ac_npc_rcn_guide2_schedule.c_inc`). Both flags are per `player_no` (`mEv_SAVED_FIRSTJOB_PLR0` / `mEv_SAVED_FIRSTINTRO_PLR0` in `src/game/m_event.c`).
- Device row: the menu edits `s_pending` (`pc/src/pc_settings_menu.c`). `apply_pending` copies it onto `g_pc_settings` and `pc_settings_save` writes `player2_input`. Back without Apply leaves index 1 at None. Defaults are `{Key and Pad, None, None, None}` in `pc/src/pc_settings.c`.
- Other resident: `mPr_CheckPrivate` is `mLd_CHECK_LAND_ID` (`(land_id & 0xFF00) == 0x3000`, `include/m_land.h`). `mPr_InitPrivateInfo` (`src/game/m_private.c`) copies `land_info->id`. Empty slots stay `0xFFFF` from `mPr_ClearPersonalID`. The selected `player_no` is skipped. One created resident means no guest.

**Camera and rendering**
- `include/m_play.h` — one `Camera2 camera` on `GAME_PLAY`. `Game_play_camera_proc` runs after actor move, once per frame, and follows the selected resident.
- Split, when `mPlib_other_player_actor` is set (`src/game/m_play.c` `play_draw_split_side`): the left half is built and shown with `pc_graph_flush_world` (`src/graph.c`), then the right half and the HUD are built in a fresh list. One list cannot hold two towns; writing both used to run past `sys_dynamic` and the frame never presented. `pc_gx_keep_framebuffer` keeps the left image when the second pass starts. `play_split_live` skips that second town while `fb_wipe_mode` or `fb_mode` is active, so a door wipe is one list. `play_guest_camera` shifts the right eye and center by the other resident while `play->camera.now_main_index` is `CAMERA2_PROCESS_NORMAL`. Any other camera (door, talk) keeps the last normal offset on the right half. `setScissorView` drives both the viewport and the perspective aspect (`src/game/m_view.c`). One actor leaves that path and keeps a full-screen draw.
- HUD, when that same guest is set (`Game_play_draw`): `banti_draw` runs once per half through `showSplitHudView` (`src/game/m_view.c`). `mSM_submenu_draw` runs once, full screen, after `PC_NOOP_CLEAR_DEPTH`. The menu prerender blit in `makeBumpTexture` stays on the one-actor path. With two residents the world keeps drawing under the menu. See the menu trace below.
- `setScissorX` scales the 320x240 rect by 2 into the 640x480 EFB. `emu64::dl_G_SETSCISSOR` turns the far corner into a width and height (`src/static/libforest/emu64/emu64.c`).
- `pc/src/pc_gx.c` — `GXSetViewport` / `GXSetScissor` scale that EFB rect to the window.
- Acres are still loaded around the selected resident. The right half can show unloaded ground when Player 2 walks far.

**Menu, mailbox, and house door (reuse; do not re-search)**
- Submenu is one `Submenu` on `GAME_PLAY`. Open sets `mSM_PROCESS_PREWAIT` and `mSM_MODE_PRERENDER_INIT` (`mSM_submenu_ctrl` in `src/game/m_submenu.c`). Move runs before draw. The draw frame copies the EFB and sets `PRERENDER_WAIT`. The next draw promotes that to `PRERENDER_DONE`. The move after that goes `PREWAIT` → `LINKWAIT` → `PLAY`. `mSM_submenu_draw` calls `draw_proc` only when mode is at least `PRERENDER_DONE` and process is `PLAY`.
- `mSM_menu_ovl_draw` (`src/game/m_submenu_ovl.c`) loads the full-screen ortho (`mFont_CulcOrthoMatrix`) and draws the current overlay. `mSM_setup_view(FALSE)` copies `view->viewport` and, on PC, sets the scissor from `view->screen` doubled. A full view is still `0,0,640,480`.
- Mailbox: `aMBX_pl_open` in `src/actor/ac_mailbox_move.c_inc` opens `mSM_OVL_INVENTORY` / `mSM_IV_OPEN_MAILBOX`, which opens `mSM_OVL_MAILBOX`. The panel is `mMB_mailbox_ovl_draw` / `pos_win_mode` (`src/game/m_mailbox_ovl.c`, `src/data/model/pos_win.c`). The notice board is the same `mSM_submenu_draw`. That display list sets `G_DECAL_LEQUAL`. `emu64::zmode` depth-tests decals. Retail `PRERENDER_DONE` replaces the 3D world with a 2D blit, so the depth buffer is empty and the panel shows. With a guest the blit is skipped and both towns stay, so the panel was rejected. `PC_NOOP_CLEAR_DEPTH` calls `pc_gx_clear_depth`, which must `pc_gx_draw_pending` first. A clear that runs while world triangles are still queued lets those triangles refill the depth after the clear. `PC_NOOP_MENU_DEPTH_OFF` / `ON` set `pc_gx_menu_no_depth`. `emu64::zmode` then disables the depth test and forces color write for that draw. `pc_gx_menu_no_depth` is declared in `pc/include/pc_platform.h` inside the existing `extern "C"` block, next to `g_pc_widescreen_stretch`. `emu64.c` includes that header at the top, so both `zmode` and `dl_G_NOOP` see it. `pc_gx_clear_depth` is only called from `dl_G_NOOP`, so its `extern "C"` stays in the file-scope block next to `pc_gx_tlut_set_native_le`, above that function. `emu64.c` is compiled as C++. An `extern "C"` inside a method does not compile. A declaration below the first use is not in scope. 2026-10-25 playtest of the 4:3 band plus `PC_NOOP_MENU_DEPTH_OFF`: the mailbox and notice board still do not appear. Do not treat that draw as a fix.
- The menu is one draw in the opener's half. Today that is the left half (`pads[0]`). A 4:3 band (`l=0`, `r=160`, `top=60`, `bottom=180` in 320-space) was playtested and stayed empty. A magenta `fade_rgba8888_draw` on the full left half, in that same pass, did show. The panel now uses that full half. Playtest: green strip, no panel. A magenta transformed quad (font ortho, z=0, no cull) showed in that pass, then the probes were removed. Alpha test and blend forced off still showed no panel. `emu64::cullmode` now uses `GX_CULL_NONE` while `pc_gx_menu_no_depth` is set, because both panels set `G_CULL_BACK` and the quad did not. The clock also uses `G_CULL_BACK` and shows, but in the town pass at z near 0. Cull-none was playtested 2026-10-25 and both panels stayed invisible. The catch-up below is the current trace. Do not change z until a fill inside the frame draw has been playtested. The other half stays the town. A full-screen menu was rejected. Joystick sounds with no panel means move is running. The clock stays per half.
- The panel used to be appended to the right-half town list (`Game_play_draw` after `makeBumpTexture`). `POLY_OPA` is 9952 commands. `GRAPH_ALLOC` grows down from that same buffer. `pos_win_mode` takes color from tile 0 and alpha from tile 1 (`emu64::combine_manual`, the `TEXEL0` / `TEXEL1` case). `G_RM_AA_TEX_EDGE` has no `Z_CMP` and no `FORCE_BL`. `emu64::alpha_compare` still requires alpha >= `tex_edge_alpha` (144) because `AA_EN | CVG_X_ALPHA | ALPHA_CVG_SEL` are set. `emu64_init` binds every map to an I8 texture of `0x88` (136), which fails that test. `texture_cache_alloc` returns null when the cache is full, so the panel keeps that default map. `emu64_init` rewinds `texture_cache_data` only when `is_overflow` is set. A separate pass did not rewind it. `emu64_refresh` does. Single player does not draw the town on a menu frame (`makeBumpTexture` returns at the prerender blit). The panel pass now calls `emu64_refresh` after `play_restart_disp`.
- While process is not `WAIT`, `Game_play_move_fbdemo_not_move` skips `Actor_info_call_actor`. That is the whole town, including `CollisionCheck_OC`. The PC branch still runs `CollisionCheck_OC`, `CollisionCheck_clear`, the guest `mv_proc`, and `CollisionCheck_Status_Clear`. The selected resident stays in the menu. Ground check is inside the player move. `OC` is actor-against-actor.
- Mailbox close: `mMB_move_End` calls `mPlib_request_main_mail_land_from_submenu` (`src/game/m_player_lib.c`). The flag is global. Every player move calls `Player_actor_request_main_change_from_submenu` (`src/game/m_player.c`) and then clears it. The guest is the list head, so it used to take `mPlayer_INDEX_MAIL_LAND` and leave the selected resident in `mPlayer_INDEX_MAIL_JUMP`, which ignores the stick. A guest slot returns before consuming the flag. The notice board does not request a player index, so leaving it never stuck Player 1.

**Split menu catch-up (2026-10-25). Do not re-test the failed list. Do not re-search these paths.**
- What showed, and why that is not the panel:
  - Magenta fill and the later green strip are `fade_rgba8888_draw` (`src/game/m_rcp.c`). `emu64::fill_rectangle` switches to the NDC ortho from `emu64_init` and ignores the game projection, the model matrix, textures, cull, and z. They only prove the menu pass is presented and the left-half scissor covers that rect. The green strip was after `mSM_submenu_draw` returned. It does not prove the window display list ran.
  - The magenta quad was inline ±40 verts, `G_CC_PRIMITIVE`, `G_RM_OPA_SURF`, no cull, font ortho, `Matrix_scale(16,16,1)`, z=0. It was drawn before `mSM_submenu_draw`, then removed. It proves z=0 untextured triangles can land in that pass.
  - The clock is `banti_draw` (`src/game/m_banti.c`): `clk_win_mode`, font ortho, `Matrix_scale(16,16,16)`, optional `Matrix_translate(-184,0,0)`, no z=140. It is drawn in the town pass, once per half, via `showSplitHudView`. It fades because `banti_calc_disp_alpha_rate` drives alpha to 0 whenever `submenu.process_status != mSM_PROCESS_WAIT`. That starts at `PREWAIT`, before the panel is allowed to draw. The fade proves the submenu left `WAIT`. It does not prove `mSM_menu_ovl_draw` submitted `pos_win` or `kei_win`. When `mode > 2` the clock moves from `FONT` to `POLY_OPA`. It is still the clock. In a half it uses `mFont_CulcHudHalfOrthoMatrix` while `pc_gx_hud_half` is set: content `x 0..160` spread over the 320-wide half, so content maps 2 EFB per unit on both axes as in single player, and the retail `-184` slide becomes `-92`.
- What both panels do that none of those draws did: `mMB_set_frame_dl` / `mNT_set_frame_dl` do `Matrix_scale(16,16,1)` then `Matrix_translate(x, y, 140)`. Mailbox then branches `pos_win_mode` and `pos_win_model` (`src/data/model/pos_win.c`: `G_CULL_BACK`, 2-cycle, `G_RM_AA_TEX_EDGE`, combiner TEXEL0 color / TEXEL1 alpha). Notice branches `kei_win_model` (`src/data/model/kei_win.c`: 1-cycle `G_RM_XLU_SURF`, pin piece switches to tex-edge). Vertices are disc blobs: `assets/pos_win_v.bin` at DOL `0x78A600` size `0x610`, `assets/kei_win_v.bin` at `0x7AD340` size `0x3B0` (`pc/src/pc_assets.c`). Clock verts are a different blob and are visibly loaded.
- Draw gate: `mSM_submenu_draw` calls `draw_proc` only when `mode >= mSM_MODE_PRERENDER_DONE`, `process_status == mSM_PROCESS_PLAY`, and `SubmenuArea_visit == &SubmenuArea_dlftbl[mSM_DLF_SUBMENU_OVL]`. `mSM_move_LINKWait` sets `draw_proc` to `none_proc1`. `mSM_menu_ovl_init` then sets it to `mSM_menu_ovl_draw` and calls move once. Stick navigation means that init has run. `mSM_menu_ovl_draw` calls `mSM_setup_view(..., TRUE)`, which only loads `mFont_CulcOrthoMatrix`. It does not change the half scissor. `mSM_setup_view(FALSE)` is the path that copies `view->viewport` into `GRAPH_ALLOC` (seg2k0) and sets the doubled scissor.
- Off-screen start, then 0: overlay index 6 `mSM_OVL_NOTICE` starts at y=300. Index 11 `mSM_OVL_MAILBOX` starts at x=-300 (`mSM_set_new_start_data`). `mSM_move_Move` targets 0. After the user can navigate, `proc_status` is `PLAY` and the settled translate is 0,0,140. A stuck start position would be off the ±2560 font ortho, but navigation means the slide finished.
- Mailbox `pre_draw` is the inventory overlay. Inventory calls `mSM_change_view`: perspective, viewport `width<<1` (not the `*4` used by `set_viewport`). Notice opened from the field has `pre_draw` `none_proc1` and fails the same way, so a stuck inventory camera is not sufficient. It can still hide the mailbox after the notice panel is fixed.
- Font ortho is `guOrtho(±160*16, ±120*16, -800, 800, 1)` in `mFont_CulcOrthoMatrix`. `emu64::dl_G_MTX` rewrites that ortho to near=-800, far=800. z=0 lands near oz=-0.5. z=140 lands near oz=-0.59. Both are inside the clip. z=140 is not a proven clip. It is also not playtested.
- `seg2k0` (`emu64_utility.c`, 32-bit only): bit 0 is a direct pointer. Top nibble != 0, or addr < `0x03000000`, is returned as-is. Otherwise an address outside `pc_image_base..pc_image_end` is `segments[seg] + offset`. `GRAPH_ALLOC` matrices that land in `0x03xxxxxx..0x0Fxxxxxx` and outside the image are misread. The viewport copy in `mSM_setup_view(FALSE)` exists for this. The working quad's matrix was an earlier alloc in the fresh menu list. A later panel matrix can still hit the bad range.
- `emu64::dl_G_DL` returns without drawing if the branched list's first opcode is outside `NUM_COMMANDS`. `pos_win_mode` starts with `gsSPLoadGeometryMode`, so that check passes for the mode list. A bad `seg2k0` of the model pointer fails the same check and skips the whole model.
- Playtested, not the cause: depth clear while world triangles were queued; depth off; 4:3 band; separate display list after `pc_graph_flush_world`; `emu64_refresh`; alpha test forced `GX_ALWAYS`; blend forced `GX_BM_NONE`; `GX_CULL_NONE`. The alpha, blend, and cull bypasses are gone now that the panel shows; single player proves those retail paths draw these panels. Only the depth bypass in `emu64::zmode` stays, because the panel still has to beat the town's depth buffer.
- Menu pass today (`Game_play_draw` in `src/game/m_play.c`, guest only): flush the left town, `play_restart_disp`, `emu64_refresh`, `play_set_view_menu_half` (4:3 band `l=0 r=160 top=60 bottom=180`), `showSplitHudView`, `PC_NOOP_CLEAR_DEPTH`, `PC_NOOP_MENU_DEPTH_OFF`, `mSM_submenu_draw`, `PC_NOOP_MENU_DEPTH_ON`. `pc_gx_menu_half` is a host flag set around that call. `mSM_change_view` halves position and box when it is set, so the panel and the 3D hand share the band's 1x space instead of the retail 2x full-screen space; without that split the hand walks off the half. The same branch also follows the GX layer's aspect correction: `GXSetProjection` multiplies the ortho's `m00` by `g_aspect_factor` on a wide window (`stretch == 0`, which is what the co-op menu pass runs under), while `GXSetViewport` only remaps sub-viewports under `stretch == 2`, so a 3D item placed by its viewport box stayed unsqueezed and drifted with the window size. `mSM_change_view` now squeezes the box centre (`vp_x + width`, i.e. twice the centre) about the band centre by `pc_gx_aspect_factor()` and leaves the box width alone; the squeezed projection already scales the model inside it. That covers the hand and the 128-wide player model from the same place. Right half stays the town. Probes are not in the tree.
- 2026-10-25: single-player mailbox and notice board were confirmed working on this port, so the defect is the guest/split pass. Colour bisects contradicted each other and were retired. A file trace found the cause: `SubmenuArea_visit` is one global area, `load_player` swaps it to `mSM_DLF_PLAYER_ACTOR`, and every `Player_actor_*_call` runs `load_player`. Single player skips the actor loop while a menu is open, but the co-op branch at `Game_play_move_fbdemo_not_move` keeps calling the guest's `mv_proc`, so `load_player` reclaimed the area every frame and `mSM_submenu_draw` rejected the draw on its `SubmenuArea_visit` condition. That is why the panel never drew while the cursor still worked. Fix: `load_player` returns early unless `process_status == mSM_PROCESS_WAIT`; `mSM_move_End` sets WAIT before its `load_player`, so closing a menu still restores the actor area. A temporary file trace (`mSM_probe_trace`, `probe_log.txt`) plus gate bits in `mSM_submenu_draw` confirmed it and has been removed. Pass facts to reuse: fills queued before `emu64_refresh` in the menu pass are discarded; the scissor there is still the right half until `play_set_view_half(play, 0)` runs; `mSM_menu_ovl_init` sets `menu_draw_func` to `none_proc1` until an overlay's `*_set_proc` runs.
- Own house: `src/actor/ac_my_house_move.c_inc`. `aMHS_check_player_sub` uses `GET_PLAYER_ACTOR`, 40 units, facing, and `chkTrigger(BUTTON_A)` on `pads[0]`. `mDemo_Request(mDemo_TYPE_DOOR, house, aMHS_set_demo_info)` selects `CAMERA2_PROCESS_DOOR`. `wait_door_start` in `src/game/m_demo.c` requests `mPlib_request_main_door_type1` on `GET_PLAYER_ACTOR_NOW`. Door `wait_end` is `allways_false`. The demo ends only from `mDemo_End`.
- The zoom is `Camera2_request_main_door` (`src/game/m_camera2.c`). It centers on the actor it is given (the selected resident) and morphs for 140 steps. There is one `Camera2` on `GAME_PLAY`. Copying that eye onto Player 2 is why both halves zoomed. `play_guest_camera` is the split for that.
- Indoor switch: `aMHS_open_door` when the house keyframe stops. `aMHS_actor_move` sets `arg2_f` from `drtbl[request_type]` every move. `0` calls `aMHS_goto_next_pl_scene` (`SCENE_MY_ROOM_*` from home size, `WIPE_TYPE_TRIFORCE`, then `fb_fade_type = FADE_TYPE_DEMO`) and `mDemo_End`. `3` sets `world.angle.z` and the same move calls `goto_other_scene`. `request_type` 6 is the `drtbl` slot that yields 3 (`mSc_DIRECT_NORTH_EAST`). Entering your own house uses `request_type` 0.
- `goto_other_scene` (`src/game/m_scene.c`) needs a player and `fb_wipe_mode == WIPE_MODE_NONE`, then sets `next_scene_no`. `FADE_TYPE_DEMO` finishes in `Game_play_change_scene_move_end`. Guest spawn is `SCENE_FG` only, from `Actor_info_ct`, so the indoor scene has one actor. Player 2 is destroyed on the way in and created again on the way out.
- 2026-10-25 playtest: Player 1 enters their own house. Player 2's camera stays on the last normal follow during the door zoom. The split returns when Player 1 exits. While Player 1 is inside, Player 2's half is gone. Required: Player 2 stays in their own outdoor state. Cause is the guest lifetime, not the door zoom. Do not re-walk the door path above.

**Input**
- `include/dolphin/pad.h:23` — `PAD_MAX_CONTROLLERS 4`
- PC `PADRead` is `pc/src/pc_pad.c`. `src/padmgr.c` `padmgr_UpdatePC` copies four channels into `game->pads`. Gameplay still reads channel 0. See the player-data paths above.
- `pc/src/pc_keybindings.c` — keybindings for the keyboard device.

**Menus**
- `include/player_select.h` — player-select screen (reuse for inhabitant selection)
- `src/static/initial_menu.c` — title/initial menu (rework for Single/Co-op/Settings)
- `pc/src/pc_settings_menu.c` — settings menu
- `pc/src/pc_pause_menu.c` — pause menu (per-player behavior needed)

**Saves (PC side)**
- `pc/src/pc_m_card.c`, `pc/src/pc_card.c` — memory card emulation
- `pc/src/pc_save_bswap.c` — save byte-swap
- Saves are GCI format (Dolphin-compatible), stored in `save/`

## 5. Core refactor plan (local co-op)

**Phase 0: Analysis (done 2026-10-24)**
- Player part is an unbounded linked list with one scene spawn. The blocker is callers that use the head, plus `Now_Private` and `player_actor_exists`.
- Per frame, when the submenu is idle: demo/event, then actor move (distance vs head player), then one camera update, then draw.
- `now_main_index` is action state. Leave those call sites alone until a caller has a real player index.

**Phase 1: Player indexing (lookup landed; call sites still index 0)**
- `get_player_actor_by_index` walks the player list. `get_player_actor_withoutCheck` is index 0, so every existing macro stays single-player.
- Do not rewrite call sites to pass `0`. Switch a caller to `GET_PLAYER_ACTOR_INDEX` only when it has a non-zero index.
- List order is newest-first. `mPlib_register_player_slot` records the actor beside the struct, keyed by `Common_Get(player_no)`. Lookup uses that slot. `PLAYER_ACTOR` stays 0x13A8.
- Verify single-player is unchanged (build, then a known save once the exe can see the disc).

**Phase 2: Multiple active players**
- Allocate and update up to 4 player actors in the actor list.
- Per-player input routing (controller index → player index).
- Per-player state for held items, interaction, and menus.

**Phase 3: Split-screen rendering**
- One viewport and camera per active player (use `GXSetViewport`/`GXSetScissor`).
- Per-player HUD and menus.
- Handle 1-, 2-, 3-, and 4-player layouts.

**Phase 4: Session management**
- Co-op menu flow (section 3)
- Join and leave logic, including queued exits and cancel timeouts
- Save on join and leave

**Phase 5: Polish and test**
- Conflict tie-break (controller index)
- Event blocking and group-event handling
- Test with 2, 3, and 4 players on the PC build (`build_pc.sh`, MSYS2 MINGW32)

## 6. Build and test

- Build: MSYS2 MINGW32, `./build_pc.sh` (see repo README)
- Packages: `mingw-w64-i686-gcc`, `cmake`, `SDL2`, `make`
- Output: `pc/build32/bin/AnimalCrossing.exe`
- The exe looks for a disc in the current directory, `orig/`, and `rom/`. If those are empty, it uses the repo root three levels above `pc/build32/bin`. A disc in `rom/` wins.
- Debug flags: `--verbose`, `--no-framelimit`, `--model-viewer`, `--time HOUR`

## 7. Open items

**Ghost model on load: fixed (2026-10-25, playtested).** A P1-shaped model appeared on the ground at P1's door for about two seconds after loading with Player 2 active. Every player actor runs `Player_actor_ct` → DMA → `Player_actor_request_proc_index_fromDma`, which reads `actor_specific & 7`: 1-3 request `mPlayer_INDEX_OUTDOOR` (the `mPlayer_ANIM_GO_OUT_O1` "walk out of a doorway" state plus an `mDemo_TYPE_OUTDOOR` request). `Actor_info_spawn_guest` passed the host's `player_data->arg`, so the guest replayed P1's house exit at P1 + 80 x, 80 units into the open where no door hides it; single player never sees the host's own version. It also issued a second `mDemo_TYPE_OUTDOOR` against the one global demo, which made P1's exit roughly twice as long. Fix: the guest spawns with arg 0 → INTRO → WAIT. Playtest: no model at the door, P2 stands normally, P1 controllable sooner. Probes are gone.

**Player 2 indoors is deferred behind dual/multi-world instancing** (decided 2026-10-25). Keeping Player 2's outdoor half live while Player 1 is inside their own house needs two simultaneous worlds: `play_cleanup` and `play_init` → `Gameplay_Scene_Read` tear the play down and re-point `current_scene_data`, and there is one field, one `Camera2`, one collision set, one actor list. Same rule applies to any resident's house. Smaller slices come first: per-player actors (guest singleton → 3-4 actors, 2x2 layout), per-player state, acre streaming, co-op menu and join/leave. Do not re-search the paths in section 4.

**Next slice: per-player actors (up to 4) and 2x2 layout** (approved 2026-10-25, branch `feature/multi-player-actors`). Four residents walk at once, each on its own device, in a 2x2 layout for 3-4 players; the 2-player split stays as it is.

Scope in:
- Spawn up to 3 guests, not one: every other resident with valid `private_data`, each with its own private data, pad channel, look, and spawn offset.
- Per-actor pad channel. `guest_begin` (`src/game/m_player_call.c`) hardcodes `game->pads[1]`, which only holds for one guest, and a resident slot cannot be the pad index because the host may be resident 2 or 3. Add `s_pad_by_slot[]` plus `mPlib_register_player_pad(slot, pad)` and `mPlib_pad_for_player_slot(slot)` (`src/game/m_player_lib.c`, `include/m_player_lib.h`); `mPlib_queue_guest_slot` takes the pad, `mPlib_take_guest_slot` returns it. `s_guest_mcon` becomes one per pad. The host registers pad 0. Pad index is the player row (0 = host, 1-3 = guests in spawn order), not the resident slot.
- Accessors `mPlib_extra_player_count()` and `mPlib_extra_player_at(i)` in pad order. Replace the five `mPlib_other_player_actor()` call sites in `src/game/m_play.c`: menu-open guest move (line ~550), `play_split_live`, the prerender gate, the split draw, and the HUD gate.
- Per-player camera offsets: `s_guest_eye_ofs`, `s_guest_center_ofs`, `s_guest_ofs_ready` become `[PLAYER_NUM]` arrays indexed by pad.
- Layout: `play_set_view_player(play, index, count)`. Two players keep left/right halves; 3-4 use quadrants (160x120 content each), and three players leave the bottom-right empty (decided 2026-10-25). P1 is top-left. `index` is the view slot (0..count-1), not the Controls row, so sparse rows (e.g. 1 and 3) still fill TL, TR, BL; pad lookup stays separate and only feeds the camera offset.
- Draw loop: the first `count - 1` views are drawn early with `pc_graph_flush_world` and a list restart after each, the last view is drawn where the right half is drawn now. One town per list still holds; three flushes per frame is new (`src/graph.c`). The wipe path stays one full-screen list.
- HUD per view: loop views, set the view, `showSplitHudView`, `pc_gx_hud_half` only when `count == 2`, then `banti_draw`. In a quadrant the retail ortho maps 320x240 content onto 320x240 EFB, so the clock is 1x there and 2x in a half; no new matrix function.
- Menu band in P1's view when 3-4. `play_set_view_menu` picks the left-half 4:3 band at two players and the top-left quadrant (already 4:3) at three or four, and the `pc_gx_menu_half` bool became `pc_gx_menu_mode` (0 none, 1 half, 2 quadrant; `pc/src/pc_gx.c`, `pc/include/pc_platform.h`, `src/game/m_submenu_ovl.c`). `mSM_change_view` halves position and box in both modes; the only difference is the y base: 240 vp units (EFB 120, the band top) for a half, 0 for the top-left quadrant. The x squeeze centre stays 320 vp units (EFB 160), which is both the band centre and the quadrant centre. Half values stay byte-for-byte.

Scope out: per-player state (actions, talk, menus stay P1's), save/join-leave, Player 2 indoors, per-player audio and vibration.

Spawn detail (`Actor_info_spawn_guest` in `src/game/m_actor.c`): checkpoint 2 changed this to iterate joined Controls rows 1-3, take each row's stored resident slot (`g_pc_coop_guest_slot`), queue slot+pad, then `make_actor` with spawn arg 0 and the row's offset (+80 x, +80 z, -80 z), snap Y. The original per-player-actors slice paired resident slots with configured rows in order; the row-to-resident mapping now keeps a guest on the same resident when another guest leaves. Queue and spawn one at a time because `make_actor` runs `ct` synchronously and the queue is consumed by that call.

Verification (checkpoint 2 code is written and builds clean; playtests pending):
- MINGW32 `./build_pc.sh`.
- Two-player regression: halves, both walk, clock at single-player size, mailbox and notice band plus hand on the letter, window resize, door zoom. The 2-player path keeps the same rects, cameras, HUD mode and menu mode as checkpoint 1, but the draw/HUD loops were restructured, so this still needs a pass.
- 3-4 players: needs 3-4 residents in the save (currently 2; creating two more is part of the playtest). One resident per device with no input bleed, four quadrants, clock in each, menu in P1's top-left quadrant with the hand on the letter, resize.
- Three players only: bottom-right empty, no black frame.
- Frame time: four world passes and three flushes per frame; the menu-open branch runs collision once per guest.

Risks: `pc_graph_flush_world` three times per frame instead of one (buffer and timing; the menu path already does two), collision per guest while a menu is open, and per-guest face/cloth reload each frame (texture churn).

**Next slice: field window per view** (approved 2026-10-25, branch `feature/field-window-per-view` off `feature/multi-player-actors`). Each split view draws the world around its own player instead of the selected resident. Two checkpoints.

Checkpoint 1, terrain (built and playtested 2026-10-25):
- Ground geometry is not streamed: `aFD_DrawBlock` fetches each block's display list straight from ROM (`mFI_GetBGDisplayListRom`), and `mFI_BlockCheck` only gates field bounds. The only per-frame window is the draw-side 3x3 table in `FIELD_DRAW_ACTOR.block` (`src/actor/ac_field_draw.c`), which `aFD_PrepareFieldDraw` filled once from the selected resident in ct/move.
- `aFD_PrepareFieldDraw` is split: `aFD_SetBlockTable(field_draw, player)` fills the 9 blocks, and the caller keeps the non-draw parts (`aFD_SetActorPosition`, `aFD_MakeMarinScrollInfo`). Move/ct keep the selected resident so water sounds, `mCoBG_WaveCos2BgCheck` and the Marin animation advance once per frame; `Bg_Draw_Actor_draw` rebuilds only the table.
- View-player hook: `mPlib_set_view_player_actor` / `mPlib_view_player_actor` (`src/game/m_player_lib.c`, `include/m_player_lib.h`). `play_draw_split_side` (`src/game/m_play.c`) sets it per view (view 0 = selected resident, view i = `mPlib_extra_player_at(i - 1)`) and clears it right after `Actor_info_draw_actor`. NULL falls back to the selected resident, so single player is unchanged.
- Safety of one 3x3 table for every view: each earlier view is flushed by `pc_graph_flush_world` before the next view is built (`play_restart_disp`), the same ordering the split HUD relies on.
- Playtest: with P1 at the plaza and P2 across town, P2's half keeps floor tiles, boundary walls, rivers and cliffs, with no void and no flicker; P1's half is unchanged. Props still pop until checkpoint 2. Collision near doors needing both players close is a separate issue (world actors still target the selected resident).

Checkpoint 2, props (built and playtested 2026-10-25):
- Background props do not need the 4-block DMA window for drawing: every town block's fg item data is resident (`mFM_SetFgUtPtoSaveData` points `block_info[].fg_info.items_p` at save data or `l_fg_outer_fill`), so a view can build its own prop tables without the window.
- `mFI_GetViewItemTable(item_table, wpos)` (`src/game/m_field_info.c`) fills up to `mFM_VISIBLE_BLOCK_NUM` blocks around any position using the same block choice as `mFI_BGDisplayListRefresh` (current block unless `BG_TYPE_292`, nearest left/right, nearest vertical and diagonal; `mFI_BlockCheck`; unused entries 0xFF/NULL). It reads block data directly and does not touch `remove_cut_tree_info_bitfield`, `mFI_DmaBg` or the window.
- `bg_item_common_draw_blocks_for_view` (`src/bg_item/bg_item_common.c_inc`) builds 4 static draw tables from `mFI_GetViewItemTable` when the view player is not the selected resident, cull-checks them with the current view's projection (`bg_item_common_draw_check`) and draws those. Drops, holes, pits, ten-coins and fades still draw once per view from actor state. Host tables and move state are untouched, so the next move reads what it wrote.
- Host-half fix in the same checkpoint: `bg_item_common_move` cull-checks the host tables before any view is set, i.e. against the last view of the previous frame (the guest's). The host view now re-runs `bg_item_common_draw_check` per table in draw with its own projection. That was also the old "props pop in and out" complaint.
- Playtest: with either player far away both halves keep trees, weeds, flowers and fruit; no duplicates at close range; shaking a tree empties it in both views; dropped items show in both. Single player unchanged. Stump/cut-tree untested (no axe in the save); the tables are rebuilt from fg data, so a cut tree cannot come back.
- Not covered: signs (checkpoint 4) and field actors (checkpoint 3).

Checkpoint 3, field actors (built and playtested 2026-10-25):
- House popping came from actor birth control, not from draw: `aBC_deleteActor_part` (`src/actor/ac_birth_control.c`) runs on every block crossing and deletes item/NPC actors outside `play->last_block_table` / `play->block_table`. Those are one global pair set by whoever crossed last (`mFI_SetBearActor` from the player block-crossing path), so P1's crossing deleted the houses around a stationary P2 and P2's next crossing re-created them from fg items.
- Fix: `Actor_is_near_active_player` (`src/game/m_actor.c`, declared in `include/m_actor.h`) returns TRUE when the actor is within `keep_range` blocks (Chebyshev) of any active player (host plus `mPlib_extra_player_at`). `aBC_deleteActor_part` keeps such actors in split before the retail rule; the draw-time `Actor_delete_check` uses the same helper. In split `keep_range` is 1, matching the 3x3 terrain window; single player keeps the retail comparison and range exactly; no player actor (title demo) falls back to `play->block_table`.
- Playtest: houses stay in either half while the other player walks away, no duplicates, no popping at block edges, single player unchanged.
- Known: occasional hitches after a player crosses a long distance (block streaming), not in one spot; `util`/frame pacing, not a logic stall.

Checkpoint 4, signs (built and playtested 2026-10-25):
- `aSIGN_actor_set_draw` (`src/actor/ac_sign.c`) is split into `aSIGN_actor_set_draw_tables(sign, play, center, table)` plus a host wrapper that keeps retail's `Camera2_getCenterPos_p()` centre, the `mPlayer_INDEX_DOOR` z offset and the `NMREG` bias.
- `aSIGN_actor_draw` builds a static 4-entry table from `play->view.center` (the current view's look-at centre, the same value view 0 gets from Camera2) when the view player is a guest, with the same 2-unit z bias but no door/NMREG tweaks, and draws that table. The host view, move-side `sign->draw` and `aSIGN_actor_set_target` (P1 talk prompt) are untouched.
- `aSIGN_draw_set`'s cull test uses `play->projection_matrix`, so at draw time it follows the current view. The talk-area cull (`Camera2_CheckCullingMode` / `Camera2_CheckEnterCullingArea`) still uses the host camera; it only activates during P1's talk camera, and that is acceptable.
- Playtest: notice board unchanged, P2 sees signs and the notice board in their own half with P1 across town, P1 can still read a sign while P2 is away, single player unchanged. Door zoom not applicable yet (P2's half is not kept during P1's door).

Scope out: collision/data (already per block, no window), actor spawn, shells/mushrooms/block-kind frame state (stay on the selected resident), 3-4 player validation.

**Player camera** (built and playtested 2026-10-25, branch `feature/player-camera` off `feature/field-window-per-view`). Each split view follows its own player with the retail normal-camera response.
- Cause of the residual shift: the guest eye was the host's live eye shifted by (guest − host), so the host's spring moved it; and the normal camera's border size (borderless-acres setting) plus the top-row pitch swing both keyed off `play->block_table.block_x`, which is whichever player crossed a block last.
- `src/game/m_camera2.c`: `Camera2_BorderSize`, `Camera2_GetBorderScaleForBlock`, `Camera2_Normal_Swing` and the goal distance/direction math take a player/block. New `Camera2_GetNormalCenterGoal` (centre clamp from the player's block), `Camera2_GetNormalFollowGoal` (base angle/distance, shared C-stick additions, the player's own swing) and `Camera2_SetEyePos_fromFollow`. The host normal camera uses its own block instead of `play->block_table`, so a guest crossing an edge no longer moves it.
- `src/game/m_play.c`: per-pad `s_guest_center`, `s_guest_dir`, `s_guest_dist` sprung with retail's constants (`add_calc` 0.13397461/8.75/0.25; `add_calc_short_angle2` 0.051316679, 100/75/150). Door and talk cameras keep the last normal offset relative to the guest.
- Playtest: no shift when the players alternate at opposite map edges, each half stable while the other moves, follow feel matches, single player unchanged. C-stick input stays shared by design.
- Open: none for 2 players; 3-4 player validation still waits on two more residents.

**Per-player menus** (branch `feature/per-player-menu`). One menu at a time is a stepping stone; per-player submenu state is the long-term design (see below).

Checkpoint 1, open + input (built and playtested 2026-10-25):
- `chkTrigger_pad` reads any Controls pad. `mSM_submenu_ctrl` checks every active player's Start/Y and records the opener (actor, pad, save slot) with `mPlib_set_menu_opener`; every other menu open (mail, NPC, mailbox) keeps the selected resident as opener.
- `mPlib_menu_context_begin/end` (`src/game/m_player_call.c`) swap `pads[0]`, `mcon`, `Now_Private` and `player_no` to the opener around the submenu update (`mSM_submenu_move`) and the submenu draw, then restore.
- A guest opener keeps the world live: `Game_play_move_fbdemo_not_move` runs the host and other players through `Actor_info_call_actor` with the opener skipped by `Actor_info_set_wait_actor`; `Game_play_move` runs the camera/world proc while a guest opener has the menu (the host camera used to freeze at the menu-open tile). Host menus and single player keep the retail freeze.
- Playtest: P2 sees and edits P2's items with P2's pad only; P1 keeps walking with a live camera; P1's menu is unchanged; closing either menu resumes both.
- Limitation for now: one menu at a time, and `mPlib_able_submenu_type1`/`start_refuse` are still host-state checks, so P1's state can block a guest's Start. A waiting player has no collision body.

Checkpoint 2, draw in the opener's view (built and playtested 2026-10-25):
- `play_set_view_menu(play, count, index)` uses the opener's view slot: its half at two players (keeping the vertically centred 4:3 band) or its quadrant at 3-4. `play_menu_view_index` maps the opener actor back to the slot; `play_menu_mode` returns 1/2 for the two-player halves and 3-6 for the quadrants.
- `mSM_change_view` shifts the 3D item/hand viewport by the view origin before the aspect squeeze. After its halving the viewport x is `2*X - width`, so moving one half (320 EFB) is `+640`, and one quadrant row (240 EFB) is `+480`; the first attempt used half those and put the hand off screen.
- Playtest: P2's panel, hand/cursor and player model sit in P2's half and track the selection; P1's menu stays in the left half; resize keeps them aligned; closing and single player unchanged.
- Order ownership (same slice): the pending change-data order now records its owner slot (`mPlib_change_data_owner`), taken from `Common_Get(player_no)` while no order is pending — the menu context already points at the opener. `Player_actor_request_main_change_from_submenu` skips actors that are not the owner instead of skipping every guest, so a guest's fruit drop or outfit change applies to that guest; host orders keep the old mailbox-jump protection. Playtest passed.
- Resize fix (same slice): the 3D menu viewport offset was added before the aspect squeeze, so resizing scaled the half/quadrant offset and pushed the hand and player model out of the panel. Squeeze the box in band-local space and add the view origin afterwards, matching the 2D panel. Playtest passed.

**Guest interactions: signs** (same branch, built and playtested 2026-10-26):
- Demo requests carry an owner (`pc_owner_slot`/`pc_owner_pad`): the TALK A gate uses that request's pad, and `choice_demo` switches the player context to the chosen request's owner before the setup proc runs (`mPlib_set_demo_talker_by_slot`), so each request's talk runs as its player.
- Reserve signs (`ac_reserve`, `SIGN00..`): `aRSV_wait` checks every active player's position/facing, requires that player's own A edge, records the demo talker and marks the request `pc_no_a_gate`; the chooser accepts it without re-reading A a frame later. Sign talk turns are off, matching retail for P1.
- Signboards (`ac_sign`): the target search reads the fg sign item around each player instead of the host camera's draw table; A is read from that player's pad; the demo talker is recorded.
- Messages: `mMsg_Main` runs in the talker's context (`pads[0]`, `mcon`, `Now_Private`, `player_no`), so each player advances their own dialog.
- Actor moves: cull visibility accumulates over all views (`Actor_info_cull_begin` clears `ACTOR_STATE_NO_CULL` once per frame and `Actor_cull_check` only sets it). The last view used to overwrite the flag, so a sign seen only by P1 stopped moving when P2 looked elsewhere and P1 could not read it. Side effects: actors visible in any view are drawn in every view (clipped) and kept alive, and the move pass now covers actors near any player.
- Actor-opened menus: `mSM_submenu_ctrl` used to reset the opener to the selected resident on the frame after an actor opened a menu, because `process_status` only advances inside that function (`menu_type` was set, `process_status` still `WAIT`). It now defaults only when `menu_type == mSM_OVL_NONE`. This is what made P2's music board open in P1's half and answer to P1.
- Playtest: both players read signs regardless of the other's distance, each dismisses their own message with their own A, no rotation.
- Not covered: doors/shops for a guest (scene per player, deferred). NPC talk is the slice below.

Also open after this slice (paths in section 4):
- Per-player `Now_Private`, `player_actor_exists`, held item, and menu state
- Per-player HUD for players 3 and 4: covered by the approved "per-player actors" slice above (quadrant viewport + retail HUD ortho). Two residents draw the clock once per half.
- Join/leave queue, including joins waited out through a host event
- Guest NPC talk (built and playtested 2026-10-26, `feature/per-player-menu`): both players can start a villager talk. The message draws in the talker's bottom 4:3 band. A guest turn uses that actor's save slot against `player_no`, then `search_position_angleY` from the NPC to that actor plus the retail −180 (`Player_actor_Movement_Talk`). The host still uses `player_angle_y`. `get_player_actor_withoutCheck` is the bound guest during their move, so it cannot tell guest from host. One extra conversation can run beside the primary demo (`mDemo_side_*` in `m_demo.c`, second `mMsg_window`): a different NPC, or a sign. The A edge is stored on the request at queue time (`pc_no_a_gate`); `mDemo_side_try` does not re-read `pads[0]`, because a guest demo has replaced it. `aQMgr_actor_talk_request` keeps queueing a different villager after the first talk leaves `aQMgr_MODE_NORMAL`. Signs stamp `mPlib_set_request_owner` and do not replace the active talker. Closing the side window hides that window in place (`mMsg_MainSetup_Hide`); `mMsg_init` still resets only the primary window. A side page with no next message number closes on A (`mDemo_side_msg`); the quest still owns that handoff for the primary talk. The side talk shows and dismisses its own message; quest globals are snapshotted around setup and the side NPC skips `talk_start_proc`, so item handoff and choices that advance the primary quest are still one conversation. Villagers that call out (`mDemo_TYPE_SPEAK`) and event NPCs that reject on the host's `player_distance_xz` before requesting are still host-only.
- Per-player menus, long term: one menu at a time in the menu slice's checkpoint 1 is a stepping stone only. 3-4 player co-op needs simultaneous menus, which means per-player submenu state (mode, overlay, item pointer, opener pad) and drawing each menu in its opener's view instead of one global `play->submenu`.
- Spawn at own house: long-term goal after per-player inventory/state. Each resident should appear at their own house door, not at a fixed offset from the selected resident. Needs per-player identity, door/scene transitions and per-world instancing (the same dependency as Player 2 indoors).

**Audio sync** (requested 2026-10-26): each player runs their own audio channel, which is fine, but the channels are not time-aligned. Most noticeable in the plaza when a player has mail: two players' mail jingles fire slightly apart and read as one jingle pinged twice as fast. Find where per-player audio/sequence timing starts and sync the shared clocks (or start shared jingles from one source). Investigate `jaudio` sequence start offsets per player context.

**Per-player clock** (requested 2026-10-26, branch `feature/per-player-clock` off `master`; `src/game/m_banti.c`, `include/m_banti.h`). The clock fade was linked: `banti_move` read `mPlib_Get_address_able_display()` (the selected resident's `address_able_display`, a per-actor field set from each player's own `now_main_index` table in `m_player_common.c_inc`), so one player running faded both clocks.

Checkpoint 1, per-view fade (built and playtested 2026-10-26, committed `76d024e`):
- `Banti_c` splits: per-view `Banti_view_c view[banti_VIEW_NUM=4]` (addressable_type, timer, alpha); `disabled` and the slide fields (`disp_left`, `move_left`, `move_timer`) stay shared this checkpoint, with the slide still driven by the selected resident (view 0) as retail.
- `banti_move` loops active views (view i player = selected resident / `mPlib_extra_player_at(i-1)`) and runs the retail addressable-timer and alpha logic per view from that player's own field; the demo/title forced fade stays global. The fade-in gate is now "this player has no menu open" (`banti_player_menu_open`: their primary opener match, or their own side slot) instead of the one global submenu.
- `banti_draw(play, view_idx)` takes the view index; the split HUD loop passes `i`, single player passes 0. View 0's behaviour is byte-identical retail.
- Playtest: per-player run/menu fade confirmed; one gap: a talk faded BOTH clocks (the forced-fade demo check was global).

Checkpoint 2, per-view slide, scoped demo fade, half-slide placement (built 2026-10-26, MINGW32 clean, playtest pending):
- The demo/title forced fade is now per view: an active demo forces the fade only on its own player (talker match, side-talk player match) or when unattributable (talker NULL, e.g. door zooms — retail-global behaviour); the title demo stays global. This is what made both clocks fade during one player's NPC talk.
- The slide is per view: `banti_view_slide(view_idx, screen_pos, dt)` runs the retail state machine (10-frame timers, `disabled` gate, `disp_left`) on that view's own fields. The PC driver in `Game_play_move` projects each view's player through a rebuilt per-view matrix (`play_view_projection_matrix`: `guPerspective` × `guLookAt` from that view's camera and rect, applied via the new `Game_play_Projection_Trans_m` in `m_lib.c`) — the old shared check projected the selected resident through the stale last-drawn view's matrix, so the slide could trigger for the wrong player. The retail path keeps the single-camera projection for view 0.
- Half-slide placement fix: a half's ortho shows only content x 0..160 (the retail right half) and the clock sits at about x +40..+150 there, so the old `-92` slide landed half the clock past the half's left edge — the observed "50% on, 50% off". The half now slides `-40` (flush to the half's left edge, fully visible); quadrants keep the retail `-184` (full ortho).
- Playtest: each player parking in their own view's top-right corner slides only their clock; the half clock stays fully on screen; talks fade only the talker's clock; single player unchanged.

**Co-op menu and join/leave** (branch `feature/coop-menu` off `master` after the per-player-menu merge). Phase 4.

Checkpoint 1, title mode menu (playtested 2026-10-26, committed `a2585fb`):
- Title menu labels are `Single Player / Co-op / Settings / Quit Game` (`aAL_pc_menu_draw`, `aAL_pc_game_start_wait` in `src/actor/ac_animal_logo.c`). Both play entries use the existing start path; navigation now runs to index 3 and the list starts one line higher so the fourth item clears the copyright line.
- `g_pc_coop_session` (`pc/src/pc_settings.c`, declared in `pc/include/pc_settings.h`) records the choice for the session only. It is not written to `settings.ini`; the title sets it before a session starts.
- `Actor_info_spawn_guest` (`src/game/m_actor.c`) returns unless the session is co-op, so Single Player stays one actor even when P2-P4 device rows are configured. Checkpoint 2 removed the load-time auto-spawn: Co-op starts full screen and a row joins with Start.
- Playtest: four items navigate and draw; Single Player with a P2 row configured has no guest; Co-op with the same settings spawned P2 at the time of this checkpoint (checkpoint 2 removed that auto-spawn); Settings still opens and Quit exits.
- Not this checkpoint: press-to-join into a live town, queued leave, save on join/leave (all checkpoint 2).

Checkpoint 2, per-player session menus and join/leave (built 2026-10-26, playtest pending):
- Full screen by default: Co-op resets `g_pc_coop_joined[]` and `g_pc_coop_guest_slot[]` on the title. No guest spawns until its row presses Start in town. The session's joined rows and their resident slots are remembered; `Actor_info_spawn_guest` restores exactly those rows on an outdoor scene reload (house round trip) instead of pairing residents by slot order again.
- Join: `play_coop_session_proc` (`src/game/m_play.c`, end of `Game_play_move`) calls `Actor_info_join_guest` (`src/game/m_actor.c`) in a safe frame (no menu, demo, fade or wipe). It takes the first configured pad whose Start edge fired and that has no actor, finds the first free resident, queues slot+pad, spawns beside the selected resident with arg 0, records `g_pc_coop_guest_slot[pad]`, sets the joined flag and saves. One join per frame. The first free resident is used until a character-select step exists.
- Session menu: `pc/src/pc_session_menu.c` keeps one state per Controls row. Back/Select on a guest's pad opens it (Escape when the keyboard row is that guest); it draws in the opener's half/quadrant with `Player N / Resume / Leave Session`; A confirms, B/Back/Escape closes. The opener's actor is added to the per-frame wait list (`Actor_info_add_wait_actor`); other players keep playing. Several guests can have menus open at once. `pc_pad_row_for_instance`/`pc_pad_keyboard_row` (`pc/src/pc_pad.c`) map SDL events to a row, and `padmgr` zeros that row's game input while its menu is open. Opening requires the submenu to be `WAIT`/`NONE`/`IDLE`, no guest menu opener, no primary or side demo, no message window in either slot, and no fade/wipe; the overlay resolves the opener's view with `play_actor_view_index`.
- Leave: `Leave Session` sets a 3 s request. The request runs when the frame is safe and the actor is idle (`mPlayer_INDEX_WAIT`); it deletes the actor, clears the row's joined flag and resident mapping, closes the menu and saves. A guest that is the primary demo talker or the side talker is never deleted. Unused pads reset `s_follow_ready`/`s_ofs_ready` and discard stale requests, so a rejoining row starts clean.
- Host pause: Escape or Back on row 0 keeps the existing Resume/Settings/Quit menu and still freezes the game, but it is drawn in P1's band/quadrant instead of full screen. `pc_pause_menu_begin_frame` lets the post-game fallback skip the duplicate draw. Guest menus do not include Settings. The pause refuses to open while a co-op demo/dialogue is active or while any game menu (inventory, notice board, map board) owns the submenu, so it cannot freeze or cover a conversation/menu; press again after.
- Actor keep-alive: `Actor_is_near_active_player` (`src/game/m_actor.c`) now gets the host from `mPlib_host_player_actor()` (the actor whose Controls row is 0) instead of `get_player_actor_withoutCheck`, which returns the bound guest during a guest move. Without this, a guest's birth-control pass never checked the host and could delete the host's structures (player houses). The old lookup remains only as a pre-registration fallback.
- Boards: `mSM_submenu_ctrl` (`src/game/m_submenu.c`) loops every active player for the notice/map board, checks that player's facing and `item_in_front` and A, records them as the menu opener, and resets that player's A. P2 can open the notice board and map board in their own view.
- Band drawing: `play_draw_coop_overlays` (`src/game/m_play.c`) sets the opener's view/scissor with `play_set_view_menu`, then `pc_session_menu.c` or `pc_pause_menu.c` draws. `pc_menu_util.c` gained a band backdrop path using `fade_rgba8888_draw`, the same full-screen fill used by playtest probes; the band scissor clips it to the opener's rect. A texture rectangle was transformed by the band viewport, and a projected quad was rejected in the font list, so both were retired. The overlay is bracketed with `PC_NOOP_MENU_DEPTH_OFF`/`ON`; when no overlay is drawn, the helper leaves the game's view/scissor untouched.
- Save/join/leave: `Actor_info_save_game` (`src/game/m_actor.c`) snapshots the save's `fg` table, restores structure/item fg entries, writes, then restores the live snapshot. The mid-game join/leave save previously persisted spawned houses/shops as reserved/empty entries; the normal save avoids this by running after play teardown.
- Save: `pc_save_commit` (`pc/src/pc_m_card.c`) runs the same pre-write side effects as a full save (clears the reset code, updates play time and copy protection) and rewrites the home GCI. It no-ops until `pc_save_ready`. Mid-game join/leave use `Actor_info_save_game` (`src/game/m_actor.c`): it snapshots the save's `fg` table, calls `restore_fgdata_all` so structures/items put their fg entries back, writes, then restores the live snapshot. Writing the live table directly persisted spawned houses/shops as reserved/empty entries and was the cause of the missing player houses; the normal save path avoids this by running after play teardown.
- Playtest 2026-10-26: Checkpoint 2 passed and was approved for commit. Houses survive join/leave and reload; the session menu and P1 pause backdrops draw with the band fill; Start-to-join, leave/rejoin, per-player menus, P2 boards, input isolation and host pause all work; session menus are blocked by inventories, talks and message windows, and the host pause is blocked during a co-op dialogue and any game menu. Remaining: 3-4 player/simultaneous-menu validation.
- Not covered: host leave, character select for the joiner, a join/leave prompt or HUD hint, cross-scene join (town only), guest Settings, and per-player state leftovers when a guest leaves mid-hold.

**Return to Title** (branch `feature/coop-menu`, playtested 2026-10-26). P1's pause menu only; guest session menus do not get it.
- Pause main page: `Resume / Settings / Return to Title / Quit Game` (`pc/src/pc_pause_menu.c`). The new entry opens a No/Yes confirm page warning `Unsaved progress will be lost`.
- Yes calls `return_to_title`: clears `g_pc_paused`, sets `g_pc_pause_input_drain`, closes guest session menus, and for a play scene sets `fb_wipe_type = WIPE_TYPE_FADE_BLACK` and `fb_fade_type = FADE_TYPE_OUT_RETURN_TITLE`, which the existing `Game_play_fbdemo` path sends to trademark -> title. A non-play scene switches to trademark directly. No save is written; the title's save reload discards the session.
- Playtest: single player, co-op without P2, and co-op with P2; confirm Yes returns to title, unsaved furniture/mail changes revert, and guest session menus never show the entry.

**Simultaneous submenus** (branch `feature/simultaneous-menus` off `master`, approved and fully playtested 2026-10-26). Phase 4. Two or more players have their own Start/Y inventory or map open at once, each drawn in the opener's view, without freezing each other.

Decisions (2026-10-26):
- In co-op with guests present, P1's own menu stops freezing the world and works like a guest's. Single player keeps the retail freeze.
- Checkpoint 1 is Start/Y inventory and map only. Actor-opened menus (mailbox, boards, shops, NPC give/sell, music board) stay on the primary `play->submenu`, one at a time.

Design (mirrors guest NPC talk: primary plus side slots, owner stamped at open, context swapped around that slot's move/draw, closing one never resets the others):
- Slot = Controls pad (0-3), `mSM_SLOT_NUM` 4. Slot 0 is `play->submenu`. Pads 1-3 get a side `Submenu` copied into `play->submenu` for their own move/draw and copied back, with `item_p` re-pointed at the copy's `items[0]` (it points into its own struct).
- Overlay state is all static and single-instance, so it is per slot: `ovl_base`, every `*_ovl_data`, `SubmenuArea_dlftbl`/`SubmenuArea_visit`, and the tick accumulators. `graph_dt_60hz_ticks` adds `dt` per call, so two open menus sharing one accumulator would each run at double speed.
- `mSM_cur_slot()` / `mSM_set_cur_slot()` (`m_submenu.c`) pick the slot. Overlay statics are `X_slot[mSM_SLOT_NUM]` behind `#define X X_slot[mSM_cur_slot()]`, so the overlay code itself is unchanged.

Steps:
1. Per-slot overlay data and area link state (built 2026-10-26, MINGW32 clean, committed). The slot is always 0, so single player is unchanged by construction. Per-slot: `ovl_base`, 26 `*_ovl_data` statics, `SubmenuArea_dlftbl`/`visit`, and the accumulators in `m_submenu.c`, `m_submenu_ovl.c`, tag, mscore, inventory, cporiginal, cpmail and catalog. Overlays that already `zelda_malloc` their data (map, catalog, calendar, gba, needlework) follow `ovl_base`. Not made per slot (out of scope): `mLE_md_name`, `mLE_npc_end_word_edit`, `gb_ovl_change_flg`, the `m_map_ovl.c` model-pointer caches.
2. Side-slot storage and the copy-in/copy-out helper, with the `item_p` fix. (built and committed 2026-10-26, `6f7ecc4`)
3. Route a guest's (and, in co-op, P1's) Start/Y into that player's own slot; fix `mPlib_able_submenu_type1` and `start_refuse`, which check host state. (built and playtested 2026-10-26; guest Start/Y routes to the guest's side slot = its Controls pad; per-player able check `mPlib_able_submenu_type1_player`; the guest open gates on its own slot's refuse state, not the primary's; P1's Start/Y is already slot 0. The retail gate procs on `PLAYER_ACTOR` — `check_request_main_priority_proc`, `able_submenu_request_main_index_proc`, `check_request_change_item_proc` — all ignore their receiver and read `GET_PLAYER_ACTOR_GAME` (the selected resident), and `mPlib_CheckScene_AbleSubmenu()` on PC is just `SubmenuArea_IsPlayer()` ("no primary menu open"), so the guest gate uses player-explicit `_for` forms (`m_player_common.c_inc`, declared in `m_player.h`) and the scene table alone. Guest map via X is still not routed.)
4. Run each slot's move/draw in its opener's context, one menu per slot per frame. Check `mSM_move_End`'s `mSc_dmacopy_all_exchange_bank`: one menu closing may reload object banks another open menu still uses. (built and playtested 2026-10-26: side moves run after the primary's every frame — the WAIT tick must keep running so a closed slot's `start_refuse_timer` decrements, otherwise every later open is silently refused; move and draw both run under `mSM_side_slot_begin/end` (the draw pass needs it too: without it `SubmenuArea_visit` compares against slot 0 and every per-slot static resolves to slot 0); each panel gets its own flush/restart/`emu64_refresh`/band/depth-clear pass; side-slot openers join the per-frame wait list; `play_coop_safe` treats an open side menu as unsafe for join/leave; side-slot prerender modes are promoted in `makeBumpTexture` without the EFB copy, which split never displays. Playtest: P1 and P2 inventories open at once, each editing its own items with its own pad; one closes while the other stays; the open sound and panel track the right slot; the bank-reload close risk did not reproduce.)
5. Per-slot wait list and a live world while any menu is open (P1's menu included, co-op only). (built and playtested 2026-10-26: the host-menu branch's manual guest `mv_proc` loop is replaced by the full `Actor_info_call_actor` loop with the slot-0 opener in the wait list whenever co-op guests exist; mMsg_Main runs; the camera proc gate runs while co-op guests exist; the demo processor also runs while any co-op menu is open so a guest can start a sign/NPC talk during the other player's menu, with `mDemo_owner_menu_open` (`m_demo.c`) refusing demo requests whose owner has their side slot open or opened the primary that is open — both request paths guarded; `mEv_run` stays gated on the primary WAIT. Single player keeps the retail freeze. Playtest: P2 walks, collides, reads signs and boards, and opens their inventory while P1's menu is open; P1 reads signs while P2's menu is open.)
6. Draw each menu in its opener's view (`play_set_view_menu` already does this); keep close paths independent. (done with steps 4-5; playtested: each panel sits in its opener's half and tracks the opener, closes are independent.)
6. Draw each menu in its opener's view (`play_set_view_menu` already does this); keep close paths independent.
7. Update the `pc_session_menu.c` gate, which blocks while any game menu is open. (built and playtested 2026-10-26: a session menu is blocked only by the requesting player's own things — their own primary menu (opener match), their own side-slot inventory, their own demo/side-talk/message window; an active demo or window with no attributable owner blocks everyone; `FirstIntro`, fades/wipes stay global. The host pause (`pc_coop_dialogue_active`) is blocked only by the host's own primary menu; a guest's menu no longer holds it hostage. Playtest follow-up: a keyboard guest's ESC (their session button) leaked into the host pause whenever the session gate declined it, because the ESC branch in `pc_main.c` toggled the pause unconditionally after the session handler declined — the pause then drew in P1's band and read as "P2's menu over P1's screen" (confirmed by the `pause:` probe; the session gate itself never fired). Fix: ESC toggles the pause only when `pc_pad_keyboard_row() == 0`; a declined guest ESC does nothing. Probes removed; retested clean.)

Verification: MINGW32 `./build_pc.sh`; single player inventory, mailbox and board unchanged; P1 and P2 inventories open at once, each editing their own items; one closes while the other stays; resize with the hand on the item; a menu open during the other player's NPC talk; 3-4 players once the save has four residents. (Playtested 2026-10-26 except the 3-4 player pass, which is deferred by the operator: P1 and P2 inventories open at once and edit their own items; one closes while the other stays; P2 walks, collides, reads signs and boards and opens their inventory while P1's menu is open; P1 reads signs while P2's menu is open; single player unchanged; the bank-reload close risk did not reproduce.)

Open after the 2026-10-25 playtest of `feature/per-player-hud`. Paths are in section 4. Do not re-search them.
- Mailbox and notice board in co-op: done. Both panels draw in the opener's half (band `l=0 r=160 top=60 bottom=180`), colours/textures match single player, and the hand plus the player model sit on the panel at any window size. Two fixes carried it: `load_player` no longer steals `SubmenuArea_visit` while a menu is open, and `mSM_change_view` now maps 3D viewport centres through the same aspect squeeze the 2D panel gets. Details in section 4, "Split menu catch-up". Do not retry alpha, blend, or cull.
- Player 2 indoors: Player 1 entering their own house removes Player 2's half. Required: Player 2 keeps their outdoor state while Player 1 is inside. Guest spawn is `SCENE_FG` only, from `Actor_info_ct` (`src/game/m_actor.c`). The indoor scene is not `SCENE_FG`, so that actor is destroyed on the way in and created again on the way out. Player 2's camera during the door zoom is already correct. Another resident's house used to go black and stop answering. `play_split_live` keeps a wipe on one list. Not retested since Player 1's own house started working. **Deferred: needs dual/multi-world instancing** (2026-10-25).
- Mailbox close no longer leaves Player 1 in `MAIL_JUMP`.
- Per-half clock: done. A half is 320x480 EFB while the font ortho spans content `x ±160`, so the clock mapped 1 EFB per content across but 2 down and read as 2:1 squashed with a retail height. It now uses `mFont_CulcHudHalfOrthoMatrix` (content `x 0..160`, `y ±120`) while `pc_gx_hud_half` is set, which is the retail right half across the whole half, and the `disp_left` slide is `-92`. Playtest: numbers no longer crunched and the size matches single player. Details in section 4.

## 8. Handoff notes

- Repo is a PC port, not a decomp. The decomp (`ac-decomp`) is upstream; the port lives in `pc/`.
- Do not change game asset handling. The disc is read at runtime.
- Keep single-player behavior unchanged at every phase.
- Use `Write` for new files, and keep changes to `src/` minimal and well-scoped.
- `emu64.c` is C++. A PC symbol used from a method is declared in `pc_platform.h`'s `extern "C"` block, with `g_pc_widescreen_stretch`. Do not declare it inside a method. Do not declare it later in the file than the first use. The file-scope `extern "C"` next to `pc_gx_tlut_set_native_le` only covers calls below it, such as `pc_gx_clear_depth` from `dl_G_NOOP`.