/*
    OpenMCM2 - the options screens, ported from the game (OptionProcs.cpp in the original): the
    options screen with its six pages (game, graphics, sound, controls, messages, garage), the
    advanced graphics dialog and the restore dialogs. Each function here replaces the game's at
    the address in its PORT line, and was checked against the original's instructions.

    The pages edit copies of the game's settings; the options screen takes them from the game
    when it opens and gives them back when it closes.
*/

#include "gui.h"
#include "dirlist.h"

#ifdef OPENMCM2_DEV
#include "../dev/dev.h"
#endif

BYTE* __fastcall dialog_find_control(BYTE* self, void* edx_unused, const char* name, int type);
BYTE* __fastcall dialog_construct(BYTE* self, void* edx_unused, DWORD flag, const char* name);
void  __fastcall dialog_end(BYTE* self, void* edx_unused, DWORD code);
void  __fastcall dialog_notify_controls(BYTE* self, void* edx_unused, DWORD flag);
void  __fastcall dialog_set_controls(BYTE* self, void* edx_unused, DWORD id, DWORD value);
void  __fastcall dialog_enable_controls(BYTE* self, void* edx_unused, DWORD id, DWORD enabled);
void  __fastcall dialog_add_timer(BYTE* self, void* edx_unused, DWORD id, DWORD period,
                                  BYTE* target);
void  __fastcall dialog_remove_timer(BYTE* self, void* edx_unused, DWORD id);
void  __fastcall dialog_tell_parent(BYTE* self, void* edx_unused, DWORD code, DWORD kind);
BYTE* __fastcall gui_manager_open(gui_manager* self, void* edx_unused, BYTE* dialog, DWORD a,
                                  DWORD flags, DWORD area, BYTE* parent, DWORD arg, DWORD c,
                                  DWORD wait);
int   __fastcall control_set_text(BYTE* self, void* edx_unused, const char* text);
int   __fastcall control_load_text(BYTE* self, void* edx_unused, HINSTANCE module, UINT id);
void  __fastcall control_show(BYTE* self, void* edx_unused, DWORD shown, BOOL animate);
int   __fastcall control_set_colour(BYTE* self, void* edx_unused, COLORREF colour);
int   __fastcall control_set_align(BYTE* self, void* edx_unused, DWORD align);
BOOL  __fastcall option_set_count(BYTE* self, void* edx_unused, int count);
int   __fastcall option_load_text(BYTE* self, void* edx_unused, int index, HINSTANCE module,
                                  UINT id);
int   __fastcall option_select(BYTE* self, void* edx_unused, int index);
int   __fastcall option_current_index(BYTE* self);
BOOL  __fastcall radio_set(BYTE* self, void* edx_unused, int number);
int   __fastcall radio_number(BYTE* self);
void  __fastcall list_clear(BYTE* self);
BOOL  __fastcall list_add_text(BYTE* self, void* edx_unused, const char* text, DWORD data, DWORD b);
int   __fastcall list_set_text(BYTE* self, void* edx_unused, int index, const char* text);
int   __fastcall list_selected(BYTE* self);
DWORD __fastcall list_data(BYTE* self, void* edx_unused, int index);
int   __fastcall list_select_data(BYTE* self, void* edx_unused, DWORD data);
int   __fastcall list_select_text(BYTE* self, void* edx_unused, const char* text);
int   __fastcall list_select_all(BYTE* self, void* edx_unused, int index);
int   __fastcall list_set_colour(BYTE* self, void* edx_unused, COLORREF colour, int index);
int   __fastcall list_set_text_colour(BYTE* self, void* edx_unused, COLORREF colour, int index);
int   __fastcall list_set_208(BYTE* self, void* edx_unused, DWORD value);
int   __fastcall list_set_colour_2(BYTE* self, void* edx_unused, COLORREF colour);
void  __fastcall list_sort(BYTE* self, void* edx_unused, BOOL outer);
int   __fastcall slider_value_at(BYTE* self, void* edx_unused, int range);
BOOL  __fastcall slider_set_value(BYTE* self, void* edx_unused, int value, unsigned range);
int   __fastcall slider_set_steps(BYTE* self, void* edx_unused, int count);
int   __fastcall slider_get_value(BYTE* self);
void  __fastcall edit_bind(BYTE* self, void* edx_unused, char* buffer, int size);

int   __cdecl    game_FUN_0053b6e0(const char* a, const char* b);               /* _stricmp */
int   __cdecl    game_FUN_0054cd80(const char* a, const char* b, size_t count); /* strncmp */
int   __cdecl    game_FUN_0053a11f(char* to, const char* format, ...);          /* sprintf */
BOOL  __fastcall game_FUN_00526fc0(BYTE* game, void* edx_unused, UINT id, char* text, int size);
BOOL  __fastcall game_FUN_00527300(BYTE* game);                 /* no Restore button */
void __fastcall profile_defaults(char* self);              /* the profile's defaults */
void __fastcall profile_load_controls(char* self);
void __fastcall profile_save(char* self);
BOOL __fastcall profile_load(char* self);              /* save (dlgprocs) / load it */
BOOL __fastcall profile_open_help(char* self, void* edx_unused, const char* topic, const char* parameters);

/* The sound system: a sound's volume, play and stop, and loading a bank. */
void  __fastcall game_FUN_004c1330(void* sound, void* edx_unused, DWORD volume, DWORD a);
void  __fastcall game_FUN_004c0e00(void* sound, void* edx_unused, DWORD a, DWORD b, DWORD c);
void  __fastcall game_FUN_004c1090(void* sound, void* edx_unused, DWORD a);
void  __fastcall game_FUN_004c3060(void* sound, void* edx_unused, DWORD bank, DWORD a, DWORD bits);

/* The input devices: the joystick's filter, its kind check and an axis. */
void  __fastcall game_FUN_004c8230(BYTE* joystick, void* edx_unused, DWORD filter);
BOOL  __fastcall game_FUN_0048a770(BYTE* joystick, void* edx_unused, int kind);
float __fastcall game_FUN_0048adb0(BYTE* joystick, void* edx_unused, int axis);

/* The key map (game_main + 0x33FC); its first dword is the device being set up. */
void  __fastcall game_FUN_0044a2e0(BYTE* controls, void* edx_unused, int device, int action,
                                   char* text);                 /* an action's key, named */
BOOL  __fastcall game_FUN_0044a310(BYTE* controls, void* edx_unused, int kind, int code,
                                   char* text);                 /* a key's name */
void  __fastcall game_FUN_00449c20(BYTE* controls, void* edx_unused, int device, int action,
                                   int kind, int code);         /* bind */
BOOL  __fastcall game_FUN_00449c50(BYTE* controls, void* edx_unused, int action, int kind,
                                   int code, int* other);       /* bound to another action */
void  __fastcall game_FUN_00449e20(BYTE* controls, void* edx_unused, const char* path, int a);

/* The directory lister (dirlist.cpp) at game_main + 0x3338. */

/* The game's settings, by vtable slot of game_main: numbers, texts, and DriverInfo numbers. */
typedef DWORD (__fastcall* setting_number)(void* game, void* edx_unused, const char* name,
                                           DWORD fallback);
typedef int   (__fastcall* setting_text)(void* game, void* edx_unused, const char* name,
                                         const char* fallback, char* text, DWORD* size);
typedef void  (__fastcall* setting_put)(void* game, void* edx_unused, const char* name,
                                        DWORD value);
#define DRIVER_NUMBER   0x50
#define SETTING_NUMBER  0x58
#define SETTING_TEXT    0x5C
#define DRIVER_PUT      0x64
#define SETTING_PUT     0x6C

typedef void (__fastcall* control_bind)(BYTE* control, void* edx_unused, void* value);
typedef void (__fastcall* list_moved)(BYTE* list, void* edx_unused, int* done);
typedef int  (__fastcall* input_moved)(BYTE* device, void* edx_unused, int input, DWORD mask,
                                       DWORD a);

#define NAME(address) ((const char*) (address))
#define OPTIONPROCS_CPP NAME(0x005756BC)    /* "D:\aardvark\VC\krusty2\OptionProcs.cpp" */
#define COMBO_LIST      0x1FC               /* a combo box's list */
#define BIKE_SIZE       0x94

#define game_language   FIELD(game_main, 0x420, HINSTANCE)
#define game_profile    ((char*) game_main + 0x578)
#define game_session    FIELD(game_main, 0x56C, BYTE*)
#define session_gui     FIELD(game_session, 0x2C, gui_manager*)
#define game_files      FIELD(game_main, 0x3338, dir_list*)
#define game_controls   FIELD(game_main, 0x33FC, BYTE*)
#define controls_device (*(int*) game_controls)     /* the setup in use, its CurrentInputDevice */
#define KEYBOARD_CONTROLS 6         /* "Keyboard"; 0 to 5 are joysticks' setups, 7 the mouse's */
#define game_joystick   FIELD(game_inputs, 0xC, BYTE*)
#define game_mouse      FIELD(game_inputs, 0x30, BYTE*)
#define game_display    FIELD(game_main, 0xC, BYTE*)    /* modes: count 8, array 0x10 */
#define display_card    ((const char*) game_display + 0x4BC)
#define cursors_locked  (game_main[0x2D5] & 2)

#ifdef OPENMCM2_DEV
/*
    The options screen opened from the pause menu over a race (ingameprocs.c): what cannot change
    mid-race is put away - Restore (it reloads the profile over the race's settings), Help (an
    outside viewer, out of full screen), the Garage (the bike is built as the race loads) and the
    sound hardware, EAX and bits (they make the sound buffer anew). The resolution and the details
    take effect from the next race, as they always have.
*/
#define race_menu_up (FIELD(game_main, 0x333C, int) != 0)

/* A control put away, if the layout has it. */
static void put_away(BYTE* control, BOOL hide)
{
    if (!control)
        return;
    if (hide)
        control_show(control, NULL, FALSE, TRUE);
    else
        VIRTUAL(control, 0xC4, void (__fastcall*)(BYTE*, void*, DWORD))(control, NULL, FALSE);
}
#endif

/* The copies of the game's settings the pages edit. */
#define garage_settings  ((unsigned long*) 0x0068E948)   /* 0x17 dwords: game + 0x1550 */
#define sound_settings   ((unsigned long*) 0x0068E8D8)   /* 0xB: game + 0xF98 */
#define video_settings   ((unsigned long*) 0x0068E918)   /* 7: game + 0xFC4 */
#define control_settings ((unsigned long*) 0x0068E9A8)   /* 0xC: game + 0xFE0 */
#define game_settings    ((unsigned long*) 0x0068E9D8)   /* 0xDD: game + 0xC24 */
#define messages         ((char (*)[0x80]) 0x0068ED50)   /* 10 lines: game + 0x19D4 */

