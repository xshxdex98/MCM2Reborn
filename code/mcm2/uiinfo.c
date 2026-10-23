/*
    OpenMCM2 - the player's profile, ported from the game (uiinfo.cpp in the original).

    The profile is the 0x2DC0-byte object at game_main + 0x578: the player's name, options, race
    settings and riders, saved as ui\profile\<name>\<name>.prf. It also finds the game's files,
    on the hard disk or the CD. Each function here replaces the game's at the address in its PORT
    line, and was checked against the original's instructions.
*/

#include "gui.h"
#include "dirlist.h"
#include <float.h>
#include <shellapi.h>

#ifdef OPENMCM2_DEV
#include "../dev/dev.h"
#endif

#define NAME(address) ((const char*) (address))
#define UIINFO_CPP      NAME(0x0057B8D4)    /* "D:\aardvark\VC\krusty2\uiinfo.cpp" */
#define PROFILE_FOLDER  NAME(0x0056F548)    /* "ui\profile" */
#define PROFILE_PATH    NAME(0x0057B90C)    /* "%s\%s\%s.prf" */
#define CONTROLS_PATH   NAME(0x0056FE30)    /* "%s\%s\%s" */
#define CONTROLS_FILE   NAME(0x0056FE3C)    /* "control.ctl" */
#define READ_BINARY     NAME(0x0056C66C)    /* "rb" */
#define WRITE_BINARY    NAME(0x0056E39C)    /* "wb" */
#define READ_TEXT       NAME(0x0056CA38)    /* "r" */
#define IN_FOLDER       NAME(0x0056C7DC)    /* "%s\%s" */
#define JOINED          NAME(0x0056ED34)    /* "%s%s" */
#define FILE_NAME       NAME(0x00578E44)    /* "%s.%s" */
#define FILE_IN_FOLDER  NAME(0x0057B91C)    /* "%s\%s.%s" */
#define HELP_FILE       NAME(0x0057B98C)    /* "%s.hlp" */
#define HELP_EXTENSION  NAME(0x0057B994)    /* "hlp" */
#define CD_LABEL        NAME(0x0057B950)    /* "MCM2" */
#define CD_GAME_FOLDER  NAME(0x0057B948)    /* "game" */
#define HERE            NAME(0x0056EFA4)    /* "." */
#define SCHEDULE_FILE   NAME(0x0057792C)    /* "PCSched.pb" */
#define BONUS_TRACK     NAME(0x00577A1C)    /* "BonusTrack" */
#define TRACK_KEY       NAME(0x00577A28)    /* "Track_%d" */

#define KEY_MRU_PROFILE     NAME(0x005736D4)    /* "MRUProfile" */
#define KEY_USE_CONTROLLER  NAME(0x005777B8)    /* "UseControllerId" */
#define KEY_LAST_CONTROLLER NAME(0x0057B8F8)    /* "LastControllerId" */
#define KEY_PRESET          NAME(0x00575204)    /* "PresetSelected" */
#define KEY_USE_LAST        NAME(0x0057522C)    /* "UseLastController" */
#define KEY_INSTALL_TYPE    NAME(0x0057B974)    /* "InstallType" */
#define KEY_ROOT            NAME(0x0057B958)    /* "HardDriveRootPath" */

#define game_sound     FIELD(game_main, 0x4, BYTE*)
#define game_driver    FIELD(game_main, 0xC, BYTE*)       /* the video driver and its modes */
#define game_window    FIELD(game_main, 0x31C, HWND)
#define game_title     ((const char*) game_main + 0x3A0)
#define game_language  FIELD(game_main, 0x420, HINSTANCE)
#define game_session   FIELD(game_main, 0x56C, BYTE*)
#define game_profile   ((char*) game_main + 0x578)
#define game_controls  FIELD(game_main, 0x33FC, BYTE*)
#define KEYBOARD_CONTROLS 6         /* "Keyboard"; 0 to 5 are joysticks' setups, 7 the mouse's */
#define game_resources (*(void* const*) 0x00578C84)

/* The profile's parts, by offset. */
#define PROFILE_SIZE 0x2DC0
#define P_SCENES     0x0A0      /* the six event kinds' scene names, 0x100 each */
#define P_SCENE      0x6A0      /* the current kind's */
#define P_OPTIONS    0x6AC
#define P_SOUND      0xA20
#define P_DISPLAY    0xA4C      /* the display mode's index first */
#define P_VOLUMES    0xA68
#define P_ENTRIES    0xA98
#define P_PRESETS    0xFD8      /* three of 0x5C; the first is the one in use */
#define P_BONUS      0x1384     /* six flags, then six track names of 0x20 at 0x139C */
#define P_NAMES      0x145C     /* ten riders' names, 0x80 each */
#define P_PLAYERS    0x1BE4
#define P_DEVICE     0x23A8     /* the video driver's GUID */
#define P_INSTALL    0x23CC     /* 2: everything on the hard disk */
#define P_ROOT       0x23D0
#define P_CD_ROOT    0x24D4
#define P_CD_DRIVE   0x25D8     /* "X:\" */
#define P_DRIVES     0x25DC
#define P_LISTS      0x25E0     /* three directory lists */
#define P_RACE       0x27F8     /* the race settings, 0x7B DWORDs; kept copies at 0x29E4, 0x2BD0 */
#define P_EVENT      0x2800

#define RACE_DWORDS  0x7B
#define PLAYER_SIZE  0xF8
#define PLAYER_COUNT 8

