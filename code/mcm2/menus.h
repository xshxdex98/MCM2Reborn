/*
    OpenMCM2 - what the menus' screens share: the race settings and the names of their controls,
    the single-player pages' layout, and the small helpers every screen uses.

    The screens are ported in dlgprocs.c, procircuitprocs.c and selectgamepicprocs.c; this is
    what they would have shared through the original's headers.
*/

#pragma once

#include "gui.h"
#ifdef OPENMCM2_DEV
#include "../dev/dev.h"
#endif

/* The controls and lists the helpers use, ported in ui.c. */
BYTE* __fastcall dialog_find_control(BYTE* self, void* edx_unused, const char* name, int type);
void  __fastcall control_show(BYTE* self, void* edx_unused, DWORD shown, BOOL animate);
void  __fastcall list_clear(BYTE* self);
BOOL  __fastcall list_add_text(BYTE* self, void* edx_unused, const char* text, DWORD data,
                               DWORD b);
int   __fastcall list_selected(BYTE* self);
int   __fastcall list_select_all(BYTE* self, void* edx_unused, int index);

/* The game's: _stricmp, sprintf, and a string from its language module. */
int  __cdecl    game_FUN_0053b6e0(const char* a, const char* b);
int  __cdecl    game_FUN_0053a11f(char* to, const char* format, ...);
BOOL __fastcall game_FUN_00526fc0(BYTE* game, void* edx_unused, UINT id, char* text, int size);

#define COMBO_LIST 0x1FC     /* a combo box's list */
#define game_profile ((char*) game_main + 0x578)    /* char[16]: the player's profile */
#define game_language FIELD(game_main, 0x420, HINSTANCE)
#define game_session FIELD(game_main, 0x56C, BYTE*)       /* the menus' session */
#define NAME(address) ((const char*) (address))
#define session_gui     FIELD(game_session, 0x2C, gui_manager*)
#define session_players FIELD(game_session, 0x48, BYTE*)    /* 200 bytes each */
#define player_model(player) FIELD(FIELD(player, 0xC0, BYTE*), 0x1A0, BYTE*)
#define BIKE_SIZE   0x94
#define RIDER_SIZE  200
#define RIDER_TEXT_LENGTH 0x3F

/*
    The race settings the menus edit, 0x7B dwords at the game's 0x2D70. They, the bike's
    (0x1550), the rider's (0x1EEC) and 0x60C have copies (0x2F5C, 0x15AC, 0x1FB4, 0x610) that
    the single-player screen opens with and that Back and Start keep.
*/
#define race_settings   (game_main + 0x2D70)
/* race_mode is RaceModeDropDown's (2 a series, 4 a ghost race); race_kind 0 quarry, 1 Baja,
   2 national, 3 SX, 4 tag, 5 enduro. */
#define race_mode       FIELD(game_main, 0x2D70, int)
#define race_kind       FIELD(game_main, 0x2D74, int)
#define race_round      FIELD(game_main, 0x2D7C, int)
#define race_events     FIELD(game_main, 0x570, BYTE*)
#define series_started  FIELD(game_main, 0x1664, int)
#define record_race     FIELD(game_main, 0x3334, int)
#define SERIES          2
#define GHOST_RACE      4