#define bike_cc          (*(int*) 0x0068E948)
#define suspension       ((float*) 0x0068E954)           /* six, as the garage sliders */
#define eq_bands         ((int*) 0x0068E96C)             /* 11 bands' power */
#define eq_base_hz       (*(int*) 0x0068E998)
#define eq_step_hz       (*(int*) 0x0068E9A0)
#define sound_ui         (*(int*) 0x0068E8DC)
#define sound_hardware   (*(int*) 0x0068E8E8)
#define sound_eax        (*(int*) 0x0068E8EC)
#define sound_ui_volume  (*(int*) 0x0068E8F4)
#define sound_16bit      (*(int*) 0x0068E900)
#define video_mode_chosen (*(DWORD*) 0x0068E918)
#define video_terrain    (*(int*) 0x0068E930)
#define chat_mode        (*(int*) 0x0068E9E0)
#define ui_anims         (*(int*) 0x0068E9F4)
#define curve_chosen     ((int*) 0x0068ED38)             /* each engine class's curve */
#define engine_sizes     ((const int*) 0x00572BEC)       /* 125, 250, 400, 500, 600 */

#define RATCHET          NAME(0x0057518C)    /* "Ratchet03" */
#define BACK             NAME(0x0056F8CC)    /* "Back" */
#define HELP             NAME(0x0056F8A8)    /* "Help" */
#define HELP_TOPIC       NAME(0x0056F560)    /* "MCM2HELP" */
#define NO_NAME          NAME(0x0057D888)    /* "" */
#define DECIMAL          NAME(0x0056EEA8)    /* "%d" */
#define PATH_3           NAME(0x0056FE30)    /* "%s\%s\%s" */
#define CONTROL_FILE     NAME(0x0056FE3C)    /* "control.ctl" */
#define PROFILE_FOLDER   NAME(0x0056F548)    /* "ui\profile" */
#define PROFILE_FILES    NAME(0x0056F334)    /* "*" */
#define LIST_PROFILES    NAME(0x0056F3E8)    /* "LstProfiles" */
#define MESSAGE_BOX      NAME(0x0056F734)    /* "messbox2.dtm" */
#define PROFILE_COLOUR   0xFEB97A

typedef struct binding {
    const char* name;
    void*       value;
} binding;

static BOOL named(const ui_event* event, const char* name)
{
    return !game_FUN_0053b6e0(name, (const char*) event->b);
}

static BYTE* find(BYTE* self, const char* name, int type)
{
    return dialog_find_control(self, NULL, name, type);
}

static BYTE* combo_list(BYTE* self, const char* name)
{
    return FIELD(find(self, name, 6), COMBO_LIST, BYTE*);
}

static void control_enable(BYTE* control, DWORD enabled)
{
    VIRTUAL(control, 0xC4, method_1)(control, NULL, enabled);
}

/* Each named control edits the setting given, in order. */
static void bind_controls(BYTE* self, const binding* list, int count)
{
    for (; count; count--, list++) {
        BYTE* control = find(self, list->name, 0);

#ifdef OPENMCM2_DEV
        if (!control)               /* a layout without it */
            continue;
#endif
        VIRTUAL(control, 0xD8, control_bind)(control, NULL, list->value);
    }
}

static void load_string(UINT id, char* text)
{
    game_FUN_00526fc0(game_main, NULL, id, text, 0x80);
}

static DWORD setting(const char* name, DWORD fallback)
{
    return VIRTUAL(game_main, SETTING_NUMBER, setting_number)(game_main, NULL, name, fallback);
}

static void setting_set(const char* name, DWORD value)
{
    VIRTUAL(game_main, SETTING_PUT, setting_put)(game_main, NULL, name, value);
}

/* A dialog of `size` bytes read from `file`, given its kind's vtable (NULL when out of memory). */
static BYTE* dialog_new(size_t size, int line, const char* file, void* vtable)
{
    BYTE* dialog = game_new(size, OPTIONPROCS_CPP, line);

    if (dialog) {
        dialog_construct(dialog, NULL, 1, file);
        *(void**) dialog = vtable;
    }
    return dialog;
}

/* A dialog over `self` that it hears the end of. */
static void box_open(gui_manager* manager, BYTE* self, BYTE* box)
{
    gui_manager_open(manager, NULL, box, 0, 0xC, 0, self, 0, 0, 1);
}

static void tooltips_enable(BYTE* self, int on)
{
    ((tooltip*) gui_manager_user(FIELD(self, 0x30, gui_manager*), NULL, 0)->tooltip)->enabled =
        on != 0;
}

/*
    A slider's ratchet sound, at the menus' volume while it moves and stopped when it rests. The
    volume is set before the sound is checked for, as the original does.
*/
static void ratchet_play(BYTE* self)
{
    void* sound = (void*) dialog_find(self, NULL, RATCHET);

#ifdef OPENMCM2_DEV
    if (!sound)                     /* GLOBAL.DAT's, found through the message dialog: none, nothing to play */
        return;
#endif
    game_FUN_004c1330(sound, NULL, FIELD(self, 0x30, gui_manager*)->unknown_34c, 0);
    if (sound)
        game_FUN_004c0e00(sound, NULL, 0, 0, 0);
}

static void ratchet_stop(BYTE* self)
{
    void* sound = (void*) dialog_find(self, NULL, RATCHET);

    if (sound)
        game_FUN_004c1090(sound, NULL, 0);
}

__declspec(noinline) void __fastcall controls_take_axis(BYTE* self);
__declspec(noinline) void __fastcall garage_slide_band(BYTE* self, void* edx_unused, BYTE* slider);
__declspec(noinline) void __fastcall garage_show_bands(BYTE* self);
__declspec(noinline) void __fastcall garage_fill_curves(BYTE* self);
__declspec(noinline) void __fastcall sound_apply(BYTE* self);

/* ---- Game settings page ---- */

#define CHK_TOOLTIPS     NAME(0x005750E0)    /* "ChkToolTips" */
#define MULT_CHAT_MODE   NAME(0x005750B8)    /* "MultChatMode" */

static const binding game_bindings[] = {
    { NAME(0x00575174), (void*) 0x0068E9EC },   /* RiderPositionCheckBox */
    { NAME(0x00575160), (void*) 0x0068E9DC },   /* RaceStatsCheckBox */
    { NAME(0x00575150), (void*) 0x0068E9D8 },   /* GuagesCheckBox */
    { NAME(0x00575144), (void*) 0x0068E9F4 },   /* ChkUIAnims */
    { NAME(0x00575130), (void*) 0x0068E9E4 },   /* OverviewCheckBox */
    { NAME(0x0057511C), (void*) 0x0068E9E8 },   /* RiderNamesCheckBox */
    { NAME(0x00575110), (void*) 0x0068E9F0 },   /* ChkPodiums */
    { NAME(0x00575100), (void*) 0x0068E9F8 },   /* ChkIntroMovie */
    { NAME(0x005750EC), (void*) 0x0068E9FC },   /* ChkPlayerIndicator */
    { NAME(0x005750E0), (void*) 0x0068EA00 },   /* ChkToolTips */
    { NAME(0x005750C8), (void*) 0x0068EA04 },   /* ChkWreckResetOnTrack */
};

/* OptGame.dtm (vtable 0x0055ABB0): the tooltips follow their box at once. */
PORT(0x004B6710)
void __fastcall game_page_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    char pid[0x80];
    char caption[0x80];
    char text[0x100];
    DWORD size;
    BYTE* chat;
    BYTE* label;

    (void) edx_unused;
    if (event->type == EVENT_CLICK) {
        if (named(event, CHK_TOOLTIPS))
            tooltips_enable(self, option_current_index((BYTE*) event->c));
    } else if (event->type == EVENT_OPEN) {
        bind_controls(self, game_bindings, 11);
        chat = find(self, MULT_CHAT_MODE, 2);
        option_set_count(chat, NULL, 3);
        option_load_text(chat, NULL, 0, game_language, 0x140B);
        option_load_text(chat, NULL, 1, game_language, 0x140A);
        option_load_text(chat, NULL, 2, game_language, 0x140C);
        label = find(self, NAME(0x005750B0), 12);                           /* TxtPID */
        size = sizeof pid;
        game_FUN_00526fc0(game_main, NULL, 0x13B7, caption, sizeof caption);
        VIRTUAL(game_main, SETTING_TEXT, setting_text)(game_main, NULL, NAME(0x005750AC), /* PID */
                                                       NO_NAME, pid, &size);
        game_FUN_0053a11f(text, NAME(0x0056FB7C), caption, pid);           /* "%s: %s" */
        control_set_text(label, NULL, text);
        dialog_notify_controls(self, NULL, 0);
    }
}

/* Slot 31: the chat mode (0 to 2), and the tooltips unless the cursors are locked. */
PORT(0x004B69E0)
void __fastcall game_page_sync(BYTE* self, void* edx_unused, DWORD out)
{
    BYTE* chat = find(self, MULT_CHAT_MODE, 2);
    int mode;

    (void) edx_unused;
    if (!out) {
        option_select(chat, NULL, chat_mode == 1 || chat_mode == 2 ? chat_mode : 0);
        return;
    }
    mode = option_current_index(chat);
    chat_mode = mode == 1 || mode == 2 ? mode : 0;
    if (!cursors_locked)
        tooltips_enable(self, option_current_index(find(self, CHK_TOOLTIPS, 2)));
}

/* ---- Graphics page ---- */

#define RESOLUTION_DDL   NAME(0x005751A0)    /* "DisplayResolutionDropDown" */
#define TERRAIN_SLIDER   NAME(0x005751BC)    /* "TerrainQualitySliderBar" */
#define MODE_SIZE        0x24

static const binding graphics_bindings[] = {
    { NAME(0x005751F4), (void*) 0x0068E91C },   /* ShadowsCheckBox */
    { NAME(0x005751E0), (void*) 0x0068E920 },   /* ParticlesCheckBox */
    { NAME(0x005751D4), (void*) 0x0068E928 },   /* SkyCheckBox */
    { NAME(0x005751BC), (void*) 0x0068E930 },   /* TerrainQualitySliderBar */
};

#ifdef OPENMCM2_DEV
/*
    The REBORN page (tools/modern/build_options.py): every one of MCM2:Reborn's settings, each a
    row of the values it offers, clicked through. The picture: the renderer's anti-aliasing,
    texture filtering, vsync, its finish (post.c) and frame limit (gl.h, g_render). The view: the
    HUD and the first-person view, Reborn's or the game's, and the first-person view's settings
    (dev.h, camera_options). The riding: the physics, Reborn's or the game's (steptime.c), and the
    computer riders' steps a second (bikeai.c). A row is kept as soon as it is clicked, in
    OpenMCM2-Dev.ini; a setting from the file between two values shows as the nearest.
*/
#include "gl.h"

int __fastcall option_set_text(BYTE* self, void* edx_unused, int index, const char* text);

#define CHOICES_MOST 16

static const int aa_values[] = { 0, 2, 4, 8 };
static const int filtering_values[] = { 0, 1, 2, 3, 4 };
static const int on_off_values[] = { 0, 1 };
static const int fxaa_values[] = { 0, 1 };
static const int percent_values[] = { 0, 25, 50, 75, 100 };
static const int brightness_values[] = { 80, 90, 100, 110, 120, 130 };
static const int limit_values[] = { 0, 30, 60, 120, 144, 165, 240 };
static const int style_values[] = { 1, 0 };
static const int fov_values[] = { 70, 80, 90, 100, 110, 120, 130, 140, 150 };
static const int tilt_values[] = { 0, 25, 50, 75, 100 };
static const int pitch_values[] = { 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150 };
static const int corner_values[] = { 0, 1 };
static const int lean_values[] = { 0, 100, 200, 300, 352, 400, 500, 600 };
static const int ai_rate_values[] = { 0, 30 };
static const int riders_values[] = { 11, 16, 20, 24, 30 };
static const int response_values[] = { 10, 15, 20, 30 };
static const int smoothing_values[] = { 0, 30, 60, 120 };
static const int deadzone_values[] = { 5, 10, 15, 20, 25, 30 };
static const int stick_values[] = { 0, 1 };

