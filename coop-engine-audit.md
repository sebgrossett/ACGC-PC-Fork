# Co-op engine audit

Companion to `audits.md` (A17) and `tasks.md`. Working document for turning co-op support
into **one engine with the fewest touch points**, instead of co-op branches and near-copies
spread across retail files.

## Principle

Enabling co-op currently edits a large number of retail files. Every one of those edits is
a maintenance cost and a future regression (the sprint draw, insect-room tint and door
sessions all produced the same class of bug in different files). The target state:

- **One home per concern.** A cluster gets one module/API in the PC layer or one shared
  retail helper. Call sites ask that API; they do not re-implement it.
- **Data over branches.** Per-building, per-actor and per-scene differences live in a
  descriptor table. A new building should be one descriptor row, not edits in five files.
- **Retail files free of co-op branching.** `#ifdef TARGET_PC` in retail code is a last
  resort, not the pattern. Exceptions are listed and justified.
- **Per-world state captured by construction.** A shared static becomes per-world through
  one registry (E13), not a new capture/restore pair per module.
- **Fewest files, same features.** Success is measured by declining the number of files
  that contain co-op-specific code, with behaviour unchanged or better.

## Method

1. **Inventory.** List every file and site that carries co-op-specific code (markers:
   `#ifdef TARGET_PC`, `pc_world_`, `mPlib_extra`, `play_split`, `mPlib_players_here`,
   `s_world`, `PC_WORLD_MAX`, side slots, per-view branches). Record the marker counts.
2. **Cluster.** Group sites by the question they answer (who is the player, which world
   owns this, what is loaded where, who draws this view, ...).
3. **Converge.** For each cluster define one engine/API and, where the differences are
   data, one descriptor table. Name the exact call sites to migrate.
4. **Prove.** Each cluster gets a test matrix like the door one, or a measurement (profile,
   probe) for optimisation clusters. Port one member first, then the rest.
5. **Retire.** Delete the old paths in the same slice. No parallel helpers left behind.

Entries are seeded now (`E<n>` below). Clusters are the plan (`C<n>`); an entry belongs to
a cluster and carries the evidence.

## Consolidation clusters

| id | cluster | current scatter | one engine / home | data | target state | status |
|----|---------|-----------------|-------------------|------|--------------|--------|
| C1 | Player resolution | `m_player_lib.c`, `m_actor.c`, `m_play.c`, `m_demo.c`, every door/ambient actor (A12/A13 sweeps) | one `mPlib_player_*` resolver with a documented order (demo talker -> request owner -> nearest here -> resident -> NULL) | none | no ad-hoc `GET_PLAYER_ACTOR_*` fallbacks in actor code; call sites use the resolver | open (E11) |
| C2 | Transition ownership | `m_play.c` (`play_fading_play`/`play_fade_is_split`/`play_fade_view`), `pc_world.c` (`s_enter_slot`, hops), `m_demo.c` | `pc_transition` owner object: play, view, kind (demo/wipe/fade), phase; every consumer asks it | transition kind/phase table | fade/wipe/overlay routing decided once; K1's lead lives here | open (E12/E16) |
| C3 | Per-world state | `pc_world.c` plus ~10 module capture/restore pairs (`mFI`, `mCoBG`, `mEnv`, `mPlib`, `mDemo`, `aINS`, `aQMgr`, `aID`, lights, palettes) | one per-world state registry/struct with a single capture/restore entry point | registered fields | new shared static = one registration, no new bind code | open (E6/E13) |
| C4 | Doors / scene entry | every building actor, `ac_my_house`, shops, museum, `m_scene.c` | `pc_door_*` pipeline: trigger, owner, walk-in, open, exit | per-building descriptor (geometry, label, request, clips, scene) | a new building is one descriptor row | open (E14/E15) |
| C5 | Interaction loops | shops, museum, post office, NPC talks, boards (`mPlib_*_player` variants) | per-player loop helper + descriptor for the interaction kind | per-interaction descriptor | no host-keyed loop left; guests answered by their own player | open (A12/A13) |
| C6 | Resource banks | palettes (`mFM_pal_area`, BG common, per-scene set), BG textures, TLUT slots, light pool | one per-world resource-bank capture/restore, keyed by bank | bank ids | every shared render resource handled by the same mechanism | partial (E1/E2/E8 done/open; E7 open) |
| C7 | Views, cameras, HUD | `m_play.c`, `m_view.c`, `m_banti.c`, `pc_gx` half flags | one view context (world, camera, viewport, HUD mode) built once per view | view slot | per-view code reads the context, no per-call branching | partial |
| C8 | Session / join / leave / menus | `pc_world.c`, `pc_session_menu.c`, `m_submenu.c` side slots, `pc_pause_menu.c` | session engine: slots, pads, menu ownership, leave queue | slot states | join/leave/menu ownership in one module | partial |
| C9 | Input / pads | `m_player_call.c`, `pc_pad.c`, `pc_settings.c`, `m_player_lib` pad table | pad routing table (row -> pad -> actor) | Controls rows | one lookup, no copies of the resident's pad | mostly done |
| C10 | Demos / messages / choice | `m_demo.c`, `m_msg*.c`, `m_choice*.c`, side-window paths | interaction owner (already partly: `play_steps_message`, `mDemo_OwnerSlot`) | demo/side windows | one owner per machine, stepped once | partial |

