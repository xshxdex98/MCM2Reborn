/*
    OpenMCM2 dev build - debugging tools and changes to the game.

    Only the dev build (OPENMCM2_DEV, the dev branch) compiles this. Its switches come from the
    settings file beside the exe, named after it (OpenMCM2-Dev.ini, or MCM2Reborn.ini for a release);
    each is off unless the file turns it on, so the dev build plays as the normal one until asked
    otherwise.

        [trace]           what goes to openmcm2-dev.log
        screens=1         every menu screen opened, by number
        events=1          every dialog event the ported handlers see
        files=1           every file the profile code looks for
        camera=1          the rider camera's readings, twice a second
        input=1           each input device's reads, once a second (inputfix.c)
        physics=1         each bike's physics step, to physics-record.csv (physics.c)
        ai=1              the first computer rider each frame, to ai-record.csv: its steps, the
                          places drawn and stepped, its commands and wheels (bikeai.c)
        display=1         every display mode set and every surface made that is not a
                          texture, with the address of the game code that asked

        [menus]
        widescreen=1      the menus at the display mode the races use, their 640x480
                          scaled evenly to fit and centred, the backgrounds filling the screen
                          (experimental: the start of the modern menus)
        fill_sides=1      a picture narrower than the screen, such as the 640x480 menus, gets
                          a blurred, darkened copy of itself behind its side bars
        modern=1          the modern menus: widescreen, and each screen's layout taken from the
                          folder's modern\ when one is there (ui\modern\mainmenu.dat, or a loose
                          mainmenu.dtm), else the original's, scaled
        live_background=1 the main menu over a replay - the newest in Record, else the last
                          race's tape - its sound off, looping; a button that leaves the main
                          menu stops it and goes on in the plain menus (a first version)

        [graphics]        the Graphics page's own settings, which it writes here
        vsync=1           present on vertical blank (off, frames are still held to the refresh)
        antialiasing=4    samples: 0, 2, 4 or 8
        filtering=4       0 the game's, 1 trilinear, 2 to 4 anisotropic at 4, 8 and 16 times
        frame_limit=0     frames a second at most, 0 the display's refresh rate
        fxaa=0            1 smooths the picture's edges once drawn (post.c)
        sharpen=0         percent: how much the picture's detail is sharpened
        vibrance=0        percent: how much its dull colours are deepened
        brightness=100    percent, 100 as the game drew it

        [fixes]           the original's bugs, mended
        last_pick=1       the computer riders' random picks can land on the last bike or rider
        ai_rate=30        the computer riders take 30 steps a second, whatever the frame rate,
                          drawn smoothly between them, as the game moved them at 30 frames a
                          second (0 the game's one a frame, which shook them over 60; bikeai.c)

        [gamepad]         the GAMEPAD page's: XInput pads (pad.c)
        xinput=1          the first XInput pad read
        response=20       its stick's steering curve, in tenths: 10 straight, 15, 20, 30 the
                          game's own cube (which the game gives its joysticks)
        deadzone=15       percent of a stick's travel, round its middle
        sensitivity=90    percent of full lock the stick's whole travel steers
        smoothing_ms=60   the stick's steering eased toward it over about so many milliseconds,
                          its jitter taken out (0 as the stick is)
        lean_stick=0      0 the left stick leans, 1 the right
        directinput_pads_analog=1  a DirectInput pad (a DualSense) steers by its axes
        accelerate=8 ...  each action's pad control, by enum pad_control (0 none)

        [online]
        presence=1        counted as playing while the game runs, for the launcher's count of
                          the players online (Epic's lobbies, eosplay.c); 0 not
        packets=20        each bike's state sent that many times a second in a network race (the
                          game's 5 online, 15 on a LAN), in full each second; 0 the game's

    A change to the game goes in the ported function it changes, between #ifdef OPENMCM2_DEV and
    #endif, reading its switch from dev_settings; the code outside stays the faithful port.
*/

#pragma once

#include "runtime.h"

/* The game's name in the modern menus, the window's title and wherever else it is shown. */
#define GAME_TITLE "MCM2:Reborn"

/* Its version, as the menus (tools/modern/reborn_kit.py, VERSION) and the launcher show it. */
#define GAME_VERSION "0.4.0.1"

typedef struct dev_switches {
    BOOL trace_screens;
    BOOL trace_events;
    BOOL trace_files;
    BOOL trace_display;
    BOOL trace_camera;
    BOOL trace_input;
    BOOL trace_physics;
    BOOL trace_ai;
    BOOL menus_widescreen;
    BOOL menus_fill_sides;
    BOOL menus_modern;
    BOOL menus_live;
    BOOL fix_last_pick;
    int  fix_ai_rate;
    BOOL online_presence;
    int  online_rate;
} dev_switches;