#define ROW(name, values, setting) { name, values, (int) (sizeof values / sizeof values[0]), setting }

static const struct reborn_row {
    const char* name;
    const int*  values;
    int         count;
    int*        setting;
} reborn_rows[] = {
    ROW("MultAntiAlias", aa_values, &g_render.samples),
    ROW("MultFiltering", filtering_values, &g_render.filtering),
    ROW("MultVSync", on_off_values, &g_render.vsync),
    ROW("MultFxaa", fxaa_values, &g_render.fxaa),
    ROW("MultSharpen", percent_values, &g_render.sharpen),
    ROW("MultVibrance", percent_values, &g_render.vibrance),
    ROW("MultBrightness", brightness_values, &g_render.brightness),
    ROW("MultBloom", percent_values, &g_render.bloom),
    ROW("MultFrameLimit", limit_values, &g_render.frame_limit),
    ROW("MultHudStyle", style_values, &g_reborn.hud),
    ROW("MultCamStyle", style_values, &g_reborn.cameras),
    ROW("MultFpFov", fov_values, &g_camera_options.fov),
    ROW("MultFpTilt", tilt_values, &g_camera_options.tilt),
    ROW("MultFpPitch", pitch_values, &g_camera_options.pitch),
    ROW("MultFpCorner", corner_values, &g_camera_options.corner),
    ROW("MultFpLean", lean_values, &g_camera_options.lean_heading),
    ROW("MultPhysStyle", style_values, &g_reborn.physics),
    ROW("MultAiRate", ai_rate_values, &dev_settings.fix_ai_rate),
    ROW("MultRiders", riders_values, &g_reborn.riders),
    ROW("MultGear", style_values, &g_reborn.gear),
    /* the GAMEPAD page's */
    ROW("MultPadOn", on_off_values, &g_pad.on),
    ROW("MultPadResponse", response_values, &g_pad.response),
    ROW("MultPadDeadzone", deadzone_values, &g_pad.deadzone),
    ROW("MultPadSmooth", smoothing_values, &g_pad.smoothing),
    ROW("MultPadLean", stick_values, &g_pad.lean_stick),
    ROW("MultPadDirect", on_off_values, &g_pad.analog_di),
};
#define REBORN_ROWS ((int) (sizeof reborn_rows / sizeof reborn_rows[0]))

static const char* const filtering_words[] = { "GAME'S OWN", "TRILINEAR", "ANISOTROPIC 4X", "ANISOTROPIC 8X",
                                               "ANISOTROPIC 16X" };
static const char* const pad_words[PAD_CONTROLS] = {
    "NONE", "A", "B", "X", "Y", "LEFT BUMPER", "RIGHT BUMPER", "LEFT TRIGGER", "RIGHT TRIGGER", "BACK", "START",
    "LEFT STICK PRESS", "RIGHT STICK PRESS", "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT",
    "LEFT STICK UP", "LEFT STICK DOWN", "LEFT STICK LEFT", "LEFT STICK RIGHT",
    "RIGHT STICK UP", "RIGHT STICK DOWN", "RIGHT STICK LEFT", "RIGHT STICK RIGHT",
};

/* A value as its row shows it. */
static void row_words(const struct reborn_row* row, int value, char* words)
{
    if (row->values == aa_values)
        wsprintfA(words, value ? "%dX" : "OFF", value);
    else if (row->values == filtering_values)
        lstrcpyA(words, filtering_words[value]);
    else if (row->values == on_off_values)
        lstrcpyA(words, value ? "ON" : "OFF");
    else if (row->values == fxaa_values)
        lstrcpyA(words, value ? "FXAA" : "OFF");
    else if (row->values == percent_values)
        wsprintfA(words, value ? "%d%%" : "OFF", value);
    else if (row->values == brightness_values)
        wsprintfA(words, "%d%%", value);
    else if (row->values == limit_values)
        wsprintfA(words, value ? "%d FPS" : "REFRESH RATE", value);
    else if (row->values == style_values)
        lstrcpyA(words, value ? "REBORN" : "CLASSIC");
    else if (row->values == fov_values)
        wsprintfA(words, "%d\xB0", value);
    else if (row->values == tilt_values)
        wsprintfA(words, "%d.%02d", value / 100, value % 100);
    else if (row->values == pitch_values)
        wsprintfA(words, "%d.%02d (%d\xB0 DOWN)", value / 100, value % 100, 15 + value * 20 / 100);
    else if (row->values == corner_values)
        lstrcpyA(words, value ? "LEAN" : "OFF");
    else if (row->values == riders_values)
        wsprintfA(words, value == 11 ? "%d (THE GAME'S)" : "%d", value);
    else if (row->values == ai_rate_values)
        wsprintfA(words, value ? "%d A SECOND" : "THE GAME'S", value);
    else if (row->values == response_values)
        lstrcpyA(words, value == 10 ? "STRAIGHT" : value == 15 ? "SMOOTH" : value == 20 ? "SOFT" : "THE GAME'S (CUBED)");
    else if (row->values == smoothing_values)
        lstrcpyA(words, value == 0 ? "OFF" : value == 30 ? "LIGHT" : value == 60 ? "MEDIUM" : "HEAVY");
    else if (row->values == deadzone_values)
        wsprintfA(words, "%d%%", value);
    else if (row->values == stick_values)
        lstrcpyA(words, value ? "RIGHT STICK" : "LEFT STICK");

    else
        wsprintfA(words, "%d.%d", value / 10, value % 10);
}

static void reborn_rows_show(BYTE* self)
{
    int r, i;

    for (r = 0; r < REBORN_ROWS; r++) {
        const struct reborn_row* row = &reborn_rows[r];
        BYTE* control = find(self, row->name, 2);
        char words[CHOICES_MOST][24];
        int chosen = 0;

        if (!control)
            continue;
        option_set_count(control, NULL, row->count);
        for (i = 0; i < row->count; i++) {
            row_words(row, row->values[i], words[i]);
            option_set_text(control, NULL, i, words[i]);
            if ((row->values[i] - *row->setting) * (row->values[i] - *row->setting)
                < (row->values[chosen] - *row->setting) * (row->values[chosen] - *row->setting))
                chosen = i;
        }
        option_select(control, NULL, chosen);
    }
}

static void reborn_rows_keep(BYTE* self)
{
    BOOL was_vsync = g_render.vsync;
    int r;

    for (r = 0; r < REBORN_ROWS; r++) {
        BYTE* control = find(self, reborn_rows[r].name, 2);
        int i = control ? option_current_index(control) : -1;

        if (i >= 0 && i < reborn_rows[r].count)
            *reborn_rows[r].setting = reborn_rows[r].values[i];
    }
    if (g_render.vsync != was_vsync)
        gl_vsync_apply();
    dev_graphics_save();
    dev_camera_save();
    dev_reborn_save();
    dev_fixes_save();
    dev_pad_save();
}

/*
    The GAMEPAD page's actions: each a button (BtnPad4..13) showing the pad control it has. A
    click listens: the next control pressed on the pad (once all were let go) is the action's,
    taken from any other action that had it, as the controls page takes keys. Five seconds of
    nothing, or a second click, leaves it as it was.
*/
#define BIND_TICK_MS    50
#define BIND_WAIT_TICKS (5000 / BIND_TICK_MS)
#define BIND_TIMER      1

static int  g_binding = -1;             /* the action listening, or -1 */
static int  g_binding_ticks;
static BOOL g_binding_let_go;           /* every control was up since the click */

BOOL pad_binding_active(void)
{
    return g_binding >= 0;
}

static void bind_button_name(int action, char* name)
{
    wsprintfA(name, "BtnPad%d", action);
}

static void pad_binds_show(BYTE* self)
{
    char name[16];
    int a;

    for (a = 4; a < PAD_ACTIONS; a++) {
        BYTE* button;

        bind_button_name(a, name);
        button = find(self, name, 1);
        if (button)
            control_set_text(button, NULL, a == g_binding ? "PRESS A BUTTON ON THE PAD"
                                                          : pad_words[g_pad.bind[a]]);
    }
}

/* STEERING SENSITIVITY: a slider in steps of 1%, its value beside it. */
#define SENSITIVITY_SLIDER  "SldPadSensitivity"
#define SENSITIVITY_TEXT    "TxtPadSensitivity"

static void sensitivity_show(BYTE* self, BOOL move_slider)
{
    BYTE* slider = find(self, SENSITIVITY_SLIDER, 8);
    BYTE* text = find(self, SENSITIVITY_TEXT, 12);
    char words[8];

    if (slider && move_slider) {
        slider_set_steps(slider, NULL, PAD_SENSITIVITY_MOST - PAD_SENSITIVITY_LEAST + 1);
        slider_set_value(slider, NULL, g_pad.sensitivity - PAD_SENSITIVITY_LEAST,
                         PAD_SENSITIVITY_MOST - PAD_SENSITIVITY_LEAST);
    }
    if (text) {
        wsprintfA(words, "%d%%", g_pad.sensitivity);
        control_set_text(text, NULL, words);
    }
}

static BOOL sensitivity_slid(BYTE* self, const ui_event* event, BOOL let_go)
{
    if (!named(event, SENSITIVITY_SLIDER))
        return FALSE;
    g_pad.sensitivity = PAD_SENSITIVITY_LEAST
                        + slider_value_at((BYTE*) event->c, NULL, PAD_SENSITIVITY_MOST - PAD_SENSITIVITY_LEAST);
    sensitivity_show(self, FALSE);
    if (let_go)
        dev_pad_save();
    return TRUE;
}

static void pad_bind_stop(BYTE* self)
{
    g_binding = -1;
    dialog_remove_timer(self, NULL, BIND_TIMER);
    pad_binds_show(self);
}

static BOOL pad_bind_click(BYTE* self, const ui_event* event)
{
    char name[16];
    int a;

    for (a = 4; a < PAD_ACTIONS; a++) {
        bind_button_name(a, name);
        if (!named(event, name))
            continue;
        if (g_binding == a) {
            pad_bind_stop(self);
            return TRUE;
        }
        g_binding = a;
        g_binding_ticks = 0;
        g_binding_let_go = FALSE;
        dialog_add_timer(self, NULL, BIND_TIMER, BIND_TICK_MS, NULL);
        pad_binds_show(self);
        return TRUE;
    }
    return FALSE;
}