/* The entries at P_ENTRIES (0x10C bytes, a vtable and a name). */
#define ENTRY_SIZE      0x10C
#define ENTRY_COUNT     5
#define ENTRY_VTABLE    ((void*) 0x0055DD58)
#define BASE_VTABLE     ((void*) 0x00556CD8)
#define ENTRY_CONSTRUCT 0x005291C0
#define ENTRY_DESTRUCT  0x005292D0

/* The video driver: its modes (36 bytes: width, height, bits, ..., two "usable" flags). */
#define DRIVER_MODE_COUNT 0x08
#define DRIVER_MODES      0x10
#define DRIVER_GUID       0x4AC
#define MODE_SIZE         0x24

#define RAND_SCALE (1.0f / 32768)

typedef DWORD (__fastcall* setting_number)(void* game, void* edx_unused, const char* name,
                                           DWORD fallback);
typedef int   (__fastcall* setting_text)(void* game, void* edx_unused, const char* name,
                                         const char* fallback, char* text, DWORD* size);
typedef void  (__fastcall* setting_set_number)(void* game, void* edx_unused, const char* name,
                                               DWORD value);
typedef void  (__fastcall* setting_set_text)(void* game, void* edx_unused, const char* name,
                                             const char* text);
#define GAME_SETTING_NUMBER     0x58
#define GAME_SETTING_TEXT       0x5C
#define GAME_SET_SETTING_NUMBER 0x6C
#define GAME_SET_SETTING_TEXT   0x70

/* The game's C library: sprintf, fopen, fread, fwrite, fclose, _splitpath, _strnicmp, rand. */
int    __cdecl game_FUN_0053a11f(char* to, const char* format, ...);
void*  __cdecl game_FUN_0053a35b(const char* path, const char* mode);
size_t __cdecl game_FUN_0053a5d5(void* to, size_t size, size_t count, void* file);
size_t __cdecl game_FUN_0053a727(void* from, size_t size, size_t count, void* file);
int    __cdecl game_FUN_0053a2ad(void* file);
void   __cdecl game_FUN_0053b042(const char* path, char* drive, char* dir, char* name, char* ext);
int    __cdecl game_FUN_0054cd80(const char* a, const char* b, size_t count);
int    __cdecl game__strcmp(const char* a, const char* b);
int    __cdecl game__rand(void);

/* The C++ array helpers: construct and destruct `count` objects. */
void __stdcall game_FUN_0053b88e(void* array, size_t size, int count, DWORD construct,
                                 DWORD destruct);
void __stdcall game_FUN_0053b7b0(void* array, size_t size, int count, DWORD destruct);

BOOL  __fastcall game_FUN_00526fc0(BYTE* game, void* edx_unused, UINT id, char* text, int size);
drive_list* __fastcall game_FUN_00402ac0(void* self);                 /* the drive list's */
void  __fastcall game_FUN_004c3060(BYTE* sound, void* edx_unused, DWORD rate, DWORD a,
                                   DWORD bits);

/* The controls (game_main + 0x33FC): load, save, defaults, take up. */
void __fastcall game_FUN_00449920(BYTE* controls, void* edx_unused, const char* path);
void __fastcall game_FUN_00449970(BYTE* controls, void* edx_unused, const char* path);
void __fastcall game_FUN_0044a1b0(BYTE* controls);
void __fastcall game_FUN_00449e20(BYTE* controls, void* edx_unused, const char* path, int a);

/* dirlist.cpp: the drives (8 bytes) and the directory lists. */

/* The game's file object (308 bytes) on its resources. */
void* __fastcall game_FUN_00461c80(void* self, void* edx_unused, void* resources);
BOOL  __fastcall game_FUN_00461ec0(void* self, void* edx_unused, const char* name, const char* mode,
                                   DWORD a);
void  __fastcall game_FUN_00461cd0(void* self);

/* Ported below, called before their place. */
void __fastcall profile_defaults(char* self);
void __fastcall profile_pick_mode(char* self, void* edx_unused, const DWORD* device, int* mode);
void __fastcall options_defaults(char* self, void* edx_unused, BYTE* options);
void __fastcall sound_defaults(char* self, void* edx_unused, BYTE* sound);
void __fastcall display_defaults(char* self, void* edx_unused, BYTE* display);
void __fastcall volumes_defaults(char* self, void* edx_unused, BYTE* volumes);
void __fastcall names_defaults(char* self, void* edx_unused, char* names);
void __fastcall preset_reset(char* self, void* edx_unused, BYTE* preset);
void __fastcall profile_clear_flags(char* self);
BOOL __fastcall profile_find_cd(char* self);
BOOL __fastcall profile_event_scene(char* self, void* edx_unused, int a, int b, char* scene);
void __fastcall base_destruct(BYTE* self);

/* Eight pairs of a DWORD and a flag. (The original leaves the flag's padding as it finds it.) */
static void pairs_clear(char* pairs)
{
    int i;

    for (i = 0; i < 8; i++) {
        FIELD(pairs, i * 8, DWORD) = 0;
        pairs[i * 8 + 4] = 0;
    }
}

static void race_copy(char* to, const char* from)
{
    __movsd((unsigned long*) to, (unsigned long*) from, RACE_DWORDS);
}

/* ---- Player slots (0xF8 bytes) ---- */

PORT(0x00527560)
BYTE* __fastcall player_construct(BYTE* self)
{
    FIELD(self, 0xD4, DWORD) = 0;
    text_copy_most((char*) self + 0xDC, NO_TEXT, 15);
    FIELD(self, 0xC8, DWORD) = 0;
    FIELD(self, 0xCC, DWORD) = 0;
    self[0x80] = 0;
    self[0x40] = 0;
    self[0] = 0;
    FIELD(self, 0xD0, float) = FLT_MAX;
    self[0xF4] = 1;
    return self;
}

