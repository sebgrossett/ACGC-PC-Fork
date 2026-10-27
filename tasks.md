# Tasks

## Now
- P2 into their own house on `pc_world_transition`. The door names the resident who pressed A, and the interior spawns that resident. Built, needs a playtest. Then P2 into Cranny on the same note.
- Then the other shops, the museum, and the map board.
- After those buildings: held tools (this save has none), then 3-4 player validation last (needs 3 controllers and 3-4 residents).
- `crash_log.txt` stays through the shop playtests. `transition_log.txt` is gone.

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
- [x] Nook's Cranny, P1 in and out, inventory figure held (`feature/nook-shop`, `4af98a6`). P2's door is still open

## Progress
- 2026-10-27: P2's house door. `goto_other_scene` notes the demo resident, and the interior spawns and drops that resident. The house A check walks guests. Playtest: P2 enters and leaves their own house while P1 stays outside. P1's door should stay as it was.
- 2026-10-27: A7+A8 playtest held. Older playtest rows were folded; the detail is in git history.