"Current scatter" is the starting point; the inventory pass in step 1 fills in exact files
and line counts.

## Touch-point budget

Measure with the markers above, per cluster and overall:

- files containing co-op-specific code (`#ifdef TARGET_PC` or an `mPlib_*extra`/`pc_world_*` call);
- number of sites per cluster;
- retail files edited for a co-op feature in the last N slices.

Rules:

- A new co-op feature adds **one descriptor row or one helper call**, not edits in retail
  actors.
- A cluster may only grow while its engine is being built; after that, new sites are bugs.
- `#ifdef TARGET_PC` in retail code needs a one-line justification in this file.

## Entry classes and status

- `HELPER` - duplicated lookup logic that should go through one shared helper.
- `WORLD` - state that is global but should be per world (or per view/player).
- `OPT` - performance work (per-view cost, rebuilds, draw count, batching).
- `REWRITE` - candidate for a full rewrite rather than incremental fixes.
- `BUG` - a confirmed defect found during the audit.

Status: `open` | `in_progress` | `done` | `parked`. Impact: `P0` crash/wrong behaviour,
`P1` systemic risk, `P2` cosmetic/cleanup.

## Entries

| id | area | issue | class | impact | status | notes |
|----|------|-------|-------|--------|--------|-------|
| E1 | `m_field_make.c` / `m_kankyo.c` palettes | Field palettes (`mFM_pal_area`) and BG common palettes (`l_bg_pal_common_dummy`) were one shared set for every scene; the insect room forced `mTM_TERM_4` into them | WORLD | P1 | done | Fixed 2026-10-29 (`c261358`): per-world copies captured from their own bytes, settled per view. Precedent for the pattern |
| E2 | `emu64::dl_G_LOADTLUT` | Same source address with changed contents only invalidated the slot; the reload happened on the next load, which is often the other view | BUG | P1 | done | Fixed 2026-10-29 (`c261358`): both load paths fall through to the reload in the same call |
| E3 | `m_play.c` fade overlay | A plain fade emitted no view scissor, so it was replayed with the current one (full screen for the last view) | BUG | P1 | done | Fixed 2026-10-29 (`c261358`): every split overlay carries its own view scissor |
| E4 | `m_play.c` fade view resolution | `pc_world_play_slot()` is -1 for the primary play, so a town fade fell back to the selected resident (the other player) | BUG | P1 | done | Fixed 2026-10-29 (`c261358`): door fades resolve to the demo talker |
| E5 | `m_play.c` transitions | Host-enter transition washes the guest's 3D half (~3% cool tint) | BUG | P2 | parked | Known issue K1 in `tasks.md`. Lead: uniform low-alpha overlay in the non-split fade/wipe path |
| E6 | `pc_world.c` snapshot | `PcWorld` covers a hand-maintained list; known gaps: `Common_Get(player_actor_exists)`, `SubmenuArea_visit`, `banti`, `gamePT`, most module statics, `Common_Get(time)` | WORLD | P1 | open | Cluster C3. Audit the list against every `Common_Get`/module static touched per frame |
| E7 | `m_lights.c` `light_list_buf` | One global node pool; plays hold `glight->list` pointers into it, and the world snapshot copies the pool by value. Works today, fragile under reordering | WORLD | P1 | open | Cluster C6. Decide: per-world pool pointer or rebuild lists on bind |
| E8 | `m_field_make.c` BG common textures | `mFM_LoadBGCommonTex(TRUE, ...)` copies texture data into shared statics; the same class as E1 and the likely K1 carrier | WORLD | P1 | open | Cluster C6. Compare before/after the child field make, then per-world copy like the palettes |
| E9 | terrain draw | ~140 draws/view, ~16-17 per visible block (one per ground tile); ~256 draws/frame at two views | OPT | P2 | open | Cluster C7. Only real lever is a tile atlas or a merged block draw. Deliberately not taken 2026-10-29 |
| E10 | per-view tables | Terrain block table and per-view prop tables rebuild every frame for every view | OPT | P2 | open | Cluster C7. Cache on view-player block change; measure `game_logic`/`emu64_task` first |
| E11 | player lookup helpers | `mPlib_player_near`, `mPlib_players_here`, `mPlib_player_here_at`, `mPlib_now_player_or_town`, `mPlib_player_at_door`, and the demo/request owner fallbacks overlap | HELPER | P1 | open | Cluster C1. One documented resolution order used everywhere; retire the ad-hoc variants |
| E12 | transition/fade routing | `play_fading_play`, `play_fade_is_split`, `play_fade_view`, `pc_world_entering_slot`, `s_fade_player` are separate partial answers to "who owns this transition" | REWRITE | P1 | open | Cluster C2. Single transition owner (play + view + kind + phase) |
| E13 | world system | `PcWorld` + `pc_world_*` carry per-world state as ad-hoc functions; every new shared static needs another capture/restore pair | REWRITE | P1 | open | Cluster C3. Formal per-world state registry before adding more captures |
| E14 | doors / scene entry | Every building re-implements the door pipeline: trigger geometry, request owner, walk-in clip, `goto_other_scene`, interior open, exit walk-out. Host and guest take different paths through it | REWRITE | P1 | open | Cluster C4. One `pc_door_*` pipeline with a per-building descriptor |
| E15 | doors / player identity | Host vs guest entry are not symmetric: `pc_world_play_slot()` is -1 for the primary play, `s_enter_slot` is noted only at the scene change, `GET_PLAYER_ACTOR_GAME/NOW()` read the selected resident in the actor loop, interior `player_no`/`now_private` are re-stamped per world | WORLD | P1 | open | Clusters C1/C2/C4. Enumerate every difference once; the per-building patches disappear |
| E16 | transitions / loading | Field make, world capture/restore, palettes, kankyo and light pools are touched inside the fade while both worlds draw, and ownership changes mid-fade | WORLD | P1 | open | Clusters C2/C3/C6. Document transition phases and the state each owns |

