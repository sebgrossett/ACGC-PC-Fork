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
  - Shared-accumulator audit complete: the only draw-path (per-view) accumulator was the mailbox ping — fixed and playtested. Singleton systems with statics are safe. Instance-count move-side statics remain (low, see mitigations).
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
| A5 | P2 | Instance-count accumulator statics (6 sites) | open |

## Pass log

- 2026-10-26: Join and leave no longer write the town file. `Actor_info_save_game` and both call sites are gone. The gyroid save remains the only write. The accepted-as-is note about its fg snapshot is closed with the function.
- 2026-10-26: Initial audit of the co-op work (55 commits, 2026-10-24..26). Verdict: proceed. Findings A1-A5 opened. The mailbox shared-accumulator fix and the receiver-ignoring proc pattern are recorded in `plan.md` §4 and were verified during this audit.
- 2026-10-26: A1 done on `feature/audit-a1`. New helper `mSM_player_menu_open_by_slot(GAME_PLAY*, slot)` in `m_submenu.c` (primary busy with this slot as opener, or this slot's side-slot menu open/transitioning; slot-based because `PLAYER_ACTOR` is not visible in `m_submenu.h`). Reworked onto it: `mDemo_owner_menu_open` (now a one-line wrapper, pad param dropped), `banti_player_menu_open` (deleted; the clock fade gate calls the helper via `mPlib_player_slot_of`), and `pc_session_menu_can_open` (helper + the two retail `mode` tails kept inline as scoped extras). MINGW32 clean. Smoke regression owed by the operator: clock fade during a menu, demo guard vs a menu holder's A, session gate block.
- 2026-10-26: A1 smoke passed by the operator. A2+A3+A4 done on the same branch, MINGW32 clean. A2: `mSM_side_open_ok(play, target, player)` (target idle + reset clear + refuse clear + per-player able check) now backs the guest inventory, guest map and board open paths, each keeping only its specifics (FirstIntro, menu_type actor-open guard, map flag/position). A3: `play_session_wait_actors_add` + `play_side_menu_wait_actors_add` merged into `play_menu_wait_actors_add(play)` (primary opener added only when the primary menu is open, side openers, session rows); the `Actor_info_set_wait_actor`/`s_wait_actor` mechanism is deleted as dead (the consumer's `is_wait_actor` already checks both, and add+clear covers everything). A4: the two `[PAUSE]` debug printfs removed. A5 stays open (opportunistic). Operator smoke of the full menu-flow regression passed.
- 2026-10-26: Rom-path narrowed to `rom/`-only on the same branch (operator request): `find_disc_image` in `pc_disc.c` no longer scans `.`, `orig/` or the repo-root fallback; the pc_disc.h comment, the missing-ROM dialog in `pc_main.c`, `DOCUMENTATION.md`'s fallback chain and `plan.md` §6 updated. A disc-image file picker is recorded as a future-update goal in `plan.md` §7. MINGW32 clean after the game was closed (the first link failed with Permission denied because the exe was running). Next: merge, then the per-player-state slice.