extern dev_switches dev_settings;

/* Read the settings; start-up calls this before the game runs. */
void dev_install(void);

/* The renderer's settings (gl.h, g_render), written to [graphics]. */
void dev_graphics_save(void);

/* The [fixes] the REBORN page sets (the computer riders' steps a second), written back. */
void dev_fixes_save(void);

/*
    The first-person view's settings (camera.c), on the Graphics page, kept in [camera] as whole
    numbers: the field of view in degrees across; tilt (how much of the bike's lean the horizon
    takes, 0 to 1) and pitch (0 to 1.5: 15 degrees down and 20 more a whole one), in hundredths;
    corner anticipation 0 off, 1 by the lean; and how far the lean turns the view at most, in
    tenths of a degree.
*/
typedef struct camera_options {
    int fov;
    int tilt;
    int pitch;
    int corner;
    int lean_heading;
} camera_options;

extern camera_options g_camera_options;

/*
    Each of MCM2:Reborn's changes to how the race looks can be turned back to the original's, on
    the Graphics page, kept in [reborn]: 1 Reborn's, 0 the game's own. The HUD (hud.c, and the
    original's overlays drawn again, overlays.c) and the first-person view (camera.c); on the Game
    page the bike's physics the same at any frame rate (steptime.c). `gear` 1 offers every rider
    look outside the Pro Circuit too (menus.h, rider_offered).
*/
typedef struct reborn_options {
    int hud;
    int cameras;
    int physics;
    int riders;                 /* on the gate in a single race: 11, the game's, to 30 (riders.c) */
    int gear;
} reborn_options;

extern reborn_options g_reborn;

void dev_reborn_save(void);

void dev_camera_save(void);

/* [trace] camera=1: every view and projection the game hands the renderer, into the camera's
   frame-by-frame record (camera.c, camera-record.csv). `type` is D3DTRANSFORMSTATETYPE's. */
void camera_record_transform(int type, const float* matrix);

/* A line in the log, prefixed with its area ("screen", "event", ...). */
void dev_trace(const char* area, const char* fmt, ...);

/* Milliseconds since the clock was first read (dev.c). */
double dev_milliseconds(void);

/* The mouse wheel moves a bike preview's camera (at 0x7F70 of its page) in or out: TRUE when
   it did, for the page to place its camera again (dlgprocs.c). */
BOOL preview_zoom(unsigned char* page);

/* The mouse wheel's notches turned since the last frame, away from you positive: the same to
   every caller in a frame. `user` is the gui_user, `object` anything on the screen (ui.c). */
int menu_wheel(void* user, void* object);

/* Whether a dialog's layout is MCM2:Reborn's, which draws its words in its pictures (ui.c). */
BOOL menu_reborn(unsigned char* dialog);

/* A MCM2:Reborn screen with pages: the title of page `page` shown (dlgprocs.c). */
void page_title_show(unsigned char* screen, int page);

/* Whether a modern screen has the photos under it (ui.c). */
BOOL menu_photos_shown(void);

/* A rectangle on the 640x480 menu screen, where the scaled menus put it (ui.c). */
void menu_place_view(unsigned char* dialog, RECT* view);

/* An event a control is about to send its dialog (when events are traced). */
void dev_trace_event(DWORD type, const char* control, const void* dialog);

/*
    The live background ([menus] live_background), in dlgprocs.c. live_menu_start starts it
    instead of opening the main menu (TRUE when it did); live_menu_opening sees each dialog
    opened, for the race's own; live_menu_tick runs each frame.
*/
BOOL live_menu_start(void);
void live_menu_opening(BYTE* dialog);
void live_menu_tick(void);

/*
    Online play through Epic Online Services (eosplay.c). eos_install takes over the game's
    CoCreateInstance at start-up; eos_online makes the network again, its DirectPlay EOS's own
    or the real one. FALSE when the network could not be made, or EOS's helper not started.
*/
void eos_install(void);
BOOL eos_online(BOOL on);
/* 1 signed in to EOS, 0 signing in, -1 not (the helper did not start, or failed). */
int  eos_state(void);

/* What a lobby found shows: its race is the host's settings, kind -1 until it has published them. */
typedef struct online_lobby {
    int  players, most;
    int  kind, laps, trees;                 /* race_kind's numbers (menus.h) */
    BOOL closed;                            /* racing: joining is closed */
    char host[32];
    char track[48];
    char riders[30][32];                    /* the host's first; empty ones are open */
    struct { char name[24], text[76]; } chat[4];    /* the host's lobby chat, the oldest first */
} online_lobby;

/* The lobby a session's instance GUID stands for: FALSE when it is not one of EOS's. */
BOOL eos_lobby(const GUID* instance, online_lobby* lobby);