## Worked example: door transitions (C4)

The door area is the template for every other cluster: the same failures kept reappearing
as each building was enabled, because each building re-implements the pipeline.

What the audit must answer, per building:

1. **Who is entering?** The demo talker, the request owner, or the selected resident.
   They differ when a guest enters (A14/E11/C1).
2. **Which world owns the transition?** The fade/wipe starts on the town, the scene opens
   on the child, the interior fades in on the child; `play_fading_play()` and
   `pc_world_play_slot()` disagree about the primary (C2/E15).
3. **What is loaded, and when?** Field make (palettes E1, BG textures E8, kankyo/light
   E7/E16) runs inside the fade while the town still draws for the other player.
4. **What survives the handoff?** `s_enter_slot`, `s_hop_*`, pads, `player_no`,
   `now_private`, door data, palette/texture copies.
5. **Exit:** the returning walker, the walk-out clip, the fade-in on the town.

Known failure classes seen so far (all fixed unless noted): wrong walker identity (police
box, museum), exit animation missing (Able Sisters), duplicate/ghost door (structures),
interior item give and talk handshake (police, shops), guest session menu indoors,
palette/texture bleed into the town half (insect room fixed, K1 parked).

### Test matrix

Rows are the buildings that currently split into a child world; columns are the owner of
the transition. Run each cell with the other player both outside and inside.

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

A cell is `held` only when: correct walker, correct half fades/wipes, interior opens with
the right resident, exit clip plays, no leftover state (palette/texture/light), and the
other player's half is untouched throughout.

## Rewrite eligibility criteria

An area is `REWRITE` when two or more of these hold:

1. It already carries more than one P0/P1 finding, or repeated regressions in the same
   machinery (transitions, world binding, palettes).
2. Its state is global by design, and per-player/per-world special cases keep being added
   around it instead of inside it.
3. Its interfaces are ad-hoc across files (each caller resolves the player/world
   differently).
4. Incremental changes have needed probes to prove behaviour more than once.

A rewrite must still be sliceable: say which slice lands first and what proves it.

| id | subsystem | why | first slice |
|----|-----------|-----|-------------|
| C2/E12 | transition/fade ownership | Repeated bugs this session (wrong half, unclipped fade, missed colour fade); state split across five helpers | `pc_transition` owner (play/view/kind/phase); port the fade overlay and view resolution to it |
| C3/E13 | per-world state | Every shared static is a future regression; the snapshot list is hand-maintained | Inventory state, then one `pc_world_state_capture/restore` table with a comparison test |
| C1/E11 | player resolution | Same question answered in four places with subtly different fallbacks | One resolver + one documented order; migrate call sites in one pass |
| C4/E14 | door / scene-entry pipeline | Every building repeats trigger, owner, walk-in, open, exit; host and guest diverge inside it | One `pc_door_*` pipeline + per-building descriptor; port one building, then run the matrix |

## Definition of done (A17)

- Every cluster has one home, and its call sites are migrated; no parallel helpers remain.
- Retail files touched for co-op are reduced to the documented exceptions list.
- Each cluster has its proof: a test matrix (doors, interactions) or a measurement
  (views/terrain).
- `audits.md` findings that this audit closes are marked done with the slice hash; K1 and
  the A16/A13 playtests keep their own status.

## Pass log

- 2026-10-29: file opened, A17 registered. Seeded with E1-E13 from the sprint draw and
  insect-room tint sessions (palettes, TLUT reload, fade routing/scissor, K1, world
  snapshot gaps, light pool, BG texture bank, per-view cost, helper overlap, rewrite
  candidates). No code changes in this pass.
- 2026-10-29: added the door/transition area (E14-E16), the host-vs-guest questions, the
  per-building test matrix, and the consolidation clusters C1-C10 with the touch-point
  budget. Door transitions are the template: one pipeline plus a descriptor table, applied
  cluster by cluster. No code changes in this pass.
