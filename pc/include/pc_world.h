#ifndef PC_WORLD_H
#define PC_WORLD_H

#include "types.h"

struct game_s;
struct game_play_s;

/* One simulated place. Players in the same scene share a world, so four
 * players in town are one play, and a player in a house, shop, museum, or
 * post office adds one more. Empty worlds are not stepped. */

#define PC_WORLD_MAX 5

void pc_world_attach_primary(struct game_s* game);
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

/* A player actor in any live world, including ones that are not bound. */
int pc_world_any_player(void);

/* The resident just walked back into a town that was already loaded. The
 * first load into town still uses the retail door. */
void pc_world_mark_door_return(int slot);
int pc_world_door_return_pending(int slot);
void pc_world_clear_door_return(int slot);

#endif
