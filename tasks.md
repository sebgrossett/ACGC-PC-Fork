# Tasks

## Now
- Split drawing works. Next slice is one HUD per half (clock, mail, gyroid deposit).
- Logged, not this slice: own house door does not open; another house freezes black; menus black out Player 2's half. See `plan.md` section 7.
- A third resident and the co-op menu stay after the HUD. Title stays Start Game.

## Milestones
- [x] Phase 0: player list, frame order, camera/viewport
- [x] Phase 1 lookup: `get_player_actor_by_index`, index 0 unchanged
- [x] Controls tab: one device row per player, saved in `settings.ini`
- [x] Player lookup is the resident save slot. Single-player registers slot 0
- [ ] Phase 2: up to 4 player actors, each reading the pad channel from its device row
- [ ] Phase 3: split-screen viewport, camera, HUD
- [ ] Phase 4: co-op menu, queued join/leave, save on join and leave
- [ ] Phase 5: tie-break, group events, 2/3/4 player pass on the PC build

## Progress
- 2026-10-24: Mapped the player list (prepended linked list, one scene spawn), frame order (actors, then one `play->camera`, then one viewport), and confirmed `now_main_index` is action state. Added `get_player_actor_by_index`; `get_player_actor_withoutCheck` is index 0. Ignored `*.ciso` / `*.iso` / `*.gcm`. Next: device dropdowns, then spawn a second player only after index means save slot.
- 2026-10-24: Added Player 1–4 device rows on the Controls tab. Default remains keyboard plus the first gamepad on player 1. `PADRead` writes that choice onto pad channels. The build that is already running will not include this. Next: second `./build_pc.sh`, then play player 1.
- 2026-10-24: Disc search still prefers `rom/`. If that folder has no image, the loader reads the repo root above `pc/build32/bin`. Next: one rebuild, then play.
- 2026-10-24: Rebuilt. Controls shows Player 1–4. Player 1 gamepad still moves the character. Disc at the repo root loads. Next: save-slot index, using the new town save for a later second actor.
- 2026-10-24: Player ctor registers `Common_Get(player_no)` in a side table. Lookup returns that slot. `PLAYER_ACTOR` size is unchanged. Single-player stays slot 0. Next: rebuild against the new town save, then a second actor.
- 2026-10-25: Rebuilt. First resident loads and moves with the gamepad. Title is still Start Game. `get_player_actor_withoutCheck` now follows the selected resident, so a later slot does not resolve as an empty slot 0. Next: rebuild, create the second resident, then a second actor.
- 2026-10-25: New-resident crash. The select actor stays in slot 0, then `player_no` becomes the free slot and the next frame dereferences a null lookup. One registered actor is now moved to the selected slot. Next: rebuild, resume the first resident, create the second resident again.
- 2026-10-25: Rebuilt. Second resident was created in the existing town. Next: spawn that resident as a second actor with its own private data and device row.
- 2026-10-25: Wrote the resident-select, save-init, private-pointer, appearance, and pad-channel paths into `plan.md` section 4 so later player-data edits do not re-search them. Next: second actor, using those paths.
- 2026-10-25: Outdoors spawn of the other resident when Player 2's device is not None. That actor is bound for its own private data and `pads[1]`. Player 2 left at None does not spawn them. Next: rebuild and walk both in town.
- 2026-10-25: Guest was invisible because `make_actor` runs `ct` immediately. Spawning the guest first let that `ct` take the session slot, and the selected actor consumed the guest queue. Guest now spawns after the selected actor is registered. Next: rebuild and confirm the other resident stands beside you.
- 2026-10-25: Still one resident while walking town. Draw would place a spawned guest at its own position (`Actor_draw`, `Player_actor_draw_Normal`). Spawn returned on `mEv_CheckFirstJob`, which stays set after `aID_retire_rcn_guide_wait` clears First Intro, until `aID_first_job` sees `_1A8`. Spawn runs only from `Actor_info_ct`. That check is gone. First Intro still skips the scripted scene. Traces are in `plan.md` section 4. Next: rebuild, Apply Player 2, load outdoors.
- 2026-10-25: Second resident is visible and ignores the keyboard. `PADRead` wrote channel 1, then `padmgr_HandleDoneReadPadMsg` cleared it because `device_type[1]` was not a controller. All four channels stay connected. Guest move still copies `pads[1]` onto `pads[0]` in `guest_begin`. Next: rebuild and walk the guest with the keyboard.
- 2026-10-25: Rebuilt. Both residents move at once, on separate devices, in different directions. One camera still follows the selected resident. Next: a second camera and viewport.
- 2026-10-25: Player 2 set back to None stays one actor. That check is closed. Next: split the window.
- 2026-10-25: World is drawn twice when another resident is out. Left half is the selected camera. Right half shifts that eye and center onto the other resident (`play_draw_split_side` in `src/game/m_play.c`). Scissor far-corner is a width in `emu64::dl_G_SETSCISSOR`. One actor keeps one full draw. Next: rebuild and walk both halves.
- 2026-10-25: Split load stayed black and the window stopped answering, while audio kept going. Both halves were appended to one display list and ran past `sys_dynamic`, so the frame never presented. The left half is shown first (`pc_graph_flush_world`), and the right half is a new list that keeps the framebuffer. Next: rebuild and enter town with Player 2.
- 2026-10-25: Rebuilt. Left and right halves both draw outdoors. Own house door does not open. Another house freezes black. Mail and gyroid deposit black out Player 2's half, the menu is not drawn, and the clock sits on that half. B can leave the menu. Logged in `plan.md` section 7. Next: per-player HUD.
- 2026-10-25: Working split saved on `feature/local-coop-split` before the HUD slice.
