# Audits

Scope of this audit: the co-op work landed 2026-10-24 through 2026-10-26 (55 commits, ~88 files, +4151/-456 under `src/`, `include/`, `pc/src`). Backend (native C / GL port); no frontend. Principles: maintainability, KISS, DRY, small surface, efficiency.

## Checklist notes

- **Structure / ownership of concerns**
  - Per-slot menu state lives correctly in `m_submenu.c` (storage, copy-in/out, open gates); per-player identity in `m_player_lib.c` (pad/openers/slots tables); view/layout in `m_play.c`; session/pause UI in `pc/`. The layering held up across seven slices.
  - `m_play.c` is large with co-op logic interleaved into the decomp file. Accepted: `src/` mirrors decomp structure and the helpers need `m_play.c` statics; do not split without a strong reason.
  - Four separate "owner" state systems exist: per-slot menu openers (`s_menu_opener_*_tbl`), demo talker (`s_demo_talker_*`), request owner (`mPlib_request_owner`), and the change-data order owner. They overlap conceptually and each has its own clear/swap discipline. See P1.
- **Duplication vs justified reuse**
  - "Does player P have a game menu open / owns the open menu" is implemented three times with subtly different conditions: `mDemo_owner_menu_open` (`m_demo.c`), `banti_player_menu_open` (`m_banti.c`), and inline in `pc_session_menu_can_open`. See P1.
  - `mSM_submenu_ctrl` runs three near-identical open-gate chains (guest inventory, guest map, board loop): slot-idle + refuse + reset + FirstIntro + able. See P2.
  - The per-frame wait-actor setup is three calls (retail `Actor_info_set_wait_actor` + session rows + side-slot openers). See P2.
  - The per-slot copy-in/out + context-wrap appears in both the move loop and the draw pass. Justified (different call shapes); do not abstract.
  - Session/pause menus already share `pc_menu_util`/`pc_text_draw` ✓.
- **State / rebuild surface**
  - Side-slot `Submenu` swap is sound (backup + restore; `item_p` re-pointed both ways; per-frame tick keeps refuse timers alive; scene cleanup resets). The swap's implicit invariant — everything inside the window sees the side copy — is documented in comments and `plan.md` §4.
  - Receiver-ignoring `PLAYER_ACTOR` procs are wrapped with `_for` forms; the retail wrappers are byte-identical. Pattern is recorded in `plan.md` §4.
- **Mock vs real seams** — N/A (no test seams; playtests are the verification path).
- **Dependency weight** — no new dependencies beyond SDL2 (already required). ✓
- **Perf risks**
  - Per-frame overhead measured/assessed: idle side-slot move copies (~2.6 KB/frame) — negligible; per-view HUD/clock loops — negligible; per-open-menu panel passes (flush/restart/`emu64_refresh`) — acceptable at 2 players, watch at 3-4 (deferred validation covers it).
  - Shared-accumulator audit complete: the mailbox ping and the six instance-count move-side gates (A5) are fixed and playtested. Singleton systems with statics are safe.
  - Positional mic anomaly (`play->camera.mic_pos` garbage in one probe log) — dismissed by the operator as a pre-fix artifact.
- **Accessibility baseline** — N/A (game port); input isolation per player is the relevant proxy and is playtested.
- **Tests on critical paths** — no automated tests; every slice is playtested before merge (established workflow). No change recommended.

## Findings

### P1
1. **Consolidate the menu-open ownership checks.** One helper in `m_submenu.c`, e.g. `mSM_player_menu_open(GAME_PLAY*, PLAYER_ACTOR*)` (and a slot form), covering: primary busy with this player as opener, and this player's side slot open/transitioning. Rework the three current implementations onto it. Do this BEFORE the per-player-state slice, which will otherwise copy the pattern again.

### P2
2. Extract the repeated open-gate chain in `mSM_submenu_ctrl` into one per-target helper (guest inventory / guest map / board loop keep their specifics).
3. Unify the per-frame wait-actor setup into one `play_menu_wait_actors_add(play)` (primary opener + side openers + session rows) in `m_play.c`.
4. Remove or `--verbose`-gate the two `printf("[PAUSE] ...")` lines in `pc_pause_menu.c` (debug debris).
5. Instance-count accumulator statics in move-side code (`ef_turi_mizu`, `ef_motiyuge`, `ac_train0`, `ac_s_car`, `ac_kamakura_indoor`, `m_msg_cursol`): gate rate scales with active instance count. Retail used global frame parity. Low impact; fix opportunistically when touching those files.
6. Accepted as-is: idle side-slot per-frame copies; per-open-menu panel passes. Revisit only if profiling flags them. The mid-game `Actor_info_save_game` snapshot was removed when join/leave stopped writing the file (2026-10-26).