static void pad_bind_tick(BYTE* self)
{
    int control, a;

    if (g_binding < 0)
        return;
    if (++g_binding_ticks > BIND_WAIT_TICKS) {
        pad_bind_stop(self);
        return;
    }
    control = pad_pressed();
    if (!g_binding_let_go) {
        g_binding_let_go = control == PAD_NONE;
        return;
    }
    if (control == PAD_NONE)
        return;
    for (a = 4; a < PAD_ACTIONS; a++)
        if (g_pad.bind[a] == control)
            g_pad.bind[a] = PAD_NONE;
    g_pad.bind[g_binding] = control;
    dev_pad_save();
    pad_bind_stop(self);
}

/* OptReborn.dtm, OptPad.dtm: pages of the options screen, made from the game settings page's vtable. */
static void __fastcall reborn_page_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_OPEN) {
        g_binding = -1;
        reborn_rows_show(self);
        pad_binds_show(self);
        sensitivity_show(self, TRUE);
        dialog_notify_controls(self, NULL, 0);
    } else if (event->type == EVENT_SLIDE) {
        if (sensitivity_slid(self, event, FALSE))
            ratchet_play(self);
    } else if (event->type == EVENT_SLIDE_END || event->type == EVENT_SLIDE_HOLD) {
        sensitivity_slid(self, event, TRUE);
    } else if (event->type == EVENT_CLICK) {
        if (!pad_bind_click(self, event))
            reborn_rows_keep(self);
    } else if (event->type == EVENT_TIMER) {
        pad_bind_tick(self);
    } else if (event->type == EVENT_CLOSE && g_binding >= 0) {
        pad_bind_stop(self);
    }
}

static void __fastcall reborn_page_sync(BYTE* self, void* edx_unused, DWORD out)
{
    (void) edx_unused;
    if (out)
        reborn_rows_keep(self);
    else
        reborn_rows_show(self);
}

#define PAGE_VTABLE      0x0055ABB0     /* OptGameSettingsDlg: slot 29 the events, 31 the settings */
#define PAGE_SLOTS       32

static void* g_reborn_vtable[PAGE_SLOTS + 1];  /* the object locator before the slots, as MSVC's */
static BYTE* g_reborn_page;                    /* the options screen's own page open (REBORN, GAMEPAD) */
static int   g_reborn_page_shown = -1;          /* which */

static void* reborn_page_vtable(void)
{
    if (!g_reborn_vtable[1]) {
        __movsd((unsigned long*) g_reborn_vtable, (const unsigned long*) (PAGE_VTABLE - 4), PAGE_SLOTS + 1);
        g_reborn_vtable[1 + 29] = (void*) reborn_page_handle;
        g_reborn_vtable[1 + 31] = (void*) reborn_page_sync;
    }
    return &g_reborn_vtable[1];
}
#endif

/* The resolutions the card offers (by the windowed or full-screen flag), the chosen selected. */
static void graphics_page_open(BYTE* self)
{
    int flag = FIELD(game_main, 0x2D0, int) ? 0x18 : 0x14;
    char text[0x80];
    BYTE* box;
    BYTE* list;
    int i;

    bind_controls(self, graphics_bindings, 4);
    box = find(self, RESOLUTION_DDL, 0);
    control_set_colour(box, NULL, 0xFFFFFF);
    control_set_align(box, NULL, 0x12);
    list = FIELD(box, COMBO_LIST, BYTE*);
    if (game_main[0x2D4] & 2) {
        for (i = 0; i < FIELD(game_display, 8, int); i++) {
            const BYTE* mode = FIELD(game_display, 0x10, BYTE*) + i * MODE_SIZE;

            if (FIELD(mode, flag, int) == 1) {
                game_FUN_0053a11f(text, NAME(0x00575198), FIELD(mode, 0, int),   /* "%d X %d" */
                                  FIELD(mode, 4, int));
                list_add_text(list, NULL, text, i, 0);
            }
        }
    }
    list_select_data(list, NULL, video_mode_chosen);
    dialog_notify_controls(self, NULL, 0);
}

/* OptGraph.dtm (vtable 0x0055AB2C): the terrain slider sets all four terrain details at once. */
PORT(0x004B6AC0)
void __fastcall graphics_page_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    switch (event->type) {
    case EVENT_OPEN:
        graphics_page_open(self);
        break;
    case EVENT_SLIDE:
        if (!named(event, RESOLUTION_DDL))
            ratchet_play(self);
        if (named(event, TERRAIN_SLIDER)) {
            int quality = slider_get_value((BYTE*) event->c);

            video_terrain = quality;
            FIELD(game_main, 0x1ED8, int) = quality;
            FIELD(game_main, 0x1EE0, int) = quality;
            FIELD(game_main, 0x1EDC, int) = quality;
            FIELD(game_main, 0x1EE4, int) = quality;
        }
        break;
    case EVENT_SLIDE_END:
    case EVENT_SLIDE_HOLD:
        if (!named(event, RESOLUTION_DDL))
            ratchet_stop(self);
        break;
    }
}

PORT(0x004B6D80)
void __fastcall graphics_page_sync(BYTE* self, void* edx_unused, DWORD out)
{
    (void) edx_unused;
    if (out)
        video_mode_chosen = list_data(combo_list(self, RESOLUTION_DDL), NULL, -1);
    else
        list_select_data(combo_list(self, RESOLUTION_DDL), NULL, video_mode_chosen);
}

/* ---- Advanced graphics ---- */

#define CHK_SHOW_PICK_3D     NAME(0x00575340)  /* "ChkShowPick3D" */
#define CHK_FILTER_INPUT     NAME(0x00575240)  /* "ChkFilterInput" */
#define CHK_PICK_CONTROLLER  NAME(0x00575214)  /* "ChkShowPickController" */
#define RAD_TERR_SINGLE      NAME(0x0057531C)  /* "RadTerrSingle" */
#define RAD_TERR_DUAL        NAME(0x00575310)  /* "RadTerrDual" */
#define RAD_TERR_NONE        NAME(0x00575250)  /* "RadTerrNone" */
#define TXT_3D_CARD          NAME(0x0057528C)  /* "Txt3DCard" */
#define USE_LAST_VIDEO_CARD  NAME(0x0057532C)  /* "UseLastVideoCard" */
#define USE_LAST_CONTROLLER  NAME(0x0057522C)  /* "UseLastController" */
#define PRESET_SELECTED      NAME(0x00575204)  /* "PresetSelected" */
#define JOYSTICK_FILTER      NAME(0x005736C4)  /* "JoystickFilter" */
#define TEXTURE_METHOD_KEY   NAME(0x005752E4)  /* "DriverInfo\%s\TerrainDetailTextureMethod" */
/* "DriverInfo\%s\CanRenderDualTextureInSinglePass" */
#define DUAL_TEXTURE_KEY     NAME(0x0057525C)

static void advanced_graphics_open(BYTE* self, int* detail)
{
    const binding detail_bindings[] = {
        { NAME(0x005752D0), detail },           /* ChkForceVideoMem */
        { NAME(0x005752C0), detail + 1 },       /* SldEcosystem */
        { NAME(0x005752B4), detail + 2 },       /* SldObjects */
        { NAME(0x005752A8), detail + 3 },       /* SldTerrain */
        { NAME(0x00575298), detail + 4 },       /* SldVisibility */
    };
    char key[0x400];
    const char* radio;
    int method, single;
    DWORD value;

    bind_controls(self, detail_bindings, 5);
    if (FIELD(game_main, 0x2D0, int))
        control_load_text(find(self, TXT_3D_CARD, 12), NULL, game_language, 0x1463);
    else
        control_set_text(find(self, TXT_3D_CARD, 12), NULL, (const char*) game_display + 0x7C0);
    value = !setting(USE_LAST_VIDEO_CARD, 1);
    option_select(find(self, CHK_SHOW_PICK_3D, 2), NULL, value);

    /* The terrain's detail texture: none, dual or single pass, as the card can. */
    game_FUN_0053a11f(key, TEXTURE_METHOD_KEY, display_card);
    method = VIRTUAL(game_main, DRIVER_NUMBER, setting_number)(game_main, NULL, key, (DWORD) -1);
    game_FUN_0053a11f(key, DUAL_TEXTURE_KEY, display_card);
    single = VIRTUAL(game_main, DRIVER_NUMBER, setting_number)(game_main, NULL, key, 0);
    if (method == -1 || (method == 2 && !single))
        radio = single == 1 ? RAD_TERR_SINGLE : RAD_TERR_DUAL;
    else if (method == 2)
        radio = RAD_TERR_SINGLE;
    else if (method == 0)
        radio = RAD_TERR_NONE;
    else if (method == 1)
        radio = RAD_TERR_DUAL;
    else
        radio = NULL;
    if (radio)
        radio_set(find(self, radio, 4), NULL, 0);
    if (!single)
        control_enable(find(self, RAD_TERR_SINGLE, 4), FALSE);

    value = setting(JOYSTICK_FILTER, 0);
    option_select(find(self, CHK_FILTER_INPUT, 2), NULL, value);
    value = !setting(USE_LAST_CONTROLLER, 0);
    option_select(find(self, CHK_PICK_CONTROLLER, 2), NULL, value);
    dialog_notify_controls(self, NULL, 0);
}

/* OptAdvG.dtm (vtable 0x0055A814): Back ends it with 0x52. */
PORT(0x004B6DD0)
void __fastcall advanced_graphics_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    int* detail = (int*) (game_main + 0x1ED4);  /* video memory, then four details */
    char key[0x400];
    int method;

    (void) edx_unused;
    switch (event->type) {
    case EVENT_CLICK:
        if (named(event, BACK)) {
            dialog_end(self, NULL, 0x52);
        } else if (named(event, CHK_FILTER_INPUT)) {
            int filter = option_current_index((BYTE*) event->c);

            setting_set(JOYSTICK_FILTER, filter);
            if (game_joystick)
                game_FUN_004c8230(game_joystick, NULL, filter);
        } else if (named(event, CHK_PICK_CONTROLLER)) {
            setting_set(USE_LAST_CONTROLLER, !option_current_index((BYTE*) event->c));
            setting_set(PRESET_SELECTED, 0);
        } else if (named(event, HELP)) {
            profile_open_help(game_profile, NULL, HELP_TOPIC, 0);
        }
        break;
    case EVENT_OPEN:
        advanced_graphics_open(self, detail);
        break;
    case EVENT_CLOSE:
        setting_set(USE_LAST_VIDEO_CARD, !option_current_index(find(self, CHK_SHOW_PICK_3D, 2)));
        if (option_current_index(find(self, RAD_TERR_SINGLE, 4)))
            method = 2;
        else
            method = option_current_index(find(self, RAD_TERR_DUAL, 4)) != 0;
        game_FUN_0053a11f(key, TEXTURE_METHOD_KEY, display_card);
        VIRTUAL(game_main, DRIVER_PUT, setting_put)(game_main, NULL, key, method);
        video_terrain = (detail[1] + detail[2] + detail[3] + detail[4]) / 4;
        break;
    }
}

/* ---- Sound page ---- */

