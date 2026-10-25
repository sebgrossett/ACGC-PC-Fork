/* pc_pad.c - GC controller input via SDL gamepad + keyboard */
#include "pc_platform.h"
#include "pc_typing.h"
#include "pc_keybindings.h"
#include "pc_settings.h"
#include <dolphin/pad.h>

/* analog stick constants */
#define STICK_MAGNITUDE     80
#define RUMBLE_DURATION_MS  200

/* Index 0 is the first connected gamepad (Pad 1 in settings). */
static SDL_GameController* g_controllers[PC_PLAYER_INPUT_PAD_COUNT];
static SDL_JoystickID g_controller_ids[PC_PLAYER_INPUT_PAD_COUNT];

/* deadzone percent (0-40) -> raw SDL axis threshold */
static int deadzone_threshold(int percent) {
    if (percent < 0)  percent = 0;
    if (percent > 90) percent = 90;
    return percent * 32767 / 100;
}

static int player_mode(int player) {
    int mode;

    if (player < 0 || player >= PC_PLAYER_COUNT) return PC_PLAYER_INPUT_NONE;
    mode = g_pc_settings.player_input[player];
    if (mode < PC_PLAYER_INPUT_KEY_AND_PAD || mode > PC_PLAYER_INPUT_NONE)
        return (player == 0) ? PC_PLAYER_INPUT_KEY_AND_PAD : PC_PLAYER_INPUT_NONE;
    return mode;
}

static SDL_GameController* controller_for_mode(int mode) {
    if (mode == PC_PLAYER_INPUT_KEY_AND_PAD) return g_controllers[0];
    if (mode >= PC_PLAYER_INPUT_PAD1 && mode < PC_PLAYER_INPUT_PAD1 + PC_PLAYER_INPUT_PAD_COUNT)
        return g_controllers[mode - PC_PLAYER_INPUT_PAD1];
    return NULL;
}

/* Keep open handles, ordered so slot 0 stays the first connected gamepad. */
static void controllers_refresh(void) {
    SDL_GameController* next[PC_PLAYER_INPUT_PAD_COUNT];
    SDL_JoystickID next_ids[PC_PLAYER_INPUT_PAD_COUNT];
    int n = 0;
    int i, s;

    for (s = 0; s < PC_PLAYER_INPUT_PAD_COUNT; s++) {
        next[s] = NULL;
        next_ids[s] = -1;
    }

    for (i = 0; i < SDL_NumJoysticks() && n < PC_PLAYER_INPUT_PAD_COUNT; i++) {
        SDL_JoystickID id;
        int moved = 0;

        if (!SDL_IsGameController(i)) continue;
        id = SDL_JoystickGetDeviceInstanceID(i);
        for (s = 0; s < PC_PLAYER_INPUT_PAD_COUNT; s++) {
            if (g_controllers[s] != NULL && g_controller_ids[s] == id) {
                next[n] = g_controllers[s];
                next_ids[n] = id;
                g_controllers[s] = NULL;
                n++;
                moved = 1;
                break;
            }
        }
        if (moved) continue;
        next[n] = SDL_GameControllerOpen(i);
        if (next[n] != NULL) {
            next_ids[n] = id;
            n++;
        }
    }

    for (s = 0; s < PC_PLAYER_INPUT_PAD_COUNT; s++) {
        if (g_controllers[s] != NULL) SDL_GameControllerClose(g_controllers[s]);
        g_controllers[s] = next[s];
        g_controller_ids[s] = next_ids[s];
    }
}

/* is a remappable pad binding currently held? */
static int pad_code_pressed(SDL_GameController* controller, PCPadCode code) {
    if (controller == NULL || code < 0) return 0;
    if (code & PC_PAD_AXIS_BIT) {
        return SDL_GameControllerGetAxis(controller,
            (SDL_GameControllerAxis)(code & 0xFF)) > PC_PAD_AXIS_PRESS;
    }
    return SDL_GameControllerGetButton(controller, (SDL_GameControllerButton)code);
}

/* analog trigger value for the L/R binding (digital bindings read as full press) */
static u8 pad_trigger_value(SDL_GameController* controller, PCPadCode code) {
    if (controller == NULL || code < 0) return 0;
    if (code & PC_PAD_AXIS_BIT) {
        s16 v = SDL_GameControllerGetAxis(controller, (SDL_GameControllerAxis)(code & 0xFF));
        if (v < 0) v = 0;
        return (u8)(v >> 7);
    }
    return SDL_GameControllerGetButton(controller, (SDL_GameControllerButton)code) ? 255 : 0;
}