/* A line of the lobby's chat as the game shows it: hosting, it goes on the lobby for searches. */
void eos_chat(const char* name, const char* text);

/* Seconds since the lobbies were last found, or -1 before they ever were. */
int  eos_found_ago(void);

/* The game's race overlays had their draw pass this frame: the race is on the screen (hud.c). */
void hud_overlays_drawn(void);

/* DirectInput's devices watched, so that what was held when the window lost the focus is let go
   when it comes back (inputfix.c). */
void inputfix_install(void);

/* The computer riders' animation run once a frame, not in their fixed steps (bikeai.c). */
void ai_install(void);

/* A jump to `to` written over the `size` bytes at `at` (5 to 16), the rest nops; FALSE, and nothing
   written, unless those bytes are still `was`, the game's. */
BOOL jump_over(DWORD at, const BYTE* was, int size, void (*to)(void));

/* The terrain grid's cells kept inside it (gridfix.c). */
void gridfix_install(void);

/* The physics step's per-step constants taken by the step's length (steptime.c). */
void steptime_install(void);

/*
    Game pads through XInput ([gamepad], the GAMEPAD page; pad.c): the first connected steers
    with its left stick and leans with the left or right; each action of the game's may have one
    of its controls. Its stick's response also stands for the game's DirectInput joysticks.
*/
enum pad_control {
    PAD_NONE, PAD_A, PAD_B, PAD_X, PAD_Y, PAD_LB, PAD_RB, PAD_LT, PAD_RT, PAD_BACK, PAD_START,
    PAD_LS, PAD_RS, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT,
    PAD_LEFT_UP, PAD_LEFT_DOWN, PAD_LEFT_LEFT, PAD_LEFT_RIGHT,
    PAD_RIGHT_UP, PAD_RIGHT_DOWN, PAD_RIGHT_LEFT, PAD_RIGHT_RIGHT,
    PAD_CONTROLS
};

#define PAD_SENSITIVITY_LEAST 20
#define PAD_SENSITIVITY_MOST 100          /* pad.c's steering is not clamped after it */
#define PAD_ACTIONS 14                  /* the game's: steer left, right, lean forward, back, ... */

typedef struct pad_options {
    int on;                             /* XInput pads read */
    int response;                       /* a stick's curve in tenths: 10, 15, 20, or 30 the game's */
    int deadzone;                       /* percent of a stick's travel, round its middle */
    int lean_stick;                     /* 0 the left stick leans, 1 the right */
    int analog_di;                      /* DirectInput game pads (a DualSense) steer by their axes */
    int bind[PAD_ACTIONS];              /* each action's control (pad_control); 0..3 are the sticks */
    int sensitivity;                    /* percent of full lock at the stick's edge, 20 to 100 */
    int smoothing;                      /* ms the stick's steering is eased over, 0 none */
} pad_options;

extern pad_options g_pad;

/* The pad's steering and lean, -1..1 as the game's keys make them (-1 left, -1 forward): FALSE
   with no pad. */
BOOL  pad_sticks(float* steer, float* lean);
/* The game's action `action` held on the pad. */
BOOL  pad_held(int action);
BOOL  pad_start_down(void);
BOOL  pad_back_down(void);
BOOL  pad_accept_down(void);
int   pad_menu_direction(void);
int   pad_menu_tab(void);
BOOL  pad_connected(void);
BOOL  pad_binding_active(void);
BOOL  pad_menus_active(void);
extern BOOL pad_navigation_active;
void pad_menu_tick(void* manager);
BOOL pad_close_dropdown(void* control);
BOOL pad_menu_back(void);
BOOL pad_back_from_dialog(void* dialog);
extern int g_track_source[6];
extern int g_track_counts[6][3];
BOOL track_counts_refresh(void);
BOOL track_is_stock(int kind, const char* stem);
BOOL  input_is_xinput(void* device);
BOOL  input_in_foreground(void);
/* The first control down that an action may be given, or PAD_NONE (the GAMEPAD page's binding). */
int   pad_pressed(void);
/* A stick's steering through the chosen response and sensitivity. */
float pad_curve(float v);
/* An analog stick's steering, each frame, eased toward the stick over g_pad.smoothing ms. */
float pad_smooth(float v);
void  dev_pad_save(void);
/* Alt+F4 and Alt+Tab out of the game whatever it is doing (escape.c). */
void  escape_install(void);
/* Whether the game runs under Wine or Proton (ntdll exports wine_get_version). */
BOOL  dev_under_wine(void);
/* The rider named in the launcher at its install ([launcher] rider), for the game's first profile,
   or NULL; and that it has been made, so it is not made again. */
