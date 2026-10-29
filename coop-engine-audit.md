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

## Rewrite candidates

| id | subsystem | why | first slice |
|----|-----------|-----|-------------|
| E12 | transition/fade ownership | Repeated bugs this session (wrong half, unclipped fade, missed colour fade); state split across five helpers | Introduce `pc_transition_owner()` returning play/view/kind; port the fade overlay and view resolution to it |
| E13 | per-world state | Every shared static is a future regression; the snapshot list is hand-maintained | Inventory state, then one `pc_world_state_capture/restore` table with tests by comparison |
| E11 | player resolution | Same question answered in four places with subtly different fallbacks | One resolver + one documented order; migrate call sites in one pass |

## Pass log

- 2026-10-29: file opened, A17 registered. Seeded with E1-E13 from the sprint draw and
  insect-room tint sessions (palettes, TLUT reload, fade routing/scissor, K1, world
  snapshot gaps, light pool, BG texture bank, per-view cost, helper overlap, rewrite
  candidates). No code changes in this pass.
