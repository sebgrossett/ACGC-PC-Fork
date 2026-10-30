# Animal Crossing (GameCube) PC Port: Local Co-op Plan

**Status:** Post office, both Crannies, P2's house, and museum checkpoint 1 are held; the house-enter transition and C2 transition owner are merged (`ca95639`, `18fcb17`). Interiors still untested: Able Sisters, police box, villager/NPC houses, the Nook levels beyond the two Crannies, Redd's tent, tents/events, the lighthouse, and the own-house extra floors (backlog in `tasks.md`). The perf/popping session is merged to `master` (wide 5x5 draw and spawn windows, instant block DMA, spawn-pool raises, per-view prop cache, tree-shake crash fix). Frame pacing is closed: vsync ON removes the co-op sprint judder and is now the shipped default (`vsync = 1`). Queued next: C3/E13 (per-world state registry), then the building playtest backlog (A12: Redd's tent, fortune cart, kamakura, island cottage), the A13 ambient-proc playtest, and the parked colour-fade/insect-room tint. The `GET_PLAYER_ACTOR_NOW()` audit is merged (`1b7780e`) with its smoke test deferred to A16 until a save with held tools exists. Multiplayer audio is logged for later. Held tools are second last. 3-4 players are last. Crash paths stay in `docs/code-points.md`.

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

**Indoor / outdoor gates** (2026-10-27, operator)
- A per-player loop (shops, museum, signs, music board, map board, notice board) calls the per-player form (`mPlib_able_submenu_type1_player` and the other `*_player` / `*_for` procs). The host form reads the selected resident and faults when that resident is indoors. The music board (`aMSC_menu_open_wait`) was the first of this class.
- Town menus, the talker, and other town-global state step only while the town world is bound. The house world has its own resident and must not clear or rebind the outdoor one.
- Position and distance use the player in that loop, or `mPlib_extra_player_at` when the selected resident is gone. Do not add another copy of the same NULL test.

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
- On leave: character disappears from the map. Items, money, house, and save data remain with the player in the session.

**Saves**
- Co-op writes to the same save file as single player when it's the same town.
- The gyroid save is the only write (operator decision 2026-10-26). Join, leave, and Return to Title do not write the file. Return to Title reloads the file and drops the session. One file means a gyroid save commits every player in the session.

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

Moved to `docs/code-points.md` (45 KB) on 2026-10-29. Read it only when a slice needs the symbol or address detail.

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
- Gyroid save is the only write. Join and leave do not write the file.

**Phase 5: Polish and test**
- Conflict tie-break (controller index)
- Event blocking and group-event handling
- Test with 2, 3, and 4 players on the PC build (`build_pc.sh`, MSYS2 MINGW32)

## 6. Build and test

- Build: MSYS2 MINGW32, `./build_pc.sh` (see repo README)
- Cross-platform (planned 2026-10-29, see `tasks.md` Later): Linux first, then macOS, then Android; MSYS2 MINGW32 remains the current target.
- Packages: `mingw-w64-i686-gcc`, `cmake`, `SDL2`, `make`
- Output: `pc/build32/bin/AnimalCrossing.exe`
- The exe looks for a disc in `rom/` only (operator decision 2026-10-26; a file picker for choosing the disc image is planned for a future update).
- Debug flags: `--verbose`, `--no-framelimit`, `--model-viewer`, `--time HOUR`

## 7. Open items

Moved to `docs/plan-history.md` (54 KB) on 2026-10-29. Active order lives in `tasks.md` `## Next up`; read the archive only when a slice needs the old detail.

## 8. Handoff notes

- Repo is a PC port, not a decomp. The decomp (`ac-decomp`) is upstream; the port lives in `pc/`.
- Do not change game asset handling. The disc is read at runtime.
- Keep single-player behavior unchanged at every phase.
- Use `Write` for new files, and keep changes to `src/` minimal and well-scoped.
- `emu64.c` is C++. A PC symbol used from a method is declared in `pc_platform.h`'s `extern "C"` block, with `g_pc_widescreen_stretch`. Do not declare it inside a method. Do not declare it later in the file than the first use. The file-scope `extern "C"` next to `pc_gx_tlut_set_native_le` only covers calls below it, such as `pc_gx_clear_depth` from `dl_G_NOOP`.