# Co-op engine audit

Companion to `audits.md` (A17) and `tasks.md`. Purpose: one place to record where the
engine still assumes a single player, where per-world state is incomplete, where the
same helper is re-implemented, and which subsystems are candidates for optimization or
a full rewrite.

This file is the working document for the audit. `audits.md` keeps the ranked findings
and mitigations; this file keeps the list, the evidence and the verdict per area.

## How to use

Every entry gets an id (`E<n>`), an area (file/subsystem), what is wrong or missing, a
class, an impact, a proposed action and a status. Do not delete entries: mark them
`done` and keep the note, or `parked`/`rewrite` with the reason.

Classes:

- `HELPER` - duplicated lookup logic that should go through one shared helper.
- `WORLD` - state that is global but should be per world (or per view/player).
- `OPT` - performance work (per-view cost, rebuilds, draw count, batching).
- `REWRITE` - candidate for a full rewrite rather than incremental fixes.
- `BUG` - a confirmed defect found during the audit.

Status: `open` | `in_progress` | `done` | `parked`.

Impact: `P0` (crash/wrong behaviour) | `P1` (systemic risk) | `P2` (cosmetic/cleanup).

## Rewrite eligibility criteria

An area is `REWRITE` when two or more of these hold:

1. It already carries more than one P0/P1 finding, or repeated regressions in the
   same machinery (transitions, world binding, palettes).
2. Its state is global by design, and per-player/per-world special cases keep being
   added around it instead of inside it.
3. Its interfaces are ad-hoc across files (each caller resolves the player/world
   differently).
4. Incremental changes have needed probes to prove behaviour more than once.

A rewrite must still be sliceable: say which slice lands first and what proves it.

## Entries

| id | area | issue | class | impact | status | notes |
|----|------|-------|-------|--------|--------|-------|
| E1 | `m_field_make.c` / `m_kankyo.c` palettes | Field palettes (`mFM_pal_area`) and BG common palettes (`l_bg_pal_common_dummy`) were one shared set for every scene; the insect room forced `mTM_TERM_4` into them | WORLD | P1 | done | Fixed 2026-10-29 (`c261358`): per-world copies captured from their own bytes, settled per view. Precedent for the pattern |
| E2 | `emu64::dl_G_LOADTLUT` | Same source address with changed contents only invalidated the slot; the reload happened on the next load, which is often the other view | BUG | P1 | done | Fixed 2026-10-29 (`c261358`): both load paths fall through to the reload in the same call |
| E3 | `m_play.c` fade overlay | A plain fade emitted no view scissor, so it was replayed with the current one (full screen for the last view) | BUG | P1 | done | Fixed 2026-10-29 (`c261358`): every split overlay carries its own view scissor |
| E4 | `m_play.c` fade view resolution | `pc_world_play_slot()` is -1 for the primary play, so a town fade fell back to the selected resident (the other player) | BUG | P1 | done | Fixed 2026-10-29 (`c261358`): door fades resolve to the demo talker |
| E5 | `m_play.c` transitions | Host-enter transition washes the guest's 3D half (~3% cool tint) | BUG | P2 | parked | Known issue K1 in `tasks.md`. Lead: uniform low-alpha overlay in the non-split fade/wipe path |
| E6 | `pc_world.c` snapshot | `PcWorld` covers a hand-maintained list; known gaps: `Common_Get(player_actor_exists)`, `SubmenuArea_visit`, `banti`, `gamePT`, most module statics, `Common_Get(time)` | WORLD | P1 | open | Audit the list against every `Common_Get`/module static touched per frame. Candidate for a registry/struct with a single capture/restore entry point |
| E7 | `m_lights.c` `light_list_buf` | One global node pool; plays hold `glight->list` pointers into it, and the world snapshot copies the pool by value. Works today, fragile under reordering | WORLD | P1 | open | Decide: per-world pool pointer or rebuild lists on bind |
| E8 | `m_field_make.c` BG common textures | `mFM_LoadBGCommonTex(TRUE, ...)` copies texture data into shared statics; the same class as E1 and the likely K1 carrier | WORLD | P1 | open | Compare before/after the child field make, then per-world copy like the palettes |
| E9 | terrain draw | ~140 draws/view, ~16-17 per visible block (one per ground tile); ~256 draws/frame at two views | OPT | P2 | open | Only real lever is a tile atlas or a merged block draw. Deliberately not taken 2026-10-29 |
| E10 | per-view tables | Terrain block table and per-view prop tables rebuild every frame for every view | OPT | P2 | open | Cache on view-player block change; measure `game_logic`/`emu64_task` first |
| E11 | player lookup helpers | `mPlib_player_near`, `mPlib_players_here`, `mPlib_player_here_at`, `mPlib_now_player_or_town`, `mPlib_player_at_door`, and the demo/request owner fallbacks overlap | HELPER | P1 | open | One documented resolution order (demo talker -> request owner -> nearest here -> resident -> NULL) used everywhere; retire ad-hoc variants |
| E12 | transition/fade routing | `play_fading_play`, `play_fade_is_split`, `play_fade_view`, `pc_world_entering_slot`, `s_fade_player` are separate partial answers to "who owns this transition" | REWRITE | P1 | open | Single "transition owner" API (play + view + kind) that every fade/wipe/overlay consumer asks |
| E13 | world system | `PcWorld` + `pc_world_*` carry per-world state as ad-hoc functions, and every new shared static needs another capture/restore pair | REWRITE | P1 | open | Evaluate a formal per-world state struct/registry before adding more captures |
| E14 | doors / scene entry | Every building re-implements the door pipeline: trigger geometry, request owner, walk-in clip, `goto_other_scene`, interior open, exit walk-out. Host and guest take different paths through it | REWRITE | P1 | open | One `pc_door_*` pipeline with a per-building descriptor (geometry, label, request, walk-in/out, scene). Each current conversion is a row in the matrix below |
| E15 | doors / player identity | Host vs guest entry are not symmetric: `pc_world_play_slot()` is -1 for the primary play, `s_enter_slot` is only noted at the scene change, `GET_PLAYER_ACTOR_GAME/NOW()` read the selected resident during the actor loop, and interior `player_no`/`now_private` are re-stamped per world | WORLD | P1 | open | Enumerate every host/guest difference in one place and route it through E11/E12; the per-building fixes should disappear into the pipeline |
| E16 | transitions / loading | Field make, world capture/restore, palettes, kankyo and light pools are all touched inside the fade while both worlds draw, and ownership changes mid-fade | WORLD | P1 | open | Document transition phases (demo -> wipe out -> scene open -> wipe in) with the state each phase owns; anything shared gets captured per world (E1 pattern) |