static const binding sound_bindings[] = {
    { NAME(0x005753D0), (void*) 0x0068E8E0 },   /* InGameSoundEffectsCheck */
    { NAME(0x005753B8), (void*) 0x0068E8E8 },   /* SoundHardwareCheckBox */
    { NAME(0x0057539C), (void*) 0x0068E8DC },   /* UserInterfaceSoundsCheckBox */
    { NAME(0x00575394), (void*) 0x0068E8EC },   /* ChkEAX */
    { NAME(0x00575380), (void*) 0x0068E8F0 },   /* ChkWaypointChime */
    { NAME(0x00575364), (void*) 0x0068E8F4 },   /* UserInterfaceVolumeSlider */
    { NAME(0x00575350), (void*) 0x0068E8F8 },   /* InGameVolumeSlider */
};

/* The page's controls into the settings, and those into the sound system and the menus' volume. */
PORT(0x004B7420)
void __fastcall sound_apply(BYTE* self)
{
    BYTE* flags;

    dialog_notify_controls(self, NULL, 1);
    if (FIELD(game_main, 4, BYTE*)
#ifdef OPENMCM2_DEV
        && !race_menu_up                /* a new sound buffer mid-race: not from the pause menu */
#endif
        ) {
        flags = FIELD(game_main, 4, BYTE*) + 0x45C;
        *flags = (BYTE) ((*flags & ~1) | (sound_hardware & 1));
        flags = FIELD(game_main, 4, BYTE*) + 0x45C;
        *flags = (BYTE) (((sound_eax & 1) << 3) | (*flags & 0xF7));
        game_FUN_004c3060(FIELD(game_main, 4, void*), NULL, 0x5622, 1, sound_16bit ? 0x10 : 8);
    }
    FIELD(self, 0x30, gui_manager*)->unknown_34c =
        sound_ui ? (sound_ui_volume * 5 - 500) * 5 - 200 : -10000 - 200;
}

/* OptSound.dtm (vtable 0x0055AAA8): every change applies at once. */
PORT(0x004B7230)
void __fastcall sound_page_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    switch (event->type) {
    case EVENT_CLICK:
        sound_apply(self);
        break;
    case EVENT_SLIDE:
        ratchet_play(self);
        sound_apply(self);
        break;
    case EVENT_OPEN:
        bind_controls(self, sound_bindings, 7);
        dialog_notify_controls(self, NULL, 0);
#ifdef OPENMCM2_DEV
        if (race_menu_up) {
            put_away(find(self, NAME(0x005753B8), 2), FALSE);          /* SoundHardwareCheckBox */
            put_away(find(self, NAME(0x00575394), 2), FALSE);          /* ChkEAX */
            put_away(find(self, NAME(0x005753E8), 4), FALSE);          /* Rad8Bit */
            put_away(find(self, NAME(0x005753F0), 4), FALSE);          /* Rad16Bit */
        }
#endif
        break;
    case EVENT_SLIDE_END:
    case EVENT_SLIDE_HOLD:
        ratchet_stop(self);
        sound_apply(self);
        break;
    }
}

#define RAD_8BIT  NAME(0x005753E8)    /* "Rad8Bit" */
#define RAD_16BIT NAME(0x005753F0)    /* "Rad16Bit" */

PORT(0x004B73C0)
void __fastcall sound_page_sync(BYTE* self, void* edx_unused, DWORD out)
{
    (void) edx_unused;
    if (out)
        sound_16bit = radio_number(find(self, RAD_16BIT, 4)) - 1;
    else
        radio_set(find(self, sound_16bit ? RAD_16BIT : RAD_8BIT, 4), NULL, 0);
}

/* ---- Controls page ---- */

/*
    A key is set by picking an action, then pressing it: the page arms (CTL_ARMING) and its timer
    starts; the first tick listens (CTL_LISTENING), each tick checks the joystick's axes against
    those saved (CTL_AXES) and the mouse, and a key released while listening is taken.
*/
#define CTL_AXES         0x7F58     /* float[6] */
#define CTL_LISTENING    0x7F70
#define CTL_ARMING       0x7F74
#define CTL_FEEDBACK     0x7F78     /* the joystick has force feedback */

#define MAP_KEY_LIST     NAME(0x0057541C)    /* "MapKeyListBox" */
#define ACTION_LIST      NAME(0x0057543C)    /* "ActionListBox" */
#define INPUT_DEVICE_DDL NAME(0x0057542C)    /* "InputDeviceDDL" */
#define LISTBOX_SCROLL   NAME(0x005753FC)    /* "ListboxScroll" */
#define ACTIONS          14

static const binding control_bindings[] = {
    { NAME(0x005754F8), (void*) 0x0068E9C8 },   /* ForceFeedCheck */
    { NAME(0x005754E8), (void*) 0x0068E9A8 },   /* SteeringSlider */
    { NAME(0x005754D8), (void*) 0x0068E9AC },   /* CrossupSlider */
    { NAME(0x005754CC), (void*) 0x0068E9B0 },   /* PitchSlider */
    { NAME(0x005754B8), (void*) 0x0068E9B4 },   /* PressThrottleSlider */
    { NAME(0x005754A4), (void*) 0x0068E9B8 },   /* PressBrakesSlider */
    { NAME(0x0057548C), (void*) 0x0068E9BC },   /* ReleaseThrottleSlider */
    { NAME(0x00575478), (void*) 0x0068E9C0 },   /* ReleaseBrakesSlider */
    { NAME(0x00575468), (void*) 0x0068E9C4 },   /* FeedbackSlider */
    { NAME(0x0057545C), (void*) 0x0068E9CC },   /* ChkGasGyro */
    { NAME(0x0057544C), (void*) 0x0068E9D0 },   /* ChkBrakeGyro */
};

/* MapKeyListBox: each action's key on the device being set up. */
PORT(0x004B7AF0)
void __fastcall controls_fill_keys(BYTE* self)
{
    BYTE* list = find(self, MAP_KEY_LIST, 3);
    char text[0x80];
    int i;

    list_clear(list);
    for (i = 0; i < ACTIONS; i++) {
        game_FUN_0044a2e0(game_controls, NULL, controls_device, i, text);
        list_add_text(list, NULL, text, i, 0);
    }
}

/* The first four keys show in another colour on device 1. */
PORT(0x004B9AC0)
void __fastcall controls_colour_lists(BYTE* self)
{
    COLORREF colour = controls_device == 1 ? 0x5555FF : 0xFF000000;
    BYTE* list;
    int i;

    list = find(self, MAP_KEY_LIST, 3);
    for (i = 0; i < 4; i++)
        list_set_colour(list, NULL, colour, i);
    list = find(self, ACTION_LIST, 3);
    for (i = 0; i < 4; i++)
        list_set_colour(list, NULL, colour, i);
}

static void controls_open(BYTE* self)
{
    char text[0x80];
    BYTE* list;
    BYTE* analog;
    int i;

    FIELD(self, CTL_LISTENING, int) = 0;
    FIELD(self, CTL_ARMING, int) = 0;
    FIELD(self, CTL_FEEDBACK, int) = 0;
#ifdef OPENMCM2_DEV
    if (dev_settings.menus_modern)
        controls_device = KEYBOARD_CONTROLS;    /* this page edits keyboard bindings only */
#endif
    bind_controls(self, control_bindings, 11);
    list = find(self, ACTION_LIST, 0);
    for (i = 0; i < ACTIONS; i++) {
        load_string(0x192 + i, text);
        list_add_text(list, NULL, text, i, 0);
    }
    list = combo_list(self, INPUT_DEVICE_DDL);
    list_clear(list);
    for (i = 0; i < 8; i++) {
#ifdef OPENMCM2_DEV
        if (dev_settings.menus_modern && i != KEYBOARD_CONTROLS)
            continue;
#endif
        load_string(0x1CC + i, text);
        list_add_text(list, NULL, text, i, 0);
    }
    controls_fill_keys(self);

    /* As the original: the keys go in a second time, uncleared. */
    list = find(self, MAP_KEY_LIST, 0);
    for (i = 0; i < ACTIONS; i++) {
        game_FUN_0044a2e0(game_controls, NULL, controls_device, i, text);
        list_add_text(list, NULL, text, i, 0);
    }
    controls_colour_lists(self);

    analog = find(self, NAME(0x0057540C), 0);                              /* ChkAnalogAxes */
    VIRTUAL(analog, 0xD8, control_bind)(analog, NULL, (void*) 0x0068E9D4);
    if (game_joystick)
        game_FUN_0048a770(game_joystick, NULL, 4);
    control_show(analog, NULL, FALSE, TRUE);
    FIELD(self, CTL_FEEDBACK, int) = 0;
    if (game_joystick)
        FIELD(self, CTL_FEEDBACK, int) = FIELD(game_joystick, 0xC, int) == 3;
    dialog_enable_controls(self, NULL, 0x12F, FIELD(self, CTL_FEEDBACK, DWORD));
    dialog_notify_controls(self, NULL, 0);
    FIELD(self, CTL_ARMING, int) = 0;
    dialog_notify_controls(self, NULL, 0);
}

/* A device, or an action to set: "press a key" in its place, the buttons off, the timer on. */
static void controls_select(BYTE* self, const ui_event* event)
{
    char text[0x80];
    BYTE* list;
    int index, i;

    if (FIELD(self, CTL_LISTENING, int) || FIELD(self, CTL_ARMING, int))
        return;
    if (named(event, INPUT_DEVICE_DDL)) {
        controls_device = list_data(combo_list(self, INPUT_DEVICE_DDL), NULL, -1);
        controls_fill_keys(self);
        return;
    }
    if (!named(event, MAP_KEY_LIST) && !named(event, ACTION_LIST))
        return;
    index = list_selected((BYTE*) event->c);
    list = find(self, MAP_KEY_LIST, 3);
    load_string(0x1C2, text);
    list_set_text(list, NULL, index, text);
    dialog_set_controls(self, NULL, 0x65, 0);
    dialog_set_controls(self, NULL, 0xCA, 0);
    dialog_set_controls(self, NULL, 0x12F, 0);
    if (game_joystick) {
        for (i = 0; i < 6; i++)
            FIELD(self, CTL_AXES + i * 4, float) = game_FUN_0048adb0(game_joystick, NULL, i);
    }
    FIELD(self, CTL_ARMING, int) = 1;
    dialog_add_timer(self, NULL, 0, 0xFA, NULL);
}

/* OptCont.dtm (vtable 0x0055AA24); the key map is saved to the profile when it closes. */
PORT(0x004B74E0)
void __fastcall controls_page_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    char path[0x104];

    (void) edx_unused;
    switch (event->type) {
    case EVENT_SELECT:
        controls_select(self, event);
        break;
    case EVENT_SLIDE:
        if (!named(event, INPUT_DEVICE_DDL) && !named(event, LISTBOX_SCROLL))
            ratchet_play(self);
        break;
    case EVENT_OPEN:
        controls_open(self);
        break;
    case EVENT_CLOSE:
        game_FUN_0053a11f(path, PATH_3, PROFILE_FOLDER, game_profile, CONTROL_FILE);
        game_FUN_00449e20(game_controls, NULL, path, -1);
        break;
    case EVENT_TIMER:
        FIELD(self, CTL_ARMING, int) = 0;
        if (!FIELD(self, CTL_LISTENING, int))
            FIELD(self, CTL_LISTENING, int) = 1;
        controls_take_axis(self);
        break;
    case EVENT_KEY:
    case EVENT_KEY_UP:
        event->param = 0;
        break;
    case EVENT_SLIDE_END:
    case EVENT_SLIDE_HOLD:
        if (!named(event, INPUT_DEVICE_DDL) && !named(event, LISTBOX_SCROLL))
            ratchet_stop(self);
        break;
    }
}