PORT(0x005275E0)
void __fastcall player_set_names(BYTE* self, void* edx_unused, const char* first,
                                 const char* second, const char* third)
{
    (void) edx_unused;
    text_copy_most((char*) self, first, 63);
    text_copy_most((char*) self + 0x40, second, 63);
    text_copy_most((char*) self + 0x80, third, 63);
}

PORT(0x00527680)
void __fastcall player_clear_c8(BYTE* self)
{
    FIELD(self, 0xC8, DWORD) = 0;
}

/* ---- The profile ---- */

PORT(0x00527690)
char* __fastcall profile_construct(char* self)
{
    static const char* const scenes[6] = {
        NAME(0x0056F66C),   /* "Teraform Quarries" */
        NAME(0x0056CB6C),   /* "Teraform Baja" */
        NAME(0x0056F60C),   /* "Teraform National" */
        NAME(0x0056F63C),   /* "Teraform SX" */
        NAME(0x0057B8C4),   /* "Teraform Tag" */
        NAME(0x0056CB5C),   /* "Teraform Enduro" */
    };
    drive_list* drives;
    int i;

    game_FUN_0053b88e(self + P_ENTRIES, ENTRY_SIZE, ENTRY_COUNT, ENTRY_CONSTRUCT, ENTRY_DESTRUCT);
    for (i = 0; i < PLAYER_COUNT; i++)
        player_construct((BYTE*) self + P_PLAYERS + i * PLAYER_SIZE);
    pairs_clear(self + 0x2944);
    pairs_clear(self + 0x2B30);
    pairs_clear(self + 0x2D1C);
    profile_defaults(self);

    FIELD(self, 0x23A4, DWORD) = 0;
    FIELD(self, 0x6A8, DWORD) = 1;
    for (i = 0; i < 4; i++)
        FIELD(self, P_DEVICE + i * 4, DWORD) = 0;
    FIELD(self, P_INSTALL, int) = 2;
    text_copy(self + P_ROOT, NO_TEXT);
    text_copy(self + P_CD_ROOT, NO_TEXT);
    text_copy(self + P_CD_DRIVE, NO_TEXT);

    FIELD(self, P_DRIVES, drive_list*) = NULL;
    drives = game_new(8, UIINFO_CPP, 0x47);
    FIELD(self, P_DRIVES, drive_list*) = drives = drives ? game_FUN_00402ac0(drives) : NULL;
    /* Listed even when out of memory (kept). */
    if (!drives_scan_entry(drives)) {
        drives = FIELD(self, P_DRIVES, drive_list*);
        if (drives) {
            drives_destruct(drives);
            game_delete(drives);
        }
        FIELD(self, P_DRIVES, drive_list*) = NULL;
    }

    for (i = 0; i < 3; i++)
        FIELD(self, P_LISTS + i * 4, BYTE*) = NULL;
    FIELD(self, 0x9C, DWORD) = 1;
    self[0x25EC] = 0;
    FIELD(self, 0x26F0, DWORD) = 0;
    self[0x26F4] = 0;
    FIELD(self, 0x1A34, int) = -1;

    /* A random number from 100 to 998. */
    FIELD(self, 0x1BCC, int) = (int) (game__rand() * (double) RAND_SCALE * 999.0f);
    if (FIELD(self, 0x1BCC, int) < 100)
        FIELD(self, 0x1BCC, int) += 100;

    for (i = 0; i < 6; i++) {
        FIELD(self, P_BONUS + i * 4, DWORD) = 0;
        self[P_BONUS + 0x18 + i * 0x20] = 0;
    }
    FIELD(self, 0x1BD0, int) = -1;
    FIELD(self, 0x1BD4, int) = -1;
    FIELD(self, 0x1BD8, DWORD) = 0;
    FIELD(self, 0x1BE0, DWORD) = 0;
    FIELD(self, 0x10EC, DWORD) = 0;
    for (i = 0; i < 6; i++)
        text_copy_most(self + P_SCENES + i * 0x100, scenes[i], 0xFF);
    return self;
}

PORT(0x00527A50)
void __fastcall profile_destruct_entries(BYTE* entries)
{
    game_FUN_0053b7b0(entries, ENTRY_SIZE, ENTRY_COUNT, ENTRY_DESTRUCT);
}

