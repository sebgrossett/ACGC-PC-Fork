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

## Inventory (2026-10-29)

Measured over `src/`, `include/` and `pc/` (5,844 source files, `*.c`/`*.h`/`*.inc`/`*.cpp`).

**Headline: 1,581 co-op-specific sites in 135 files — 17 PC-layer files and 118 retail files.**

Method correction: the first pass used a narrow symbol union (438 sites / 44 files) and
missed co-op code that goes through generic helpers — the player-context system
(`m_player_call.c`), the nine-module `*_world_store/load` family, the split-HUD helpers
(`showSplitHudView`, `mFont_CulcHudHalfOrthoMatrix`), the per-player mailbox and villager
tables, the per-player menu declarations, and `game_child_ct`. The corrected pass is
history-anchored: every file touched by a co-op feature merge (146 files across 19 merges),
then filtered to the files that still contain a co-op construct. The complete 135-file list
is the appendix at the end.

Marker caveat: `#ifdef TARGET_PC` is the *port* marker, not the co-op marker — 17,892 sites
in 2,791 files, of which 427 guards sit in the 118 retail co-op files. It is only a co-op
marker where it wraps a co-op construct. The inventory counts the union of the co-op
helpers (`mPlib_*` slot/pad/guest/resolver/menu/talker/context), the world-state family
(`pc_world_*`, `*_world_store/load`, `game_child_ct`), transition and view state
(`play_fade_*`, `play_view_*`, `showSplitHudView`, `banti_VIEW_NUM`), session and menu state
(`g_pc_coop_*`, `pc_session_*`, `pc_pause_menu`, `mSM_SLOT_NUM`, `s_mbx_*`), input rows
(`pc_pad_*`, `PC_PLAYER_INPUT`), and demo ownership (`mDemo_OwnerSlot`, `mDemo_side_*`).

Per cluster (clusters overlap, so the column does not sum to 1,581):

| cluster | sites | files |
|---|---|---|
| C1 player resolution | 483 | 45 |
| C2 transition | 80 | 8 |
| C3 per-world state | 289 | 32 |
| C4 doors | 54 | 19 |
| C5 interaction loops | 245 | 45 |
| C6 resource banks | 65 | 9 |
| C7 views / cameras / HUD | 144 | 20 |
| C8 session / menus | 251 | 25 |
| C9 input / pads | 121 | 20 |
| C10 demos / messages | 74 | 9 |

What the counts already say:

- **Three files carry 43% of the surface.** `m_play.c` (266), `m_player_lib.c` (216) and
  `pc_world.c` (203) are 685 of the 1,581 sites. The remaining 132 files average 6.8 sites
  each: the co-op layer has a centre, and the rest is small guards.
- **C1 is the largest real scatter.** 483 sites in 45 files: 34 of 36 request wrappers still
  read `GET_PLAYER_ACTOR_GAME`, 11 loops read the raw extra table, and 41
  `mPlib_extra_player_at` sites in 23 files are the ad-hoc "who else is here" answer. The
  shop family (`ac_shop.c`, `ac_conveni.c`, `ac_super.c`, `ac_depart.c`, `ac_br_shop.c`)
  still does `#ifdef TARGET_PC` + `mPlib_extra_player_at(0)`.
- **C2 is the smallest surface.** 80 sites in 8 files (state plus consumers), and its proof
  (the door matrix plus K1) is already written. Highest regression rate per site.
- **C3 is a nine-module family, not a few capture pairs.** `mFI`, `mCoBG`, `mEnv` light,
  `mPlib`, `mDemo`, `aINS`, `aQMgr`, `aID` and the matrix stack each have their own
  `*_world_store/load`, plus `game_child_ct` and the per-world room bank owner. `PcWorld`
  captures 11 of the 117 `Common_` fields; E6's gaps are outside that list by definition.
- **C8 is bigger than the menus.** 251 sites in 25 files: the 10 per-player stores, the
  per-player mailbox opener (`s_mbx_*`), the villager scheduler's `extra_pos/extra_scope`,
  and the pause/session/menu overlays.
- **C6 is concentrated.** 65 sites in 9 files; the palette bank is the proven pattern, the
  BG texture data and the light pool are the open halves.
- **Slice cost so far:** the player-actor audit merge touched 28 files, villager scheduling
  3, insect-room palettes 7. The 28-file slice is the number C1/C4 must beat.

Cross-cutting measurements (same pass):

| measure | value | where |
|---|---|---|
| co-op surface | 1,581 sites in 135 files | 17 PC layer, 118 retail |
| `#ifdef TARGET_PC` guards in the 118 retail co-op files | 427 | `m_play.c` 76, `m_player_lib.c` 27, `m_actor.c` 20, `m_demo.c` 20, `m_submenu.c` 11 |
| engine centre | 3 files, 685 sites (43%) | `m_play.c` 266, `m_player_lib.c` 216, `pc_world.c` 203 |
| raw player loops `1 + mPlib_extra_player_count()` | 11 sites in 8 files | `m_submenu.c` 3, `m_play.c` 2, 6 others 1 each |
| `mPlib_extra_player_count()` reads | 30 sites in 13 files | vs 18 presence-checked `mPlib_players_here`/`mPlib_player_here_at` sites in 6 files |
| `mPlib_request_main_*` wrappers | 36 defined | 2 use `mPlib_request_player` (the door pair); 34 still read `GET_PLAYER_ACTOR_GAME` (75 reads in `m_player_lib.c`) |
| per-player state tables | 10 arrays/globals in 3 modules | `g_pc_coop_joined`, `g_pc_coop_guest_slot`, `g_pc_coop_session`, `player_input[4]`, `s_player_by_slot`, `s_pad_by_slot`, `s_slot_world`, `s_slot_pad`, `s_menu`, `s_leave_request` |
| world store/load modules | 9 | `mFI`, `mCoBG`, `mEnv` light, `mPlib`, `mDemo`, `aINS`, `aQMgr`, `aID`, matrix |
| `Common_Get/Set` fields | 117 distinct fields, 2,132 sites | `PcWorld` captures 11 of them (+ `Save_Get(scene_no)` and `structure_exit_door_data`) |
| transition state symbols | 9 in 2 files, 6 consumer files | see C2 |
| view/actor mappings | 2 independent ones | `play_actor_view_index` (extras order) vs `pc_world_slot_view` (pad-ordered slots) |

## Cluster map (summary)

The one-line version of the findings below; exact counts are in the Inventory.