## Verdict

**Proceed.** No P0. P1 is a small consolidation that should land before the per-player-state slice; P2s are opportunistic.

## Open mitigations

| id | severity | summary | status |
|----|----------|---------|--------|
| A1 | P1 | Consolidate menu-open ownership checks into one `m_submenu.c` helper | done |
| A2 | P2 | Extract the repeated submenu open-gate chain | done |
| A3 | P2 | Unify the per-frame wait-actor setup | done |
| A4 | P2 | Remove/gate the `[PAUSE]` debug printfs | done |
| A5 | P2 | Instance-count accumulator statics (6 sites) | done |
| A6 | P1 | Co-op surface audit: world snapshot, inventory, entrances, performance. Charter only. | in_progress |

## Pass log

- 2026-10-26: A5 playtested on `feature/audit-a5`. The six gates latch on `game_frame` and step once per frame (`ef_turi_mizu`, `ef_motiyuge`, `ac_train0`, `ac_s_car`, `ac_kamakura_indoor`, `m_msg_cursol`). One copy of each is unchanged.
- 2026-10-26: Join and leave no longer write the town file. `Actor_info_save_game` and both call sites are gone. The gyroid save remains the only write. The accepted-as-is note about its fg snapshot is closed with the function.
- 2026-10-26: Initial audit of the co-op work (55 commits, 2026-10-24..26). Verdict: proceed. Findings A1-A5 opened. The mailbox shared-accumulator fix and the receiver-ignoring proc pattern are recorded in `plan.md` §4 and were verified during this audit.
- 2026-10-26: A1 done on `feature/audit-a1`. New helper `mSM_player_menu_open_by_slot(GAME_PLAY*, slot)` in `m_submenu.c` (primary busy with this slot as opener, or this slot's side-slot menu open/transitioning; slot-based because `PLAYER_ACTOR` is not visible in `m_submenu.h`). Reworked onto it: `mDemo_owner_menu_open` (now a one-line wrapper, pad param dropped), `banti_player_menu_open` (deleted; the clock fade gate calls the helper via `mPlib_player_slot_of`), and `pc_session_menu_can_open` (helper + the two retail `mode` tails kept inline as scoped extras). MINGW32 clean. Smoke regression owed by the operator: clock fade during a menu, demo guard vs a menu holder's A, session gate block.
- 2026-10-26: A1 smoke passed by the operator. A2+A3+A4 done on the same branch, MINGW32 clean. A2: `mSM_side_open_ok(play, target, player)` (target idle + reset clear + refuse clear + per-player able check) now backs the guest inventory, guest map and board open paths, each keeping only its specifics (FirstIntro, menu_type actor-open guard, map flag/position). A3: `play_session_wait_actors_add` + `play_side_menu_wait_actors_add` merged into `play_menu_wait_actors_add(play)` (primary opener added only when the primary menu is open, side openers, session rows); the `Actor_info_set_wait_actor`/`s_wait_actor` mechanism is deleted as dead (the consumer's `is_wait_actor` already checks both, and add+clear covers everything). A4: the two `[PAUSE]` debug printfs removed. A5 stays open (opportunistic). Operator smoke of the full menu-flow regression passed.
- 2026-10-26: Rom-path narrowed to `rom/`-only on the same branch (operator request): `find_disc_image` in `pc_disc.c` no longer scans `.`, `orig/` or the repo-root fallback; the pc_disc.h comment, the missing-ROM dialog in `pc_main.c`, `DOCUMENTATION.md`'s fallback chain and `plan.md` §6 updated. A disc-image file picker is recorded as a future-update goal in `plan.md` §7. MINGW32 clean after the game was closed (the first link failed with Permission denied because the exe was running). Next: merge, then the per-player-state slice.
- 2026-10-27: A6 opened. Playtest 40 is `0181c0c`. Charter below. Examination has not started. No code until a finding is picked.

## Co-op surface audit (2026-10-27)

Charter only. The operator is refreshing context so this pass starts clean. Do not change code until they pick a finding. Safe return: `0181c0c` on `feature/indoors-split` (not pushed). Shops, the museum, and the map board wait on the verdict.

Scope: the whole co-op surface, not one building. Backend (native C / GL). Principles: maintainability, KISS, DRY, small surface, efficiency.

### Questions to answer

Rank each answer P0 / P1 / P2. A finding needs a file and a reason. Do not propose a rewrite without saying what the current code already does.

1. **Per-play state.** `PcWorld` in `pc/src/pc_world.c` copies process globals on every bind: arena, `fdinfo`, `Clip_c`, demo, `dlftbls[256]`, the player slot table, scene ids, field type, `now_private`, both door structs, `mFI`, `mCoBG`, env light, `Light_buffer`, look banks, the matrix stack, insect controller and clip, quest manager, intro-demo clip. Each `*_world_store` / `*_world_load` was added after a crash (`m_player_lib.c`, `m_demo.c`, `m_field_info.c`, `m_collision_bg.c`, `sys_matrix.c`, `ac_insect.c`, `ac_quest_manager.c`, `ac_intro_demo.c`). Decide whether bind should keep copying that list, or one world object should own it so a bind is a pointer swap. Name the cost of the copy.

2. **Globals still shared.** These already broke and are patched beside the snapshot, not inside it: `mco_work` (zeroed in `pc_world_destroy_child`), `now_home` (not snapshotted), the triforce wipe accumulator, and the one mic (`Camera2_SetMicPos` follows `get_player_actor_withoutCheck`). List the next statics that a town frame and an interior frame both write. Do not start a per-player mic.

3. **Inventory.** `m_inventory_ovl.c`, `Now_Private`, the side-slot `Submenu`, and the `mPlib` slot tables move with the world or the menu opener. The mailbox land-order stays with the opener so the indoor resident does not clear it. Say whether inventory and pending item orders belong on the player slot instead of the play, and what would break if they moved.

4. **Rooms, exits, entrances.** `pc_scene_splits` already treats the player room, NPC houses, shops (`SHOP0`, `CONVENI`, `SUPER`, `DEPART`, `DEPART_2`, `BROKER_SHOP`), post office, needlework, police box, and the four museum scenes as a second play. Only the house is playtested. `ac_shop`, `ac_conveni`, `ac_super`, `ac_depart`, `ac_br_shop`, `ac_my_house`, `ac_tent`, `ac_boat`, `ac_buggy`, and `ac_bridge_a` each carry their own `TARGET_PC` branch. Decide whether enter/leave can be one path (scene, door, wipe, who) instead of a special case per building. `pc_world_transition` and `pc_world_spawn_return` are the current path. `plan.md` §7 still says Player 2 indoors is deferred; that note is stale.

5. **Performance.** A split frame runs the town move, then `pc_world_move_rest` for every other live play, and a draw with a flush per early view. Bind captures and restores the whole `PcWorld`. `play_world_has_player` keeps the town actor loop running while someone is indoors and nobody is outside, so birth and structure scans still run. Bound the cost or name the duplicated walk. Do not optimize without a number or a duplicated walk.

6. **Copied gates.** The indoor/outdoor rule is: a per-player loop calls the `*_player` / `*_for` proc, town menu and talker state steps only while the town world is bound, and position uses the loop player or `mPlib_extra_player_at`. Name the copies. Do not add another NULL test that does the same job.

### Do not re-break

These held through playtest 40. A cleanup that deletes one of them is a regression, not a finding to implement:

- Look-bank ids stay with the world (black face).
- Matrix stack is restored on the way out.
- `player_actor_exists` stays set while any world has a player.
- Mailbox prop-id check, menu mode 0, and the land-order owner.
- Insect, quest-manager, and intro-demo clip stores.
- House re-entry returns to WAIT. Wishing-well uses `DUMMY_SHRINE` and rescans all players. Gyroid and furniture duplicates are refused.
- A joiner keeps the guest slot while the host is indoors. A fade-in keeps both worlds. An in-progress fade-in is not restarted. `FADE_TYPE_OUT_RETURN_TITLE` stays set through the last frame.
- Children drop at fade end, not at the confirm. `mco_work` is cleared when an interior is freed.
- The camera proc runs only when this play has a player. An exit requests normal camera priority 9.
- The first town load uses the retail door. The walk-out clip runs only after `pc_world_spawn_return` (`s_door_return`).
- A structure name that already has an actor is not spawned again.
- The indoor light prompt draws in the indoor view with the half ortho.
- Diagnostics (`pc_world_trace`, `crash_log.txt`) stay until a playtest holds after they are removed.

### Read first

- This file, `tasks.md` Now, `plan.md` §3 (Indoor / outdoor gates) and §4 (interior transition).
- `pc/include/pc_world.h`, `pc/src/pc_world.c`.
- `src/game/m_play.c` (`play_world_has_player`, `play_draw_keeps_one_world`, move and draw).
- `src/game/m_player_lib.c` (world store, guest slot).
- `src/game/m_actor.c` (join, resident door).
- `src/actor/ac_my_house_move.c_inc` (door return).
- `src/actor/ac_structure_clip.c_inc` (already-spawned structure).

### Output of the pass

Update this file with checklist notes, ranked findings, and a verdict (`proceed` to the buildings, or `fix-first`). Add an open-mitigation row per new finding. Add a `tasks.md` checkbox for each new P0/P1. One dated Progress row. No code in the same pass as the ranking.