PORT(0x00527A70)
void __fastcall profile_defaults(char* self)
{
    DWORD screen;

    options_defaults(self, NULL, (BYTE*) self + P_OPTIONS);
    sound_defaults(self, NULL, (BYTE*) self + P_SOUND);
    display_defaults(self, NULL, (BYTE*) self + P_DISPLAY);
    /* The game's own profile picks, into its own device field, whichever profile this is (kept). */
    profile_pick_mode(game_profile, NULL, NULL, (int*) (self + P_DISPLAY));
    volumes_defaults(self, NULL, (BYTE*) self + P_VOLUMES);
    names_defaults(self, NULL, self + P_NAMES);
    preset_reset(self, NULL, (BYTE*) self + P_PRESETS);
    preset_reset(self, NULL, (BYTE*) self + P_PRESETS + 0x5C);
    preset_reset(self, NULL, (BYTE*) self + P_PRESETS + 0xB8);
    profile_clear_flags(self);

    FIELD(self, P_RACE, DWORD) = 0;
    FIELD(self, 0x2804, DWORD) = 2;
    FIELD(self, 0x2808, DWORD) = 1;
    FIELD(self, 0x280C, DWORD) = 1;
    FIELD(self, 0x2810, DWORD) = 0;
    FIELD(self, 0x2814, DWORD) = 4;
    FIELD(self, 0x2818, DWORD) = 5;
    FIELD(self, 0x281C, DWORD) = 0;
    FIELD(self, 0x2820, DWORD) = 0;
    self[0x2824] = 0;
    self[0x2825] = 30;
    FIELD(self, 0x2828, DWORD) = 1;
    FIELD(self, 0x2938, float) = 15.0f;
    FIELD(self, 0x23B8, DWORD) = 0;
    race_copy(self + 0x2BD0, self + P_RACE);

    self[0x1970] &= ~1;
    FIELD(self, 0x23BC, DWORD) = 0x4124CCCD;    /* 10.3f */
    FIELD(self, 0x23C0, DWORD) = 0x3EF14638;    /* 0.4712f, 27 degrees */
    FIELD(self, 0x23C4, DWORD) = 0x3F88467A;    /* 1.0647f, 61 degrees */
    FIELD(self, 0x23C8, DWORD) = 0x42AA0000;    /* 85.0f */
    screen = FIELD(self, P_DISPLAY + 0x18, DWORD);
    FIELD(self, 0x196C, DWORD) = screen;
    race_copy(self + 0x29E4, self + P_RACE);
    FIELD(self, 0x1968, DWORD) = screen;
    FIELD(self, 0x1964, DWORD) = screen;
    FIELD(self, 0x1960, DWORD) = screen;

    FIELD(self, 0x1BDC, DWORD) = 0;
    FIELD(self, 0x90, DWORD) = 0;
    FIELD(self, 0x2DBC, DWORD) = 0;
    FIELD(self, 0x1BCC, int) = 101;
    __stosd((unsigned long*) (self + P_BONUS), 0, 6);
    FIELD(self, 0x98, DWORD) = 2;
    FIELD(self, 0x94, DWORD) = 2;
}

/* The directory lists go; the drive list is left (kept). */
PORT(0x00527C20)
void __fastcall profile_destruct(char* self)
{
    int i;

    for (i = 0; i < 3; i++) {
        BYTE* list = FIELD(self, P_LISTS + i * 4, BYTE*);

        if (list)
            VIRTUAL(list, 0, method_1)(list, NULL, 1);
    }
    game_FUN_0053b7b0(self + P_ENTRIES, ENTRY_SIZE, ENTRY_COUNT, ENTRY_DESTRUCT);
}

PORT(0x00527CB0)
BOOL __fastcall profile_reset_players(char* self)
{
    BYTE player[PLAYER_SIZE];
    int i;

    FIELD(self, 0x6A4, DWORD) = 0;
    FIELD(self, 0xFD4, DWORD) = 0;
    FIELD(self, 0x1BE0, DWORD) = 0;
    if (FIELD(self, P_RACE, int) != 2)
        for (i = 0; i < PLAYER_COUNT; i++)
            __movsd((unsigned long*) (self + P_PLAYERS + i * PLAYER_SIZE),
                    (unsigned long*) player_construct(player), PLAYER_SIZE / 4);
    pairs_clear(self + 0x2944);
    self[0x282D] = 0;
    return TRUE;
}

PORT(0x00527D50)
void __fastcall options_defaults(char* self, void* edx_unused, BYTE* options)
{
    int i;

    (void) self;
    (void) edx_unused;
    for (i = 0; i < 12; i++)
        FIELD(options, i * 4, DWORD) = 1;
    FIELD(options, 0x08, DWORD) = 0;
    FIELD(options, 0x30, DWORD) = 2;
    for (i = 0; i < 5; i++) {
        FIELD(options, 0x34 + i * 4, DWORD) = 0;
        FIELD(options, 0x360 + i * 4, DWORD) = 0;
    }
}

PORT(0x00527DB0)
void __fastcall sound_defaults(char* self, void* edx_unused, BYTE* sound)
{
    (void) self;
    (void) edx_unused;
    FIELD(sound, 0x00, DWORD) = 0;
    FIELD(sound, 0x04, DWORD) = 1;
    FIELD(sound, 0x08, DWORD) = 1;
    FIELD(sound, 0x0C, DWORD) = 1;
    FIELD(sound, 0x10, DWORD) = 0;
    FIELD(sound, 0x14, DWORD) = 0;
    FIELD(sound, 0x18, DWORD) = 1;
    FIELD(sound, 0x1C, DWORD) = 100;
    FIELD(sound, 0x20, DWORD) = 100;
    FIELD(sound, 0x24, DWORD) = 100;
    FIELD(sound, 0x28, DWORD) = 0;
}

PORT(0x00527E00)
void __fastcall display_defaults(char* self, void* edx_unused, BYTE* display)
{
    (void) self;
    (void) edx_unused;
    FIELD(display, 0x00, DWORD) = 0;
    FIELD(display, 0x04, DWORD) = 1;
    FIELD(display, 0x08, DWORD) = 1;
    FIELD(display, 0x0C, DWORD) = 0;
    FIELD(display, 0x10, DWORD) = 1;
    FIELD(display, 0x18, DWORD) = 6;
}