| id | cluster | current scatter | one engine / home | data | target state | status |
|----|---------|-----------------|-------------------|------|--------------|--------|
| C1 | Player resolution | `m_player_lib.c`, `m_player_call.c` (context), `m_actor.c`, `m_play.c`, `m_demo.c`, every door/ambient actor (A12/A13 sweeps) | one `mPlib_player_*` resolver with a documented order (demo talker -> request owner -> nearest here -> resident -> NULL) | none | no ad-hoc `GET_PLAYER_ACTOR_*` fallbacks in actor code; call sites use the resolver | open (E11/E17/E20) |
| C2 | Transition ownership | `m_play.c` (`play_fading_play`/`play_fade_is_split`/`play_fade_view`), `pc_world.c` (`s_enter_slot`, hops), `m_demo.c` | `pc_transition` owner object: play, view, kind (demo/wipe/fade), phase; every consumer asks it | transition kind/phase table | fade/wipe/overlay routing decided once; K1's lead lives here | done except K1 (E5 parked by the operator) |
| C3 | Per-world state | `pc_world.c` plus nine `*_world_store/load` modules (`mFI`, `mCoBG`, `mEnv` light, `mPlib`, `mDemo`, `aINS`, `aQMgr`, `aID`, matrix), `game_child_ct`, the room bank owner | one per-world state registry/struct with a single capture/restore entry point | registered fields | new shared static = one registration, no new bind code | open (E6/E13/E18) |
| C4 | Doors / scene entry | every building actor, `ac_my_house`, shops, museum, `m_scene.c` | `pc_door_*` pipeline: trigger, owner, walk-in, open, exit | per-building descriptor (geometry, label, request, clips, scene) | a new building is one descriptor row | open (E14/E15/E21) |
| C5 | Interaction loops | shops, museum, post office, NPC talks, boards (`mPlib_*_player` variants), `m_player.h` per-player checks | per-player loop helper + descriptor for the interaction kind | per-interaction descriptor | no host-keyed loop left; guests answered by their own player | open (A12/A13/E17/E20) |
| C6 | Resource banks | palettes (`mFM_pal_area`, BG common, per-scene set), BG textures, TLUT slots, light pool | one per-world resource-bank capture/restore, keyed by bank | bank ids | every shared render resource handled by the same mechanism | partial (E1/E2 done, E8 texture half, E7 open) |
| C7 | Views, cameras, HUD | `m_play.c`, `m_view.c` (`showSplitHudView`), `m_font.c` (HUD half ortho), `m_banti.c`, `m_camera2.c`, `pc_gx` half flags | one view context (world, camera, viewport, HUD mode) built once per view | view slot | per-view code reads the context, no per-call branching | partial (E19) |
| C8 | Session / join / leave / menus | `pc_world.c`, `pc_session_menu.c`, `m_submenu.c` side slots, `pc_pause_menu.c`, `ac_mailbox.c` opener, `aSNMgr` extra players | session engine: slots, pads, menu ownership, leave queue | slot states | join/leave/menu ownership in one module | partial (E18) |
| C9 | Input / pads | `m_player_call.c`, `pc_pad.c`, `pc_settings.c`, `m_player_lib` pad table, `chkTrigger_pad` | pad routing table (row -> pad -> actor) | Controls rows | one lookup, no copies of the resident's pad | mostly done; fold into C8 |
| C10 | Demos / messages / choice | `m_demo.c`, `m_msg*.c`, `m_choice*.c`, side-window paths | interaction owner (already partly: `play_steps_message`, `mDemo_OwnerSlot`) | demo/side windows | one owner per machine, stepped once | partial |

## Cluster findings (step 2)

Each cluster is assessed on the same four questions: what the code does now, what is
duplicated or wrong, what the single home would be, and what proves the migration. Slice
sizes come from the inventory where it gives a count, otherwise they are marked `~`.

### C1 player resolution

**Now:** nine mechanisms answer "who is acting", with different orders and fallbacks:

1. `get_player_actor_withoutCheck` (`m_player_lib.c:1842`) — bound actor -> selected slot ->
   the single registered actor if it is in this play -> list head; rebinds the slot when the
   only actor is not a guest.
2. `mPlib_now_player_or_town` (`:1939`) — selected if in play -> first player here.
3. `mPlib_player_here_at` (`:1957`) — resident -> extras, presence-checked.
4. `mPlib_players_here` (`:1917`) — count.
5. `mPlib_player_near` (`:1991`) — nearest of those.
6. `mPlib_player_at_door` (`:4708`) — talker -> resident -> here.
7. `mPlib_request_player` (`:2946`) — request owner -> selected -> first here.
8. `mPlib_extra_player_at` (`:1564`) — raw table, **no presence check**.
9. `m_player_call.c` — the context system (`player_context_begin/end` plus the menu, demo
   and slot wrappers) rebinds `player_no`, mcon and `now_private` around a call, which is a
   fourth way to say who is acting.

**Duplicated or wrong:** the resolver exists but most call sites bypass it — 483 sites in 45
files. 34 of 36 `mPlib_request_main_*` wrappers still read `GET_PLAYER_ACTOR_GAME` (only the
door pair uses the resolver), so any guest interaction that goes through a request (talk,
give, sit, lie, hold, sting, shop item) still acts on the selected resident. 11 loops build
`1 + mPlib_extra_player_count()` by hand and read the raw table. `mPlib_extra_player_at` is
still the fallback in 41 sites across 23 files. The context system is a fourth path that
does not go through the resolver at all. The order is only documented in code comments, one
function at a time.

**Home:** one `mPlib_player_resolve(play, intent)` with an explicit intent enum (actor loop,
door, request, nearest, view) over one documented order. `mPlib_extra_player_at` becomes
internal.

**Slice:** the 34 wrappers + 11 loops + 5 shop files; ~10 files. **Risk:** medium — every
interaction path. **Proof:** A16's tool save plus the shop/museum matrix.

### C2 transition ownership

**Now:** nine symbols answer "who owns this transition": `play_fading_play`
(`m_play.c:1900`), `play_fade_is_split` (`:1919`), `play_fade_view` (`:1934`),
`play_fade_every_view` (`:1930`), `play_split_live` (`:1884`), `s_fade_player` (`:223`),
`s_allow_fade_in` (`:226`), `pc_world_entering_slot`/`s_enter_slot`
(`pc_world.c:93,1032`), `pc_world_door_return` (`:81`). Consumers: `makeBumpTexture`
(2092-2166), `Game_play_draw` (2432), `pc_world_transition`, `m_camera2.c:2118`,
`ac_museum.c`, `ac_cottage.c`, `m_scene.c`.