PORT(0x004B7B50)
void __fastcall controls_page_sync(BYTE* self, void* edx_unused, DWORD out)
{
    (void) edx_unused;
    if (!game_controls)
        return;
    if (out) {
        controls_device = list_data(combo_list(self, INPUT_DEVICE_DDL), NULL, -1);
    } else {
        list_select_data(combo_list(self, INPUT_DEVICE_DDL), NULL, controls_device);
        controls_fill_keys(self);
    }
}

/* Listening over: the timer off, the buttons back. */
PORT(0x004BA020)
void __fastcall controls_end_wait(BYTE* self)
{
    FIELD(self, CTL_LISTENING, int) = 0;
    dialog_remove_timer(self, NULL, 0);
    dialog_set_controls(self, NULL, 0x65, 1);
    dialog_set_controls(self, NULL, 0xCA, 1);
    dialog_set_controls(self, NULL, 0x12F, 1);
}

/* A key's name; the first four actions only take the keyboard (device 0). */
PORT(0x004B9B70)
BOOL __cdecl key_name(int action, int device, int code, char* text)
{
    text_copy(text, NO_NAME);
    if (action < 4 && device)
        return FALSE;
    return game_FUN_0044a310(game_controls, NULL, device, code, text);
}

/* Vtable slot 23: keys are swallowed while listening. */
PORT(0x004B9BD0)
int __fastcall controls_press(BYTE* self, void* edx_unused, DWORD key, DWORD arg)
{
    (void) edx_unused;
    if (FIELD(self, CTL_LISTENING, int))
        return 1;
    return game_vf_23_46A500(self, NULL, key, arg);
}

/*
    Vtable slot 22: a key released while listening is the action's (key[1] its kind, key[0] its
    code), taken from any action that had it; Escape keeps the old one.
*/
PORT(0x004B9C00)
int __fastcall controls_release(BYTE* self, void* edx_unused, const DWORD* key, DWORD arg)
{
    char text[0x80];
    BYTE* list;
    int index, other;

    (void) edx_unused;
    if (!FIELD(self, CTL_LISTENING, int))
        return game_vf_22_46A4B0(self, NULL, (DWORD) key, arg);
    list = find(self, MAP_KEY_LIST, 3);
    index = list_selected(list);
    if (key[1] == 0 && key[0] == 1) {
        game_FUN_0044a2e0(game_controls, NULL, controls_device, index, text);
        list_set_text(list, NULL, index, text);
    } else {
        if (!key_name(index, key[1], key[0], text))
            return 1;
        if (game_FUN_00449c50(game_controls, NULL, index, key[1], key[0], &other)) {
            list_set_text(list, NULL, other, NO_NAME);
            game_FUN_00449c20(game_controls, NULL, controls_device, other, -1, 0);
        }
        list_set_text(list, NULL, index, text);
        game_FUN_00449c20(game_controls, NULL, controls_device, index, key[1], key[0]);
    }
    controls_end_wait(self);
    return 1;
}

static BOOL mouse_moved(BYTE* mouse, int input)
{
    return VIRTUAL(mouse, 0x10, input_moved)(mouse, NULL, input, 0x3F, 0) != 0;
}

/*
    Each tick while listening: a joystick axis moved more than 16384 from where it was (kind 2,
    codes -3, -5 .. -13), else the mouse (kind 1, -3 or -5), sets an axis pair - the even action
    and the next. Only the first five actions take axes; an unpicked action (-1) becomes -2, as
    in the original.
*/
PORT(0x004B9D60)
void __fastcall controls_take_axis(BYTE* self)
{
    BYTE* joystick = game_joystick;
    BYTE* mouse = game_mouse;
    BYTE* list = find(self, MAP_KEY_LIST, 3);
    int index = list_selected(list);
    int kind = 0, code = 0, other, axis;
    char text[0x80];

    if (index >= 5)
        return;
    if (joystick) {
        for (axis = 0; axis < 6; axis++) {
            double moved = (double) game_FUN_0048adb0(joystick, NULL, axis)
                           - FIELD(self, CTL_AXES + axis * 4, float);

            if (moved < 0.0)
                moved = -moved;
            if (moved > 16384.0) {
                kind = 2;
                code = -3 - 2 * axis;
                break;
            }
        }
    }
    if (!kind) {
        if (!mouse)
            return;
        if (mouse_moved(mouse, -3) || mouse_moved(mouse, -2))
            code = -3;
        else if (mouse_moved(mouse, -5) || mouse_moved(mouse, -4))
            code = -5;
        else
            return;
        kind = 1;
    }

    if (index % 2)
        index--;
    if (game_FUN_00449c50(game_controls, NULL, index, kind, code, &other)) {
        list_set_text(list, NULL, other, NO_NAME);
        game_FUN_00449c20(game_controls, NULL, controls_device, other, -1, 0);
        if (other < 4) {
            game_FUN_00449c20(game_controls, NULL, controls_device, other + 1, -1, 0);
            list_set_text(list, NULL, other + 1, NO_NAME);
        }
    }
    game_FUN_00449c20(game_controls, NULL, controls_device, index, kind, code);
    game_FUN_0044a2e0(game_controls, NULL, controls_device, index, text);
    list_set_text(list, NULL, index, text);
    if (index < 4) {
        game_FUN_00449c20(game_controls, NULL, controls_device, index + 1, kind, code);
        list_set_text(list, NULL, index + 1, text);
    }
    controls_end_wait(self);
}

/* ---- Garage page ---- */

/*
    The engine's power curve: 11 bands for the bike's engine class, from one of three stock curves
    (the session's) or three of the player's (the game's). The session sets each class's band
    range (0x43C, 0x450), its total power (0x400) and each band's limit (0x324).
*/
#define GARAGE_USED      0x7F58     /* the power the bands take */
#define DDL_CURVES       NAME(0x0057551C)    /* "DDLCurves" */
#define TXT_UNUSED       NAME(0x00575534)    /* "TxtUnused" */
#define HP_UNUSED        NAME(0x00575574)    /* "HPUnused" */
#define SLD_EQ           NAME(0x00575508)    /* "SldEQ" */
#define BANDS            11

#define eq_least(cls)        FIELD(game_session, 0x43C + (cls) * 4, int)
#define eq_most(cls)         FIELD(game_session, 0x450 + (cls) * 4, int)
#define eq_total(cls)        FIELD(game_session, 0x400 + (cls) * 4, int)
#define band_limit(cls, b)   FIELD(game_session, 0x324 + ((cls) * BANDS + (b)) * 4, int)

static int* stock_curve(int cls, int curve)
{
    return (int*) (game_session + 0x68) + BANDS * (cls * 3 + curve);
}

/* The player's curves are 3 to 5 (the original writes 0x1668 with curve - 3: the same place). */
static int* user_curve(int cls, int curve)
{
    return (int*) (game_main + 0x15E4) + BANDS * (cls * 3 + curve);
}

/* The bike's engine class by its size: 0 to 4, those between two sizes counted with the lower. */
static int engine_class(void)
{
    int cc = bike_cc;
    int i;

    if (cc <= engine_sizes[0])
        return 0;
    if (cc >= engine_sizes[4])
        return 4;
    for (i = 1; i < 4; i++) {
        if (cc > engine_sizes[i - 1] && cc < engine_sizes[i + 1])
            return i;
    }
    return 1;
}

/* DDLCurves: the six curves for the bike, the class's chosen selected. */
PORT(0x004B7F70)
void __fastcall garage_fill_curves(BYTE* self)
{
    /* In the original's order: string 0x146A is read into `format` as if it were 0x7F long. */
    struct {
        char format[0x20];
        char line[0x80];
        char size[0x80];
        char stock[0x80];
        char custom[0x80];
    } t;
    BYTE* list;
    int i;

    game_FUN_00526fc0(game_main, NULL, 0x146A, t.format, 0x7F);
    game_FUN_0053a11f(t.size, t.format, engine_sizes[engine_class()]);
    game_FUN_00526fc0(game_main, NULL, 0x146B, t.stock, 0x7F);
    game_FUN_00526fc0(game_main, NULL, 0x146C, t.custom, 0x7F);
    list = combo_list(self, DDL_CURVES);
    list_clear(list);
    for (i = 0; i < 6; i++) {
        if (i < 3)
            game_FUN_0053a11f(t.line, NAME(0x00575540), bike_cc, t.size, t.stock, i + 1);
        else
            game_FUN_0053a11f(t.line, NAME(0x00575540), bike_cc, t.size, t.custom, i - 2);
        list_add_text(list, NULL, t.line, i, 0);                   /* "%dcc (%s) %s #%d" */
    }
    list_select_data(list, NULL, curve_chosen[engine_class()]);
}

/* The bands' sliders and labels; only the player's curves can be changed. */
PORT(0x004B8240)
void __fastcall garage_show_bands(BYTE* self)
{
    int cls = engine_class();
    char name[0x80];
    char text[0x80];
    int curve, band;

    FIELD(self, GARAGE_USED, int) = 0;
    curve = list_data(combo_list(self, DDL_CURVES), NULL, -1);
    control_show(find(self, TXT_UNUSED, 0), NULL, curve >= 3, TRUE);
    control_show(find(self, HP_UNUSED, 0), NULL, curve >= 3, TRUE);
    for (band = 1; band <= BANDS; band++) {
        int power = eq_bands[band - 1];
        BYTE* slider;

        game_FUN_0053a11f(name, NAME(0x0057556C), band);                   /* "Txt%d" */
        game_FUN_0053a11f(text, NAME(0x00575564),                          /* "%2.1fk" */
                          (double) ((band - 1) * eq_step_hz + eq_base_hz) * 0.001f);
        control_set_text(find(self, name, 0), NULL, text);
        game_FUN_0053a11f(name, NAME(0x0057555C), band);                   /* "SldEQ%d" */
        slider = find(self, name, 8);
        FIELD(slider, 0x21C, DWORD) = 1;
        slider_set_steps(slider, NULL, eq_most(cls) - eq_least(cls) + 1);
        slider_set_value(slider, NULL, power - eq_least(cls), eq_most(cls) - eq_least(cls));
        control_enable(slider, curve >= 3);
        FIELD(self, GARAGE_USED, int) += power;
        game_FUN_0053a11f(name, NAME(0x00575554), band);                   /* "HP%d" */
        game_FUN_0053a11f(text, DECIMAL, power);
        control_set_text(find(self, name, 0), NULL, text);
    }
    game_FUN_0053a11f(text, DECIMAL, eq_total(cls) - FIELD(self, GARAGE_USED, int));
    control_set_text(find(self, HP_UNUSED, 0), NULL, text);
}

