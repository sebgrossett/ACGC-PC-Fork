#ifndef PC_WORLD_H
#define PC_WORLD_H

#include "types.h"

struct game_s;
struct game_play_s;
struct actor_s;

/* One simulated place. Players in the same scene share a world, so four
 * players in town are one play, and a player in a house, shop, museum, or
 * post office adds one more. Empty worlds are not stepped. */

#define PC_WORLD_MAX 5

void pc_world_attach_primary(struct game_s* game);
void pc_world_detach_primary(void);
int pc_world_live(void);
int pc_world_init_is_child(void);
int pc_world_cleanup_is_child(void);
struct game_s* pc_world_primary(void);

/* End of a scene wipe. Returns TRUE when this play stays up and the retail
 * game swap must not run. */
int pc_world_transition(struct game_play_s* play);

/* The resident a door just sent inside. The wipe ends later, after the demo
 * talker is gone, so the transition reads this instead of the selected slot. */
void pc_world_note_entering(int slot);

/* How many residents still have a view, including one whose town actor was
 * removed because they are indoors. */
int pc_world_session_count(void);
/* The town play is not destroyed. An outdoor door must return to its wait
 * action instead of the retail dead proc. */
int pc_world_town_stays(void);
int pc_world_entering_slot(void);
int pc_world_slot_view(int slot);
int pc_world_slot_world(int slot);
/* The player actor for a resident, whichever world they are in. */
struct player_actor_s* pc_world_slot_player(int slot);
/* Drop the world a leaving resident is in (their interior). */
void pc_world_leave_slot(int slot);
int pc_world_play_slot(struct game_play_s* play);

/* Step every world but the primary. `move` is Game_play_move for one play.
 * Primary stays bound on return. */
void pc_world_move_rest(void (*move)(struct game_s*));

/* Free every interior. Return to Title keeps one play, or the title demo
 * draws inside the house and the title menu never shows. */
void pc_world_drop_children(void);

/* View index -> world index. Primary must be bound. */
void pc_world_fill_views(int* view_world, int count);
struct game_play_s* pc_world_at(int index);
void pc_world_bind(int index);

/* Swap a world's copy of the shared field palette area back in. Call right
 * before that world's view is flushed. No-op until the world has a copy. */
void pc_world_settle_palette(int index);

/* A player actor in any live world, including ones that are not bound. */
int pc_world_any_player(void);

/* The resident just walked back into a town that was already loaded. The
 * first load into town still uses the retail door. */
void pc_world_mark_door_return(int slot);
int pc_world_door_return_pending(int slot);
void pc_world_clear_door_return(int slot);

/* One transition owner: which world is changing scene, which view shows it,
 * and what kind of fade/wipe is running. Resolved once per frame before the
 * draw; every consumer reads this instead of re-deriving it. */
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

/* The returned object is a static, valid until the next resolve. */
const PcTransition* pc_transition_resolve(struct game_play_s* town);
/* Fade-in actor (join / walk-out). NULL means the selected resident. */
void pc_transition_set_fade_actor(struct actor_s* who);
/* TRUE only while Game_play_start_fade_in runs. */
int pc_transition_fade_in_allowed(void);
void pc_transition_set_fade_in_allowed(int allow);
#endif