PORT(0x00527E30)
void __fastcall volumes_defaults(char* self, void* edx_unused, BYTE* volumes)
{
    int i;

    (void) self;
    (void) edx_unused;
    for (i = 0; i < 8; i++)
        FIELD(volumes, i * 4, DWORD) = 50;
    FIELD(volumes, 0x20, DWORD) = 1;
    FIELD(volumes, 0x24, DWORD) = 0;
    FIELD(volumes, 0x28, DWORD) = 1;
    FIELD(volumes, 0x2C, DWORD) = 1;
}

/* The riders' names, strings 5143 to 5152; the game's profile takes them, not `names` (kept). */
PORT(0x00527E70)
void __fastcall names_defaults(char* self, void* edx_unused, char* names)
{
    char text[0x100];
    int i;

    (void) self;
    (void) edx_unused;
    (void) names;
    for (i = 0; i < 10; i++) {
        game_FUN_00526fc0(game_main, NULL, 0x1417 + i, text, 0x80);
        text_copy_most(game_profile + P_NAMES + (i + 1) % 10 * 0x80, text, 0x7F);
    }
}

/* Counts from the session (10 and 0 without one), into the preset and the profile's 0x10F0. */
PORT(0x00528220)
void __fastcall preset_session_counts(char* self, void* edx_unused, BYTE* preset)
{
    DWORD* to = (DWORD*) (self + 0x10F0);
    int i, j;

    (void) edx_unused;
    for (i = 0; i < 11; i++)
        FIELD(preset, 0x24 + i * 4, DWORD) =
            game_session ? FIELD(game_session, 0xEC + i * 4, DWORD) : 10;
    for (i = 0; i < 5; i++)
        for (j = 0; j < 33; j++)
            *to++ = game_session ? FIELD(game_session, 0x43C + i * 4, DWORD) : 0;
}

PORT(0x005282A0)
void __fastcall preset_default_rates(char* self, void* edx_unused, BYTE* preset)
{
    int i;

    (void) self;
    (void) edx_unused;
    for (i = 3; i < 9; i++)
        FIELD(preset, i * 4, float) = 0.5f;
}

PORT(0x005282C0)
void __fastcall preset_default(char* self, void* edx_unused, BYTE* preset)
{
    (void) edx_unused;
    FIELD(preset, 0, DWORD) = 250;
    FIELD(preset, 4, DWORD) = 0;
    preset_session_counts(self, NULL, preset);
}

PORT(0x005282E0)
void __fastcall preset_reset(char* self, void* edx_unused, BYTE* preset)
{
    (void) edx_unused;
    preset_default_rates(self, NULL, preset);
    preset_default(self, NULL, preset);
}

PORT(0x00528300)
void __fastcall profile_clear_flags(char* self)
{
    pairs_clear(self + 0x2944);
}

/* The display mode, and the three directory lists; FALSE when one is missing (bar the first). */
PORT(0x00528330)
BOOL __fastcall profile_make_lists(char* self)
{
    void* list;

    profile_pick_mode(self, NULL, NULL, (int*) (self + P_DISPLAY));
    list = game_new(0x21C, UIINFO_CPP, 0x17F);
    FIELD(self, P_LISTS, dir_list*) = list ? dir_list_construct(list) : NULL;
    list = game_new(0x424, UIINFO_CPP, 0x180);
    FIELD(self, P_LISTS + 4, merged_list*) = list ? merged_list_construct(list) : NULL;
    list = game_new(0x424, UIINFO_CPP, 0x181);
    FIELD(self, P_LISTS + 8, merged_list*) = list ? merged_list_construct(list) : NULL;
    return FIELD(self, P_DRIVES, drive_list*) && FIELD(self, P_LISTS + 4, BYTE*)
        && FIELD(self, P_LISTS + 8, BYTE*);
}

static BYTE* driver_mode(int index)
{
    return FIELD(game_driver, DRIVER_MODES, BYTE*) + index * MODE_SIZE;
}

