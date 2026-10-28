# Tasks

## Now
- Post office: black screen after the return fade is fixed (field-draw actor accumulation; see Progress). The slice is ready to commit, probes removed.
- Then the museum, then the map board.
- After those buildings: held tools (this save has none), then 3-4 player validation last (needs 3 controllers and 3-4 residents).
- `crash_log.txt` stays through the shop playtests.

## Milestones
- [x] Phase 0: player list, frame order, camera/viewport
- [x] Phase 1 lookup: `get_player_actor_by_index`, index 0 unchanged
- [x] Controls tab: one device row per player, saved in `settings.ini`
- [x] Player lookup is the resident save slot. Single-player registers slot 0
- [x] Audit A1–A8 (`audits.md`). A9–A11 are accepted decisions, no code
- [x] Phase 2: player actors read the pad channel from the device row (2 players playtested; 4 is phase 5)
- [x] Phase 3: split-screen viewport, camera, HUD (2 players playtested; quadrants are phase 5)
- [x] Phase 4: co-op menu, queued join/leave. The gyroid save is the only write
- [ ] Phase 5: tie-break, group events, 3-4 player pass on the PC build
- [x] One player indoors, playtest 40. A7+A8 held on `feature/audit-a7`
- [x] Nook's Cranny, P1 in and out, inventory figure held (`feature/nook-shop`, `4af98a6`). P2's house and P2's Cranny in and out held 2026-10-27

## Progress
- 2026-10-27: Return fade finished with no peephole, then stayed black and the process died. P2 never showed. Leave log ends at `cleanup done` after a full leave. Crash log was not rewritten. Uncommitted. Safe return `aee5c2f`.
- 2026-10-27 evening (post office black screen, solved): the "crash" was never an exit — the render loop stayed healthy and fast while drawing a whole-map terrain pile. Root cause: `Player_actor_ct_other_func1` spawns a field-draw actor with every player-actor construction; the persistent town play accumulated them (`afdlive` 1 -> 7), the display-list buffer gave out, and the frame stopped presenting. Fixed with three changes, all playtested by the operator:
  - `m_player_other_func.c_inc`: spawn the field-draw actor only when the play has none (actor-list scan). Global for all buildings.
  - `ac_my_house_move.c_inc`: `aMHS_guest_walking_out` now requires the player within 60 units of that house's spawn point (house actor position + the house-side diagonal, matching `Actor_info_resident_door`). Kills the synced house door on any interior exit; joins and own-house exits still open the door (retested after two anchor corrections — the spawn point sits ~40 units past the retail door-trigger radius).
  - `pc_world.c`: after every completed enter/leave, the town play's `next_scene_no` resets to its own scene and `s_enter_slot` clears, so a forced scene end can never re-enter the last building or grab the wrong resident.
- 2026-10-27 22:28: join-door playtest held — the join's house door animation is back. All probes removed (watchdog, panic/hang/world/mhs/leave logs, counters) and the bin-folder txts deleted. `crash_log.txt` and its SEH filter stay per audit A8 through the shop playtests. Earlier 21:xx diagnostic rows folded; the chain was: no exit, no stall, healthy fast play rendering nothing, correct door data every cycle.
- 2026-10-27: Doors that would go dead after entry now ask `pc_world_town_stays` and return to wait. The second-exit camera latch is cleared when the return body is created.
- 2026-10-27: A7+A8 playtest held. Older playtest rows were folded; the detail is in git history.

## Later
- Chunk/tile loading and frame pacing (added 2026-10-27). Trees pop in/out on the title screen; in-game the symptom is frame drops and bad pacing when sprinting across the map (fast block-crossing forces streaming work). Suspect the same overflow/over-accumulation class as the field-draw actor bug: audit what accumulates per block cross and per scene. Considered and parked: rendering the whole map at once — the display-list buffer is sized for a window's content and the whole map overflowed it (the black-screen cause), so the fix direction is per-window efficiency, not more geometry. First step: frame-time profile of a sprint across the map (the `pc_profiler` and the `ms=` state-log field exist); also fix the frame limiter disengaging after the first interior leave (16.7 ms -> 0.1 ms, found 2026-10-27).
- Performance audit: split-screen frame timing and pacing sometimes slips. The machine has the headroom, so find the stall. It may also show up in single player.
- Multiplayer audio. P2's effects (shoe squeak, door) go silent when they are far from the listener. Any entrance resets the soundtrack. Decide who the music follows, and keep every player's effects audible. One option is P1's music with everyone's effects. Do not start this until the buildings are held.