**Duplicated or wrong:** four of the nine are partial answers to the same question, and
`play_fade_view` is a six-step fallback chain (fade player -> demo talker -> fading play
slot -> entering slot -> host). K1's lead is recorded twice and differently: `tasks.md` and
E5 say the non-split fade overlay; the insect-room pass log says draw-time fog/light
binding. One owner would make that a single open question.

**Home:** `pc_transition` in `pc_world.c`: `{play, view, kind, phase}` resolved once per
frame; every consumer reads fields.

**Slice:** 8 files, 80 sites. **Risk:** medium-high — transitions are where the regressions
cluster. **Proof:** the door matrix plus the K1 repro.

### C3 per-world state

**Now:** `PcWorld` has 35 fields; `pc_world_capture`/`pc_world_restore` are hand-written
mirror functions (`pc_world.c:128,168`) covering 11 `Common_` fields. Nine modules carry
their own `*_world_store/load` pair — `mFI`, `mCoBG`, `mEnv` light, `mPlib`, `mDemo`, `aINS`,
`aQMgr`, `aID` and the matrix stack — plus `game_child_ct` for the child play and the
per-world room bank owner in `ac_my_room.c`. Palettes are a separate store/settle pair. The
per-player tables (10, above) live outside the struct.

**Duplicated or wrong:** 117 distinct `Common_` fields exist; 11 are captured, and the gaps
(E6: `player_actor_exists`, `SubmenuArea_visit`, `banti`, `time`) are exactly the ones
nobody remembered. `world->lights` copies the 32-node pool by value (E7). Every new shared
static needs another pair, which is how the list got hand-maintained: 289 sites across 32
files, and the nine module pairs are all the same shape.

**Home:** one `PcWorldState` registry table `{offset, size, kind}` plus one
capture/restore loop; move the per-player tables into one `PcSession`.

**Slice:** `pc_world.c` plus the nine module shims and `game_child_ct` (mechanical).
**Risk:** high blind, medium with a field-diff assert. **Proof:** an identity check (capture
then restore over every registered field) plus the door matrix. This is the only genuinely
rewrite-shaped item.

### C4 doors / scene entry

**Now:** the shared helpers exist (`mPlib_player_at_door`, `mPlib_players_here`,
`mPlib_request_player`), but only museum, cottage and tent call `mPlib_player_at_door`
directly. The shop family (`ac_shop.c`, `ac_conveni.c`, `ac_super.c`, `ac_depart.c`,
`ac_br_shop.c`) each repeats `#ifdef TARGET_PC` + `mPlib_extra_player_at(0)` + a
null-guarded position check. `pc_scene_splits` hardcodes 17 scenes; `pc_world_open_scene`
is 125 lines of hop/pad/palette/`player_no` re-stamping.

**Duplicated or wrong:** the pipeline is real, but its data is scattered: geometry offsets
per actor, the scene list in `pc_world.c`, walk-in clips per actor, owner resolution across
three helpers. A new building is still edits in several files, which is the original
complaint.

**Home:** `pc_door` descriptor `{scene, geometry, label, request, clips, exit}` plus one
pipeline.

**Slice:** 19 files, 54 sites; the 5 shop files + cottage/museum/tent migration first.
**Risk:** medium. **Proof:** the per-building matrix.

### C5 interaction loops

**Now:** seven `_player` variants in `m_player_lib.c` (54 sites in 9 files) plus 11 inline
loops. `mSM_submenu_ctrl` is 200+ lines with 8 `TARGET_PC` branches and three separate
"loop over players" copies. `mSM_SLOT_NUM` is 4, but there is one opener table set.

**Duplicated or wrong:** the loop shape is copied, not shared; two of the three copies use
the raw extra table, one uses the presence-checked helper. This is where the "either pad
dismisses the other's dialogue" class came from.

**Home:** one `for_each_player_here(play)` iterator plus per-slot state; an interaction
descriptor for the loop kind.

**Slice:** 45 files, 245 sites; `m_submenu.c` + the three loops first. **Risk:** medium.
**Proof:** A7/A13 playtests.

### C6 resource banks

**Now:** field palettes and BG common palettes are per-world (E1 done); BG common *textures*
are still one shared `l_bg_tex_common_dummy` written by `mFM_LoadBGCommonTex`
(`m_field_make.c:1132`); the light pool is copied by value (`pc_world.c:160`); render-state
resets are inline (`play_draw_split_side`, `m_play.c:1635`).

**Duplicated or wrong:** E8's palette half landed with E1; its texture half did not. 48 of
53 bank sites are in `m_field_make.c` and `m_lights.c`, so the API is cheap. The inline
prim reset is the same class of leak the palette work fixed.

**Home:** one `pc_bank` capture/restore keyed by bank id, registered with C3; the prim reset
moves into the bank reset.

**Slice:** 9 files, 65 sites; `m_field_make.c`, `m_lights.c`, 3 lines of `m_play.c`.
**Risk:** low-medium. **Proof:** insect room + K1 repro.

### C7 views / cameras / HUD

**Now:** `play_player_count` is `pc_world_session_count`; `pc_world_fill_views` builds the
view->world map; `play_view_world` reads it; `play_view_player` and `play_actor_view_index`
map index->actor from the **extras table**; `pc_world_slot_view` maps slot->view from the
**pad-ordered slot list**; `play_view_camera`, `s_draw_view_world`, `s_eye_ofs`,
`s_cam_hold`, per-view palette settle. The HUD side has its own set: `showSplitHudView`
(`m_view.c`), `mFont_CulcHudHalfOrthoMatrix` (`m_font.c`), `banti_VIEW_NUM` (4),
`Camera2_GetNormal*` taking a player, `Game_play_Projection_Trans_m` and
`pc_gx_menu_no_depth`.

**Duplicated or wrong:** two independent mappings, and `play_view_player` reads the bound
world's table, so a view's actor depends on which world is bound at that moment. At two
players the two orders coincide; at 3-4 (an indoor guest plus two town players) they do
not, and `play_view_player(index)` falls back to the resident. The HUD/overlay code
re-derives the view from the slot each time (`play_draw_coop_overlays`), and the HUD half
has a second set of helpers that repeat the viewport/scissor math. 144 sites in 20 files.

**Home:** one `PcView` array `{world, slot, actor, camera, rect, hud}` built once per frame
by `pc_world`.

**Slice:** `m_play.c` draw + `pc_world.c` + `m_camera2.c` + the `m_view`/`m_font`/`m_banti`
HUD helpers. **Risk:** medium. **Proof:** the split draw profile plus a 3-4 player pass.

### C8 session / join / leave / menus