static BOOL same_guid(const DWORD* a, const DWORD* b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

/*
    The display mode: the one kept, if it is the same driver's and still usable; else 640x480x16,
    else the tallest usable 16-bit mode. The driver's GUID becomes the profile's.
*/
PORT(0x00528450)
void __fastcall profile_pick_mode(char* self, void* edx_unused, const DWORD* device, int* mode)
{
    BYTE* driver = game_driver;
    int usable;
    int i;

    (void) edx_unused;
    if (!driver)
        return;
    if ((FIELD(game_main, 0x548, BYTE) & 1) || !device
        || !same_guid(device, (const DWORD*) (driver + DRIVER_GUID))
        || !FIELD(driver_mode(*mode), 0x14, DWORD)) {
        usable = FIELD(game_main, 0x2D0, int) ? 0x18 : 0x14;
        *mode = 0;
        for (i = 0; i < FIELD(game_driver, DRIVER_MODE_COUNT, int); i++) {
            BYTE* m = driver_mode(i);

            if (FIELD(m, 0, int) == 640 && FIELD(m, 4, int) == 480 && FIELD(m, 8, int) == 16
                && FIELD(m, usable, DWORD)) {
                *mode = i;
                break;
            }
        }
        /* 0 stands for "none yet", so the first two modes are taken whatever they are (kept). */
        if (!*mode)
            for (i = 0; i < FIELD(game_driver, DRIVER_MODE_COUNT, int); i++) {
                BYTE* m = driver_mode(i);

                if (!*mode || (FIELD(m, usable, DWORD) && FIELD(m, 8, int) == 16
                               && FIELD(m, 4, int) > FIELD(driver_mode(*mode), 4, int)))
                    *mode = i;
            }
    }
    __movsd((unsigned long*) (self + P_DEVICE), (unsigned long*) (game_driver + DRIVER_GUID), 4);
}

/* The profile's control.ctl; the defaults instead unless the preset is the last controller's. */
PORT(0x00528630)
void __fastcall profile_load_controls(char* self)
{
    char used[0x80];
    char last[0x80];
    char path[MAX_PATH];
    DWORD size;

    if (!game_controls)
        return;
    game_FUN_0053a11f(path, CONTROLS_PATH, PROFILE_FOLDER, self, CONTROLS_FILE);
    game_FUN_00449920(game_controls, NULL, path);
    size = sizeof used;
    VIRTUAL(game_main, GAME_SETTING_TEXT, setting_text)(game_main, NULL, KEY_USE_CONTROLLER,
                                                        NO_TEXT, used, &size);
    /* `size` as the first read left it (kept). */
    VIRTUAL(game_main, GAME_SETTING_TEXT, setting_text)(game_main, NULL, KEY_LAST_CONTROLLER,
                                                        NO_TEXT, last, &size);
    if (!VIRTUAL(game_main, GAME_SETTING_NUMBER, setting_number)(game_main, NULL, KEY_PRESET, 0)
        || game__strcmp(last, used))
        game_FUN_0044a1b0(game_controls);
#ifdef OPENMCM2_DEV
    /* No legacy joystick is ever picked (controllerpick.c), so a joystick's setup steers nothing:
       the keyboard's is used instead. The modern controls page once saved setup 0, a joystick's. */
    if (*(int*) game_controls < KEYBOARD_CONTROLS)
        *(int*) game_controls = KEYBOARD_CONTROLS;
#endif
    game_FUN_00449e20(game_controls, NULL, path, -1);
}

PORT(0x00528760)
void __fastcall profile_save_controls(char* self)
{
    char used[0x80];
    char path[MAX_PATH];
    DWORD size;

    if (!game_controls)
        return;
    if (VIRTUAL(game_main, GAME_SETTING_NUMBER, setting_number)(game_main, NULL, KEY_USE_LAST, 0))
        VIRTUAL(game_main, GAME_SET_SETTING_NUMBER, setting_set_number)(game_main, NULL,
                                                                        KEY_PRESET, 1);
    size = sizeof used;
    VIRTUAL(game_main, GAME_SETTING_TEXT, setting_text)(game_main, NULL, KEY_USE_CONTROLLER,
                                                        NO_TEXT, used, &size);
    VIRTUAL(game_main, GAME_SET_SETTING_TEXT, setting_set_text)(game_main, NULL,
                                                                KEY_LAST_CONTROLLER, used);
    game_FUN_0053a11f(path, CONTROLS_PATH, PROFILE_FOLDER, self, CONTROLS_FILE);
    game_FUN_00449970(game_controls, NULL, path);
}

/* What a .prf holds, in order. */
static const struct {
    WORD offset;
    WORD size;
} profile_parts[] = {
    { 0x0000, 0x010 }, { 0x009C, 4 },     { 0x06AC, 0x374 }, { 0x0A20, 0x02C }, { 0x0A4C, 0x01C },
    { 0x0A68, 0x030 }, { 0x145C, 0x500 }, { 0x195C, 0x018 }, { 0x1034, 0x05C }, { 0x1090, 0x05C },
    { 0x1A3C, 0x0C8 }, { 0x1B04, 0x0C8 }, { 0x23A8, 0x010 }, { 0x23B8, 4 },     { 0x23BC, 4 },
    { 0x23C0, 4 },     { 0x23C4, 4 },     { 0x23C8, 4 },     { 0x0010, 0x080 }, { 0x29E4, 0x1EC },
    { 0x2BD0, 0x1EC }, { 0x1384, 0x018 }, { 0x1BDC, 4 },     { 0x0090, 4 },     { 0x0098, 4 },
    { 0x10F0, 0x294 }, { 0x1BCC, 4 },
};

static void profile_transfer(char* self, void* file,
                             size_t (__cdecl* move)(void*, size_t, size_t, void*))
{
    int i;

    for (i = 0; i < sizeof profile_parts / sizeof *profile_parts; i++)
        move(self + profile_parts[i].offset, profile_parts[i].size, 1, file);
}

/* The profile named in it, from its .prf; FALSE, and no name, when there is none. */
PORT(0x00528820)
BOOL __fastcall profile_load(char* self)
{
    char path[MAX_PATH];
    char scratch[PROFILE_SIZE];
    BYTE* sound;
    void* file;

    game_FUN_0053a11f(path, PROFILE_PATH, PROFILE_FOLDER, self, self);
    file = game_FUN_0053a35b(path, READ_BINARY);
    if (!file) {
        /*
            The original makes and drops a default profile here, which spends a rand() and gives
            the game's profile the driver's GUID and the default riders' names (kept).
        */
        text_copy(self, NO_TEXT);
        profile_construct(scratch);
        profile_destruct(scratch);
        return FALSE;
    }
    profile_transfer(self, file, game_FUN_0053a5d5);
    game_FUN_0053a2ad(file);

    self[0x282D] = 0;
    profile_pick_mode(self, NULL, (const DWORD*) (self + P_DEVICE), (int*) (self + P_DISPLAY));
    FIELD(game_session, 0x48C, DWORD) = 1;     /* unchecked: without a session, a crash (kept) */
    profile_load_controls(self);
    sound = game_sound;
    if (sound) {
        FIELD(sound, 0x45C, BYTE) = (FIELD(sound, 0x45C, BYTE) & ~8)
                                  | (FIELD(self, P_SOUND + 0x14, BYTE) & 1) << 3;
        game_FUN_004c3060(game_sound, NULL, 22050, 1, FIELD(self, P_SOUND + 0x28, DWORD) ? 16 : 8);
    }
    race_copy(self + P_RACE, self + 0x29E4);
    FIELD(self, 0x94, DWORD) = FIELD(self, 0x98, DWORD);
    __movsd((unsigned long*) (self + P_PRESETS), (unsigned long*) (self + P_PRESETS + 0x5C), 0x17);
    __movsd((unsigned long*) (self + 0x1974), (unsigned long*) (self + 0x1A3C), 0x32);
    return self[0] != 0;
}

/* Into its .prf, the profile remembered as the last one, and its controls saved. */
PORT(0x00528BB0)
void __fastcall profile_save(char* self)
{
    char path[MAX_PATH];
    void* file;

    if (!self[0])
        return;
    game_FUN_0053a11f(path, PROFILE_PATH, PROFILE_FOLDER, self, self);
    file = game_FUN_0053a35b(path, WRITE_BINARY);
    if (!file)
        return;
    profile_transfer(self, file, game_FUN_0053a727);
    game_FUN_0053a2ad(file);
    VIRTUAL(game_main, GAME_SET_SETTING_TEXT, setting_set_text)(game_main, NULL, KEY_MRU_PROFILE,
                                                                self);
    if (game_session)
        FIELD(game_session, 0x48C, DWORD) = 1;
    profile_save_controls(self);
}

/* Minimised, asks for the CD (string 5045) until it is in; FALSE, the game closing, on Cancel. */
PORT(0x00528E30)
BOOL __fastcall profile_wait_for_cd(char* self)
{
    char text[0x80];

    SendMessageA(game_window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    while (!profile_find_cd(self)) {
        /* Without the string it asks nothing and tries again at once (kept). */
        if (!LoadStringA(game_language, 0x13B5, text, sizeof text))
            continue;
        ShowCursor(TRUE);
        if (MessageBoxA(game_window, text, game_title, MB_RETRYCANCEL | MB_ICONHAND) == IDCANCEL) {
            SendMessageA(game_window, WM_CLOSE, 0, 0);
            return FALSE;
        }
    }
    SendMessageA(game_window, WM_SYSCOMMAND, SC_RESTORE, 0);
    return TRUE;
}

/*
    `name` (relative, or under either root) under the hard-disk root, else under the CD's (asked
    for if missing); its path into `path`, empty when not found.
*/
PORT(0x00528F20)
BOOL __fastcall profile_find_file(char* self, void* edx_unused, const char* name, char* path)
{
    const char* root = self + P_ROOT;
    const char* cd = self + P_CD_ROOT;
    char found[MAX_PATH];
    int skip;
    void* file;

    (void) edx_unused;
#ifdef OPENMCM2_DEV
    if (dev_settings.trace_files)
        dev_trace("file", "find %s", name);
#endif
    text_copy(path, NO_TEXT);
    skip = (int) text_length(root);
    if (!game_FUN_0054cd80(name, root, skip))
        skip++;                         /* its backslash; with no root, the name's first (kept) */
    else {
        skip = (int) text_length(cd);
        skip = skip && !game_FUN_0054cd80(name, cd, skip) ? skip + 1 : 0;
    }
    name += skip;

    game_FUN_0053a11f(found, IN_FOLDER, root, name);
    file = game_FUN_0053a35b(found, READ_TEXT);
    if (!file) {
        if (FIELD(self, P_INSTALL, int) == 2)
            return FALSE;
        if (!profile_find_cd(self) && !profile_wait_for_cd(self))
            return FALSE;
        game_FUN_0053a11f(found, IN_FOLDER, cd, name);
        file = game_FUN_0053a35b(found, READ_TEXT);
        if (!file)
            return FALSE;
    }
    text_copy(path, found);
    game_FUN_0053a2ad(file);
    return TRUE;
}

/* [folder\]name.ext found; `dir` gets the folder it is in, without its last backslash. */
PORT(0x00529090)
BOOL __fastcall profile_find_named_file(char* self, void* edx_unused, const char* folder,
                                        const char* name, const char* ext, char* dir)
{
    char drive[4];
    char wanted[MAX_PATH];
    char directory[0x100];
    char found[MAX_PATH];
    char extension[0x100];
    char file_name[0x100];
    BOOL ok;

    (void) edx_unused;
    text_copy(dir, NO_TEXT);
    if (!folder[0])
        game_FUN_0053a11f(wanted, FILE_NAME, name, ext);
    else
        game_FUN_0053a11f(wanted, FILE_IN_FOLDER, folder, name, ext);
    ok = profile_find_file(self, NULL, wanted, found);
    if (!ok)
        return ok;
    game_FUN_0053b042(found, drive, directory, file_name, extension);
    game_FUN_0053a11f(dir, JOINED, drive, directory);
    dir[text_length(dir) - 1] = 0;      /* dir[-1] when empty (kept) */
    return ok;
}

PORT(0x005291A0)
BOOL __fastcall profile_race_scene(char* self, void* edx_unused, char* scene)
{
    (void) edx_unused;
    return profile_event_scene(self, NULL, FIELD(self, 0x27FC, short), FIELD(self, P_RACE, short),
                               scene);
}

PORT(0x005291C0)
BYTE* __fastcall entry_construct(BYTE* self)
{
    *(void**) self = ENTRY_VTABLE;
    self[4] = 0;
    FIELD(self, 0x108, DWORD) = 0;
    return self;
}

/* The current event kind's scene name. The two values are not used. */
PORT(0x005291E0)
BOOL __fastcall profile_event_scene(char* self, void* edx_unused, int a, int b, char* scene)
{
    (void) edx_unused;
    (void) a;
    (void) b;
    text_copy(scene, self + P_SCENES + FIELD(self, P_EVENT, int) * 0x100);
    return TRUE;
}

PORT(0x00529220)
BYTE* __fastcall entry_delete(BYTE* self, void* edx_unused, DWORD flags)
{
    (void) edx_unused;
    base_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* The CD (labelled MCM2) and its game folder; always TRUE for a full install. */
PORT(0x00529240)
BOOL __fastcall profile_find_cd(char* self)
{
    int index;

    if (FIELD(self, P_INSTALL, int) == 2)
        return TRUE;
    if (!FIELD(self, P_DRIVES, drive_list*))
        return FALSE;
    drives_scan(FIELD(self, P_DRIVES, drive_list*));
    if (!drives_find(FIELD(self, P_DRIVES, drive_list*), NULL, CD_LABEL, &index, DRIVE_CDROM))
        return FALSE;
    if (!drives_root(FIELD(self, P_DRIVES, drive_list*), NULL, index, self + P_CD_DRIVE))
        return FALSE;
    game_FUN_0053a11f(self + P_CD_ROOT, JOINED, self + P_CD_DRIVE, CD_GAME_FOLDER);
    return TRUE;
}

/* The base class's destructor, which the linker shares among several classes (Net.cpp's too). */
PORT(0x005292D0)
void __fastcall base_destruct(BYTE* self)
{
    *(void**) self = BASE_VTABLE;
}

/*
    The install, and the hard-disk root ("." by default). In this copy the type read is ignored
    and the install always taken as full; the original made it 2 for "Full", else 1.
*/
PORT(0x005292E0)
BOOL __fastcall profile_read_install(char* self)
{
    char type[MAX_PATH];
    DWORD size = sizeof type;

    VIRTUAL(game_main, GAME_SETTING_TEXT, setting_text)(game_main, NULL, KEY_INSTALL_TYPE, NO_TEXT,
                                                        type, &size);
    FIELD(self, P_INSTALL, int) = 2;
    size = MAX_PATH;
    return VIRTUAL(game_main, GAME_SETTING_TEXT, setting_text)(game_main, NULL, KEY_ROOT, HERE,
                                                               self + P_ROOT, &size) != 0;
}

/* topic.hlp, opened with the shell, the game minimised. */
PORT(0x00529380)
BOOL __fastcall profile_open_help(char* self, void* edx_unused, const char* topic,
                                  const char* parameters)
{
    char name[MAX_PATH];
    char dir[MAX_PATH];
    char found[MAX_PATH];

    (void) edx_unused;
    if (!profile_find_named_file(self, NULL, NO_TEXT, topic, HELP_EXTENSION, dir))
        return FALSE;
    /* The original tests "" against itself where it meant `dir`, so never uses dir\topic.hlp. */
    game_FUN_0053a11f(name, HELP_FILE, topic);
    if (!profile_find_file(self, NULL, name, found))
        return FALSE;
    SendMessageA(game_window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    ShellExecuteA(game_window, NULL, found, parameters, dir, SW_SHOWNORMAL);
    return TRUE;
}

/* The bonus tracks' names, per event kind, from PCSched.pb. */
PORT(0x005294A0)
void __fastcall profile_load_bonus_tracks(char* self)
{
    static const struct {
        const char* section;
        int kind;
    } kinds[4] = {
        { NAME(0x00577924), 5 },    /* "Enduro" */
        { NAME(0x0057791C), 1 },    /* "Baja" */
        { NAME(0x00577910), 2 },    /* "Nationals" */
        { NAME(0x00577904), 3 },    /* "Supercross" */
    };
    param_file reader;
    char key[0x100];
    void* file;
    int number;
    int i;

    param_construct(&reader);
    file = game_new(0x134, UIINFO_CPP, 0x369);
    file = file ? game_FUN_00461c80(file, NULL, game_resources) : NULL;
    for (i = 0; i < 6; i++) {
        FIELD(self, P_BONUS + i * 4, DWORD) = 0;
        self[P_BONUS + 0x18 + i * 0x20] = 0;
    }
    /* Opened even when out of memory (kept). */
    if (game_FUN_00461ec0(file, NULL, SCHEDULE_FILE, READ_TEXT, 0)) {
        param_open(&reader, NULL, file, 0, TRUE);
        for (i = 0; i < 4; i++) {
            param_select(&reader, NULL, kinds[i].section);
            param_int(&reader, NULL, BONUS_TRACK, 0, &number);
            game_FUN_0053a11f(key, TRACK_KEY, number);
            param_text(&reader, NULL, key, NO_TEXT, self + P_BONUS + 0x18 + kinds[i].kind * 0x20,
                       -1);
        }
    }
    if (file) {
        game_FUN_00461cd0(file);
        game_delete(file);
    }
    param_destruct(&reader);
}

PORT(0x00529730)
char* __fastcall profile_set_event(char* self, void* edx_unused, int kind)
{
    (void) edx_unused;
    FIELD(self, P_EVENT, int) = kind;
    return FIELD(self, P_SCENE, char*) = self + P_SCENES + kind * 0x100;
}

PORT(0x00529750)
int __fastcall profile_event(char* self)
{
    return FIELD(self, P_EVENT, int);
}