#define SET_MODE        0x00
#define SET_EVENT       0x04
#define SET_RACES       0x0C
#define SET_TREES       0x10    /* tree collision */
#define SET_LAPS        0x20
#define SET_OPPONENTS   0x24
#define SET_28          0x28    /* cleared by Start */
#define SET_RANDOM_GATES 0x2C   /* BYTE */
#define SET_GATES       0x2D    /* BYTE */
#define SET_SEED        0x30
#define SET_TRACK_KIND  0x34    /* BYTE */
#define SET_TRACK       0x36    /* char[0x100] */
#define SET_TIME        0x140   /* float: the laps list's value for the timed events (0 and 4) */
#define TRACK_ENTRY     0x54    /* the session's tracks (0x60): kind 0x4, id 0x8, name 0x14 */
#define SEED_MAX        999
#define SP_EVENT_TYPE       NAME(0x0056FA84)    /* "EventTypeDropDown" */
#define SP_RACE_MODE        NAME(0x0056FA70)    /* "RaceModeDropDown" */
#define SP_OPPONENTS        NAME(0x0056FA5C)    /* "OpponentsListBox" */
#define SP_RECORD           NAME(0x0056FA4C)    /* "ChkRecordRace" */
#define SP_LOD_EASY         NAME(0x0056FA40)    /* "RadLODEasy" */
#define SP_GATES            NAME(0x0056FA34)    /* "LstNumGates" */
#define SP_SEED             NAME(0x0056FA1C)    /* "EditSeed" */
#define SP_RANDOM_GATES     NAME(0x0056FA00)    /* "ChkRandomGates" */
#define SP_TRACK_LEFT       NAME(0x0056F9F4)    /* "TrackLeft" */
#define SP_TRACK_RIGHT      NAME(0x0056F9E8)    /* "TrackRight" */
#define SP_GHOST_LEFT       NAME(0x0056F9DC)    /* "GhostLeft" */
#define SP_GHOST_RIGHT      NAME(0x0056F9C0)    /* "GhostRight" */
#define SP_SEED_LEFT        NAME(0x0056F9B4)    /* "SeedLeft" */
#define SP_SEED_RIGHT       NAME(0x0056F9A8)    /* "SeedRight" */
#define SP_OPPONENTS_LEFT   NAME(0x0056F994)    /* "OpponentsLeftArrow" */
#define SP_OPPONENTS_RIGHT  NAME(0x0056F980)    /* "OpponentsRightArrow" */
#define SP_LAPS_BOX         NAME(0x0056FB18)    /* "LapsControlBox" */
#define SP_RACES_BOX        NAME(0x0056FB08)    /* "RacesControlBox" */
#define SP_GHOST_BOX        NAME(0x0056FAFC)    /* "GhostBox" */
#define SP_OPPONENTS_BOX    NAME(0x0056FAE8)    /* "OpponentsControlBox" */
#define SP_WAYPOINTS        NAME(0x0056FADC)    /* "WaypointBox" */
#define SP_RACES_LEFT       NAME(0x0056FACC)    /* "RacesLeftArrow" */
#define SP_RACES_RIGHT      NAME(0x0056FABC)    /* "RacesRightArrow" */
#define SP_RACES_LABEL      NAME(0x0056FAB0)    /* "RacesLabel" */
#define SP_RACES            NAME(0x0056FAA0)    /* "RacesListBox" */
#define SP_LAPS             NAME(0x0056FB38)    /* "LapsListBox" */
#define SP_LAPS_LABEL       NAME(0x0056FB44)    /* "LapsLabel" */
#define SP_TREES            NAME(0x0056FB50)    /* "ChkTreeCollision" */
#define SP_PICTURE          NAME(0x0056FB28)    /* "PictureBox" */
#define ONE_NUMBER          NAME(0x0056EEA8)    /* "%d" */
#define METRES              NAME(0x0056FB34)    /* "%dm" */
#define RACE_OF             NAME(0x0056FA98)    /* "%d/%d" */
/*
    The bike and rider page (SPBikeR.dtm, vtable 0x00556DEC) shows the session's rider and bike
    models (0x46C, 0x470) through its camera (0x468), circling the session's point (0x474).
*/
#define BR_POSITION      0x7F58     /* vector3: the camera */
#define BR_TARGET        0x7F64     /* vector3: what it looks at */
#define BR_DISTANCE      0x7F70     /* float: how far it circles */
#define BR_SLOT          0x7F74     /* the page's background slot */
#define BR_MANUAL        0x7F78     /* the mouse turns it (ChkAutoRotate off) */
#define BR_HELD          0x7F7C     /* the mouse button is down (hold_release clears it) */
#define BR_IDLING        0x7F80     /* the rider is playing WaitR */
#define BR_BIKE_CHANGED  0x7F84
#define BR_RIDER_CHANGED 0x7F88
#define BR_VIEW          0x7F8C     /* RECT: where the models are drawn */

#define session_camera FIELD(game_session, 0x468, BYTE*)
#define session_rider  FIELD(game_session, 0x46C, BYTE*)
#define session_bike   FIELD(game_session, 0x470, BYTE*)
#define session_centre FIELD(game_session, 0x474, vector3)