**Now:** one session is spread over 10 stores (table above): coop globals in
`pc_settings.c`, slot/pad tables in `m_player_lib.c`, world tables in `pc_world.c`, menu and
leave queues in `pc_session_menu.c`, view state in `m_play.c`. Join slot selection lives in
`m_actor.c:604`, leave in `pc_world_leave_slot` + `play_coop_join`, menu ownership in
`pc_session_menu.c` + `m_submenu.c` + `pc_pause_menu.c`. Per-player state also lives in
`ac_mailbox.c` (`s_mbx_opener`, `s_mbx_menu_open`, `mbx_se_accum`), `aSNMgr`
(`extra_pos`, `extra_scope`, `extra_last_block`) and the Controls UI
(`pc_settings_menu.c`).

**Duplicated or wrong:** each store has its own validity rule, which is the A15 class:
a slot-table pointer is not proof of presence, and a joined flag is not proof of a body.
The 2026-10-28 fixes added `mPlib_player_in_play` and per-store fallbacks; the next store
will need the same again. 251 sites in 25 files.

**Home:** one `PcSession` `{slots[4], pads[4], worlds[4], menus[4], leave[4]}` with one
validity predicate, `pc_session_player(pad)`.

**Slice:** 8 files, ~250 sites. **Risk:** high — join/leave is where the crashes were.
**Order:** after C2/C3.

### C9 input / pads

**Now:** pad routing is already data (`player_input[4]` rows, `pc_pad.c` mirrors PADRead's
ownership). Residual copies: `mPlib_pad_for_player`, `mPlib_pad_in_use`,
`pc_pad_row_for_instance`, `guest_slot_for_actor` (`m_player_call.c:218`).

**Duplicated or wrong:** four implementations of "pad -> row -> slot"; only `pc_pad.c`'s
knows about device ownership.

**Home:** one lookup in the session (C8). **Slice:** small. **Risk:** low. **Status:** mostly
done; fold into C8.

### C10 demos / messages / choice

**Now:** the request carries `pc_owner_slot`/`pc_owner_pad` — the data-over-branches pattern
already works. `mDemo_OwnerSlot` gates the message/choice machines. Remaining: three
globals answer "who is talking" (`mPlib_demo_talker`, `mPlib_request_owner`, `s_side`), and
the side window is a single `mDemo_Side_c` (one side talk at a time).

**Duplicated or wrong:** the owner is data, but the talker/request-owner/side-player are
three separate lifetimes; the side window cannot represent two guest talks, which is a
3-4 player limit, not a bug today.

**Home:** one `mDemo_Owner` `{slot, pad, actor, kind}` used by request, talker and side
window.

**Slice:** 9 files, 74 sites; `m_demo.c` + `m_player_lib.c` accessors. **Risk:** medium.
**Proof:** the item give and the side-talk playtest.

## Rewrite verdict

**No full rewrite of the retail engine. Yes to consolidating the co-op layer.** The two
are different questions, and the complete inventory separates them:

- The retail game loop, actor system, scene loader and renderer are the engine. Rewriting
  them would mean re-implementing the game, not the co-op support, and there is no test
  harness to prove it.
- The co-op layer itself has a centre: 685 of 1,581 sites (43%) live in `m_play.c`,
  `m_player_lib.c` and `pc_world.c`, and the per-request owner is already data. The other
  132 files average 6.8 sites — mostly one-line guards, not competing implementations.
- The hard-won behaviour is ~40 small guards; the A15 rounds alone took four passes. A
  rewrite would have to rediscover them from playtests.

So the work is a **consolidation rewrite of the co-op layer**, not of the engine: extract
the state registry (C3), the transition owner (C2), the session (C8) and the resolver (C1)
into their own homes, and reduce the 118 retail files to a documented exception list.

**Two items are rewrite-shaped and should be done as mechanical extractions, not
redesigns:** C3 (state registry) and C2 (transition owner). Both keep behaviour identical
and are proved by a comparison test plus the existing door matrix.

Recommended order, cheapest-first and dependency-driven:

1. **C2/E12 transition owner** — smallest surface (80 sites, 8 files), highest regression
   rate, proof exists.
2. **C3/E13 state registry** — stops the nine-module capture growth before C4/C6 add fields.
3. **C1/E11 resolver** — the 34 request wrappers, 11 raw loops, 5 shop files; biggest
   retail win.
4. **C4/E14 door pipeline** — descriptor table; the shop family is the first five rows.
5. **C6/E8/E7 resource banks** — texture half plus the light pool, registered with C3.
6. **C7/E19 view context** — one view table plus the split-HUD helpers; unblocks 3-4 players.
7. **C5/C8 interaction + session** — one session struct; join/leave touched last.
8. **C10/E20 demo owner**, with **C9** folded into C8.

Target after the sequence: co-op files 135 -> ~60, retail co-op files 118 -> ~50, sites
1,581 -> ~700, `TARGET_PC` guards in retail co-op files 427 -> <250. If a cluster cannot hit
its slice size, that is the signal to stop and re-plan, not to keep adding guards.

## Slice plan: C2 transition owner (E12/E16)

Self-contained plan for the first slice. Read this section alone before starting; it does
not depend on the rest of the audit except for the proof matrix below.

### Goal

One owner for "who owns this transition": which play, which view, what kind, what phase.
Every consumer asks it. The five partial helpers and the two fade statics are deleted.
Behaviour is unchanged; K1 is decided in a separate follow-up commit.

**Status 2026-10-29:** steps 1-5 landed on `feature/pc-transition` (owner API, both draw
consumers, the two statics moved, the five helpers deleted). Build clean. The door matrix
held (operator, 2026-10-29), including the solo-co-op cells that exposed A18. K1 was
deferred by the operator after exhaustive probing and stays parked as E5. Marker set after
the slice: 49 sites in 3 files (was 80 in 8).

### Current state (all in two files)

`src/game/m_play.c`:

- `play_split_live` (1884) — is the frame split? `pc_world_live() >= 2` early, then extras.
- `play_fading_play` (1900) — first world with `fb_wipe_mode`, `fb_fade_type` or a
  non-zero `color_fade.color.a`; falls back to the town.
- `play_fade_is_split` (1919) — split **and** that world is actually fading.
- `play_fade_every_view` (1930) — only `FADE_TYPE_OUT_RETURN_TITLE`.
- `play_fade_view` (1934) — six-step view fallback: `s_fade_player` -> demo talker ->
  `pc_world_play_slot(fading)` -> `pc_world_entering_slot()` -> host.