/*
    A band's slider moved: its power, kept within the band's limit and the power left over, goes
    into the curve chosen - also for a stock curve, whose sliders are only disabled; that writes
    over another class's curves, as in the original.
*/
PORT(0x004B8120)
void __fastcall garage_slide_band(BYTE* self, void* edx_unused, BYTE* slider)
{
    int cls = engine_class();
    int band = FIELD(slider, 0x7C, int);
    int value = slider_value_at(slider, NULL, eq_most(cls) - eq_least(cls)) + eq_least(cls);
    int limit = band_limit(cls, band);
    int most = value < limit ? value : limit;
    int power = eq_bands[band] + (eq_total(cls) - FIELD(self, GARAGE_USED, int));
    int curve;

    (void) edx_unused;
    if (power >= most)
        power = most;
    eq_bands[band] = power;
    curve = list_data(combo_list(self, DDL_CURVES), NULL, -1);
    user_curve(cls, curve)[band] = power;
    garage_show_bands(self);
}

/* The curve arrows step through DDLCurves, round. */
static void curve_step(BYTE* self, int by)
{
    BYTE* list = combo_list(self, DDL_CURVES);
    int count = FIELD(list, 0x1EC, int);

    list_select_all(list, NULL, (list_selected(list) + (by < 0 ? count - 1 : 1)) % count);
    VIRTUAL(list, 0x108, list_moved)(list, NULL, NULL);
}

static void curve_choose(BYTE* self, int curve)
{
    int cls = engine_class();
    int band;

    curve_chosen[cls] = curve;
    for (band = 0; band < BANDS; band++)
        eq_bands[band] = curve < 3 ? stock_curve(cls, curve)[band] : user_curve(cls, curve)[band];
    garage_show_bands(self);
}

static BOOL is_band(const ui_event* event)
{
    const char* name = (const char*) event->b;

    return text_length(name) > 5 && !game_FUN_0054cd80(name, SLD_EQ, 5);
}

/* OptGar.dtm (vtable 0x0055A91C). */
PORT(0x004B7BE0)
void __fastcall garage_page_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    switch (event->type) {
    case EVENT_CLICK:
        if (named(event, NAME(0x00575528)))                                /* CurveLeft */
            curve_step(self, -1);
        else if (named(event, NAME(0x00575510)))                           /* CurveRight */
            curve_step(self, 1);
        break;
    case EVENT_SELECT:
        if (named(event, DDL_CURVES))
            curve_choose(self, list_data((BYTE*) event->c, NULL, -1));
        break;
    case EVENT_SLIDE:
        if (!named(event, DDL_CURVES)) {
            ratchet_play(self);
            if (is_band(event))
                garage_slide_band(self, NULL, (BYTE*) event->c);
        }
        break;
    case EVENT_OPEN:
        FIELD(find(self, TXT_UNUSED, 0), 0x1E8, DWORD) = 1;
        garage_fill_curves(self);
        garage_show_bands(self);
        dialog_notify_controls(self, NULL, 0);
        break;
    case EVENT_SLIDE_END:
    case EVENT_SLIDE_HOLD:
        if (!named(event, DDL_CURVES)) {
            ratchet_stop(self);
            if (is_band(event))
                garage_slide_band(self, NULL, (BYTE*) event->c);
        }
        break;
    }
}

static const char* const suspension_sliders[6] = {
    NAME(0x005755C8), NAME(0x005755B8),     /* SldFrontComp, SldFrontDecomp */
    NAME(0x005755AC), NAME(0x0057559C),     /* SldRearComp, SldRearDecomp */
    NAME(0x0057558C), NAME(0x00575580),     /* SldFrontDamp, SldRearDamp */
};

/* Slot 31: the suspension, 0 to 1 against sliders of 0 to 100 (the value read as unsigned). */
PORT(0x004B84F0)
void __fastcall garage_page_sync(BYTE* self, void* edx_unused, DWORD out)
{
    int i;

    (void) edx_unused;
    for (i = 0; i < 6; i++) {
        if (out) {
            suspension[i] = (float) ((double) (DWORD) slider_get_value(
                                         find(self, suspension_sliders[i], 8)) * 0.01f);
        } else {
            int value = (int) ((double) suspension[i] * 100.0f);

            slider_set_value(find(self, suspension_sliders[i], 8), NULL, value, 100);
        }
    }
}

/* ---- Messages page ---- */

/* OptMess.dtm (vtable 0x0055A9A0): ten chat lines, Message1 to Message9 then Message0. */
PORT(0x004B8720)
void __fastcall messages_page_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    static const char* const lines[10] = {
        NAME(0x005755D8), NAME(0x00575644), NAME(0x00575638), NAME(0x0057562C),
        NAME(0x00575620), NAME(0x00575614), NAME(0x00575608), NAME(0x005755FC),
        NAME(0x005755F0), NAME(0x005755E4),                     /* "Message0" .. "Message9" */
    };
    int i;

    (void) edx_unused;
    if (event->type != EVENT_OPEN)
        return;
    for (i = 1; i <= 10; i++)
        edit_bind(find(self, lines[i % 10], 11), NULL, messages[i % 10], 0x80);
    dialog_notify_controls(self, NULL, 0);
}

/* ---- The options screen ---- */

#define OPT_PAGES        0x7F58     /* BYTE*[6], one open at a time */
#define BUT_RESTORE      NAME(0x005756E4)    /* "Restore" */

static const struct {
    const char* tab;
    UINT        text;
    size_t      size;
    int         line;
    const char* file;
    void*       vtable;
} option_pages[6] = {
    /* GameSettings, OptGame.dtm */
    { NAME(0x00575720), 0x13DD, 0x7F58, 0x40C, NAME(0x00575838), (void*) 0x0055ABB0 },
    /* Graphics, OptGraph.dtm */
    { NAME(0x00575714), 0x13DE, 0x7F58, 0x412, NAME(0x00575828), (void*) 0x0055AB2C },
    /* Sound, OptSound.dtm */
    { NAME(0x0057570C), 0x13DF, 0x7F58, 0x418, NAME(0x00575818), (void*) 0x0055AAA8 },
    /* Controls, OptCont.dtm */
    { NAME(0x00575700), 0x13E0, 0x7F7C, 0x41E, NAME(0x0057580C), (void*) 0x0055AA24 },
    /* Messages, OptMess.dtm */
    { NAME(0x005756F4), 0x13E1, 0x7F58, 0x424, NAME(0x00575800), (void*) 0x0055A9A0 },
    /* Garage, OptGar.dtm */
    { NAME(0x005756EC), 0x13E2, 0x7F5C, 0x42A, NAME(0x005757F4), (void*) 0x0055A91C },
};

static BYTE** options_pages(BYTE* self)
{
    return (BYTE**) (self + OPT_PAGES);
}

#ifdef OPENMCM2_DEV
#define REBORN_PAGE  6              /* after the game's six, its own: the screen has room for six */

/* MCM2:Reborn's own pages, by their tabs: each a page of rows (reborn_rows, found by name). */
static const struct { const char* tab; const char* file; } own_pages[] = {
    { "Reborn", "OptReborn.dtm" },
    { "Gamepad", "OptPad.dtm" },
};
#define OWN_PAGES ((int) (sizeof own_pages / sizeof own_pages[0]))

static void reborn_page_close(void)
{
    if (g_reborn_page) {
        object_destroy(g_reborn_page);
        g_reborn_page = NULL;
        g_reborn_page_shown = -1;
    }
}
#endif

/* Show `page` (0 to 5; in the dev build 6, REBORN), closing the others; one already open stays. */
PORT(0x004B90B0)
void __fastcall options_show_page(BYTE* self, void* edx_unused, int page)
{
    RECT area = { 20, 35, 620, 395 };
    BYTE** pages = options_pages(self);
    int i;

    (void) edx_unused;
    for (i = 0; i < 6; i++) {
        if (pages[i] && page != i) {
            object_destroy(pages[i]);
            pages[i] = NULL;
        }
    }
#ifdef OPENMCM2_DEV
    if (page != g_reborn_page_shown)
        reborn_page_close();
    if (page >= REBORN_PAGE && page < REBORN_PAGE + OWN_PAGES && !g_reborn_page) {
        g_reborn_page = dialog_new(0x7F58, __LINE__, own_pages[page - REBORN_PAGE].file, reborn_page_vtable());
        g_reborn_page_shown = page;
        gui_manager_open(session_gui, NULL, g_reborn_page, 0, 1, (DWORD) &area, self, 0, 0, 1);
    }
#endif
    if ((unsigned) page > 5 || pages[page])
        return;
    pages[page] = dialog_new(option_pages[page].size, option_pages[page].line,
                             option_pages[page].file, option_pages[page].vtable);
    gui_manager_open(session_gui, NULL, pages[page], 0, 1, (DWORD) &area, self, 0, 0, 1);
}

static void settings_take(void)
{
    __movsd(garage_settings, (unsigned long*) (game_main + 0x1550), 0x17);
    __movsd(game_settings, (unsigned long*) (game_main + 0xC24), 0xDD);
    __movsd(sound_settings, (unsigned long*) (game_main + 0xF98), 0xB);
    __movsd(video_settings, (unsigned long*) (game_main + 0xFC4), 7);
    __movsd(control_settings, (unsigned long*) (game_main + 0xFE0), 0xC);
    __movsd((unsigned long*) messages, (unsigned long*) (game_main + 0x19D4), 0x140);
}

static void settings_give_back(void)
{
    __movsd((unsigned long*) (game_main + 0x1550), garage_settings, 0x17);
    __movsd((unsigned long*) (game_main + 0xC24), game_settings, 0xDD);
    __movsd((unsigned long*) (game_main + 0xF98), sound_settings, 0xB);
    __movsd((unsigned long*) (game_main + 0xFC4), video_settings, 7);
    __movsd((unsigned long*) (game_main + 0xFE0), control_settings, 0xC);
    __movsd((unsigned long*) (game_main + 0x19D4), (unsigned long*) messages, 0x140);
}

/* The tabs worded, and the page asked for (param 1 controls, 2 garage, else game) shown. */
static void options_open(BYTE* self, const ui_event* event)
{
    BYTE** pages = options_pages(self);
    int page, i;

    settings_take();
    for (i = 0; i < 6; i++)
        pages[i] = NULL;
    for (i = 0; i < 6; i++) {
        BYTE* tab = find(self, option_pages[i].tab, 4);

#ifdef OPENMCM2_DEV
        if (menu_reborn(self))                  /* its tabs' words are in their pictures */
            break;
#endif
        option_load_text(tab, NULL, 0, game_language, option_pages[i].text);
        option_load_text(tab, NULL, 1, game_language, option_pages[i].text);
        control_set_align(tab, NULL, 0x22);
    }
    page = event->param == 1 ? 3 : event->param == 2 ? 5 : 0;
#ifdef OPENMCM2_DEV
    if (event->param == 1 && dev_settings.menus_modern)
        page = REBORN_PAGE + 1;           /* the controller shortcut opens modern GAMEPAD */
#endif
    options_show_page(self, NULL, page);
#ifdef OPENMCM2_DEV
    if (page >= REBORN_PAGE)
        radio_set(find(self, own_pages[page - REBORN_PAGE].tab, 4), NULL, 0);
    else
#endif
        radio_set(find(self, option_pages[page].tab, 4), NULL, 0);
    if (game_FUN_00527300(game_main))
        control_show(find(self, BUT_RESTORE, 0), NULL, FALSE, TRUE);
#ifdef OPENMCM2_DEV
    if (race_menu_up) {
        put_away(find(self, BUT_RESTORE, 0), TRUE);
        put_away(find(self, HELP, 0), TRUE);
        put_away(find(self, option_pages[5].tab, 4), TRUE);
    }
#endif
}

