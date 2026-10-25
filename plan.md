# Animal Crossing (GameCube) PC Port: Local Co-op Plan

**Status:** Two residents walk outdoors in a left/right split. Player 2 at None keeps the full window. House entry and menus are broken with two residents and are logged below. Next slice is per-player HUD. Title stays Start Game.

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
- `Player_actor_ct` registers `s_player_by_slot[player_no]`. `Player_actor_dt` clears every slot holding that pointer. Profile wrappers are `Player_actor_*_call` in `src/game/m_player_call.c`.
- Select scene still has the slot-0 actor when new-resident init changes `player_no`. The one-actor rebind in `get_player_actor_withoutCheck` is what keeps that frame from a null deref. A second registered actor must not use that rebind.

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
- Guest resident (`Actor_info_spawn_guest` in `src/game/m_actor.c`): outdoors (`SCENE_FG`, enum 7), not during `mEv_CheckFirstIntro`, and only when `g_pc_settings.player_input[1]` is not `PC_PLAYER_INPUT_NONE` (6). Spawned after the selected actor, because `make_actor` runs `ct` immediately and `mPlib_guest_slot_ready` (`src/game/m_player_lib.c`) waits for the session slot. `mPlib_queue_guest_slot` is consumed by `Player_actor_ct`. `Player_actor_*_call` binds that actor, points `Now_Private` at `private_data[slot]`, and copies `pads[1]` onto `pads[0]` for that call. Face and cloth reload around the guest draw, then reload the selected resident. Body mesh stays the selected resident's gender bank. A created guest is drawn at its own `world.position` (`Actor_draw` in `m_actor.c`, `Player_actor_draw_Normal` in `m_player_draw.c_inc`). Cull does not delete it (`block` is -1, `npc_id` is `EMPTY_NO`).

Guest spawn trace (do not re-search):
- Scene id is already the destination when `Actor_info_ct` runs. `Game_play_change_scene_move_end` (`src/game/m_play.c`) writes `scene_no` from `play->next_scene_no`, then `graph_proc` (`src/graph.c`) leaves `while (game_is_doing)` and the next loop calls `play_init`. `Gameplay_Scene_Read` sets `play->scene_id` before `Actor_info_ct`.
- Select doors (`aNPS2_make_door_data` in `src/actor/npc/ac_npc_p_sel2_talk.c_inc`): home and both station doors are `SCENE_FG`. Talk 26 is `SCENE_START_DEMO2`.
- `mEv_SetFirstJob` / `mEv_SetFirstIntro` run together at the end of `ac_npc_guide_move.c_inc` and `aNG2_scene_change_wait_init` in `ac_npc_guide2_move.c_inc`. `aID_retire_rcn_guide_wait` (`src/actor/ac_intro_demo_move.c_inc`) clears First Intro when the guide actor is gone, then the player can walk. `aID_first_job` clears First Job only when `_1A8` is set (`aNRG2_all_job_end_init` in `ac_npc_rcn_guide2_schedule.c_inc`). Both flags are per `player_no` (`mEv_SAVED_FIRSTJOB_PLR0` / `mEv_SAVED_FIRSTINTRO_PLR0` in `src/game/m_event.c`).
- Device row: the menu edits `s_pending` (`pc/src/pc_settings_menu.c`). `apply_pending` copies it onto `g_pc_settings` and `pc_settings_save` writes `player2_input`. Back without Apply leaves index 1 at None. Defaults are `{Key and Pad, None, None, None}` in `pc/src/pc_settings.c`.
- Other resident: `mPr_CheckPrivate` is `mLd_CHECK_LAND_ID` (`(land_id & 0xFF00) == 0x3000`, `include/m_land.h`). `mPr_InitPrivateInfo` (`src/game/m_private.c`) copies `land_info->id`. Empty slots stay `0xFFFF` from `mPr_ClearPersonalID`. The selected `player_no` is skipped. One created resident means no guest.

**Camera and rendering**
- `include/m_play.h` — one `Camera2 camera` on `GAME_PLAY`. `Game_play_camera_proc` runs after actor move, once per frame, and follows the selected resident.
- Split, when `mPlib_other_player_actor` is set (`src/game/m_play.c` `play_draw_split_side`): the left half is built and shown with `pc_graph_flush_world` (`src/graph.c`), then the right half and the HUD are built in a fresh list. One list cannot hold two towns; writing both used to run past `sys_dynamic` and the frame never presented. `pc_gx_keep_framebuffer` keeps the left image when the second pass starts. Right rect copies `play->view` eye and center, shifted by the other resident's position minus the selected resident's. `setScissorView` drives both the viewport and the perspective aspect (`src/game/m_view.c`). One actor leaves that path and keeps a full-screen draw.
- `setScissorX` scales the 320x240 rect by 2 into the 640x480 EFB. `emu64::dl_G_SETSCISSOR` turns the far corner into a width and height (`src/static/libforest/emu64/emu64.c`).
- `pc/src/pc_gx.c` — `GXSetViewport` / `GXSetScissor` scale that EFB rect to the window.
- Acres are still loaded around the selected resident. The right half can show unloaded ground when Player 2 walks far. HUD, the clock, and messages stay one full-screen pass after both halves.

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

- Per-player `Now_Private`, `player_actor_exists`, held item, and menu state
- Per-player HUD layout for 2-4 players. The world split is left/right for two residents. The clock and messages still draw once (`banti_draw`, `mSM_submenu_draw` in `Game_play_draw`). That single pass is the menu work
- Field data follows the selected resident, so the right half can show unloaded acres when Player 2 is far
- Join/leave queue, including joins waited out through a host event

Known from the 2026-10-25 split playtest. Do not re-search these before the HUD and house slices:
- The selected resident cannot enter their own house. Cause not confirmed. Door checks still use one player (`GET_PLAYER_ACTOR` / `pads[0]`).
- Entering another resident's house goes black and the window stops answering. Guest spawn itself is outdoors only (`Actor_info_spawn_guest` returns unless `Save_Get(scene_no) == SCENE_FG`). The freeze is on the transition. `makeBumpTexture` flushes the left half whenever a guest actor exists, including while a door wipe is running.
- Mail and the gyroid deposit open to a black Player 2 half. The game keeps running and B can leave. The menu is not drawn. The clock and date land on Player 2's half. One `Submenu` on `GAME_PLAY` (`mSM_submenu_draw`). The prerender branch in `makeBumpTexture` returns before the right-half world is built.

## 8. Handoff notes

- Repo is a PC port, not a decomp. The decomp (`ac-decomp`) is upstream; the port lives in `pc/`.
- Do not change game asset handling. The disc is read at runtime.
- Keep single-player behavior unchanged at every phase.
- Use `Write` for new files, and keep changes to `src/` minimal and well-scoped.