- `s_fade_player` (223), `s_allow_fade_in` (226) — set around `Game_play_start_fade_in`
  (469) and `Game_play_start_fade_in_player` (483); `s_allow_fade_in` is read by
  `Game_play_fbdemo_wipe_proc` (436) to suppress a re-started IN wipe.
- Consumers: `makeBumpTexture` (2092-2166), `Game_play_draw` (2432), `play_draw_fade_overlay`
  (2005), `play_menu_blocks_fade` (1988).

`pc/src/pc_world.c`:

- `pc_world_entering_slot`/`s_enter_slot` (1032/93), `pc_world_note_entering` (510).
- `pc_world_door_return` (283-301) — per-slot return flag; also read by `m_camera2.c:2118`,
  `m_play.c:1830`, `ac_museum.c:287/297`.
- Entry points that start fades: `pc_world_open_scene` (458), `pc_world_fade_in_town` (657).

### Target API (`pc_world.h`)

```c
typedef enum {
    PC_TRANSITION_KIND_NONE = 0,
    PC_TRANSITION_KIND_WIPE,   /* fb_wipe_mode != WIPE_MODE_NONE */
    PC_TRANSITION_KIND_FADE,   /* fb_fade_type != FADE_TYPE_NONE or colour alpha != 0 */
} PcTransitionKind;

typedef enum {
    PC_TRANSITION_PHASE_IDLE = 0,
    PC_TRANSITION_PHASE_OUT,   /* leaving the current scene */
    PC_TRANSITION_PHASE_IN,    /* entering the new scene */
} PcTransitionPhase;

typedef struct {
    struct game_play_s* play;  /* world that owns the transition */
    int slot;                  /* resident slot, -1 when unresolved */
    int view;                  /* view index, 0..count-1 */
    int kind;
    int phase;
    int split;                 /* more than one live view */
    int every_view;            /* return-title fade covers every view */
    int count;                 /* live views */
} PcTransition;

/* Resolve once per frame, after the worlds have moved and before the draw. */
const PcTransition* pc_transition_resolve(struct game_play_s* town);
/* Fade-in actor (join / walk-out). NULL means the selected resident. */
void pc_transition_set_fade_actor(struct actor_s* who);
/* TRUE only while Game_play_start_fade_in runs; replaces s_allow_fade_in. */
int pc_transition_fade_in_allowed(void);
```

`pc_world.h` gains a `struct actor_s;` forward declaration. The returned object is a static
in `pc_world.c`, valid until the next `pc_transition_resolve` (once per frame).

`HOLD` stays a draw-time detail: the `S_se_endcheck_timeout > 115.0f` check remains in
`play_draw_fade_overlay`. Do not move it into the phase enum; it is a presentation hold, not
a transition state.

Resolution rules (copy the current order exactly):

1. `kind`: wipe if `fb_wipe_mode != NONE`; else fade if `fb_fade_type != NONE` or
   `color_fade.color.a != 0`; else none.
2. `play`: first world (index order) with a non-idle kind; else `town`.
3. `phase`: `FADE_TYPE_IN` -> IN; other non-idle -> OUT; else IDLE.
4. `split`: `pc_world_live() >= 2` **or** (`mPlib_extra_player_count() > 0` and a host
   actor exists). This is `play_split_live` verbatim.
5. `count`: `pc_world_session_count()`.
6. `view`, in order:
   - `fade_actor` if `mPlib_player_slot_of(actor) >= 0` and `pc_world_slot_view(slot) >= 0`;
   - demo talker in this play (`mPlib_player_in_play(town, talker)`), same slot/view test;
   - `pc_world_play_slot(play)` -> `pc_world_slot_view`;
   - `pc_world_entering_slot()` -> `pc_world_slot_view`;
   - host view 0.
   If none resolves, 0.
7. `every_view`: `play->fb_fade_type == FADE_TYPE_OUT_RETURN_TITLE`.

### Migration steps (one commit each)

1. **Add the API, no consumers.** `pc_transition_resolve`, the two setters, the struct in
   `pc_world.c/h`. Resolve but do not use it. Build.
2. **Port `makeBumpTexture`.** Replace `fade_play`, `clip_fade`, `fade_view`,
   `play_fade_every_view(fade_play)` with the resolved object. Keep the old helpers as thin
   wrappers for this commit so a regression is attributable. Build; door smoke.
3. **Port `Game_play_draw` and `play_draw_fade_overlay`.** `play_split_live(play)` becomes
   `transition->split`; `play_fade_every_view` becomes `transition->every_view`. Build.
4. **Move the two statics.** `s_fade_player` -> `pc_transition_set_fade_actor`;
   `s_allow_fade_in` -> `pc_transition_fade_in_allowed`, set/cleared inside
   `Game_play_start_fade_in`. `Game_play_fbdemo_wipe_proc` reads the accessor. Build.
5. **Delete the old helpers** (`play_split_live`, `play_fading_play`, `play_fade_is_split`,
   `play_fade_view`, `play_fade_every_view`) and their forward declarations. Build.
6. **Verify** with the matrix below. No behaviour change is expected.
7. **K1 (separate commit, after 6).** Decide the lead: instrument the non-split path to log
   whether the overlay draws during a host-enter transition, and log per-view fog/ambient.
   Fix the confirmed carrier, or re-park with evidence. Repro: co-op, P1 enters their house
   while P2 watches outside; 30 fps capture, measure the guest half.

### Files

- `pc/include/pc_world.h`, `pc/src/pc_world.c` — API and resolution.
- `src/game/m_play.c` — consumers, statics, delete helpers.
- No other file is required. `m_camera2.c`, `ac_museum.c`, `m_play.c:1830` keep using
  `pc_world_door_return_pending`; it is scene-entry state, not transition ownership.

### Verification

Build: `./build_pc.sh` (MINGW32), clean.

Door matrix (from the worked example), each cell with the other player both outside and
inside:

- own house, Nook's Cranny, police box, post office, museum entrance + one room.
- P1 enters / P2 outside; P2 enters / P1 outside; both in town; one indoors and the other
  transitions.
- Join fade (P2 joins while P1 is in town) and Return to Title fade (every-view path).
- Split fade scissor: enter a building while both players are in town; the other half must
  not be washed.
- Single player: house enter/exit, shop, museum, Return to Title.
- K1: the 30 fps capture above.

A cell is `held` only when: correct walker, correct half fades/wipes, interior opens with
the right resident, exit clip plays, no leftover state, and the other half is untouched.

### Risks and invariants

- `play_fading_play` picks the **first** world in index order; keep it. Two simultaneous
  fades are not a supported case, but the order must not change.
- `play_split_live`'s early `pc_world_live() >= 2` return is what keeps a view alive when
  the last outdoor player leaves; do not fold it into the extras test.