## Door transitions and loading

This is the highest-yield area: the same failures kept reappearing as each building was
enabled. Treat it as one pipeline, not per building.

What the audit must answer, per building:

1. **Who is entering?** The demo talker, the request owner, or the selected resident.
   They are not the same player when a guest enters (A14/E11).
2. **Which world owns the transition?** The fade/wipe starts on the town, the scene
   opens on the child, and the interior fades in on the child. `play_fading_play()` and
   `pc_world_play_slot()` disagree about the primary (E12/E15).
3. **What is loaded, and when?** Field make (palettes E1, BG textures E8, kankyo/light
   E7/E16) runs inside the fade while the town still draws for the other player.
4. **What survives the handoff?** `s_enter_slot`, `s_hop_*`, pads, `player_no`,
   `now_private`, door data, the palette/texture copies.
5. **Exit:** the returning walker, the walk-out clip, and the fade-in on the town.

Known failure classes seen so far (all fixed unless noted): wrong walker identity
(police box, museum), exit animation missing (Able Sisters), duplicate/ghost door
(structures), interior item give and talk handshake (police, shops), guest session menu
indoors, palette/texture bleed into the town half (insect room fixed, K1 parked).

### Test matrix

Rows are the buildings that currently split into a child world; columns are the owner
of the transition. Run each cell with the other player both outside and inside.

| building / scene | P1 enters | P2 enters | P1 exits | P2 exits | notes |
|---|---|---|---|---|---|
| own house (`SCENE_MY_ROOM_*`) | held | held | held | held | host-enter wash = K1 |
| Nook's Cranny (`SCENE_SHOP0`) | held | held | held | held | |
| Nook's conveni / super / depart | held | held | held | held | |
| police box | held | held | held | held | A14 decided here |
| Able Sisters (`SCENE_NEEDLEWORK`) | partial | partial | partial | partial | exit clip fix; duplicate door unexplained |
| post office | held | held | held | held | |
| museum entrance + 4 rooms | held | partial | held | partial | insect-room palette fixed |
| villager house (`SCENE_NPC_HOUSE`) | untested | untested | untested | untested | |
| island cottage | blocked | blocked | blocked | blocked | A12, not in this save |
| Redd's tent / fortune cart / kamakura | blocked | blocked | blocked | blocked | A12, not in this save |

A cell is `held` only when: correct walker, correct half fades/wipes, interior opens
with the right resident, exit clip plays, no leftover state (palette/texture/light),
and the other player's half is untouched throughout.

## Rewrite candidates

| id | subsystem | why | first slice |
|----|-----------|-----|-------------|
| E12 | transition/fade ownership | Repeated bugs this session (wrong half, unclipped fade, missed colour fade); state split across five helpers | Introduce `pc_transition_owner()` returning play/view/kind; port the fade overlay and view resolution to it |
| E13 | per-world state | Every shared static is a future regression; the snapshot list is hand-maintained | Inventory state, then one `pc_world_state_capture/restore` table with tests by comparison |
| E11 | player resolution | Same question answered in four places with subtly different fallbacks | One resolver + one documented order; migrate call sites in one pass |
| E14 | door / scene-entry pipeline | Every building repeats trigger, owner, walk-in, open, exit; host and guest diverge inside it and the same bugs recur per building | One `pc_door_*` pipeline + per-building descriptor; port one building, then run the matrix

## Pass log

- 2026-10-29: file opened, A17 registered. Seeded with E1-E13 from the sprint draw and
  insect-room tint sessions (palettes, TLUT reload, fade routing/scissor, K1, world
  snapshot gaps, light pool, BG texture bank, per-view cost, helper overlap, rewrite
  candidates). No code changes in this pass.
- 2026-10-29: added the door/transition area (E14-E16), the host-vs-guest questions the
  audit must answer, and the per-building test matrix. Same failures recurred across
  buildings, so it is treated as one pipeline plus a descriptor table, not per-building
  patches. No code changes in this pass.
