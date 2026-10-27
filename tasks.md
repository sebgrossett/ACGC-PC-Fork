# Tasks

## Now
- Shops, then the museum, then the map board. Same enter/leave as the house (`pc_world_transition`). A per-player loop uses the `*_player` / `*_for` proc. Town menu and talk step only on the town world. First building is Nook (`SCENE_SHOP0`). Start from `feature/audit-a7` (`6e59e1b`, playtest held 2026-10-27).
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
- [x] One player indoors, playtest 40 (`0181c0c`). A7+A8 playtest held on `feature/audit-a7`

## Progress
- 2026-10-27: A7+A8 playtest held. Shops are next. Older playtest rows were folded; the detail is in git history.
- 2026-10-27: A7+A8 on `feature/audit-a7` (`6e59e1b`). Item popup and side talk step on the town. Light prompt and shop bell step once on the interior. `pc_world_trace` removed. `crash_log.txt` stays.
- 2026-10-27: Co-op surface audit. Bind stays a copy. Inventory and the pending order stay put. Entrances are already one path.
- 2026-10-27: Indoors split playtest 40 holds (`0181c0c`). House enter/leave, mailbox, rejoin, return to title.
- 2026-10-26: Menus, clock, join/leave, spawn-at-house, gyroid, guest map, and A1–A5 playtested. A1–A5 are on `master`.