- `s_fade_player` returning 0 when the actor has no view is deliberate (join / walk-out);
  keep the fallback order.
- `s_allow_fade_in` only guards the `game_frame != 0` IN-wipe suppression; moving it must
  not widen the window.
- No test harness: the matrix is the proof. If a cell fails, revert the step, not the whole
  slice.

### Definition of done

- The five helpers and two statics are gone; one resolved object drives the draw.
- Build clean, matrix held, single player unchanged.
- C2 sites drop from 80 to ~40 and files from 8 to 3 (API + consumers).
- K1 has a decided lead and a repro result (fixed or re-parked with evidence).

### Out of scope

- C3 (state registry), C1 (resolver), C4 (doors). Do not refactor them in this slice.
- The door-return flag and entering-slot storage stay in `pc_world.c`.
- No new descriptor tables; C2's only data is the kind/phase enum.

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
| E5 | `m_play.c` transitions | Host-enter transition washes the guest's 3D half (~3% cool tint) | BUG | P2 | parked | Known issue K1 in `tasks.md`. Operator deferred it 2026-10-29 after exhaustive probing: the C2 owner landed and the matrix held with the wash unchanged, so it is not transition routing. Keep parked; revisit only with a new measurement (per-view fog/ambient during a host enter) |
| E6 | `pc_world.c` snapshot | `PcWorld` covers a hand-maintained list; known gaps: `Common_Get(player_actor_exists)`, `SubmenuArea_visit`, `banti`, `gamePT`, most module statics, `Common_Get(time)` | WORLD | P1 | open | Cluster C3. Audit the list against every `Common_Get`/module static touched per frame |
| E7 | `m_lights.c` `light_list_buf` | One global node pool; plays hold `glight->list` pointers into it, and the world snapshot copies the pool by value. Works today, fragile under reordering | WORLD | P1 | open | Cluster C6. Decide: per-world pool pointer or rebuild lists on bind |
| E8 | `m_field_make.c` BG common textures | `mFM_LoadBGCommonTex(TRUE, ...)` copies texture data into shared statics; the same class as E1 and the likely K1 carrier | WORLD | P1 | open | Cluster C6. **Palette half is done** (the five `l_bg_pal_common_dummy` buffers ride in `mFM_field_palette_store/load`). Remaining: `l_bg_tex_common_dummy` texture data, written by `mFM_LoadBGCommonTex` (`m_field_make.c:1132`) |
| E9 | terrain draw | ~140 draws/view, ~16-17 per visible block (one per ground tile); ~256 draws/frame at two views | OPT | P2 | open | Cluster C7. Only real lever is a tile atlas or a merged block draw. Deliberately not taken 2026-10-29 |
| E10 | per-view tables | Terrain block table and per-view prop tables rebuild every frame for every view | OPT | P2 | open | Cluster C7. Cache on view-player block change; measure `game_logic`/`emu64_task` first |
| E11 | player lookup helpers | `mPlib_player_near`, `mPlib_players_here`, `mPlib_player_here_at`, `mPlib_now_player_or_town`, `mPlib_player_at_door`, and the demo/request owner fallbacks overlap | HELPER | P1 | open | Cluster C1. One documented resolution order used everywhere; retire the ad-hoc variants |
| E12 | transition/fade routing | `play_fading_play`, `play_fade_is_split`, `play_fade_view`, `pc_world_entering_slot`, `s_fade_player` are separate partial answers to "who owns this transition" | REWRITE | P1 | done | Cluster C2. Landed 2026-10-29: `pc_transition_resolve` + `PcTransition {play, slot, view, kind, phase, split, every_view, count}` in `pc_world.c/h`; `makeBumpTexture`/`Game_play_draw` read it; the five helpers and `s_fade_player`/`s_allow_fade_in` are gone. Marker set: 49 sites in 3 files (was 80 in 8). Door matrix held; K1 remains separately parked (E5) |
| E13 | world system | `PcWorld` + `pc_world_*` carry per-world state as ad-hoc functions; every new shared static needs another capture/restore pair | REWRITE | P1 | open | Cluster C3. Formal per-world state registry before adding more captures |
| E14 | doors / scene entry | Every building re-implements the door pipeline: trigger geometry, request owner, walk-in clip, `goto_other_scene`, interior open, exit walk-out. Host and guest take different paths through it | REWRITE | P1 | open | Cluster C4. One `pc_door_*` pipeline with a per-building descriptor |
| E15 | doors / player identity | Host vs guest entry are not symmetric: `pc_world_play_slot()` is -1 for the primary play, `s_enter_slot` is noted only at the scene change, `GET_PLAYER_ACTOR_GAME/NOW()` read the selected resident in the actor loop, interior `player_no`/`now_private` are re-stamped per world | WORLD | P1 | open | Clusters C1/C2/C4. Enumerate every difference once; the per-building patches disappear |
| E16 | transitions / loading | Field make, world capture/restore, palettes, kankyo and light pools are touched inside the fade while both worlds draw, and ownership changes mid-fade | WORLD | P1 | open | Clusters C2/C3/C6. Document transition phases and the state each owns |
| E17 | `m_player_lib.c` request wrappers | 36 `mPlib_request_main_*` wrappers; only the door pair routes through `mPlib_request_player`, the other 34 read `GET_PLAYER_ACTOR_GAME` (75 reads in the file), so a guest's talk/give/sit/hold/sting request still runs on the selected resident | HELPER | P1 | open | Cluster C1/C5. Route every wrapper through the resolver; the door pair is the proven shape |
| E18 | session state | One session is spread over 10 stores in 3 modules (`g_pc_coop_session/joined/guest_slot`, `player_input`, `s_player_by_slot`, `s_pad_by_slot`, `s_slot_world`, `s_slot_pad`, `s_menu`, `s_leave_request`), each with its own validity rule — the A15 class | WORLD | P1 | open | Cluster C8. One `PcSession` struct + one presence predicate |
| E19 | view/actor mapping | `play_actor_view_index` (extras order) and `pc_world_slot_view` (pad-ordered slots) are two independent mappings; `play_view_player` reads the bound world's table, so a view's actor depends on which world is bound. Coincides at 2 players, diverges at 3-4 | WORLD | P1 | open | Cluster C7. One `PcView` array built once per frame |
| E20 | raw player loops | 11 `1 + mPlib_extra_player_count()` loops in 8 files read `mPlib_extra_player_at` without a presence check, while 18 sites in 6 files use the checked helpers | HELPER | P1 | open | Cluster C1/C5. One `for_each_player_here` iterator; retire the raw loops |
| E21 | shop-family doors | `ac_shop.c`, `ac_conveni.c`, `ac_super.c`, `ac_depart.c`, `ac_br_shop.c` each repeat `#ifdef TARGET_PC` + `mPlib_extra_player_at(0)` + a null-guarded geometry check; the only per-building difference is the offset/constants | HELPER | P2 | open | Cluster C4. First five descriptor rows |
| E22 | duplicate gyroid after door transitions | A second haniwa appears after several building in/outs, reproduced with one resident in a co-op session (P1 only). The doubles dedupe was added for this symptom but searched the ITEM part only; `Haniwa_Profile` is `ACTOR_PART_BG` | BUG | P1 | done | Cluster C3/C6 (prop respawn, not transition state). Fixed 2026-10-29: `aBC_actor_already_at` searches ITEM + BG. Operator retested the same door sequence: confirmed gone |

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

