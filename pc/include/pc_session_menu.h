/* Per-player co-op session menu. Guests open it with Back/Select (Escape if
 * the keyboard row is that guest). The world keeps running for the other
 * players and the menu draws in the opener's view. The host keeps the Escape
 * pause menu. */
#ifndef PC_SESSION_MENU_H
#define PC_SESSION_MENU_H

/* Keep SDL's main redefinition out of game headers (main.h declares main). */
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;

/* Route one SDL event. Returns 1 when a session menu consumed it. */
int  pc_session_menu_handle_event(const SDL_Event* e);

/* Is this Controls row's session menu open? Indexed by pad/row (0-3). */
int  pc_session_menu_is_open(int pad);
void pc_session_menu_close(int pad);
void pc_session_menu_close_all(void);

/* Draw the open menu for this row. The caller has already set the viewport,
 * scissor and font phase for the opener's view. */
void pc_session_menu_draw(struct game_s* game, int pad);

/* Leave action selected in the menu: returns 1 once, then clears. */
int  pc_session_menu_take_leave_request(int pad);

#ifdef __cplusplus
}
#endif

#endif /* PC_SESSION_MENU_H */