static void sample_keyboard(u16* buttons, s8* stickX, s8* stickY, s8* cstickX, s8* cstickY) {
    const u8* keys = SDL_GetKeyboardState(NULL);
    u32 mouse = SDL_GetMouseState(NULL, NULL);

    if (g_pc_typing_mode && g_pc_editor_active) return;

#define INPUT_PRESSED(code) \
    (((code) & PC_INPUT_MOUSE_BIT) \
        ? (mouse & SDL_BUTTON((code) & 0xFF)) \
        : keys[(SDL_Scancode)(code)])

    {
        PCKeybindings* kb = &g_pc_keybindings;
        if (INPUT_PRESSED(kb->a))     *buttons |= PAD_BUTTON_A;
        if (INPUT_PRESSED(kb->b))     *buttons |= PAD_BUTTON_B;
        if (INPUT_PRESSED(kb->x))     *buttons |= PAD_BUTTON_X;
        if (INPUT_PRESSED(kb->y))     *buttons |= PAD_BUTTON_Y;
        if (INPUT_PRESSED(kb->start)) *buttons |= PAD_BUTTON_START;
        if (INPUT_PRESSED(kb->z))     *buttons |= PAD_TRIGGER_Z;
        if (INPUT_PRESSED(kb->l))     *buttons |= PAD_TRIGGER_L;
        if (INPUT_PRESSED(kb->r))     *buttons |= PAD_TRIGGER_R;

        if (INPUT_PRESSED(kb->stick_up))    *stickY += STICK_MAGNITUDE;
        if (INPUT_PRESSED(kb->stick_down))  *stickY -= STICK_MAGNITUDE;
        if (INPUT_PRESSED(kb->stick_left))  *stickX -= STICK_MAGNITUDE;
        if (INPUT_PRESSED(kb->stick_right)) *stickX += STICK_MAGNITUDE;

        if (INPUT_PRESSED(kb->cstick_up))    *cstickY += STICK_MAGNITUDE;
        if (INPUT_PRESSED(kb->cstick_down))  *cstickY -= STICK_MAGNITUDE;
        if (INPUT_PRESSED(kb->cstick_left))  *cstickX -= STICK_MAGNITUDE;
        if (INPUT_PRESSED(kb->cstick_right)) *cstickX += STICK_MAGNITUDE;

        if (INPUT_PRESSED(kb->dpad_up))    *buttons |= PAD_BUTTON_UP;
        if (INPUT_PRESSED(kb->dpad_down))  *buttons |= PAD_BUTTON_DOWN;
        if (INPUT_PRESSED(kb->dpad_left))  *buttons |= PAD_BUTTON_LEFT;
        if (INPUT_PRESSED(kb->dpad_right)) *buttons |= PAD_BUTTON_RIGHT;
    }

#undef INPUT_PRESSED
}

/* ORs buttons. Sticks replace the sample only outside the deadzone, matching
 * the old single-controller path where a resting pad left the keyboard stick. */
static void sample_pad(SDL_GameController* controller, u16* buttons,
                       s8* stickX, s8* stickY, s8* cstickX, s8* cstickY,
                       u8* triggerLeft, u8* triggerRight) {
    PCPadBindings* pb;
    int stick_dz, cstick_dz;
    s16 lx, ly, rx, ry;

    if (controller == NULL) return;
    pb = &g_pc_padbindings;
    if (pad_code_pressed(controller, pb->a))     *buttons |= PAD_BUTTON_A;
    if (pad_code_pressed(controller, pb->b))     *buttons |= PAD_BUTTON_B;
    if (pad_code_pressed(controller, pb->x))     *buttons |= PAD_BUTTON_X;
    if (pad_code_pressed(controller, pb->y))     *buttons |= PAD_BUTTON_Y;
    if (pad_code_pressed(controller, pb->start)) *buttons |= PAD_BUTTON_START;
    if (pad_code_pressed(controller, pb->z))     *buttons |= PAD_TRIGGER_Z;
    if (pad_code_pressed(controller, pb->l))     *buttons |= PAD_TRIGGER_L;
    if (pad_code_pressed(controller, pb->r))     *buttons |= PAD_TRIGGER_R;
    if (pad_code_pressed(controller, pb->dpad_up))    *buttons |= PAD_BUTTON_UP;
    if (pad_code_pressed(controller, pb->dpad_down))  *buttons |= PAD_BUTTON_DOWN;
    if (pad_code_pressed(controller, pb->dpad_left))  *buttons |= PAD_BUTTON_LEFT;
    if (pad_code_pressed(controller, pb->dpad_right)) *buttons |= PAD_BUTTON_RIGHT;

    stick_dz  = deadzone_threshold(g_pc_settings.stick_deadzone);
    cstick_dz = deadzone_threshold(g_pc_settings.cstick_deadzone);

    lx = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    ly = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    if (abs(lx) > stick_dz) {
        int sx = lx >> 8;
        if (sx > 127) sx = 127; else if (sx < -128) sx = -128;
        *stickX = (s8)sx;
    }
    if (abs(ly) > stick_dz) {
        int sy = -(ly >> 8);
        if (sy > 127) sy = 127; else if (sy < -128) sy = -128;
        *stickY = (s8)sy;
    }

    rx = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
    ry = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
    if (abs(rx) > cstick_dz) {
        int srx = rx >> 8;
        if (srx > 127) srx = 127; else if (srx < -128) srx = -128;
        *cstickX = (s8)srx;
    }
    if (abs(ry) > cstick_dz) {
        int sry = -(ry >> 8);
        if (sry > 127) sry = 127; else if (sry < -128) sry = -128;
        *cstickY = (s8)sry;
    }

    *triggerLeft  = pad_trigger_value(controller, pb->l);
    *triggerRight = pad_trigger_value(controller, pb->r);
}