Assessed 2026-10-29: all four meet at least two criteria (C1 and C2 meet three or more).
They should be done as incremental extractions, not a rewrite — see "Rewrite verdict" above
for the order and the reasoning.

## Definition of done (A17)

- Every cluster has one home, and its call sites are migrated; no parallel helpers remain.
- Retail files touched for co-op are reduced to the documented exceptions list.
- Each cluster has its proof: a test matrix (doors, interactions) or a measurement
  (views/terrain).
- `audits.md` findings that this audit closes are marked done with the slice hash; K1 and
  the A16/A13 playtests keep their own status.

## Appendix: complete co-op file inventory (135 files)

Counts are co-op-symbol hits per file; the set is the corrected, history-anchored sweep.

**PC layer (17):** `pc_world.c` (203), `pc_session_menu.c` (41), `pc_pad.c` (36),
`pc_settings_menu.c` (26), `pc_pause_menu.c` (23), `pc_main.c` (16), `pc_settings.c` (15),
`pc_menu_util.c` (6), `pc_gx.c` (3), `pc_keybindings.c` (1), `pc_world.h` (27),
`pc_settings.h` (10), `pc_session_menu.h` (6), `pc_pause_menu.h` (5), `pc_menu_util.h` (4),
`pc_platform.h` (4), `pc_keybindings.h` (1).

**Headers (27):** `m_player_lib.h` (70), `m_demo.h` (15), `m_submenu.h` (4),
`ac_quest_manager.h` (4), `ac_cottage.h` (4), `m_play.h` (3), `m_player.h` (3),
`ac_set_npc_manager.h` (3), `ac_insect.h` (2), `ac_intro_demo.h` (2), `m_kankyo.h` (2),
`m_collision_bg.h` (2), `m_field_info.h` (2), `m_field_make.h` (2), `m_banti.h` (2),
`m_camera2.h` (2), `sys_matrix.h` (2), `ac_animal_logo.h` (1), `game.h` (1),
`m_actor.h` (1), `m_controller.h` (1), `m_font.h` (1), `m_lib.h` (1), `m_lights.h` (1),
`m_msg.h` (1), `m_view.h` (1), `m_watch_my_step.h` (1).

**`src/game/` (49):** `m_play.c` (266), `m_player_lib.c` (216), `m_demo.c` (78),
`m_submenu.c` (63), `m_player.c` (55), `m_field_make.c` (33), `m_actor.c` (27),
`m_player_call.c` (24), `m_lights.c` (11), `m_banti.c` (11), `m_field_info.c` (9),
`m_scene.c` (8), `m_camera2.c` (7), `m_collision_bg.c` (6), `m_kankyo.c` (6),
`m_watch_my_step.c` (5), `m_inventory_ovl.c` (4), `m_tag_ovl.c` (3), `m_submenu_ovl.c` (3),
`m_event.c` (3), `m_lib.c` (2), `m_cpmail_ovl.c` (2), `m_mscore_ovl.c` (2),
`m_cporiginal_ovl.c` (2), `m_catalog_ovl.c` (2), `m_controller.c` (1), `m_view.c` (1),
`m_msg.c` (1), `m_repay_ovl.c` (1), `m_birthday_ovl.c` (1), `m_board_ovl.c` (1),
`m_timeIn_ovl.c` (1), `m_warning_ovl.c` (1), `m_bank_ovl.c` (1),
`m_haniwaPortrait_ovl.c` (1), `m_address_ovl.c` (1), `m_passwordMake_ovl.c` (1),
`m_design_ovl.c` (1), `m_cpwarning_ovl.c` (1), `m_hand_ovl.c` (1), `m_diary_ovl.c` (1),
`m_editEndChk_ovl.c` (1), `m_editor_ovl.c` (1), `m_haniwa_ovl.c` (1), `m_music_ovl.c` (1),
`m_notice_ovl.c` (1), `m_passwordChk_ovl.c` (1), `m_cpedit_ovl.c` (1),
`m_mailbox_ovl.c` (1).

**`src/actor/` (36):** `ac_set_npc_manager.c` (21), `ac_museum.c` (19), `ac_sign.c` (15),
`ac_animal_logo.c` (12), `ac_cottage.c` (8), `ac_mailbox.c` (8), `ac_set_manager.c` (4),
`ac_mscore_control.c` (4), `ac_my_room.c` (4), `ac_gyo_test.c` (3),
`ac_event_manager.c` (3), `ac_quest_manager.c` (3), `ac_birth_control.c` (3),
`ac_intro_demo.c` (2), `ac_insect.c` (2), `ac_field_draw.c` (2), `ac_bridge_a.c` (2),
`ac_ev_turkey.c` (1), `ac_post_office.c` (1), `ac_npc_shop_common.c` (1), `ac_bee.c` (1),
`ac_ant.c` (1), `ac_super.c` (1), `ac_tent.c` (1), `ac_shop.c` (1),
`ac_needlework_shop.c` (1), `ac_gyo_kaseki.c` (1), `ac_koinobori.c` (1), `ac_depart.c` (1),
`ac_buggy.c` (1), `ac_conveni.c` (1), `ac_museum_picture.c` (1), `ac_my_house.c` (1),
`ac_museum_fossil.c` (1), `ac_br_shop.c` (1), `ac_museum_fish.c` (1).

**Top-level / other (6):** `src/game.c` (1), `src/graph.c` (3), `src/padmgr.c` (4),
`src/effect/ef_effect_control.c` (1), `src/static/libforest/emu64/emu64.c` (3),
`src/system/sys_matrix.c` (2).