static void options_click(BYTE* self, ui_event* event)
{
    BYTE** pages = options_pages(self);
    int i;

#ifdef OPENMCM2_DEV
    if (race_menu_up && (named(event, BUT_RESTORE) || named(event, HELP) || named(event, option_pages[5].tab)))
        return;
#endif
    if (named(event, BACK)) {
        for (i = 0; i < 6; i++) {
            if (pages[i]) {
                object_destroy(pages[i]);
                pages[i] = NULL;
            }
        }
#ifdef OPENMCM2_DEV
        reborn_page_close();
#endif
        dialog_end((BYTE*) event->sender, NULL, 0);
        event->e = 1;
        return;
    }
    for (i = 0; i < 6; i++) {
        if (named(event, option_pages[i].tab)) {
            options_show_page(self, NULL, i);
            return;
        }
    }
#ifdef OPENMCM2_DEV
    for (i = 0; i < OWN_PAGES; i++) {
        if (named(event, own_pages[i].tab)) {
            options_show_page(self, NULL, REBORN_PAGE + i);
            return;
        }
    }
#endif
    if (named(event, BUT_RESTORE)) {
        box_open(event->manager, self,
                 dialog_new(0x7F58, 0x3B5, NAME(0x005756B0), (void*) 0x0055A898)); /* GlobSet.dtm */
    } else if (named(event, HELP)) {
        profile_open_help(game_profile, NULL, HELP_TOPIC, 0);
    } else if (named(event, NAME(0x005756A4))) {                                    /* Advanced */
        if (pages[1])
            dialog_notify_controls(pages[1], NULL, 1);
        box_open(event->manager, self,
                 dialog_new(0x7F58, 0x3BD, NAME(0x00575698), (void*) 0x0055A814)); /* OptAdvG.dtm */
    }
}

/* Restore ended with 0x51: the pages show the restored settings; the advanced dialog's 0x52. */
static void options_child_ended(BYTE* self, const ui_event* event)
{
    BYTE** pages = options_pages(self);
    int i;

    if (event->a == 0x51) {
        settings_take();
        for (i = 0; i < 6; i++) {
            if (!pages[i])
                continue;
            dialog_notify_controls(pages[i], NULL, 0);
            if (i == 2) {
                sound_apply(pages[i]);
            } else if (i == 5) {
                garage_fill_curves(pages[i]);
                garage_show_bands(pages[i]);
            }
        }
        for (i = 0; i < FIELD(game_session, 0x54, int); i++) {
            if (FIELD(FIELD(game_session, 0x50, BYTE*) + i * BIKE_SIZE, 0x8C, int) == 0xFA) {
                FIELD(game_main, 0x1FB0, int) = i;
                break;
            }
        }
        dialog_tell_parent(self, NULL, 0x51, 9);
    } else if (event->a == 0x52) {
        if (pages[1])
            dialog_notify_controls(pages[1], NULL, 0);
    }
}

/* The options screen (vtable 0x00556BD0): the settings go back to the game when it closes. */
PORT(0x004B8880)
void __fastcall options_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    BYTE* parent;

    (void) edx_unused;
    switch (event->type) {
    case EVENT_CLICK:
        options_click(self, event);
        break;
    case EVENT_OPEN:
        options_open(self, event);
        break;
    case EVENT_CLOSE:
        settings_give_back();
        FIELD(self, 0x30, gui_manager*)->unknown_ec = ui_anims;
        parent = FIELD(self, 0x2C, BYTE*);
        if (parent)
            FIELD(parent, 0xC8, int) = ui_anims;
        break;
    case EVENT_CHILD_ENDED:
        options_child_ended(self, event);
        break;
    }
}

/* ---- Restore ---- */

#define BUT_FACTORY      NAME(0x00575874)    /* "ButFactory" */
#define BUT_COPY_USER    NAME(0x00575868)    /* "ButCopyUser" */

/* LstProfiles: the other players' profiles; with none, ButCopyUser is greyed. */
PORT(0x004B97A0)
void __fastcall restore_fill_profiles(BYTE* self)
{
    BYTE* list = find(self, LIST_PROFILES, 0);
    char name[0x104];
    BYTE* button;
    int count = 0;

    list_clear(list);
    dir_list_set_folder(game_files, NULL, PROFILE_FOLDER);
    dir_list_set_pattern(game_files, NULL, PROFILE_FILES, 0);
    VIRTUAL(game_files, 4, method_0)(game_files);
    dir_list_current_title(game_files, NULL, name);
    if (name[0] && game_FUN_0053b910(game_profile, name)) {    /* inlined strcmp in the original */
        list_add_text(list, NULL, name, 0, 0);
        count = 1;
    }
    while (dir_list_next_title(game_files, NULL, name)) {
        if (name[0] && game_FUN_0053b910(game_profile, name)) {
            list_add_text(list, NULL, name, 0, 0);
            count++;
        }
    }
    list_sort(list, NULL, TRUE);
    list_set_text_colour(list, NULL, PROFILE_COLOUR, -1);
    list_set_208(list, NULL, 0xFFFFFF);
    list_set_colour_2(list, NULL, PROFILE_COLOUR);
    if (count) {
        list_select_text(list, NULL, game_profile);     /* never listed: as the original */
        return;
    }
    button = find(self, BUT_COPY_USER, 4);
    control_enable(button, FALSE);
    control_set_colour(button, NULL, 0x757575);
}

/* Confirmed: the factory settings and controls (1), or another player's settings (2). */
static void restore_apply(BYTE* self)
{
    int from = radio_number(find(self, BUT_FACTORY, 4));
    char own[0x80];
    char controls[0x104];
    char preset[0x104];

    text_copy(own, game_profile);
    if (from == 1) {
        profile_defaults(game_profile);
        /* ui\preset.ctl */
        game_FUN_0053a11f(preset, NAME(0x0056C7DC), NAME(0x0056FE48), NAME(0x0056FE4C));
        game_FUN_0053a11f(controls, PATH_3, PROFILE_FOLDER, own, CONTROL_FILE);
        CopyFileA(preset, controls, FALSE);
        profile_load_controls(game_profile);
        profile_save(game_profile);
        game_FUN_00449e20(game_controls, NULL, controls, -1);
    } else if (from == 2) {
        BYTE* list = find(self, LIST_PROFILES, 3);
        int index = list_selected(list);

        text_copy(game_profile, FIELD(FIELD(list, 0x214, BYTE*) + index * 0x38, 0x14, const char*));
        profile_load(game_profile);
    }
    text_copy(game_profile, own);
    dialog_end(self, NULL, 0x51);
}

/* GlobSet.dtm (vtable 0x0055A898): ButLoad asks first (restore_confirm_handle). */
PORT(0x004B9470)
void __fastcall restore_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    switch (event->type) {
    case EVENT_CLICK:
        if (named(event, BUT_FACTORY))
            control_show(find(self, LIST_PROFILES, 3), NULL, FALSE, TRUE);
        else if (named(event, BUT_COPY_USER))
            control_show(find(self, LIST_PROFILES, 3), NULL, TRUE, TRUE);
        else if (named(event, NAME(0x0056F83C)))                           /* ButCancel */
            dialog_end(self, NULL, 0);
        else if (named(event, NAME(0x0056F834)))                           /* ButLoad */
            box_open(FIELD(self, 0x30, gui_manager*), self,
                     dialog_new(0x7F58, 0x445, MESSAGE_BOX, (void*) 0x0055AC34));
        break;
    case EVENT_OPEN:
        restore_fill_profiles(self);
        control_show(find(self, LIST_PROFILES, 3), NULL, FALSE, TRUE);
        break;
    case EVENT_CHILD_ENDED:
        if (event->a == 0x60)
            restore_apply(self);
        else if (event->a == 0x61)
            dialog_end(self, NULL, 0);
        break;
    }
}

#define BUTTON_LEFT   NAME(0x0056F888)      /* "ButLeft" */
#define BUTTON_MIDDLE NAME(0x0056F87C)      /* "ButMiddle" */
#define BUTTON_RIGHT  NAME(0x0056F870)      /* "ButRight" */

/* messbox2.dtm (vtable 0x0055AC34): left ends with 0x60 (restore), right with 0x61. */
PORT(0x004B9980)
void __fastcall restore_confirm_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    BYTE* prompt;

    (void) edx_unused;
    if (event->type == EVENT_CLICK) {
        if (named(event, BUTTON_LEFT)) {
            dialog_end((BYTE*) event->sender, NULL, 0x60);
            event->e = 1;
        } else if (named(event, BUTTON_RIGHT)) {
            dialog_end((BYTE*) event->sender, NULL, 0x61);
            event->e = 1;
        }
    } else if (event->type == EVENT_OPEN) {
        control_load_text(find(self, BUTTON_LEFT, 0), NULL, game_language, 0x13E9);
        control_load_text(find(self, BUTTON_RIGHT, 0), NULL, game_language, 0x13DC);
        control_show(find(self, BUTTON_MIDDLE, 1), NULL, FALSE, TRUE);
        control_load_text(find(self, NAME(0x0056F864), 12), NULL, game_language, /* TitleText */
                          0x14C9);
        prompt = find(self, NAME(0x0056F858), 12);                              /* TxtPrompt */
        FIELD(prompt, 0x1E8, DWORD) = 1;
        control_load_text(prompt, NULL, game_language, 0x14CA);
    }
}

/* ---- The unit vectors, set up by the C runtime's start-up ---- */

static void vector_set(float* v, float x, float y, float z)
{
    v[0] = x;
    v[1] = y;
    v[2] = z;
}

PORT(0x004BA070)
void __cdecl option_vector_zero_init(void)
{
    vector_set((float*) 0x0068E8C8, 0.0f, 0.0f, 0.0f);
}

PORT(0x004BA0C0)
void __cdecl option_vector_x_init(void)
{
    vector_set((float*) 0x0068E908, 1.0f, 0.0f, 0.0f);
}

PORT(0x004BA110)
void __cdecl option_vector_y_init(void)
{
    vector_set((float*) 0x0068E938, 0.0f, 1.0f, 0.0f);
}

PORT(0x004BA160)
void __cdecl option_vector_z_init(void)
{
    vector_set((float*) 0x0068E8B8, 0.0f, 0.0f, 1.0f);
}

/* The start-up table points at these jumps to them. */
PORT(0x004BA060)
void __cdecl option_vector_zero_init_entry(void)
{
    option_vector_zero_init();
}

PORT(0x004BA0B0)
void __cdecl option_vector_x_init_entry(void)
{
    option_vector_x_init();
}

PORT(0x004BA100)
void __cdecl option_vector_y_init_entry(void)
{
    option_vector_y_init();
}

PORT(0x004BA150)
void __cdecl option_vector_z_init_entry(void)
{
    option_vector_z_init();
}