#define LIST_BIKES        NAME(0x0056FBCC)    /* "DDLBikes" */
#define LIST_RIDERS       NAME(0x0056FBC0)    /* "DDLRiders" */
#define LIST_ENGINE_SIZE  NAME(0x0056FC38)    /* "DDLEngineSize" */
#define TEXT_ENGINE_SIZE  NAME(0x0056FC84)    /* "TxtEngineSize" */
#define EDIT_PLATE        NAME(0x0056FBD8)    /* "EditPlateNumber" */
#define CHECK_AUTO_ROTATE NAME(0x0056FC48)    /* "ChkAutoRotate" */

#define engine_sizes    ((const int*) 0x00572BEC)   /* 125, 250, 400, 500, 600 */
#define TURN_PER_PIXEL  0.015707964f                /* pi/200 */
#define TURN_PER_SECOND 0.39269908f                 /* pi/8 */

int   __cdecl    game__rand(void);
void  __cdecl    game_FUN_0053a27e(unsigned seed);                         /* srand */
DWORD __cdecl    game_FUN_004c41d0(void);                                  /* the time */
float __cdecl    game_FUN_00461ac0(float value);                           /* a fast square root */

/* The session's camera: its place (each pointer may be NULL), where it looks, its viewport. */
void __fastcall game_FUN_0042f8d0(BYTE* camera, void* edx_unused, const vector3* position,
                                  const void* a, const void* b, const void* c, const float* fov);
typedef int (__fastcall* camera_look_at)(void* camera, void* edx_unused, vector3 target);
void __fastcall game_FUN_004300b0(BYTE* camera, void* edx_unused, int x, int y, int width,
                                  int height);
typedef int (__fastcall* model_step)(void* model, void* edx_unused, float seconds, DWORD a,
                                     DWORD b);
void __fastcall game_FUN_004aa170(BYTE* model, void* edx_unused, const char* animation);
void __fastcall session_show_scene(BYTE* self, void* edx_unused, BYTE* dialog);  /* models in */
void __fastcall game_FUN_004eda80(BYTE* screen, void* edx_unused, BYTE* camera);

/* The models' scope on the display (11 DWORDs): begun, a model's value set in it, ended. */
void __fastcall game_FUN_00418300(DWORD* scope, void* edx_unused, BYTE* display);
void __fastcall game_FUN_00418470(DWORD* scope, void* edx_unused, BYTE* model, DWORD value);
void __fastcall game_FUN_00418370(DWORD* scope);

/* A model dressed in a look (a file), on the display. */
void __fastcall game_FUN_00445c10(BYTE* model, void* edx_unused, DWORD a, BYTE* look,
                                  BYTE* display);

/* The scenes' env file loaded. */
void __fastcall game_FUN_004ee910(BYTE* resources, void* edx_unused, const char* path);

int __fastcall control_load_text(BYTE* self, void* edx_unused, HINSTANCE module, UINT id);
int __fastcall dialog_tick(BYTE* self, void* edx_unused, float seconds);
BOOL __fastcall profile_find_named_file(char* self, void* edx_unused, const char* folder,
                                        const char* name, const char* ext, char* dir);

/* ---- Helpers ---- */

/* Less as the x87 compare tests it (C0 set): unordered counts too. */
static __inline BOOL x87_less(double a, double b)
{
    return !(a >= b);
}

/* Equal as `test ah, 40h` tests it (C3 set): unordered counts too. */
static __inline BOOL x87_equal(double a, double b)
{
    return !(a < b) && !(a > b);
}

/* Whether the event came from the control named `button`. */
static __inline BOOL clicked(const ui_event* event, const char* button)
{
    return !game_FUN_0053b6e0(button, (const char*) event->b);
}

/* A rider look offered in a single race and online: riders.pb's Available 2 and 3, and the player's
   own; the rest are the Pro Circuit's. With ALL RIDER GEAR (the dev build's) every one: another
   rider's look is found by its file (session_rider_look), so everyone sees it. */
static __inline BOOL rider_offered(const BYTE* rider)
{
    int kind = FIELD(rider, 0xC4, int);

#ifdef OPENMCM2_DEV
    if (g_reborn.gear)
        return TRUE;
#endif
    return kind == 2 || kind == 3 || FIELD(rider, 0xC0, DWORD);
}