**Touched by a co-op merge but clean now (18):** `include/ac_birth_control.h`,
`include/ac_field_draw.h`, `include/ac_npc.h`, `include/ac_structure.h`, `include/bg_item.h`,
`pc/include/pc_disc.h`, `pc/include/pc_profiler.h`, `pc/src/pc_disc.c`, `pc/src/pc_m_card.c`,
`pc/src/pc_profiler.c`, `src/actor/ac_house.c` (include only), `src/actor/ac_house_goki.c`
(port NULL guard), `src/actor/ac_kamakura_indoor.c` (port), `src/actor/ac_s_car.c`
(include only), `src/c_keyframe.c`, `src/effect/ef_motiyuge.c`, `src/effect/ef_turi_mizu.c`,
`src/static/libc64/__osMalloc.c`. These are the files a later slice must not accidentally
"fix" again; they are port/perf/signature changes, not co-op logic.

## Pass log

- 2026-10-29: file opened, A17 registered. Seeded with E1-E13 from the sprint draw and
  insect-room tint sessions (palettes, TLUT reload, fade routing/scissor, K1, world
  snapshot gaps, light pool, BG texture bank, per-view cost, helper overlap, rewrite
  candidates). No code changes in this pass.
- 2026-10-29: added the door/transition area (E14-E16), the host-vs-guest questions, the
  per-building test matrix, and the consolidation clusters C1-C10 with the touch-point
  budget. Door transitions are the template: one pipeline plus a descriptor table, applied
  cluster by cluster. No code changes in this pass.
- 2026-10-29: **step 1 inventory measured.** 438 co-op-specific sites in 44 files (36 retail,
  8 PC layer); `m_play.c` and `pc_world.c` alone carry 229. `#ifdef TARGET_PC` ruled out as a
  co-op marker (17,892 sites, port-wide). Per-cluster counts and the first-slice verdict are
  in the Inventory section. No code changes in this pass.
- 2026-10-29: **step 2 cluster assessment done; full-rewrite verdict: no.** All ten clusters
  read at the source, with the first-pass cross-cutting numbers (254 retail `TARGET_PC`
  guards in the 31 co-op files, 34/36 request wrappers still resident-keyed, 11 raw player
  loops, 10 per-player state stores, 117 `Common_` fields vs 11 captured). New entries
  E17-E21; E5 and E8 corrected (K1 lead conflict, palette half of E8 already landed).
  Verdict and slice order are in "Rewrite verdict": C2 -> C3 -> C1 -> C4 -> C6 -> C7 ->
  C5/C8 -> C10. No code changes in this pass.
- 2026-10-29: **inventory corrected to the complete surface: 1,581 sites in 135 files (17 PC
  layer, 118 retail).** The 438/44 figure came from a narrow symbol union and missed the
  player-context system (`m_player_call.c`), the nine-module `*_world_store/load` family,
  the split-HUD helpers (`showSplitHudView`, `mFont_CulcHudHalfOrthoMatrix`), per-player
  mailbox and villager state, the per-player menu declarations and `game_child_ct`.
  Re-derived from the 146 files touched by the 19 co-op merges, then filtered to current
  co-op constructs; the appendix lists all 135 files and the 18 touched-but-clean ones.
  Verdict re-stated: no retail-engine rewrite; yes to consolidating the co-op layer.
- 2026-10-29: **C2 implemented (steps 1-5), build clean, matrix pending.** Added
  `PcTransition` and `pc_transition_resolve` to `pc_world.c/h` with the documented
  fade-actor -> demo-talker -> fading-play -> entering-slot -> host order, the kind/phase
  enums and the `split`/`every_view`/`count` fields. `makeBumpTexture` and `Game_play_draw`
  now read one resolved object per frame (passed into `makeBumpTexture`); `s_fade_player`
  and `s_allow_fade_in` moved behind `pc_transition_set_fade_actor` /
  `pc_transition_fade_in_allowed` (plus the setter needed by `Game_play_start_fade_in`).
  Deleted `play_split_live`, `play_fading_play`, `play_fade_is_split`, `play_fade_view`
  and `play_fade_every_view`. Three files touched: `pc/include/pc_world.h`,
  `pc/src/pc_world.c`, `src/game/m_play.c`; no retail file outside `m_play.c`. Build
  clean (MINGW32). Next: run the door matrix (own house, Cranny, police box, post office,
  museum entrance + room, join fade, Return to Title, split scissor, single player) and
  the K1 capture; only then measure the C2 site drop and decide K1.
- 2026-10-29: **first C2 playtest found a pre-existing solo-co-op door bug (A18); fixed.**
  P1 with no guest joined could enter their house but exited without the walk-out clip and
  the door stopped accepting A. `pc_world_town_stays()` asked
  `pc_world_session_count() > 1`, so a one-resident co-op session told the house, cottage,
  post office and Able Sisters doors that the town was being torn down and they took the
  retail dead action. The town actually stays for the whole co-op session
  (`pc_world_transition` keeps it when `g_pc_coop_session != 0`), so `pc_world_town_stays`
  now returns TRUE for any co-op session and keeps the session-count test for retail
  only. P2's house worked because two residents made the old predicate true, and P1's
  door "recovering" after P2 rejoined was the same condition. New entry E22 (duplicate
  gyroids after leave/rejoin) is session/world residue, not transition state; keep it out
  of this slice.
- 2026-10-29: **C2 door matrix held except K1; gyroid dedupe fixed.** Operator retest:
  solo-co-op house in/out (A18), cottage, post office, Able Sisters, P2 join and both
  house in/outs all held, plus the rest of the matrix. K1 (the ~3% cool/saturation wash
  on the non-changing half during a host enter) is still present; it is the remaining C2
  item and the next commit. During the building run P1's haniwa doubled again with no
  P2 in the session, which rules out leave/rejoin as the trigger: `aBC_actor_already_at`,
  the dedupe added for exactly this symptom, searched `ACTOR_PART_ITEM` only while
  `Haniwa_Profile` is `ACTOR_PART_BG`, so a rescan that regained the FG name (restored
  when a culled actor is deleted) spawned a second gyroid. The search now covers ITEM and
  BG (not the full ten-part walk, for the wide-rescan frame cost). E22 corrected; operator
  retested the same sequence: confirmed gone.
- 2026-10-29: **C2 slice committed (`a2441e6`) and merged to `master` (`18fcb17`, `--no-ff`).** Operator confirmed the gyroid fix, asked to commit, merge (machine switch) and defer K1. The slice is `pc_transition` + A18 (`pc_world_town_stays`) + E22 (haniwa dedupe), merged on `master`; K1 remains parked as E5 by operator decision. Next slice per the recommended order: C3/E13 (per-world state registry).
