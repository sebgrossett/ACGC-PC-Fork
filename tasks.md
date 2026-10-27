# Tasks

## Now
- Inventory outfit figure after P2 leaves: the draw scissor stays on the last half. Built, needs a playtest. Then P2 into Cranny.
- Then P2 into Cranny on the same `pc_world_transition`. Then the other shops, the museum, and the map board.
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
- [x] Nook's Cranny, P1 in and out (`feature/nook-shop`, playtest 2026-10-27). P2's door is still open

## Progress
- 2026-10-27: Inventory figure. A one-player frame after a split kept the right-half scissor, so the outfit model drew there. The full rect is restored before the view is set. Playtest: P2 leaves, P1 opens inventory.
- 2026-10-27: Cranny playtest held (`af85e90`). P1 enter, leave, re-enter. P2 leave inside and rejoin. P1 enter alone. After P2 leaves, P1's inventory outfit figure is in the wrong place. Menus still open.
- 2026-10-27: A7+A8 playtest held. Older playtest rows were folded; the detail is in git history.