/* Makers of kind 2 and 3 are offered, and 10 once the game's 0x1EE8 has its first bit. */
static __inline BOOL maker_rideable(int kind)
{
    return kind == 2 || kind == 3 || (kind == 10 && (FIELD(game_main, 0x1EE8, BYTE) & 1));
}

static __inline BYTE* combo_list(BYTE* self, const char* name)
{
    return FIELD(dialog_find_control(self, NULL, name, 6), COMBO_LIST, BYTE*);
}

/* Event types 0 and 4 are timed: the laps list holds their time (SET_TIME). */
static __inline BOOL event_timed(int type)
{
    return type == 0 || type == 4;
}

static __inline void numbers_fill(BYTE* list, const char* format, int first, int last)
{
    char number[0x100];
    int i;

    list_clear(list);
    for (i = first; i <= last; i++) {
        game_FUN_0053a11f(number, format, i);
        list_add_text(list, NULL, number, i, 0);
    }
}

/* Vtable slot 54: the outside value a control shows and sets, synced by dialog_notify_controls. */
static __inline void control_bind(BYTE* control, void* value)
{
    VIRTUAL(control, 0xD8, method_1)(control, NULL, (DWORD) value);
}

static __inline void list_add_string(BYTE* list, UINT id, DWORD data)
{
    char text[0x80];

    game_FUN_00526fc0(game_main, NULL, id, text, sizeof text);
    list_add_text(list, NULL, text, data, 0);
}

/* The arrows beside a drop-down list step its selection round, and tell its dialog (slot 66). */
static __inline void combo_step(BYTE* self, const char* name, BOOL back)
{
    BYTE* list = combo_list(self, name);
    int count = FIELD(list, 0x1EC, int);

#ifdef OPENMCM2_DEV
    if (count <= 0)                 /* a filter can leave no tracks listed */
        return;
#endif
    if (back)
        list_select_all(list, NULL, (list_selected(list) + count - 1) % count);
    else
        list_select_all(list, NULL, (list_selected(list) + 1) % count);
    VIRTUAL(list, 0x108, method_1)(list, NULL, 0);
}

/* An engine size's class (0 to 4), as the game rounds it; 1 when it falls between. */
static __inline int engine_class(int cc)
{
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

/* The class's handling, from the session (rider kinds 0-2) or the game's own table. */
static __inline void engine_class_apply(int size_class)
{
    int kind;
    int i;

    FIELD(game_main, 0x15A0, DWORD) = FIELD(game_session, 0x2FC + size_class * 4, DWORD);
    FIELD(game_main, 0x15A4, DWORD) = FIELD(game_session, 0x310 + size_class * 4, DWORD);
    FIELD(game_main, 0x15A8, DWORD) = FIELD(game_session, 0x414 + size_class * 4, DWORD);
    FIELD(game_main, 0x1558, DWORD) = FIELD(game_session, 0x428 + size_class * 4, DWORD);
    kind = FIELD(game_main, 0xF84 + size_class * 4, int);
    for (i = 0; i < 11; i++) {
        int index = (size_class * 3 + kind) * 11 + i;

        FIELD(game_main, 0x1574 + i * 4, DWORD) = kind < 3
            ? FIELD(game_session, 0x68 + index * 4, DWORD)
            : FIELD(game_main, 0x15E4 + index * 4, DWORD);
    }
}

/* A player's model: its virtual base (through the vbtable at +4) hides (slot 4) or shows (5). */
static __inline void model_show(BYTE* player, BOOL shown)
{
    BYTE* model = FIELD(player, 0xC0, BYTE*);
    BYTE* base = model + 4 + FIELD(FIELD(model, 4, BYTE*), 4, int);

    VIRTUAL(base, shown ? 0x14 : 0x10, method_0)(base);
}

static __inline void engine_size_show(BYTE* self, BYTE* list, BOOL shown)
{
    control_show(list, NULL, shown, TRUE);
    control_show(dialog_find_control(self, NULL, TEXT_ENGINE_SIZE, 0), NULL, shown, TRUE);
    if (FIELD(self, 0x110, BYTE*))
        background_invalidate(FIELD(self, 0x110, BYTE*));
}
