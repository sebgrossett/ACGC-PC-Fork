# Audits

Backend (native C / GL). Principles: maintainability, KISS, DRY, small surface, efficiency.

## Verdict

**Proceed** to the house-enter transition slice, the `GET_PLAYER_ACTOR_NOW()` NULL audit, and per-player villager scheduling. No open P0 or P1. A9–A11 are accepted: no code.

## Open mitigations

| id | severity | summary | status |
|----|----------|---------|--------|
| A1 | P1 | One menu-open helper, `mSM_player_menu_open_by_slot` | done |
| A2 | P2 | One submenu open-gate helper, `mSM_side_open_ok` | done |
| A3 | P2 | One wait-actor setup, `play_menu_wait_actors_add` | done |
| A4 | P2 | `[PAUSE]` printfs removed | done |
| A5 | P2 | Six instance-count timers step once per `game_frame` | done |
| A6 | P1 | Co-op surface audit | done |
| A7 | P1 | Item popup and side talk step on the town. Light prompt and shop bell step once on the interior | done |
| A8 | P2 | `pc_world_trace` / `transition_log.txt` removed. `crash_log.txt` stays through shop playtests | done |
| A9 | P2 | Bind stays a value copy. Not a pointer swap | done |
| A10 | P2 | Inventory and the pending order stay where they are | done |
| A11 | P2 | Enter/leave is `pc_world_transition`. Shop NULL guards stay | done |

## Do not re-break

House enter/leave through playtest 40, plus A7. Detail is in `plan.md` §4. Short list:

- Look banks, matrix stack, insect, quest, and intro-demo clip stay with the world.
- `player_actor_exists` stays set while any world has a player.
- Mail land stays with the opener. A live mailbox keeps its prop id.
- The town actor loop keeps running while someone is indoors.
- The camera runs only when that play has a player. Exit requests normal camera priority 9.
- First town load uses the retail door. The walk-out clip runs only after a return into a town that stayed loaded.
- A structure that already has an actor is not spawned again.
- The indoor light prompt draws in the indoor view.
- `crash_log.txt` stays until the shop playtests are done.

## Pass log
- 2026-10-28: `feature/instant-block-dma` merged to `master` (`--no-ff`, 28 commits) on the operator's call. Carries the wide 5x5 draw/spawn windows, instant block DMA, spawn-pool raises, per-view prop cache, the tree-shake NULL fix, and the vsync pacing verdict (no code change). Next slices: house-enter transition, `GET_PLAYER_ACTOR_NOW()` audit, per-player villager scheduling.
- 2026-10-28: Museum checkpoint 1 held and committed (`fd9735a`). All four rooms toured by P2, walker identity/pads/door data correct, no crashes. The insect-room outdoor tint shelved (cosmetic; the emulated RDP palette state persists across display-list executions — diagnosis and fix direction in plan.md §7). Probes removed.
- 2026-10-27: Post office slice held and committed (`2453780`). Black screen root-caused (field-draw actor accumulation in the persistent town play), fixed with the spawn guard, the `aMHS_guest_walking_out` position check, and the stale transition-state clears. 10+ cycles plus the join door retested by the operator. Probes removed; `crash_log.txt` stays.
- 2026-10-27: A7+A8 playtest held (operator). Item popup, side talk, indoor light prompt.
- 2026-10-27: A7+A8 built on `feature/audit-a7` (`6e59e1b`). MINGW32 clean.
- 2026-10-27: Surface audit. A9–A11 accepted with no code. A7 was fix-first; it has since held.
- 2026-10-26: Menu audit A1–A5 done and playtested. Rom search is `rom/` only.