BOOL PADInit(void) {
    controllers_refresh();
    return TRUE;
}

u32 PADRead(PADStatus* status) {
    int kb_taken = 0;
    int pad_taken[PC_PLAYER_INPUT_PAD_COUNT] = { 0, 0, 0, 0 };
    int player;

    memset(status, 0, sizeof(PADStatus) * 4);
    controllers_refresh();

    for (player = 0; player < PC_PLAYER_COUNT; player++) {
        int mode = player_mode(player);
        int use_kb = 0;
        int pad_index = -1;
        u16 buttons = 0;
        s8 stickX = 0, stickY = 0, cstickX = 0, cstickY = 0;
        u8 triggerLeft = 0, triggerRight = 0;

        if (mode == PC_PLAYER_INPUT_NONE) continue;

        if (mode == PC_PLAYER_INPUT_KEYBOARD || mode == PC_PLAYER_INPUT_KEY_AND_PAD) {
            if (!kb_taken) {
                use_kb = 1;
                kb_taken = 1;
            }
        }
        if (mode == PC_PLAYER_INPUT_KEY_AND_PAD) {
            pad_index = 0;
        } else if (mode >= PC_PLAYER_INPUT_PAD1 &&
                   mode < PC_PLAYER_INPUT_PAD1 + PC_PLAYER_INPUT_PAD_COUNT) {
            pad_index = mode - PC_PLAYER_INPUT_PAD1;
        }
        if (pad_index >= 0) {
            if (pad_taken[pad_index]) pad_index = -1;
            else pad_taken[pad_index] = 1;
        }
        if (!use_kb && pad_index < 0) continue;

        if (use_kb) sample_keyboard(&buttons, &stickX, &stickY, &cstickX, &cstickY);
        if (pad_index >= 0) {
            sample_pad(g_controllers[pad_index], &buttons, &stickX, &stickY, &cstickX, &cstickY,
                       &triggerLeft, &triggerRight);
        }

        status[player].button = buttons;
        status[player].stickX = stickX;
        status[player].stickY = stickY;
        status[player].substickX = cstickX;
        status[player].substickY = cstickY;
        status[player].triggerLeft = triggerLeft;
        status[player].triggerRight = triggerRight;
        status[player].err = 0; /* PAD_ERR_NONE */
    }

    return PAD_CHAN0_BIT; /* Controller 1 connected */
}

void PADControlMotor(s32 chan, u32 command) {
    SDL_GameController* controller = controller_for_mode(player_mode(chan));
    if (controller != NULL) {
        u16 intensity = (command == 1) ? 0xFFFF : 0;
        SDL_GameControllerRumble(controller, intensity, intensity, RUMBLE_DURATION_MS);
    }
}

void PADControlAllMotors(const u32* commands) {
    PADControlMotor(0, commands[0]);
}

void PADCleanup(void) {
    int s;
    for (s = 0; s < PC_PLAYER_INPUT_PAD_COUNT; s++) {
        if (g_controllers[s] != NULL) {
            SDL_GameControllerClose(g_controllers[s]);
            g_controllers[s] = NULL;
            g_controller_ids[s] = -1;
        }
    }
}

BOOL PADReset(u32 mask) { (void)mask; return TRUE; }
BOOL PADRecalibrate(u32 mask) { (void)mask; return TRUE; }
BOOL PADSync(void) { return TRUE; }
void PADSetSpec(u32 spec) { (void)spec; }
void PADSetAnalogMode(u32 mode) { (void)mode; }
/* PADClamp compiled from decomp: src/static/dolphin/pad/Padclamp.c */
BOOL PADGetType(s32 chan, u32* type) { if (type) *type = 0x09000000; return TRUE; }