const char* dev_first_rider(void);
void  dev_first_rider_done(void);
/* Whether the modern menus are on and UI\modern has `file` (a screen's own layout). */
BOOL  dev_modern_has(const char* file);
/* The settings file beside the game (OpenMCM2-Dev.ini, MCM2Reborn.ini for a release). */
const char* dev_settings_file(void);

/*
    The lap leaderboards (leaders.c): each rider's best lap from the second on, by track, event
    kind (race_kind) and bike class (cc), here and on [leaderboard] url's server.
*/
typedef struct leader {
    char name[16];
    int  ms;
    char date[11];
} leader;

extern char g_leaders_url[256];

/* Everyone's boards (tools/leaderboard-worker); [leaderboard] url another, "none" no server. */
#define LEADERS_URL "https://mcm2reborn-leaderboards.mcm2reborn.workers.dev"

#define BOARD_MOST          100     /* riders a board shows */
#define TRACKS_FROM_SERVER  256

enum { LEADERS_LOCAL, LEADERS_LOADING, LEADERS_ONLINE, LEADERS_OFFLINE };   /* where a board is from */

/* Each frame of a race, the HUD on or not: a lap finished from the second on kept when it is
   the rider's best. */
void leaders_watch(void);
/* The rider's best lap on the race's board in ms, kept up as it is beaten: 0 before there is
   one, -1 for a race without a board (leaders_watch not on it). */
int  leaders_best(void);
/* A board, best first: the server's once it has answered (*state LEADERS_ONLINE; asked for by a
   thread, and again when its answer is old), else this computer's. */
int  leaders_board(int kind, int cc, const char* track, leader* board, int most, int* state);
/* The last race's board: its track into `track` (64), its bike class returned. */
int  leaders_race(char* track);
/* The row of the board that is the chosen profile's, or -1. */
int  leaders_mine(const leader* board, int n);
/* Into `tracks` after the `n` there, those with laps here or on the server; the count. */
int  leaders_tracks(int kind, int cc, char (*tracks)[64], int n, int most);
/* Changes when an answer from the server is in: the screen shows it again. */
LONG leaders_version(void);
/* The HUD's word on the last lap, if it is new enough to show: its tone (NOTICE_NONE when there
   is none), its words ("LABEL<TAB>detail") and how long ago it was said, in ms. */
enum { NOTICE_NONE, NOTICE_BEST, NOTICE_GOOD, NOTICE_BAD };
int  leaders_notice(char* words, int size, DWORD* age);
/* An event kind's tracks in the game (a Baja or enduro one a route each), as the boards name them. */
int  track_names(int kind, char (*names)[64], int most);
/* The board name of `file` (with `route` for a Baja or enduro one): FALSE when the game has no such track. */
BOOL track_board_name(int kind, const char* file, int route, char* name);

/*
    Files from the other players in a multiplayer lobby (code/mcm2/share.c): the race's track from
    the host, riders' own skins from their riders. share_take answers the lobby's messages that are
    its own; share_tick, each tick of the lobby, says SHARE_NEWS when `news` is for the chat and
    SHARE_TRACK_IN when the race's track has just been downloaded.
*/
enum { SHARE_TRACK = 1, SHARE_RIDER, SHARE_BIKE };
enum { SHARE_NEWS = 1, SHARE_TRACK_IN };

BOOL share_have(int kind, const char* name);
BOOL share_is_skin(const char* look);
void share_get(int kind, const char* name, int from);
BOOL share_take(DWORD type, const BYTE* data, int from);
int  share_tick(char* news);

/* Room for more riders in a race (riders.c); the most a single race may have. */
void riders_install(void);
int  riders_most(void);

/* A multiplayer race's riders, 0xF8 bytes each, and its start grid, 8 bytes a place (game_main +
   0x215C and + 0x2EBC in the game; riders.c). */
BYTE* entrants(void);
BYTE* start_grid(void);

/*
    The game's movies (video.c): never the Indeo 5 originals, but the MPEG-1 copy the launcher makes
    of each with ffmpeg (ui\Intro.mpg beside ui\Intro.avi), on every platform. `movie` is the UIMovie
    control (ui.c) playing it; `ended` and `user` are what the game gives a movie for when it ends.
    video_open is FALSE when there is no copy, and the game goes on as if the movie had ended.
*/
BOOL  video_open(void* movie, const char* file, BOOL play, DWORD ended, DWORD user);
void  video_close(void* movie);
void  video_tick(void* movie);
BOOL  video_size(void* movie, LONG* width, LONG* height);
void* video_picture(void* movie);       /* the IDirectDrawSurface7 its frames are in, or NULL */
void  video_play(void* movie);
void  video_pause(void* movie, BOOL paused);
void  video_stop(void* movie);
