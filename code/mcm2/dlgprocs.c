/*
    OpenMCM2 - the menus' screens, ported from the game (dlgprocs.cpp in the original).

    Each function here replaces the game's at the address in its PORT line, and was checked against
    the original's instructions. Most take the event a control sent (its dialog is the event's
    sender) and look the screen's controls up by the names the dialog files give them.
*/

#include "menus.h"
#include "dirlist.h"
#include <imm.h>

#ifdef OPENMCM2_DEV
#include "../dev/dev.h"

/* The live background's progress, and its steps that the ported code below calls. */
enum { LIVE_OFF, LIVE_WAITING, LIVE_LOADING, LIVE_READY, LIVE_SHOWN };
static int live_state;
static BYTE* live_controls;         /* the replay's own controls, closed for the main menu */
static void live_menu_show(void);
static BOOL live_menu_leaving(BYTE* menu, ui_event* event);
#endif

/* The controls and lists these use, ported in ui.c. */
BYTE*       __fastcall dialog_find_control(BYTE* self, void* edx_unused, const char* name,
                                           int type);
int         __fastcall control_set_text(BYTE* self, void* edx_unused, const char* text);
int         __fastcall control_load_text(BYTE* self, void* edx_unused, HINSTANCE module, UINT id);
int         __fastcall list_selected(BYTE* self);
const char* __fastcall list_text(BYTE* self, void* edx_unused, int index);
BOOL        __fastcall list_add_text(BYTE* self, void* edx_unused, const char* text, DWORD data,
                                     DWORD b);
void        __fastcall list_clear(BYTE* self);
void        __fastcall control_show(BYTE* self, void* edx_unused, DWORD shown, BOOL animate);
void        __fastcall dialog_end(BYTE* self, void* edx_unused, DWORD code);
BYTE*       __fastcall dialog_construct(BYTE* self, void* edx_unused, DWORD flag, const char* name);
int         __fastcall dialog_tick(BYTE* self, void* edx_unused, float seconds);
int         __fastcall dialog_release(BYTE* self, void* edx_unused, const DWORD* key, DWORD arg);
int         __fastcall list_select_all(BYTE* self, void* edx_unused, int index);
BOOL        __fastcall game_FUN_004a3ef0(void* stream);                 /* start a movie */
BYTE*       __fastcall gui_manager_open(gui_manager* self, void* edx_unused, BYTE* dialog, DWORD a,
                                        DWORD flags, DWORD area, BYTE* parent, DWORD arg, DWORD c,
                                        DWORD wait);
void        __fastcall list_sort(BYTE* self, void* edx_unused, BOOL outer);
int         __fastcall list_select_text(BYTE* self, void* edx_unused, const char* text);
int         __fastcall list_set_208(BYTE* self, void* edx_unused, DWORD value);
int         __fastcall list_set_text_colour(BYTE* self, void* edx_unused, COLORREF colour,
                                            int index);
int         __fastcall list_set_colour_2(BYTE* self, void* edx_unused, COLORREF colour);
DWORD       __fastcall list_data(BYTE* self, void* edx_unused, int index);

/* The game's: atoi, a string from its language module, and the profile it saves to. */
int  __cdecl    game_FUN_0053b6c7(const char* text);
int  __cdecl    game_FUN_0053b6e0(const char* a, const char* b);           /* _stricmp */
BOOL __fastcall game_FUN_00526fc0(BYTE* game, void* edx_unused, UINT id, char* text, int size);
BOOL __fastcall profile_load(char* self);
int  __cdecl    game_FUN_0053a11f(char* to, const char* format, ...);      /* sprintf */
char* __cdecl   game__strncpy(char* to, const char* from, size_t count);
char* __cdecl   game_FUN_0054aaa5(int value, char* text, int radix);   /* _itoa */

/* The directory lister (dirlist.cpp) at game_main + 0x3338, and its calls. */
#define game_files FIELD(game_main, 0x3338, dir_list*)

#define LIST_PROFILES   NAME(0x0056F3E8)    /* "LstProfiles" */
#define PROFILE_FOLDER  NAME(0x0056F548)    /* "ui\profile" */
#define PROFILE_FILES   NAME(0x0056F334)    /* "*" */
#define TEXT_PROFILE    NAME(0x0056F7C0)    /* "TxtCurrentProfile" */
#define TWO_WORDS       NAME(0x0056E054)    /* "%s %s" */

#define PROFILE_COLOUR  0xFEB97A
#define PROFILE_LENGTH  15

#define DLGPROCS_CPP NAME(0x0056F744)     /* "...\dlgprocs.cpp" */

static BYTE* event_dialog(const ui_event* event)
{
    return (BYTE*) event->sender;
}

/* A dialog of `size` bytes read from `name`, given its kind's vtable (NULL when out of memory). */
static BYTE* dialog_new(size_t size, int line, const char* name, void* vtable)
{
    BYTE* dialog = game_new(size, DLGPROCS_CPP, line);

    if (dialog) {
        dialog_construct(dialog, NULL, 1, name);
        *(void**) dialog = vtable;
    }
    return dialog;
}

/* ---- Sorting ---- */

/* List sort orders by number: entries' texts as integers, rising and falling. */
PORT(0x0044C040)
int __cdecl entries_by_number(const BYTE* a, const BYTE* b)
{
    int first = game_FUN_0053b6c7(FIELD(a, 0x14, const char*));
    int second = game_FUN_0053b6c7(FIELD(b, 0x14, const char*));

    if (first < second)
        return -1;
    return first != second;
}

PORT(0x0044C080)
int __cdecl entries_by_number_falling(const BYTE* a, const BYTE* b)
{
    int first = game_FUN_0053b6c7(FIELD(a, 0x14, const char*));
    int second = game_FUN_0053b6c7(FIELD(b, 0x14, const char*));

    if (second < first)
        return -1;
    return first != second;
}

/* ---- Profiles ---- */

/* The profile picked in LstProfiles becomes the player's (at most 15 characters), and is saved. */
PORT(0x0044C0C0)
BOOL __cdecl profile_choose(ui_event* event)
{
    BYTE* list = dialog_find_control(event_dialog(event), NULL, LIST_PROFILES, 0);
    short index = (short) list_selected(list);
    char name[PROFILE_LENGTH + 1];
    size_t length;

    if (index == -1)
        return FALSE;
    length = text_length(list_text(list, NULL, index));
    if (length > PROFILE_LENGTH)
        length = PROFILE_LENGTH;
    game__strncpy(name, list_text(list, NULL, index), length);
    name[length] = 0;
    length = text_length(name);
    if (length > PROFILE_LENGTH)
        length = PROFILE_LENGTH;
    game__strncpy(game_profile, name, length);
    game_profile[length] = 0;
    profile_load(game_profile);
    return TRUE;
}

/* LstProfiles: the profile files there are, sorted, the player's selected. */
PORT(0x0044D7D0)
void __fastcall profiles_fill(BYTE* self)
{
    BYTE* list = dialog_find_control(self, NULL, LIST_PROFILES, 0);
    char name[0x104];

    list_clear(list);
    dir_list_set_folder(game_files, NULL, PROFILE_FOLDER);
    dir_list_set_pattern(game_files, NULL, PROFILE_FILES, 0);
    VIRTUAL(game_files, 4, method_0)(game_files);
    dir_list_current_title(game_files, NULL, name);
    if (name[0])
        list_add_text(list, NULL, name, 0, 0);
    while (dir_list_next_title(game_files, NULL, name)) {
        if (name[0])
            list_add_text(list, NULL, name, 0, 0);
    }
    list_sort(list, NULL, TRUE);
    list_set_text_colour(list, NULL, PROFILE_COLOUR, -1);
    list_select_text(list, NULL, game_profile);
    list_set_208(list, NULL, 0xFFFFFF);
    list_set_colour_2(list, NULL, PROFILE_COLOUR);
}

/* TxtCurrentProfile: string 5222 and the player's profile. */
PORT(0x0044D8F0)
void __fastcall profile_show(BYTE* self)
{
    BYTE* label = dialog_find_control(self, NULL, TEXT_PROFILE, 12);
    char caption[0x80];
    char text[0x80];

#ifdef OPENMCM2_DEV
    /* The modern rider card says what it is: the label is the name alone. */
    if (dev_settings.menus_modern) {
        control_set_text(label, NULL, game_profile);
        return;
    }
#endif
    game_FUN_00526fc0(game_main, NULL, 0x1466, caption, sizeof caption);
    game_FUN_0053a11f(text, TWO_WORDS, caption, game_profile);
    control_set_text(label, NULL, text);
}

/* ---- Statistics ---- */

static const char* const stats_buttons[4] = {
    NAME(0x0056F7F8), NAME(0x0056F7EC), NAME(0x0056F7E0), NAME(0x0056F7D4),   /* "ButStats1".. */
};

static const char* const stats_lists[4] = {
    NAME(0x0056F828), NAME(0x0056F81C), NAME(0x0056F810), NAME(0x0056F804),   /* "LstStats1".. */
};

/* The four columns' headings, from the language module's strings; 0 leaves one as it is. */
PORT(0x0044D960)
void __cdecl stats_load_headings(ui_event* event, UINT first, UINT second, UINT third, UINT fourth)
{
    const UINT ids[4] = { first, second, third, fourth };
    int i;

    for (i = 0; i < 4; i++) {
        if (ids[i])
            control_load_text(dialog_find_control(event_dialog(event), NULL, stats_buttons[i], 0),
                              NULL, game_language, ids[i]);
    }
}

/* The four columns' headings as texts; NULL leaves one as it is. */
PORT(0x0044DA20)
void __cdecl stats_set_headings(ui_event* event, const char* first, const char* second,
                                const char* third, const char* fourth)
{
    const char* texts[4] = { first, second, third, fourth };
    int i;

    for (i = 0; i < 4; i++) {
        if (texts[i])
            control_set_text(dialog_find_control(event_dialog(event), NULL, stats_buttons[i], 0),
                             NULL, texts[i]);
    }
}

/* Empty the four columns. */
PORT(0x0044DAB0)
void __cdecl stats_clear(ui_event* event)
{
    int i;

    for (i = 0; i < 4; i++)
        list_clear(dialog_find_control(event_dialog(event), NULL, stats_lists[i], 0));
}

/* A row: a text for each column; NULL adds nothing to one. */
PORT(0x0044DB10)
void __cdecl stats_add_row(ui_event* event, const char* first, const char* second,
                           const char* third, const char* fourth)
{
    const char* texts[4] = { first, second, third, fourth };
    int i;

    for (i = 0; i < 4; i++) {
        if (texts[i])
            list_add_text(dialog_find_control(event_dialog(event), NULL, stats_lists[i], 0), NULL,
                          texts[i], 0, 0);
    }
}

/* ---- Prompts ---- */

/*
    Small dialogs of a prompt and buttons. Their handlers (vtable slot 29) word them when they
    open (event 5) and end the dialog with a code for the screen behind when a button is
    clicked (event 1, naming the button).
*/
#define BUTTON_LEFT   NAME(0x0056F888)      /* "ButLeft" */
#define BUTTON_MIDDLE NAME(0x0056F87C)      /* "ButMiddle" */
#define BUTTON_RIGHT  NAME(0x0056F870)      /* "ButRight" */
#define TITLE_TEXT    NAME(0x0056F864)      /* "TitleText" */
#define PROMPT_TEXT   NAME(0x0056F858)      /* "TxtPrompt" */
#define KEY_RETURN    0x1C

static void load_text(BYTE* self, const char* name, int type, UINT id)
{
    control_load_text(dialog_find_control(self, NULL, name, type), NULL, game_language, id);
}

/* The prompt's text, wrapped (0x1E8). */
static void load_prompt(BYTE* self, UINT id)
{
    BYTE* prompt = dialog_find_control(self, NULL, PROMPT_TEXT, 12);

    FIELD(prompt, 0x1E8, DWORD) = 1;
    control_load_text(prompt, NULL, game_language, id);
}

/* One button, the middle, taking Return; the others hidden. */
static void one_button(BYTE* self, UINT id)
{
    BYTE* button = dialog_find_control(self, NULL, BUTTON_LEFT, 1);

    control_show(button, NULL, FALSE, TRUE);
    FIELD(button, 0x1D8, DWORD) = 0;
    control_show(dialog_find_control(self, NULL, BUTTON_RIGHT, 1), NULL, FALSE, TRUE);
    button = dialog_find_control(self, NULL, BUTTON_MIDDLE, 1);
    control_load_text(button, NULL, game_language, id);
    FIELD(button, 0x1D8, DWORD) = KEY_RETURN;
}

/* Three choices: left ends with 60, middle with 61, right with 0. */
PORT(0x0044DBB0)
void __fastcall choice_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_CLICK) {
        if (clicked(event, BUTTON_LEFT)) {
            dialog_end(event_dialog(event), NULL, 60);
            event->e = 1;
        } else if (clicked(event, BUTTON_MIDDLE)) {
            dialog_end(event_dialog(event), NULL, 61);
            event->e = 1;
        } else if (clicked(event, BUTTON_RIGHT)) {
            dialog_end(event_dialog(event), NULL, 0);
            event->e = 1;
        }
    } else if (event->type == EVENT_OPEN) {
        load_text(self, BUTTON_LEFT, 1, 0x13E7);
        load_text(self, BUTTON_MIDDLE, 1, 0x13E6);
        load_text(self, BUTTON_RIGHT, 1, 0x13DC);
        load_text(self, TITLE_TEXT, 12, 0x13E8);
        load_prompt(self, 0x13FE);
    }
}

/* A notice whose one button ends it with 51. */
PORT(0x0044DD20)
void __fastcall notice_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_CLICK) {
        if (clicked(event, BUTTON_MIDDLE)) {
            dialog_end(event_dialog(event), NULL, 51);
            event->e = 1;
        }
    } else if (event->type == EVENT_OPEN) {
        one_button(self, 0x13E9);
        load_text(self, TITLE_TEXT, 12, 0x13EA);
        load_prompt(self, 0x1400);
    }
}

/* A notice whose one button ends it with 0, leaving the event to others. */
PORT(0x0044DE40)
void __fastcall notice_close_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_CLICK) {
        if (clicked(event, BUTTON_MIDDLE))
            dialog_end(event_dialog(event), NULL, 0);
    } else if (event->type == EVENT_OPEN) {
        one_button(self, 0x13E9);
        load_text(self, TITLE_TEXT, 12, 0x146D);
        load_prompt(self, 0x146E);
    }
}

/* ---- Screens ---- */

/*
    A screen with a movie (0x7F58) that starts 4 seconds after it is ready (0x7F78), while its
    page at 0x7F60 is hidden. Vtable slot 10: tick.
*/
PORT(0x0044D6F0)
int __fastcall movie_screen_tick(BYTE* self, void* edx_unused, float seconds)
{
    (void) edx_unused;
    if (FIELD(self, 0x7F6C, int)) {
        BYTE* over = dialog_find_control(self, NULL, NAME(0x0056F724), 0);  /* ScreenOverCtl */

        VIRTUAL(over, 0xC8, method_0)(over);
        if (FIELD(self, 0x7F6C, int) && !FIELD(FIELD(self, 0x7F60, BYTE*), 0x68, int)
            && FIELD(self, 0x7F78, int)) {
            if (x87_less(FIELD(self, 0x7F7C, float), 4.0f)) {
                FIELD(self, 0x7F7C, float) = (float) ((double) FIELD(game_main, 0x2F0, float)
                                                      + FIELD(self, 0x7F7C, float));
                return dialog_tick(self, NULL, seconds);
            }
            FIELD(self, 0x7F78, int) = 0;
#ifdef OPENMCM2_DEV
            video_play(FIELD(self, 0x7F58, BYTE*));         /* no stream: video.c plays it */
            video_pause(FIELD(self, 0x7F58, BYTE*), FIELD(self, 0x7F78, DWORD));
#else
            game_FUN_004a3ef0(FIELD(FIELD(self, 0x7F58, BYTE*), 0x1EC, void*));
            VIRTUAL(FIELD(FIELD(self, 0x7F58, BYTE*), 0x1EC, void*), 0x40, method_1)(
                FIELD(FIELD(self, 0x7F58, BYTE*), 0x1EC, void*), NULL, FIELD(self, 0x7F78, DWORD));
#endif
            control_show(FIELD(self, 0x7F58, BYTE*), NULL, TRUE, TRUE);
        }
    }
    return dialog_tick(self, NULL, seconds);
}

/* The single-player screen's pages: an event's, the bike's, the rider's. */
#define SP_PAGES 0x7F58             /* BYTE*[3], each a dialog open over the screen */

static const struct {
    DWORD       size;
    int         line;
    const char* file;
    void**      vtable;
} sp_pages[3] = {
    { 0x7F68, 0x545, NAME(0x0056F95C), (void**) 0x00556D68 },               /* SPEvent.dtm */
    { 0x7F9C, 0x54B, NAME(0x0056F968), (void**) 0x00556DEC },               /* SPBikeR.dtm */
    { 0x7F58, 0x551, NAME(0x0056F974), (void**) 0x00556E70 },               /* SPRInfo.dtm */
};

#ifdef OPENMCM2_DEV
/* A MCM2:Reborn screen with pages (single player's, the pro circuit's, multiplayer's) titles
   each one itself, Title0 to Title3. */
static const char* const page_titles[4] = { "Title0", "Title1", "Title2", "Title3" };

void page_title_show(BYTE* self, int page)
{
    int i;

    for (i = 0; i < 4; i++) {
        BYTE* title = dialog_find_control(self, NULL, page_titles[i], 0);

        if (title)
            control_show(title, NULL, i == page, FALSE);
    }
}
#endif

/* Show page `page` (0 to 2) over the screen, closing the others; one already open stays. */
PORT(0x0044E600)
void __fastcall sp_show_page(BYTE* self, void* edx_unused, int page)
{
    RECT area = { 16, 41, 626, 396 };
    BYTE* dialog;
    int i;

    (void) edx_unused;
#ifdef OPENMCM2_DEV
    if (page >= 0 && page <= 2)
        page_title_show(self, page);
#endif
    for (i = 0; i < 3; i++) {
        BYTE** open = &FIELD(self, SP_PAGES + 4 * i, BYTE*);

        if (*open && page != i) {
            object_destroy(*open);
            *open = NULL;
        }
    }
    if (page < 0 || page > 2 || FIELD(self, SP_PAGES + 4 * page, BYTE*))
        return;
    dialog = game_new(sp_pages[page].size, DLGPROCS_CPP, sp_pages[page].line);
    if (dialog) {
        dialog_construct(dialog, NULL, 1, sp_pages[page].file);
        *(void***) dialog = sp_pages[page].vtable;
    }
    FIELD(self, SP_PAGES + 4 * page, BYTE*) = dialog;
    gui_manager_open(FIELD(FIELD(game_main, 0x56C, BYTE*), 0x2C, gui_manager*), NULL, dialog, 0, 1,
                     (DWORD) &area, self, 0, 0, 1);
}

/* ---- Loading screens ---- */

/* loading.dtm, named for the event it loads (the name it reads its sections by). */
static BYTE* loading_construct(BYTE* self, DWORD flag, void** vtable, const char* name)
{
    dialog_construct(self, NULL, flag, NAME(0x0056FD04));                   /* loading.dtm */
    *(void***) self = vtable;
    text_copy((char*) self + 0x38, name);
    return self;
}

PORT(0x00451C50)
BYTE* __fastcall loading_supercross_construct(BYTE* self, void* edx_unused, DWORD flag)
{
    (void) edx_unused;
    return loading_construct(self, flag, (void**) 0x00556EF8, NAME(0x0056FCF8));   /* LoadSX.dtm */
}

PORT(0x00451CA0)
BYTE* __fastcall loading_enduro_construct(BYTE* self, void* edx_unused, DWORD flag)
{
    (void) edx_unused;
    return loading_construct(self, flag, (void**) 0x00556F7C, NAME(0x0056FD2C));   /* LoadEnd.dtm */
}

PORT(0x00451CF0)
BYTE* __fastcall loading_quarry_construct(BYTE* self, void* edx_unused, DWORD flag)
{
    (void) edx_unused;
    return loading_construct(self, flag, (void**) 0x00557000, NAME(0x0056FD54));   /* LoadQuar.dtm */
}

PORT(0x00451D40)
BYTE* __fastcall loading_national_construct(BYTE* self, void* edx_unused, DWORD flag)
{
    (void) edx_unused;
    return loading_construct(self, flag, (void**) 0x00557084, NAME(0x0056FD88));   /* LoadNat.dtm */
}

PORT(0x00451D90)
BYTE* __fastcall loading_tag_construct(BYTE* self, void* edx_unused, DWORD flag)
{
    (void) edx_unused;
    return loading_construct(self, flag, (void**) 0x00557108, NAME(0x0056FDB4));   /* LoadTag.dtm */
}

PORT(0x00451DE0)
BYTE* __fastcall loading_baja_construct(BYTE* self, void* edx_unused, DWORD flag)
{
    (void) edx_unused;
    return loading_construct(self, flag, (void**) 0x0055718C, NAME(0x0056FDDC));   /* LoadBaja.dtm */
}

/* ---- Ghost races ---- */

/*
    The ghost race picker keeps the recordings it found: the one chosen (0x7F58), and an array
    of 0x7F64 paths (0x7F60), all on the game's heap.
*/
#define GHOST_CHOSEN 0x7F58
#define GHOST_PATHS  0x7F60
#define GHOST_COUNT  0x7F64

/*
    The chosen record and the listed records' paths, freed. The ghost and replay screens share
    this layout; `line` is the first free's line in the leak report, the others follow it.
*/
static void records_release(BYTE* self, int line)
{
    if (FIELD(self, GHOST_CHOSEN, void*))
        game_FUN_004a4450(FIELD(self, GHOST_CHOSEN, void*), DLGPROCS_CPP, line);
    if (FIELD(self, GHOST_PATHS, char**)) {
        int i;

        for (i = 0; i < FIELD(self, GHOST_COUNT, int); i++)
            game_FUN_004a4450(FIELD(self, GHOST_PATHS, char**)[i], DLGPROCS_CPP, line + 4);
        game_FUN_004a4450(FIELD(self, GHOST_PATHS, char**), DLGPROCS_CPP, line + 6);
    }
}

static void records_free(BYTE* self, int line)
{
    records_release(self, line);
    FIELD(self, GHOST_CHOSEN, void*) = NULL;
    FIELD(self, GHOST_PATHS, char**) = NULL;
    FIELD(self, GHOST_COUNT, int) = 0;
    FIELD(self, 0x7F5C, DWORD) = 0;
}

PORT(0x0044FDB0)
void __fastcall ghosts_free(BYTE* self)
{
    records_free(self, 0x77A);
}

/*
    A ghost file is the 0xEB0-byte record of a race: the rider (0x8), the time (0x28, float),
    and the track it was run on, by kind (0x60) and name (0x62). The session's tracks are
    0x54 bytes each from its 0x60: the kind at 0x4, the name at 0x14.
*/
#define RECORD_SIZE     0xEB0
#define RECORD_RIDER    0x8
#define RECORD_TIME     0x28
#define RECORD_KIND     0x60
#define RECORD_TRACK    0x62
#define LIST_TEXT_BOX   NAME(0x0056FA10)    /* "DDLTextBox": the track chosen */
#define LIST_GHOSTS     NAME(0x0056F9CC)    /* "DDLGhostRaces" */
#define GHOST_LABEL     NAME(0x0056FB7C)    /* "%s: %s" */

void*  __cdecl game_FUN_0053a35b(const char* path, const char* mode);              /* fopen */
size_t __cdecl game_FUN_0053a5d5(void* to, size_t size, size_t count, void* file); /* fread */
int    __cdecl game_FUN_0053a2ad(void* file);                                      /* fclose */
void*  __cdecl ui_realloc(void* block, size_t size);

/* Keep a read record and its file's path; `line` is the path allocation's, for the leak report. */
static void record_keep(BYTE* self, const BYTE* record, const char* path, int line)
{
    int count = FIELD(self, 0x7F5C, int);
    char** paths;

    FIELD(self, 0x7F58, BYTE*) = ui_realloc(FIELD(self, 0x7F58, BYTE*), (count + 1) * RECORD_SIZE);
    __movsd((unsigned long*) (FIELD(self, 0x7F58, BYTE*) + count * RECORD_SIZE),
            (const unsigned long*) record, RECORD_SIZE / 4);
    FIELD(self, 0x7F5C, int)++;

    paths = ui_realloc(FIELD(self, GHOST_PATHS, char**),
                       (FIELD(self, GHOST_COUNT, int) + 1) * sizeof(char*));
    FIELD(self, GHOST_PATHS, char**) = paths;
    paths[FIELD(self, GHOST_COUNT, int)] = game_FUN_004a4410(text_length(path) + 1, DLGPROCS_CPP,
                                                            line);
    text_copy(paths[FIELD(self, GHOST_COUNT, int)], path);
    FIELD(self, GHOST_COUNT, int)++;
}

/*
    Read the ghost at `path`; one of a lap on the track chosen in DDLTextBox, with a time, is
    kept with its path and listed in DDLGhostRaces. TRUE when it was. (The original first runs
    the record's member constructors, which the read then overwrites.)
*/
/* Out of line, as the original calls it: its 4 KB frame must not join a caller's. */
__declspec(noinline) BOOL __fastcall ghost_add(BYTE* self, void* edx_unused, const char* path);

PORT(0x0044FB30)
BOOL __fastcall ghost_add(BYTE* self, void* edx_unused, const char* path)
{
    char what[0x80];
    char label[0x80];
    BYTE record[RECORD_SIZE];
    BOOL added = FALSE;
    void* file;

    (void) edx_unused;
    file = game_FUN_0053a35b(path, NAME(0x0056C66C));                          /* "rb" */
    if (!file)
        return FALSE;
    if (game_FUN_0053a5d5(record, RECORD_SIZE, 1, file)) {
        BYTE* chosen = FIELD(dialog_find_control(self, NULL, LIST_TEXT_BOX, 6), COMBO_LIST, BYTE*);
        int track = (int) list_data(chosen, NULL, -1) * 0x54;

        if (!game_FUN_0053b6e0((const char*) record + RECORD_TRACK,
                               FIELD(game_session, 0x60, char*) + track + 0x14)
            && FIELD(record, RECORD_TIME, float) > 0.0f
            && FIELD(FIELD(game_session, 0x60, BYTE*) + track, 0x4, DWORD)
               == FIELD(record, RECORD_KIND, BYTE)) {
            record_keep(self, record, path, 0x763);
            chosen = FIELD(dialog_find_control(self, NULL, LIST_GHOSTS, 6), COMBO_LIST, BYTE*);
            game_FUN_00526fc0(game_main, NULL, 0x14C8, what, sizeof what);
            game_FUN_0053a11f(label, GHOST_LABEL, what, (const char*) record + RECORD_RIDER);
            list_add_text(chosen, NULL, label, FIELD(self, 0x7F5C, int) - 1, 0);
            added = TRUE;
        }
    }
    game_FUN_0053a2ad(file);
    return added;
}

/* DDLGhostRaces: "none" (string 5319), then each Record\*.gho there is. */
PORT(0x0044FA20)
void __fastcall ghosts_fill(BYTE* self)
{
    BYTE* list;
    char none[0x80];
    char path[0x104];
    WIN32_FIND_DATAA found;
    HANDLE search;

    ghosts_free(self);
    list = FIELD(dialog_find_control(self, NULL, NAME(0x0056F9CC), 6), 0x1FC, BYTE*);
    list_clear(list);
    game_FUN_00526fc0(game_main, NULL, 0x14C7, none, sizeof none);
    list_add_text(list, NULL, none, (DWORD) -1, 0);
    list_select_all(list, NULL, 0);
#ifdef OPENMCM2_DEV
    if (list_selected(FIELD(dialog_find_control(self, NULL, LIST_TEXT_BOX, 6), COMBO_LIST, BYTE*)) < 0)
        return;
#endif
    search = FindFirstFileA(NAME(0x0056FB6C), &found);                      /* Record\*.gho */
    if (search == INVALID_HANDLE_VALUE)
        return;
    do {
        game_FUN_0053a11f(path, NAME(0x0056C7DC), NAME(0x0056FB64), found.cFileName);  /* %s\%s */
        ghost_add(self, NULL, path);
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

/* ---- Odds and ends ---- */

/* Slot 0 of a dialog kind that frees nothing of its own. */
PORT(0x0044E810)
BYTE* __fastcall bare_dialog_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    *(void***) self = (void**) 0x00556CD8;
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Vtable slot 22: the first mouse button released ends the hold (0x7F7C). */
PORT(0x004508E0)
int __fastcall hold_release(BYTE* self, void* edx_unused, const DWORD* key, DWORD arg)
{
    (void) edx_unused;
    if (key[1] == 1 && key[0] == 0)
        FIELD(self, 0x7F7C, DWORD) = 0;
    return dialog_release(self, NULL, key, arg);
}

/* ---- Waiting screens ---- */

/* Set when a key ends a wait screen; the race reads it. */
#define wait_skipped (*(DWORD*) 0x005A0F50)

BYTE* __fastcall game_FUN_0045e300(BYTE* events);   /* the race under way, or NULL */

/* Vtable slot 23: any key ends the wait, and closes the screen. */
PORT(0x00454220)
int __fastcall wait_key_close(BYTE* self, void* edx_unused, const DWORD* key, DWORD arg)
{
    (void) edx_unused;
    (void) key;
    (void) arg;
    wait_skipped = 1;
    dialog_end(self, NULL, 0);
    return 1;
}

/* Vtable slot 23: any key ends the wait. */
PORT(0x004543A0)
int __fastcall wait_key(BYTE* self, void* edx_unused, const DWORD* key, DWORD arg)
{
    (void) self;
    (void) edx_unused;
    (void) key;
    (void) arg;
    wait_skipped = 1;
    return 1;
}

/* Vtable slot 23: a key ends the wait once the screen allows it (0x7F60, or 0x7F5C, set). */
PORT(0x00454570)
int __fastcall wait_key_when_ready(BYTE* self, void* edx_unused, const DWORD* key, DWORD arg)
{
    (void) edx_unused;
    (void) key;
    (void) arg;
    if (FIELD(self, 0x7F60, int) == 1)
        wait_skipped = 1;
    return 1;
}

PORT(0x004546D0)
int __fastcall wait_key_when_shown(BYTE* self, void* edx_unused, const DWORD* key, DWORD arg)
{
    (void) edx_unused;
    (void) key;
    (void) arg;
    if (FIELD(self, 0x7F5C, int) == 1)
        wait_skipped = 1;
    return 1;
}

/*
    Vtable slot 10 of the wait screens. A count of ticks (0x7F58) moves the session on once the
    screen has drawn; a timer ends the wait after 3 seconds, or when a key skipped it.
*/
#define WAIT_SECONDS 3.0

void __fastcall session_build_scene(BYTE* self, void* edx_unused, int stage);

PORT(0x00454350)
int __fastcall wait_stage_tick(BYTE* self, void* edx_unused, float seconds)
{
    (void) edx_unused;
    if (++FIELD(self, 0x7F58, int) == 3) {
        session_build_scene(FIELD(game_main, 0x56C, BYTE*), NULL, 1);
        dialog_end(self, NULL, 0);
    }
    return dialog_tick(self, NULL, seconds);
}

/* The time waited (0x7F58); keys count once it has ticked (0x7F5C). */
PORT(0x00454680)
int __fastcall wait_timed_tick(BYTE* self, void* edx_unused, float seconds)
{
    double waited = (double) seconds + FIELD(self, 0x7F58, float);

    (void) edx_unused;
    FIELD(self, 0x7F5C, int) = 1;
    FIELD(self, 0x7F58, float) = (float) waited;
    if (wait_skipped || waited > WAIT_SECONDS)
        dialog_end(self, NULL, 0);
    return dialog_tick(self, NULL, seconds);
}

/* Both: the session moves on at the third tick, and the wait (0x7F5C) ends no sooner. */
PORT(0x004544F0)
int __fastcall wait_stage_timed_tick(BYTE* self, void* edx_unused, float seconds)
{
    double waited;

    (void) edx_unused;
    if (++FIELD(self, 0x7F58, int) == 3)
        session_build_scene(FIELD(game_main, 0x56C, BYTE*), NULL, 2);
    waited = (double) seconds + FIELD(self, 0x7F5C, float);
    FIELD(self, 0x7F60, int) = 1;
    FIELD(self, 0x7F5C, float) = (float) waited;
    if ((wait_skipped || waited > WAIT_SECONDS) && FIELD(self, 0x7F58, int) >= 3)
        dialog_end(self, NULL, 0);
    return dialog_tick(self, NULL, seconds);
}

/* Vtable slot 23: a key closes the screen once the race under way has finished (0x18A). */
PORT(0x00456AE0)
int __fastcall race_over_key(BYTE* self, void* edx_unused, const DWORD* key, DWORD arg)
{
    BYTE* race = game_FUN_0045e300(FIELD(game_main, 0x570, BYTE*));

    (void) edx_unused;
    (void) key;
    (void) arg;
    if (race && FIELD(race, 0x18A, BYTE))
        dialog_end(self, NULL, 0);
    return 1;
}

/* Vtable slot 10: the session's request to leave the menus (0x44) closes the screen. */
PORT(0x00456B20)
int __fastcall leave_request_tick(BYTE* self, void* edx_unused, float seconds)
{
    BYTE* session = FIELD(game_main, 0x56C, BYTE*);

    (void) edx_unused;
    if (FIELD(session, 0x44, int)) {
        FIELD(session, 0x44, int) = 0;
        dialog_end(self, NULL, 0);
        return 0;
    }
    return dialog_tick(self, NULL, seconds);
}

/* ---- Asking ---- */

/* Three choices ending the dialog itself: left with 101, middle with 102, right with 103. */
PORT(0x004565B0)
void __fastcall three_way_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type != EVENT_CLICK)
        return;
    if (clicked(event, BUTTON_LEFT))
        dialog_end(self, NULL, 0x65);
    else if (clicked(event, BUTTON_MIDDLE))
        dialog_end(self, NULL, 0x66);
    else if (clicked(event, BUTTON_RIGHT))
        dialog_end(self, NULL, 0x67);
}

/*
    A line of text to type: the caller's buffer (0x7F58) and its size (0x7F5C) take the edit
    box's text when OK ends the dialog with 201; Cancel ends it with 202.
*/
#define EDIT_BOX      NAME(0x0056FE58)      /* "EditBox" */
#define OK_BUTTON     NAME(0x0056FE60)      /* "OkButton" */
#define CANCEL_BUTTON NAME(0x0056FF44)      /* "CancelButton" */

int   __fastcall edit_set_capacity(BYTE* self, void* edx_unused, int length);
int   __fastcall edit_set_text(BYTE* self, void* edx_unused, const char* text);
char* __fastcall edit_get_text(BYTE* self, void* edx_unused, char* buffer, int size);

/* Out of line: the ids variant calls it, as in the original. */
__declspec(noinline) void __fastcall text_entry_setup(BYTE* self, void* edx_unused,
                                                      const char* title, const char* prompt,
                                                      char* buffer, int size);

PORT(0x00456840)
void __fastcall text_entry_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_CLICK) {
        if (clicked(event, OK_BUTTON)) {
            edit_get_text(dialog_find_control(self, NULL, EDIT_BOX, 11), NULL,
                          FIELD(self, 0x7F58, char*), FIELD(self, 0x7F5C, int));
            dialog_end(self, NULL, 0xC9);
        } else if (clicked(event, CANCEL_BUTTON)) {
            dialog_end(self, NULL, 0xCA);
        }
    } else if (event->type == EVENT_OPEN) {
        FIELD(self, 0x7F58, char*) = NULL;
        FIELD(self, 0x7F5C, int) = 0;
        gui_user_queue_capture(FIELD(self, 0x34, gui_user*), NULL,
                               dialog_find_control(self, NULL, EDIT_BOX, 0), NULL, FALSE);
    }
}

PORT(0x004568F0)
void __fastcall text_entry_setup(BYTE* self, void* edx_unused, const char* title,
                                 const char* prompt, char* buffer, int size)
{
    BYTE* edit;

    (void) edx_unused;
    FIELD(self, 0x7F58, char*) = buffer;
    FIELD(self, 0x7F5C, int) = size;
    control_set_text(dialog_find_control(self, NULL, TITLE_TEXT, 0), NULL, title);
    control_set_text(dialog_find_control(self, NULL, PROMPT_TEXT, 0), NULL, prompt);
    edit = dialog_find_control(self, NULL, EDIT_BOX, 11);
    edit_set_capacity(edit, NULL, size - 1);
    edit_set_text(edit, NULL, buffer);
}

/* The same, the title and prompt given by string ids where those are not 0. */
PORT(0x00456970)
void __fastcall text_entry_setup_ids(BYTE* self, void* edx_unused, const char* title,
                                     UINT title_id, const char* prompt, UINT prompt_id,
                                     char* buffer, int size)
{
    char title_text[0x80];
    char prompt_text[0x80];

    (void) edx_unused;
    if (title_id)
        game_FUN_00526fc0(game_main, NULL, title_id, title_text, sizeof title_text);
    if (prompt_id) {
        game_FUN_00526fc0(game_main, NULL, prompt_id, prompt_text, sizeof prompt_text);
        prompt = prompt_text;
    }
    text_entry_setup(self, NULL, title_id ? title_text : title, prompt, buffer, size);
}

/* The prompt's title and text, and the buttons' words; a button given none is hidden. */
__declspec(noinline) void __fastcall prompt_setup(BYTE* self, void* edx_unused,
                                                  const char* title, const char* prompt,
                                                  const char* left, const char* middle,
                                                  const char* right);

static void set_or_hide(BYTE* control, const char* text)
{
    if (text && *text)
        control_set_text(control, NULL, text);
    else
        control_show(control, NULL, FALSE, TRUE);
}

PORT(0x00456630)
void __fastcall prompt_setup(BYTE* self, void* edx_unused, const char* title, const char* prompt,
                             const char* left, const char* middle, const char* right)
{
    (void) edx_unused;
    control_set_text(dialog_find_control(self, NULL, TITLE_TEXT, 0), NULL, title);
    control_set_text(dialog_find_control(self, NULL, PROMPT_TEXT, 0), NULL, prompt);
    set_or_hide(dialog_find_control(self, NULL, BUTTON_LEFT, 0), left);
    set_or_hide(dialog_find_control(self, NULL, BUTTON_MIDDLE, 0), middle);
    set_or_hide(dialog_find_control(self, NULL, BUTTON_RIGHT, 0), right);
}

/* The same, each text given by a string id where that is not 0. */
PORT(0x00456700)
void __fastcall prompt_setup_ids(BYTE* self, void* edx_unused, const char* title, UINT title_id,
                                 const char* prompt, UINT prompt_id, const char* left,
                                 UINT left_id, const char* middle, UINT middle_id,
                                 const char* right, UINT right_id)
{
    char texts[5][0x80];

    (void) edx_unused;
    if (title_id)
        game_FUN_00526fc0(game_main, NULL, title_id, texts[0], sizeof texts[0]);
    if (prompt_id)
        game_FUN_00526fc0(game_main, NULL, prompt_id, texts[1], sizeof texts[1]);
    if (left_id)
        game_FUN_00526fc0(game_main, NULL, left_id, texts[2], sizeof texts[2]);
    if (middle_id)
        game_FUN_00526fc0(game_main, NULL, middle_id, texts[3], sizeof texts[3]);
    if (right_id)
        game_FUN_00526fc0(game_main, NULL, right_id, texts[4], sizeof texts[4]);
    prompt_setup(self, NULL, title_id ? texts[0] : title, prompt_id ? texts[1] : prompt,
                 left_id ? texts[2] : left, middle_id ? texts[3] : middle,
                 right_id ? texts[4] : right);
}

/*
    Asks about the thing named in the event's parameter: left ends with 31, right with 0.
    The title reads 'name' and string 0x13CF.
*/
#define QUOTED_NAME NAME(0x0056FF60)    /* "'%s' %s" */

PORT(0x00453180)
void __fastcall named_choice_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_CLICK) {
        DWORD code;

        if (clicked(event, BUTTON_LEFT))
            code = 0x1F;
        else if (clicked(event, BUTTON_RIGHT))
            code = 0;
        else
            return;
        dialog_end(event_dialog(event), NULL, code);
        event->e = 1;
    } else if (event->type == EVENT_OPEN) {
        BYTE* title = dialog_find_control(self, NULL, TITLE_TEXT, 12);
        BYTE* button = dialog_find_control(self, NULL, BUTTON_LEFT, 1);
        char what[0x80];
        char text[0x100];

        control_load_text(button, NULL, game_language, 0x13EC);
        FIELD(button, 0x1D8, DWORD) = 0;
        button = dialog_find_control(self, NULL, BUTTON_RIGHT, 1);
        control_load_text(button, NULL, game_language, 0x13EB);
        FIELD(button, 0x1D8, DWORD) = 0;
        control_show(dialog_find_control(self, NULL, BUTTON_MIDDLE, 1), NULL, FALSE, TRUE);
        game_FUN_00526fc0(game_main, NULL, 0x13CF, what, sizeof what);
        game_FUN_0053a11f(text, QUOTED_NAME, (const char*) event->param, what);
        control_set_text(title, NULL, text);
        load_prompt(self, 0x1403);
    }
}

/*
    A prompt quoting the name in the event's parameter. Left or Enter answers through
    result_confirm, Right or Escape through result_back, both with result 303.
*/
#define QUOTED      NAME(0x0056E540)    /* "\"%s\"" */
#define KEY_ESCAPE  0x1B
#define KEY_ENTER   0x0D

int  __fastcall control_set_align(BYTE* self, void* edx_unused, DWORD align);
void __cdecl    result_confirm(int code, ui_event* event);       /* below */
void __cdecl    result_back(int code, ui_event* event);

static void set_string(BYTE* control, UINT id)
{
    char text[0x80];

    game_FUN_00526fc0(game_main, NULL, id, text, sizeof text);
    control_set_text(control, NULL, text);
}

PORT(0x00453300)
void __fastcall quoted_choice_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_CLICK) {
        if (clicked(event, BUTTON_RIGHT))
            result_back(0x12F, event);
        else if (clicked(event, BUTTON_LEFT))
            result_confirm(0x12F, event);
    } else if (event->type == EVENT_OPEN) {
        BYTE* title = dialog_find_control(self, NULL, TITLE_TEXT, 0);
        char text[0x80];

        game_FUN_00526fc0(game_main, NULL, 0xFEF, text, sizeof text);
        control_set_align(title, NULL, 10);
        control_set_text(title, NULL, text);
        game_FUN_0053a11f(text, QUOTED, (const char*) event->param);
        control_set_text(dialog_find_control(self, NULL, PROMPT_TEXT, 12), NULL, text);
        set_string(dialog_find_control(self, NULL, BUTTON_LEFT, 0), 0x13E9);
        set_string(dialog_find_control(self, NULL, BUTTON_RIGHT, 0), 0x13DC);
        control_show(dialog_find_control(self, NULL, BUTTON_MIDDLE, 1), NULL, FALSE, TRUE);
    } else if (event->type == EVENT_KEY) {
        if (event->d == KEY_ESCAPE)
            result_back(0x12F, event);
        else if (event->d == KEY_ENTER)
            result_confirm(0x12F, event);
    }
}

/* ---- Leaving ---- */

void __fastcall game_FUN_0045f530(BYTE* events);
void __fastcall session_go_to(BYTE* self, void* edx_unused, int screen);
void __fastcall game_FUN_00526eb0(BYTE* game, void* edx_unused, DWORD a, DWORD b, DWORD c);

/* The session's next screen. */
static void go_to(int screen)
{
    session_go_to(FIELD(game_main, 0x56C, BYTE*), NULL, screen);
}

/*
    Where a screen's result code takes the session. Those it knows end the screen that sent the
    event (with 14 for 252, else 0) and mark the event handled; 400 asks the game instead.
*/
PORT(0x00453EA0)
void __cdecl result_go(int code, ui_event* event)
{
    DWORD end = 0;
    BYTE* session;
    int screen;

    switch (code) {
    case 0x10E:
        game_FUN_0045f530(FIELD(game_main, 0x570, BYTE*));
        go_to(FIELD(game_main, 0x8, int) ? 0x867 : 0x65);
        break;
    case 0x65:
        go_to(100);
        break;
    case 0x66:
        go_to(FIELD(FIELD(game_main, 0x56C, BYTE*), 0x30, int));
        break;
    case 0xFC:
        end = 14;
        break;
    case 0x190:
        game_FUN_00526eb0(game_main, NULL, 0, 400, 1);
        return;
    case 0x910:
    case 0x911:
        session = FIELD(game_main, 0x56C, BYTE*);
        screen = FIELD(session, 0x3C, int);
        if (screen == 0x910)
            go_to(0x65);
        else if (screen == 0x911)
            go_to(FIELD(session, 0x38, int));
        break;
    default:
        return;
    }
    dialog_end(event_dialog(event), NULL, end);
    event->e = 1;
}

/* ---- Intro ---- */

#define cursors_locked  (FIELD(game_main, 0x2D5, BYTE) & 2)

void __fastcall gui_manager_show_cursors(gui_manager* self, void* edx_unused, BOOL show);
void __fastcall game_FUN_00469760(BYTE* game);

/* The intro's next screen, opened as this one closes. */
static void open_next(size_t size, int line, const char* name, void* vtable)
{
    gui_manager_open(session_gui, NULL, dialog_new(size, line, name, vtable), 0, 2, 0, NULL, 0,
                     0, 0);
}

/*
    The intro's screens open one another as each closes: intro, intro2, intro3, then the
    intro video, and the menus after it. A key skips them (see wait_skipped).
*/

/* Without the cursor; a key skips it unless the game asks otherwise (0xC44). Then intro2. */
PORT(0x00454240)
void __fastcall intro_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_OPEN) {
        gui_manager_show_cursors(session_gui, NULL, FALSE);
        FIELD(self, 0x7F58, DWORD) = 0;
        wait_skipped = !FIELD(game_main, 0xC44, int);
    } else if (event->type == EVENT_CLOSE && !cursors_locked) {
        open_next(0x7F64, 0x101A, NAME(0x0056FFA8), (void*) 0x00557294);     /* intro2.dtm */
    }
}

/* intro3.dtm: then the intro video. */
PORT(0x00454590)
void __fastcall intro3_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_OPEN) {
        FIELD(self, 0x7F5C, DWORD) = 0;
        FIELD(self, 0x7F58, DWORD) = 0;
        game_FUN_00469760(game_main);
    } else if (event->type == EVENT_CLOSE && !cursors_locked) {
        open_next(0x7F60, 0x106F, NAME(0x0056FFFC), (void*) 0x0055739C);     /* IntroVid.dtm */
    }
}

/* intro2.dtm: then intro3, or the menus when skipped. */
PORT(0x004543B0)
void __fastcall intro2_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_OPEN) {
        FIELD(self, 0x7F60, DWORD) = 0;
        FIELD(self, 0x7F5C, DWORD) = 0;
        FIELD(self, 0x7F58, DWORD) = 0;
        game_FUN_00469760(game_main);
    } else if (event->type == EVENT_CLOSE && !cursors_locked) {
        if (!wait_skipped) {
            open_next(0x7F60, 0x1041, NAME(0x0056FFD0), (void*) 0x00557318);   /* intro3.dtm */
        } else {
            go_to(100);
            gui_manager_show_cursors(session_gui, NULL, TRUE);
        }
    }
}

/* ---- Movies ---- */

BYTE* __fastcall movie_construct(BYTE* self, void* edx_unused, DWORD a, const RECT* area,
                                 BYTE* dialog);
BOOL  __fastcall movie_open(BYTE* self, void* edx_unused, const char* file, BOOL play, DWORD a,
                            DWORD b);
BYTE* __fastcall dialog_add(BYTE* self, void* edx_unused, BYTE* control, DWORD id, DWORD keep);
int   __fastcall game_FUN_0046a0d0(void* self);                 /* unlink from its parent */
#define object_unlink game_FUN_0046a0d0

#define MOVIE_ENDED 0x0044C1C0      /* what the movie calls when it ends */

/* A movie filling the 640x480 screen, added to it. */
static BYTE* movie_add(BYTE* self, int line)
{
    RECT area = { 0, 0, 640, 480 };
    BYTE* movie = game_new(0x200, DLGPROCS_CPP, line);

    if (movie)
        movie = movie_construct(movie, NULL, 0, &area, self);
    return movie;
}

/*
    IntroVid.dtm: ui\Intro.avi, unless skipped; the menus once it closes. The dev build plays the
    launcher's MPEG-1 copy of it (video.c), and goes straight on to the menus without one.
*/
PORT(0x004540D0)
void __fastcall intro_video_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_OPEN) {
        gui_manager_show_cursors(session_gui, NULL, FALSE);
        if (!wait_skipped) {
            FIELD(self, 0x7F5C, BYTE*) = movie_add(self, 0xFEF);
            dialog_add(self, NULL, FIELD(self, 0x7F5C, BYTE*), 0, 1);
            if (movie_open(FIELD(self, 0x7F5C, BYTE*), NULL, NAME(0x0056FF80), TRUE, MOVIE_ENDED,
                           (DWORD) self))
                return;
        }
        dialog_end(self, NULL, 0);
    } else if (event->type == EVENT_CLOSE && !cursors_locked) {
        go_to(100);
        gui_manager_show_cursors(session_gui, NULL, TRUE);
    }
}

/* The credits: ui\Credits.avi behind the Back button; the menus once it closes (at once in the
   dev build when there is no MPEG-1 copy of it, as the intro). */
PORT(0x00453F80)
void __fastcall credits_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_CLICK) {
        if (clicked(event, NAME(0x0056F8CC)))                                 /* "Back" */
            dialog_end(self, NULL, 0);
    } else if (event->type == EVENT_OPEN) {
        FIELD(self, 0x7F58, BYTE*) = movie_add(self, 0xFAD);
        dialog_add(self, NULL, FIELD(self, 0x7F58, BYTE*), 0, 1);
        object_unlink(FIELD(self, 0x7F58, BYTE*));
        object_move_before(FIELD(self, 0x7F58, BYTE*), NULL,
                           dialog_find_control(self, NULL, NAME(0x0056F8CC), 0), -1);
        if (!movie_open(FIELD(self, 0x7F58, BYTE*), NULL, NAME(0x0056FF70), TRUE, MOVIE_ENDED,
                        (DWORD) self))
            dialog_end(self, NULL, 0);
    } else if (event->type == EVENT_CLOSE && !cursors_locked) {
        go_to(100);
    }
}

/* ---- Previews ---- */

int   __fastcall dialog_draw_over(BYTE* self);
void  __fastcall game_FUN_00445c10(BYTE* model, void* edx_unused, DWORD a, BYTE* look,
                                   BYTE* display);

/*
    Vtable slot 13: draw. A bike or rider picked from DDLBikes or DDLRiders since the last draw
    (0x7F84, 0x7F88) dresses the model showing it.
*/
PORT(0x00450DF0)
int __fastcall preview_draw(BYTE* self)
{
    int drawn = dialog_draw_over(self);
    BYTE* combo;
    BYTE* session;
    BYTE* look;

    if (FIELD(self, 0x110, BYTE*))
        background_release_dc(FIELD(self, 0x110, BYTE*));
    if (FIELD(self, 0x7F84, int)) {
        combo = dialog_find_control(self, NULL, NAME(0x0056FBCC), 6);       /* DDLBikes */
        session = game_session;
        look = FIELD(session, 0x50, BYTE*)
               + list_data(FIELD(combo, COMBO_LIST, BYTE*), NULL, -1) * 0x94;
        game_FUN_00445c10(player_model(FIELD(session, 0x48, BYTE*) + FIELD(look, 0, int) * 200),
                          NULL, 0, look + 0x48, game_main + 0x1C);
        FIELD(self, 0x7F84, int) = 0;
    }
    if (FIELD(self, 0x7F88, int)) {
        combo = dialog_find_control(self, NULL, NAME(0x0056FBC0), 6);       /* DDLRiders */
        session = game_session;
        look = FIELD(session, 0x58, BYTE*)
               + list_data(FIELD(combo, COMBO_LIST, BYTE*), NULL, -1) * 200;
        game_FUN_00445c10(FIELD(FIELD(session, 0x46C, BYTE*), 0x1A0, BYTE*), NULL, 0, look + 0x40,
                          game_main + 0x1C);
        FIELD(self, 0x7F88, int) = 0;
    }
    return drawn;
}

/*
    DDLBikes: the session's bikes (0x94 bytes each from 0x50, 0x54 of them) a player can ride -
    those marked at 0x88, or whose maker's player kind (0xC4) is 2 or 3, or 10 with bit 0 of the
    game's 0x1EE8 - as "maker model", sorted, with the game's bike (0x1FB0) picked. DDLRiders:
    the riders (200 bytes each from 0x58, 0x5C of them) of kind 2 or 3 or marked at 0xC0, with
    the one named at the game's 0x1F6C picked.
*/

int __fastcall list_select_data(BYTE* self, void* edx_unused, DWORD data);
int __cdecl    game__strcmp(const char* a, const char* b);

PORT(0x00450630)
void __fastcall choices_fill(BYTE* self)
{
    BYTE* list = FIELD(dialog_find_control(self, NULL, NAME(0x0056FBCC), 6), COMBO_LIST, BYTE*);
    int bike_chosen = 0;
    int rider_chosen = 0;
    char label[0x80];
    int i;

    list_clear(list);
    for (i = 0; i < FIELD(game_session, 0x54, int); i++) {
        BYTE* bike = FIELD(game_session, 0x50, BYTE*) + i * BIKE_SIZE;
        BYTE* maker = session_players + FIELD(bike, 0, int) * RIDER_SIZE;

        if (FIELD(bike, 0x88, DWORD) || maker_rideable(FIELD(maker, 0xC4, int))) {
            game_FUN_0053a11f(label, TWO_WORDS, (const char*) maker, (const char*) bike + 4);
            list_add_text(list, NULL, label, i, 0);
            if (FIELD(game_main, 0x1FB0, int) == i)
                bike_chosen = i;
        }
    }
    if (bike_chosen)
        list_select_data(list, NULL, bike_chosen);
    else
        list_select_all(list, NULL, 0);
    list_sort(list, NULL, TRUE);

    list = FIELD(dialog_find_control(self, NULL, NAME(0x0056FBC0), 6), COMBO_LIST, BYTE*);
    list_clear(list);
    for (i = 0; i < FIELD(game_session, 0x5C, int); i++) {
        BYTE* rider = FIELD(game_session, 0x58, BYTE*) + i * RIDER_SIZE;

        if (rider_offered(rider)) {
            list_add_text(list, NULL, (const char*) rider, i, 0);
            rider = FIELD(game_session, 0x58, BYTE*) + i * RIDER_SIZE;
            if (!game__strcmp((const char*) rider + 0x40, (const char*) game_main + 0x1F6C))
                rider_chosen = i;
        }
    }
    list_select_data(list, NULL, rider_chosen);
}

void __fastcall game_FUN_00418300(DWORD* scope, void* edx_unused, BYTE* display);
void __fastcall game_FUN_00418470(DWORD* scope, void* edx_unused, BYTE* model, DWORD value);
void __fastcall game_FUN_00418370(DWORD* scope);

/* Each player's model is given `value`, under one scope on the display. */
PORT(0x00450830)
void __fastcall players_set(BYTE* self, void* edx_unused, DWORD value)
{
    DWORD scope[11];
    int i;

    (void) self;
    (void) edx_unused;
    game_FUN_00418300(scope, NULL, FIELD(game_main, 0x1C, BYTE*));
    for (i = 0; i < FIELD(game_session, 0x4C, int); i++)
        game_FUN_00418470(scope, NULL, player_model(session_players + i * 200), value);
    game_FUN_00418370(scope);
}

/* ---- Starting ---- */

#define race_starting (*(DWORD*) 0x005A0FD4)

int  __fastcall game_FUN_0045d930(BYTE* events);
void __fastcall game_FUN_004b0f20(BYTE* net, void* edx_unused, DWORD type, void* data, DWORD size,
                                  DWORD to, DWORD flags);
void __cdecl net_leave(void);                 /* leave the network game */
void __fastcall session_show_cursors(BYTE* self);
void __fastcall player_clear_c8(BYTE* self);
void __fastcall dialog_add_timer(BYTE* self, void* edx_unused, DWORD id, DWORD period,
                                 BYTE* target);

/*
    Vtable slot 10: at the third tick the race is started. If it cannot be, the screen closes,
    a network game is told (message 204) and left, and the menus go back to screen 100.
    Otherwise a network game's players are readied and a timer set.
*/
PORT(0x00452080)
int __fastcall race_start_tick(BYTE* self, void* edx_unused, float seconds)
{
    (void) edx_unused;
    if (FIELD(self, 0x7F58, int) == 2 && !race_starting) {
        BYTE* net;
        int i;

        if (!game_FUN_0045d930(FIELD(game_main, 0x570, BYTE*))) {
            dialog_end(self, NULL, 0);
            net = FIELD(game_main, 0x8, BYTE*);
            if (net) {
                /* The first word is left unset by the original; zeroed here. */
                DWORD message[2] = { 0, FIELD(net, 0xC, DWORD) };

                game_FUN_004b0f20(net, NULL, 0xCC, message, sizeof message, message[1], 0);
                net_leave();
            }
            session_show_cursors(game_session);
            go_to(100);
            return 1;
        }
        if (FIELD(game_main, 0x8, BYTE*))
            for (i = 0; i < FIELD(game_main, 0x2158, int); i++)
#ifdef OPENMCM2_DEV
                player_clear_c8(entrants() + i * 0xF8);
#else
                player_clear_c8(game_main + 0x215C + i * 0xF8);
#endif
        race_starting = 1;
        dialog_add_timer(self, NULL, 0, 100, NULL);
    }
    FIELD(self, 0x7F58, int)++;
    return dialog_tick(self, NULL, seconds);
}

/*
    The loading screen. Opening puts a 20-step ProgressBar (drawn with the BlueBar animation) in
    place of StaBarRect, and starts the race loading; its timer (event 7) ends the screen and
    starts the race on the tenth tick (0x5A0F4C).
*/
#define loading_ticks   (*(int*) 0x005A0F4C)
#define loading_d8      (*(DWORD*) 0x005A0FD8)   /* cleared as the screen opens */

BYTE* __fastcall progress_construct(BYTE* self, void* edx_unused, DWORD a, const RECT* area,
                                    BYTE* dialog);
void  __fastcall progress_set_picture(BYTE* self, void* edx_unused, BYTE* picture, BOOL owned);
void  __fastcall control_set_name(BYTE* self, void* edx_unused, const char* name);
int   __fastcall dialog_find(void* dialog, void* edx_unused, const char* name);
BYTE* __fastcall anim_picture(ui_anim* self);
void  __fastcall dialog_remove_timer(BYTE* self, void* edx_unused, DWORD id);
void __fastcall session_hide_cursors(BYTE* self);
void __fastcall profile_save(char* self);
void  __fastcall game_FUN_004b0fc0(BYTE* net, void* edx_unused, DWORD a, DWORD b);
void  __fastcall game_FUN_0045e280(BYTE* events);       /* start the race */
void  __fastcall game_FUN_00527090(BYTE* game);
void  __fastcall game_FUN_00421f90(BYTE* race);
void  __fastcall game_FUN_004e6c70(BYTE* object);

static void loading_open(BYTE* self)
{
    BYTE* place = dialog_find_control(self, NULL, NAME(0x0056FE00), 5);         /* StaBarRect */
    BYTE* bar = game_new(0x204, DLGPROCS_CPP, 0xADF);

    if (bar)
        bar = progress_construct(bar, NULL, 0, (const RECT*) (place + 0x3C), self);
    dialog_add(self, NULL, bar, 0, 0);
    control_set_name(bar, NULL, NAME(0x0056FDF4));                              /* ProgressBar */
    progress_set_picture(bar, NULL,
                         anim_picture((ui_anim*) dialog_find(self, NULL, NAME(0x0056FDEC))),
                         FALSE);                                                /* BlueBar */
    FIELD(bar, 0x1F0, int) = 20;                    /* steps */
    FIELD(bar, 0x1FC, DWORD) = 0;                   /* no percentage */
    control_show(place, NULL, FALSE, TRUE);

    FIELD(game_session, 0x490, BYTE*) = self;
    session_hide_cursors(game_session);
    race_starting = 0;
    profile_save(game_profile);
    if (FIELD(game_main, 0x8, BYTE*))
        game_FUN_004b0fc0(FIELD(game_main, 0x8, BYTE*), NULL,
                          FIELD(FIELD(game_main, 0x570, BYTE*), 0x2C, DWORD),
                          FIELD(FIELD(game_main, 0x570, BYTE*), 0x30, DWORD));
    loading_ticks = 0;
    loading_d8 = 0;
    FIELD(self, 0x7F58, DWORD) = 0;
}

PORT(0x00451E30)
void __fastcall loading_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_OPEN) {
        loading_open(self);
    } else if (event->type == EVENT_CLOSE) {
        dialog_remove_timer(self, NULL, 0);
        gui_manager_show_cursors(FIELD(self, 0x30, gui_manager*), NULL, FALSE);
        FIELD(game_session, 0x490, BYTE*) = NULL;
    } else if (event->type == EVENT_TIMER) {
        if (loading_ticks >= 10) {
            if (FIELD(game_main, 0x8, BYTE*)) {
                FIELD(game_main, 0xC1C, DWORD) = 0;
                dialog_end(event_dialog(event), NULL, 0);
                event->e = 1;
            } else {
                dialog_end(event_dialog(event), NULL, 0);
                event->e = 1;
                game_FUN_00527090(game_main);
                game_FUN_00469760(game_main);
                game_FUN_00421f90(game_FUN_0045e300(FIELD(game_main, 0x570, BYTE*)));
                if (FIELD(game_main, 0x55C, BYTE*))
                    game_FUN_004e6c70(FIELD(game_main, 0x55C, BYTE*));
                if (FIELD(game_main, 0x568, BYTE*))
                    game_FUN_004e6c70(FIELD(game_main, 0x568, BYTE*));
            }
            game_FUN_0045e280(FIELD(game_main, 0x570, BYTE*));
#ifdef OPENMCM2_DEV
            if (live_state == LIVE_READY)
                live_menu_show();
#endif
        }
        loading_ticks++;
    }
}

/* ---- Tracks ---- */

#define LIST_TRACK  NAME(0x005700EC)    /* "LstTrack" */
#define TRACK_SIZE  0xEB0
#define TRACK_NAME_LENGTH 0x103

char* __fastcall profile_set_event(char* self, void* edx_unused, int kind);
__declspec(noinline) void __cdecl race_start_loading(void);     /* below */

/* The track picked in the list (of those at 0x7F58): its settings become the race's. */
static BYTE* track_take(BYTE* self, BYTE* list)
{
    BYTE* track = FIELD(self, 0x7F58, BYTE*) + list_data(list, NULL, -1) * TRACK_SIZE;

    __movsd((unsigned long*) (game_main + 0x2D70), (unsigned long*) (track + 0x34), 0x7B);
    return track;
}

/* Its name, from the screen's names (0x7F60), at most 259 characters. */
static void track_name_copy(BYTE* self, BYTE* list, char* to)
{
    char** names = FIELD(self, 0x7F60, char**);
    size_t length = text_length(names[list_data(list, NULL, -1)]);

    if ((int) length > TRACK_NAME_LENGTH)
        length = TRACK_NAME_LENGTH;
    game__strncpy(to, names[list_data(list, NULL, -1)], length);
    to[length] = 0;
}

static void track_chosen(BYTE* self)
{
    BYTE* owner = FIELD(self, 0x2C, BYTE*);

    VIRTUAL(owner, 0x68, method_0)(owner);
}

PORT(0x00455970)
void __fastcall track_choose(BYTE* self)
{
    BYTE* list = dialog_find_control(self, NULL, LIST_TRACK, 3);
    BYTE* track = track_take(self, list);

    track_name_copy(self, list, (char*) game_main + 0x2B64);
    track_chosen(self);
    profile_set_event(game_profile, NULL, FIELD(track, 0x3C, DWORD));
    race_start_loading();
}

/* The same for a track whose settings at 0x33C come with it. */
PORT(0x00456490)
void __fastcall track_choose_set(BYTE* self)
{
    BYTE* list = dialog_find_control(self, NULL, LIST_TRACK, 3);
    BYTE* track = track_take(self, list);

    profile_set_event(game_profile, NULL, FIELD(track, 0x3C, DWORD));
    FIELD(game_main, 0x3428, DWORD) = 1;
    FIELD(game_main, 0x342C, DWORD) = 1;
    FIELD(game_main, 0x1550, DWORD) = FIELD(track, 0x33C, DWORD);
    FIELD(game_main, 0x1554, DWORD) = FIELD(track, 0x340, DWORD);
    track_name_copy(self, list, (char*) game_main + 0x2C6C);
    track_chosen(self);
    race_start_loading();
}

/*
    The record screens' lists, refilled from the Record folder's files of a kind; the owner's
    Start button is shown only when some were listed.
*/
typedef BOOL (__fastcall* record_add)(BYTE* self, void* edx_unused, const char* path);

BOOL __fastcall ghost_record_add(BYTE* self, void* edx_unused, const char* path);
BOOL __fastcall replay_record_add(BYTE* self, void* edx_unused, const char* path);

#define LIST_RIDER  NAME(0x00570120)    /* "LstRider" */
#define LIST_LAPS   NAME(0x0057010C)    /* "LstLaps" */
#define LIST_LENGTH NAME(0x00570114)    /* "LstLength" */
#define LIST_DESC   NAME(0x005700CC)    /* "LstDesc" */
#define START       NAME(0x0056F8C4)    /* "Start" */

/*
    The record screens' rows read the race record's description (0x10), time (0x30, float),
    track (0x3C, and its file name at 0x6A) and rider (0x229). The track's own name comes from
    its scn file through the game's resources (0x574).
*/
#define RECORD_DESC         0x10
#define RECORD_RUN_TIME     0x30
#define RECORD_TRACK_ID     0x3C
#define RECORD_TRACK_FILE   0x6A
#define RECORD_RIDER_NAME   0x229
#define NO_NAME             NAME(0x0057012C)    /* "no name": a track the scn file leaves unnamed */

/* A track file's path. */
BOOL __fastcall profile_find_named_file(char* self, void* edx_unused, const char* folder,
                                        const char* name, const char* ext, char* dir);
void __fastcall game_FUN_004ee910(BYTE* resources, void* edx_unused, const char* path);
void __fastcall game_FUN_004eeda0(BYTE* resources, void* edx_unused, char* text, const char* file,
                                  DWORD a, const char* kind, DWORD b, DWORD c);
void __cdecl time_text(char* text, float seconds);       /* a time, as text */

/* The record's track becomes the profile's, and its env file is loaded. */
static void record_track_load(const BYTE* record)
{
    char env_path[0x104];

    profile_set_event(game_profile, NULL, FIELD(record, RECORD_TRACK_ID, DWORD));
    profile_find_named_file(game_profile, NULL, FIELD(game_main, 0xC18, const char*),
                            (const char*) record + RECORD_TRACK_FILE, NAME(0x00570138),
                            env_path);
    game_FUN_004ee910(FIELD(game_main, 0x574, BYTE*), NULL, env_path);           /* "env" */
}

/* Read the ghost at `path`; one with a time is kept and listed. TRUE when it was. */
PORT(0x00455640)
BOOL __fastcall ghost_record_add(BYTE* self, void* edx_unused, const char* path)
{
    char text[0x80];
    char scene_name[0x40];
    BYTE record[RECORD_SIZE];
    BOOL added = FALSE;
    void* file;

    (void) edx_unused;
    file = game_FUN_0053a35b(path, NAME(0x0056C66C));                          /* "rb" */
    if (!file)
        return FALSE;
    if (game_FUN_0053a5d5(record, RECORD_SIZE, 1, file)
        && FIELD(record, RECORD_RUN_TIME, float) > 0.0f) {
        BYTE* tracks;
        BYTE* riders;
        BYTE* lengths;
        BYTE* descriptions;
        size_t length;

        record_keep(self, record, path, 0x11C5);
        tracks = dialog_find_control(self, NULL, LIST_TRACK, 3);
        riders = dialog_find_control(self, NULL, LIST_RIDER, 3);
        lengths = dialog_find_control(self, NULL, LIST_LENGTH, 3);
        descriptions = dialog_find_control(self, NULL, LIST_DESC, 3);
        record_track_load(record);
        list_add_text(riders, NULL, (const char*) record + RECORD_RIDER_NAME, 0, 0);

        game_FUN_004eeda0(FIELD(game_main, 0x574, BYTE*), NULL, scene_name,
                          (const char*) record + RECORD_TRACK_FILE, 0, NAME(0x00570134), 0, 0);
        length = text_length(scene_name);                                       /* "scn" */
        if ((int) length > 0x7F)
            length = 0x7F;
        game__strncpy(text, scene_name, length);
        text[length] = 0;
        if (!game_FUN_0053b6e0(text, NO_NAME))
            game_FUN_00526fc0(game_main, NULL, 0x143B, text, sizeof text);
        list_add_text(tracks, NULL, text, FIELD(self, 0x7F5C, int) - 1, 0);

        time_text(text, FIELD(record, RECORD_RUN_TIME, float));
        list_add_text(lengths, NULL, text, 0, 0);
        list_add_text(descriptions, NULL, (const char*) record + RECORD_DESC, 0, 0);
        added = TRUE;
    }
    game_FUN_0053a2ad(file);
    return added;
}

/*
    Replays add a laps column (0x54, for race kinds 1-3 and 5; "--" otherwise, the kind at
    0x38) and name a numbered track (kinds 1 and 5 with 0x60 set) "name, #n" from 0x64; the
    scn file's name is looked up with the record's variant (0x68).
*/
#define RECORD_KIND_ID      0x38
#define RECORD_LAPS         0x54
#define RECORD_NUMBERED     0x60
#define RECORD_NUMBER       0x64
#define RECORD_VARIANT      0x68
#define NO_LAPS             NAME(0x00570144)    /* "--" */
#define NUMBERED_TRACK      NAME(0x0057013C)    /* "%s, #%d" */

PORT(0x00456040)
BOOL __fastcall replay_record_add(BYTE* self, void* edx_unused, const char* path)
{
    char text[0x80];
    char scene_name[0x40];
    BYTE record[RECORD_SIZE];
    BOOL added = FALSE;
    void* file;

    (void) edx_unused;
    file = game_FUN_0053a35b(path, NAME(0x0056C66C));                          /* "rb" */
    if (!file)
        return FALSE;
    if (game_FUN_0053a5d5(record, RECORD_SIZE, 1, file)
        && FIELD(record, RECORD_RUN_TIME, float) > 0.0f) {
        int kind = FIELD(record, RECORD_KIND_ID, int);
        BYTE* tracks;
        BYTE* riders;
        BYTE* laps;
        BYTE* lengths;
        BYTE* descriptions;

        record_keep(self, record, path, 0x129A);
        tracks = dialog_find_control(self, NULL, LIST_TRACK, 3);
        riders = dialog_find_control(self, NULL, LIST_RIDER, 3);
        laps = dialog_find_control(self, NULL, LIST_LAPS, 3);
        lengths = dialog_find_control(self, NULL, LIST_LENGTH, 3);
        descriptions = dialog_find_control(self, NULL, LIST_DESC, 3);
        record_track_load(record);

        text_copy(text, NO_LAPS);
        if (kind == 1 || kind == 2 || kind == 3 || kind == 5)
            game_FUN_0054aaa5(FIELD(record, RECORD_LAPS, int), text, 10);
        list_add_text(laps, NULL, text, 0, 0);
        list_add_text(riders, NULL, (const char*) record + RECORD_RIDER_NAME, 0, 0);

        game_FUN_004eeda0(FIELD(game_main, 0x574, BYTE*), NULL, scene_name,
                          (const char*) record + RECORD_TRACK_FILE,
                          FIELD(record, RECORD_VARIANT, BYTE), NAME(0x00570134), 0, 0);
        if (!game_FUN_0053b6e0(scene_name, NO_NAME)) {
            game_FUN_00526fc0(game_main, NULL, 0x143B, text, sizeof text);
        } else if ((kind == 1 || kind == 5) && FIELD(record, RECORD_NUMBERED, BYTE)) {
            game_FUN_004eeda0(FIELD(game_main, 0x574, BYTE*), NULL, scene_name,
                              (const char*) record + RECORD_TRACK_FILE, 0, NAME(0x00570134), 0,
                              0);
            game_FUN_0053a11f(text, NUMBERED_TRACK, scene_name, FIELD(record, RECORD_NUMBER, int));
        } else {
            text_copy(text, scene_name);
        }
        list_add_text(tracks, NULL, text, FIELD(self, 0x7F5C, int) - 1, 0);

        time_text(text, FIELD(record, RECORD_RUN_TIME, float));
        list_add_text(lengths, NULL, text, 0, 0);
        list_add_text(descriptions, NULL, (const char*) record + RECORD_DESC, 0, 0);
        added = TRUE;
    }
    game_FUN_0053a2ad(file);
    return added;
}

static void records_list(BYTE* self, const char* pattern, record_add add)
{
    char path[0x104];
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    int listed = 0;

    if (search != INVALID_HANDLE_VALUE) {
        do {
            game_FUN_0053a11f(path, NAME(0x0056C7DC), NAME(0x0056FB64), found.cFileName);
            if (add(self, NULL, path))
                listed++;
        } while (FindNextFileA(search, &found));
        FindClose(search);
    }
    control_show(dialog_find_control(FIELD(self, 0x2C, BYTE*), NULL, START, 0), NULL, listed != 0,
                 TRUE);
}

static void list_clear_named(BYTE* self, const char* name)
{
    list_clear(dialog_find_control(self, NULL, name, 3));
}

PORT(0x00455470)
void __fastcall ghost_records_fill(BYTE* self)
{
    records_free(self, 0x118B);
    list_clear_named(self, LIST_TRACK);
    list_clear_named(self, LIST_RIDER);
    list_clear_named(self, LIST_LENGTH);
    list_clear_named(self, LIST_DESC);
    records_list(self, NAME(0x0056FB6C), ghost_record_add);                  /* Record\*.gho */
}

PORT(0x00455E60)
void __fastcall replay_records_fill(BYTE* self)
{
    records_free(self, 0x125E);
    list_clear_named(self, LIST_TRACK);
    list_clear_named(self, LIST_RIDER);
    list_clear_named(self, LIST_LAPS);
    list_clear_named(self, LIST_LENGTH);
    list_clear_named(self, LIST_DESC);
    records_list(self, NAME(0x0056F848), replay_record_add);                 /* Record\*.vcr */
}

/*
    The record screens' filter panel over (20, 35)-(620, 395): the ghost filter (0x7F58) for
    kind 0, the replay filter (0x7F5C) for kind 1. The other kind's panel is closed first.
*/
enum { FILTER_GHOSTS, FILTER_REPLAYS };

static void filter_close(BYTE* self, int offset)
{
    BYTE* panel = FIELD(self, offset, BYTE*);

    VIRTUAL(panel, 8, method_0)(panel);
    FIELD(self, offset, BYTE*) = NULL;
}

PORT(0x00454E90)
void __fastcall records_filter_show(BYTE* self, void* edx_unused, int kind)
{
    RECT area = { 20, 35, 620, 395 };
    BYTE* panel;

    (void) edx_unused;
#ifdef OPENMCM2_DEV
    if (kind == FILTER_GHOSTS || kind == FILTER_REPLAYS)
        page_title_show(self, kind == FILTER_GHOSTS ? 0 : 1);
#endif
    if (FIELD(self, 0x7F58, BYTE*) && kind != FILTER_GHOSTS)
        filter_close(self, 0x7F58);
    if (FIELD(self, 0x7F5C, BYTE*) && kind != FILTER_REPLAYS)
        filter_close(self, 0x7F5C);

    if (kind == FILTER_GHOSTS) {
        if (FIELD(self, 0x7F58, BYTE*))
            return;
        panel = dialog_new(0x7F88, 0x1126, NAME(0x00570060), (void*) 0x00557420); /* GhostFil.dtm */
        FIELD(self, 0x7F58, BYTE*) = panel;
    } else if (kind == FILTER_REPLAYS) {
        if (FIELD(self, 0x7F5C, BYTE*))
            return;
        panel = dialog_new(0x7F88, 0x112C, NAME(0x00570070), (void*) 0x005574A4); /* ReplyFil.dtm */
        FIELD(self, 0x7F5C, BYTE*) = panel;
    } else {
        return;
    }
    gui_manager_open(session_gui, NULL, panel, 0, 1, (DWORD) &area, self, 0, 0, 1);
}

/*
    The record screen: TabLeft shows the ghost filter, TabRight the replay filter (shown at
    first). Start takes the open panel's track; Back closes the panels and goes to screen 100.
*/
#define TAB_LEFT    NAME(0x00570018)    /* "TabLeft" */
#define TAB_RIGHT   NAME(0x0057000C)    /* "TabRight" */
#define HELP        NAME(0x0056F8A8)    /* "Help" */
#define HELP_TOPIC  NAME(0x0056F560)    /* "MCM2HELP" */
#define TAB_CONTROL 4

/* Out of line: the original calls these. */
__declspec(noinline) void __fastcall records_filter_show(BYTE* self, void* edx_unused, int kind);
__declspec(noinline) void __fastcall track_choose(BYTE* self);
__declspec(noinline) void __fastcall track_choose_set(BYTE* self);

int  __fastcall option_load_text(BYTE* self, void* edx_unused, int index, HINSTANCE module,
                                 UINT id);
BOOL __fastcall radio_set(BYTE* self, void* edx_unused, int number);
BOOL __fastcall profile_open_help(char* self, void* edx_unused, const char* topic,
                                  const char* parameters);

static void tab_setup(BYTE* self, const char* name, UINT id)
{
    BYTE* tab = dialog_find_control(self, NULL, name, TAB_CONTROL);

#ifdef OPENMCM2_DEV
    if (menu_reborn(self))                      /* its tabs' words are in their pictures */
        return;
#endif
    option_load_text(tab, NULL, 0, game_language, id);
    option_load_text(tab, NULL, 1, game_language, id);
    control_set_align(tab, NULL, 0x22);
}

PORT(0x00454C10)
void __fastcall records_screen_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_CLICK) {
        if (clicked(event, NAME(0x0056F8CC))) {                                 /* "Back" */
            if (FIELD(self, 0x7F58, BYTE*))
                filter_close(self, 0x7F58);
            if (FIELD(self, 0x7F5C, BYTE*))
                filter_close(self, 0x7F5C);
            dialog_end(event_dialog(event), NULL, 0);
            event->e = 1;
            go_to(100);
        } else if (clicked(event, TAB_LEFT)) {
            records_filter_show(self, NULL, FILTER_GHOSTS);
        } else if (clicked(event, TAB_RIGHT)) {
            records_filter_show(self, NULL, FILTER_REPLAYS);
        } else if (clicked(event, HELP)) {
            profile_open_help(game_profile, NULL, HELP_TOPIC, 0);
        } else if (clicked(event, START)) {
            if (FIELD(self, 0x7F58, BYTE*)) {
                track_choose(FIELD(self, 0x7F58, BYTE*));
                dialog_end(self, NULL, 0);
                return;
            }
            if (FIELD(self, 0x7F5C, BYTE*))
                track_choose_set(FIELD(self, 0x7F5C, BYTE*));
            dialog_end(self, NULL, 0);
        }
    } else if (event->type == EVENT_OPEN) {
        FIELD(self, 0x7F58, BYTE*) = NULL;
        FIELD(self, 0x7F5C, BYTE*) = NULL;
        tab_setup(self, TAB_LEFT, 0x143A);
        tab_setup(self, TAB_RIGHT, 0x1439);
        records_filter_show(self, NULL, FILTER_REPLAYS);
        radio_set(dialog_find_control(self, NULL, TAB_RIGHT, TAB_CONTROL), NULL, 0);
    }
}

/* ---- Replays ---- */

void __fastcall game_FUN_0045ddd0(BYTE* events, void* edx_unused, DWORD a);
void __fastcall game_FUN_0045f720(BYTE* events, void* edx_unused, DWORD a);

/* Back ends it; closing puts back the race settings saved before it and shows the cursor. */
PORT(0x00456A10)
void __fastcall back_restore_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_CLICK) {
        if (clicked(event, NAME(0x0056F8CC)))                                 /* "Back" */
            dialog_end(self, NULL, 0);
    } else if (event->type == EVENT_CLOSE && !cursors_locked) {
        game_FUN_0045ddd0(FIELD(game_main, 0x570, BYTE*), NULL, 2);
        __movsd((unsigned long*) (game_main + 0x2D70), (unsigned long*) (game_main + 0x2F5C), 0x7B);
        __movsd((unsigned long*) (game_main + 0x1550), (unsigned long*) (game_main + 0x15AC), 0x17);
        __movsd((unsigned long*) (game_main + 0x1EEC), (unsigned long*) (game_main + 0x1FB4), 0x32);
        game_FUN_0045f720(FIELD(game_main, 0x570, BYTE*), NULL, 100);
        gui_manager_show_cursors(FIELD(self, 0x30, gui_manager*), NULL, TRUE);
    }
}

void __fastcall game_FUN_004ade20(BYTE* replay);
void __fastcall game_FUN_004b0c00(BYTE* net, void* edx_unused, DWORD a);

/*
    Leaving the race (401): the replay being recorded of the watched rider is closed, the race
    stopped, and the menus go back to the session's screen - out of a network game for a
    one-race session (0x2D70 = 2).
*/
static void race_leave(void)
{
    BYTE* replay;
    BYTE* net;

    game_FUN_00526eb0(game_main, NULL, 0, 0x191, 0);
    replay = FIELD(game_main, 0x568, BYTE*);
    net = FIELD(game_main, 0x8, BYTE*);
    if (replay && net && FIELD(net, 0x10, DWORD) && FIELD(replay, 0xA8, BYTE*)
        && FIELD(replay, 0x34, BYTE*)
        && FIELD(replay, 0xA8, BYTE*) == FIELD(FIELD(replay, 0x34, BYTE*), 0x38, BYTE*)) {
        BYTE* rider = FIELD(FIELD(replay, 0x34, BYTE*), 0x38, BYTE*);

        if (rider)
            FIELD(rider, 0x4A0, DWORD) = 1;
        game_FUN_004ade20(FIELD(game_main, 0x568, BYTE*));
    }
    game_FUN_0045ddd0(FIELD(game_main, 0x570, BYTE*), NULL, 2);
    if (FIELD(game_main, 0x8, BYTE*)) {
        game_FUN_004b0c00(FIELD(game_main, 0x8, BYTE*), NULL, 1);
        if (FIELD(game_main, 0x2D70, int) == 2) {
            net_leave();
            game_FUN_0045f720(FIELD(game_main, 0x570, BYTE*), NULL, 100);
        } else {
            game_FUN_0045f720(FIELD(game_main, 0x570, BYTE*), NULL, 0x867);
        }
    } else {
        game_FUN_0045f720(FIELD(game_main, 0x570, BYTE*), NULL,
                          FIELD(game_main, 0x3440, int) ? 0x88F : 0x65);
    }
}

/*
    The other answer to a screen's result code (see result_go). Those it knows end the screen
    that sent the event, most with 0, and mark the event handled; 401 leaves the race instead.
*/
PORT(0x004534C0)
void __cdecl result_back(int code, ui_event* event)
{
    DWORD end = 0;

    switch (code) {
    case 0xC9:
    case 0xCA:
    case 0xCB:
        go_to(0x66);
        break;
    case 0xCD:
        go_to(FIELD(game_session, 0x30, int));
        break;
    case 0xD0:
    case 0x12F:
    case 0x1F9:
    case 0xBB9:
    case 0xBBA:
    case 0xBBB:
        break;
    case 0xD8:
    case 0xD9:
        end = 14;
        break;
    case 0x104:
    case 0x105:
        go_to(0x65);
        break;
    case 0x12C:
        end = 0x15;
        break;
    case 0x191:
        race_leave();
        return;
    default:
        return;
    }
    dialog_end(event_dialog(event), NULL, end);
    event->e = 1;
}

/* ---- Records ---- */

/* A record of three 128-character names and the numbers after them, emptied; scales 1, 1, 0.5. */
PORT(0x00456BA0)
BYTE* __fastcall record_init(BYTE* self)
{
    int offset;

    self[0] = 0;
    self[0x80] = 0;
    self[0x100] = 0;
    for (offset = 0x180; offset <= 0x1B8; offset += 4)
        FIELD(self, offset, DWORD) = 0;
    FIELD(self, 0x1BC, float) = 1.0f;
    FIELD(self, 0x1C0, float) = 1.0f;
    FIELD(self, 0x1C4, DWORD) = 0;
    FIELD(self, 0x1C8, float) = 0.5f;
    FIELD(self, 0x204, DWORD) = 0;
    FIELD(self, 0x20C, DWORD) = 0;
    FIELD(self, 0x208, DWORD) = 0;
    return self;
}

/* ---- The race's settings ---- */

/*
    The race's standings: 0x50 bytes a rider from events + 0x50, events + 0x4C of them. The
    columns are named by their use: 0x04 shown as a number, 0x08, 0x14, 0x18 and 0x1C as
    times, 0x2C as a score; 0x24 leaves the rider out.
*/
#define STANDING_NUMBER   0x04
#define STANDING_TIME     0x08
#define STANDING_LAP      0x14
#define STANDING_TIME_18  0x18
#define STANDING_TIME_1C  0x1C
#define STANDING_HAS_LAP  0x20
#define STANDING_OUT      0x24
#define STANDING_POINTS   0x28
#define STANDING_SCORE    0x2C
#define STANDING_NAME     0x40
#define STANDINGS_MAX     11

static BYTE* standing(int index)
{
    return race_events + 0x50 + index * 0x50;
}

/* The race's standings as the results show them: the event's, or past 11 riders standings.c's. */
BYTE* race_standing(int index);
int   race_standings_count(void);

/* The settings into their copies (Back, Start), or back from them (opening). */
static void settings_save(void)
{
    __movsd((unsigned long*) (game_main + 0x2F5C), (unsigned long*) (game_main + 0x2D70), 0x7B);
    __movsd((unsigned long*) (game_main + 0x15AC), (unsigned long*) (game_main + 0x1550), 0x17);
    __movsd((unsigned long*) (game_main + 0x1FB4), (unsigned long*) (game_main + 0x1EEC), 0x32);
    FIELD(game_main, 0x610, DWORD) = FIELD(game_main, 0x60C, DWORD);
}

static void settings_restore(void)
{
    __movsd((unsigned long*) (game_main + 0x2D70), (unsigned long*) (game_main + 0x2F5C), 0x7B);
    __movsd((unsigned long*) (game_main + 0x1550), (unsigned long*) (game_main + 0x15AC), 0x17);
    __movsd((unsigned long*) (game_main + 0x1EEC), (unsigned long*) (game_main + 0x1FB4), 0x32);
    FIELD(game_main, 0x60C, DWORD) = FIELD(game_main, 0x610, DWORD);
}

/* ---- Starting the race ---- */

#define game_window FIELD(game_main, 0x31C, HWND)

void __fastcall gui_manager_drop_background(gui_manager* self);
BOOL __fastcall gui_manager_set_palette(gui_manager* self, void* edx_unused, DWORD palette);
void __fastcall session_drop_scene(BYTE* self);
BOOL __fastcall game_FUN_00527300(BYTE* game);

typedef BYTE* (__fastcall* loading_constructor)(BYTE* self, void* edx_unused, DWORD flag);

static const struct {
    int                 line;
    loading_constructor construct;
} loading_screens[6] = {                    /* by race_kind */
    { 0x10CF, loading_quarry_construct },
    { 0x10D5, loading_baja_construct },
    { 0x10D8, loading_national_construct },
    { 0x10C9, loading_supercross_construct },
    { 0x10D2, loading_tag_construct },
    { 0x10CC, loading_enduro_construct },
};

/*
    The menus give way to the race: the settings are saved (one player's to 0x2F5C, 0x15AC and
    0x1FB4; more players' to 0x3148, 0x1608 and 0x207C), a new series' points are cleared, the
    display mode chosen is set, and the event's loading screen opens.
*/
PORT(0x004546F0)
void __cdecl race_start_loading(void)
{
    BYTE* screen;
    int kind;
    int i;

#ifdef OPENMCM2_DEV
    /* More than the game's 10 computers in a single race only: a series and the pro circuit keep
       standings, points and files made for 11 (riders.c). */
    if (FIELD(game_main, 0x18, int) == 1) {
        int most = race_mode == SERIES || FIELD(game_main, 0x3440, int) ? 10 : riders_most() - 1;

        if (FIELD(game_main, 0x2D94, int) > most)
            FIELD(game_main, 0x2D94, int) = most;
    }
#endif
    FIELD(game_session, 0x4AC, DWORD) = 1;
    FIELD(game_session, 0x4A8, DWORD) = 0;
    session_drop_scene(game_session);
    gui_manager_drop_background(session_gui);
    if (FIELD(FIELD(game_main, 0xC, BYTE*), 0x6C, int))
        gui_manager_set_palette(session_gui, NULL, 0);

    if (FIELD(game_main, 0x18, int) == 1 && !game_FUN_00527300(game_main)
        && !FIELD(game_session, 0x4A8, int) && !FIELD(game_main, 0x3428, int))
        settings_save();
    if (FIELD(game_main, 0x18, int) > 1 && !FIELD(game_session, 0x4A8, int)
        && !FIELD(game_main, 0x3428, int)) {
        __movsd((unsigned long*) (game_main + 0x3148), (unsigned long*) (game_main + 0x2D70), 0x7B);
        __movsd((unsigned long*) (game_main + 0x1608), (unsigned long*) (game_main + 0x1550), 0x17);
        __movsd((unsigned long*) (game_main + 0x207C), (unsigned long*) (game_main + 0x1EEC), 0x32);
    }

    if (race_mode == SERIES && !series_started) {
        series_started = 1;
        FIELD(race_events, 0x48, BYTE) = 0;
        for (i = 0; i < STANDINGS_MAX; i++)
            FIELD(standing(i), STANDING_POINTS, DWORD) = 0;
    }

    FIELD(game_session, 0x494, HIMC) = ImmAssociateContext(game_window, NULL);
    if (session_gui->background)
        background_release_dc(session_gui->background);
#ifdef OPENMCM2_DEV
    if (live_state != LIVE_LOADING)             /* the flyby stays in the menus' mode */
#endif
    if (FIELD(game_main, 0xFC4, DWORD) != FIELD(FIELD(game_main, 0xC, BYTE*), 0xC, DWORD))
        VIRTUAL(game_main, 0x4C, method_1)(game_main, NULL, FIELD(game_main, 0xFC4, DWORD));
    if (race_mode == GHOST_RACE) {
        FIELD(game_main, 0x2C68, DWORD) = 0;
        record_race = 0;
        FIELD(game_main, 0x60C, DWORD) = 2;
    }

    kind = race_kind;
    if ((unsigned) kind > 5)
        return;
    screen = game_new(0x7F5C, DLGPROCS_CPP, loading_screens[kind].line);
    if (screen)
        screen = loading_screens[kind].construct(screen, NULL, 1);
    gui_manager_open(session_gui, NULL, screen, 0, 2, 0, NULL, 0, 0, 0);
}

/* ---- Single player: the event page ---- */

void __fastcall dialog_notify_controls(BYTE* self, void* edx_unused, DWORD flag);
void __fastcall dialog_enable_controls(BYTE* self, void* edx_unused, DWORD id, DWORD enabled);
void __fastcall dialog_set_controls(BYTE* self, void* edx_unused, DWORD id, DWORD value);
int  __fastcall option_select(BYTE* self, void* edx_unused, int index);
int  __fastcall option_current_index(BYTE* self);
void __fastcall edit_set_allowed(BYTE* self, void* edx_unused, const char* allowed);
int  __fastcall list_find_data(BYTE* self, void* edx_unused, DWORD data);
BOOL __fastcall list_remove(BYTE* self, void* edx_unused, int index);
void __cdecl tracks_fill(BOOL big, const char* picture_name, const char* list_name, int* chosen, BYTE* dialog, BOOL keep);       /* the kind's tracks */

static void tracks_list(BYTE* self, DWORD append)
{
    tracks_fill(1, SP_PICTURE, LIST_TEXT_BOX, 0, self, append);
}

/* Out of line, as the original calls them. */
__declspec(noinline) void __fastcall sp_event_kind_set(BYTE* self);
__declspec(noinline) void __fastcall sp_event_refresh(BYTE* self);

#ifdef OPENMCM2_DEV
/*
    MCM2:Reborn's event page (tools/modern/build_singleplayer.py) has event cards in place of
    EventTypeDropDown, and TrackList beside the game's track list: filled here with the same
    tracks and kept on the same one. It is in a group of its own with its scroll bar, which in
    the game's track group would scroll the track's picture too. A layout without them is left
    as it is.
*/
static const struct {
    const char* name;
    DWORD       kind;               /* as EventTypeDropDown's entries have it */
} event_cards[] = {
    { "CardSupercross", 3 }, { "CardNationals", 2 }, { "CardBaja", 1 }, { "CardEnduro", 5 },
    { "CardQuarry", 0 },
};

#define EVENT_CARDS (sizeof event_cards / sizeof *event_cards)

/* The card for the event chosen, and the tracks listed again. */
static void event_page_show(BYTE* self)
{
    BYTE* tracks = combo_list(self, LIST_TEXT_BOX);
    BYTE* mirror = dialog_find_control(self, NULL, "TrackList", 0);
    DWORD kind = list_data(combo_list(self, SP_EVENT_TYPE), NULL, -1);
    int i;

    track_counts_refresh();
    if (kind < 6) {
        BYTE* counts = dialog_find_control(self, NULL, "TrackCounts", 0);
        if (counts) {
            char totals[96];
            game_FUN_0053a11f(totals, "ALL %d / BASE %d / CUSTOM %d",
                             g_track_counts[kind][0], g_track_counts[kind][1], g_track_counts[kind][2]);
            control_set_text(counts, NULL, totals);
        }
    }
    for (i = 0; i < (int) EVENT_CARDS; i++) {
        BYTE* card = dialog_find_control(self, NULL, event_cards[i].name, TAB_CONTROL);

        char name[32], label[48];
        BYTE* count;
        int source = (unsigned)race_kind < 6 ? g_track_source[race_kind] : 0;
        game_FUN_0053a11f(name, "TrackCount%d", event_cards[i].kind);
        count = dialog_find_control(self, NULL, name, 0);
        if (count) {
            game_FUN_0053a11f(label, "%d TRACKS", g_track_counts[event_cards[i].kind][source]);
            control_set_text(count, NULL, label);
        }
        if (card && event_cards[i].kind == kind)
            radio_set(card, NULL, 0);
    }
    if (!mirror)
        return;
    {
        static const char* const filters[] = { "TrackAll", "TrackBase", "TrackCustom" };
        int source = (unsigned) race_kind < 6 ? g_track_source[race_kind] : 0;
        BYTE* filter = dialog_find_control(self, NULL, filters[source], 4);
        BYTE* parent = FIELD(self, 0x2C, BYTE*);
        BYTE* start = parent ? dialog_find_control(parent, NULL, "Start", 0) : NULL;

        if (filter)
            radio_set(filter, NULL, 0);
        if (start)
            VIRTUAL(start, 0xC4, method_1)(start, NULL, FIELD(tracks, 0x1EC, int) > 0);
    }
    list_clear(mirror);
    for (i = 0; i < FIELD(tracks, 0x1EC, int); i++) {
        list_add_text(mirror, NULL, list_text(tracks, NULL, i), list_data(tracks, NULL, i), 0);
    }
    list_select_all(mirror, NULL, list_selected(tracks));
}

/* TrackList on the track the game's list has, after the arrows or that list chose one. */
static void track_list_follow(BYTE* self)
{
    BYTE* tracks = combo_list(self, LIST_TEXT_BOX);
    BYTE* mirror = dialog_find_control(self, NULL, "TrackList", 0);

    if (mirror && list_selected(mirror) != list_selected(tracks))
        list_select_all(mirror, NULL, list_selected(tracks));
}

/* TrackRandom: another of the tracks listed, chosen as the arrows choose one. */
static void track_random(BYTE* self)
{
    BYTE* list = combo_list(self, LIST_TEXT_BOX);
    int count = FIELD(list, 0x1EC, int);
    int now = list_selected(list);
    int pick;

    if (count < 2)
        return;
    if (now < 0)
        pick = game__rand() % count;
    else if ((pick = game__rand() % (count - 1)) >= now)
        pick++;
    list_select_all(list, NULL, pick);
    VIRTUAL(list, 0x108, method_1)(list, NULL, 0);
}

/* A card clicked: its event chosen, as EventTypeDropDown's would be. TRUE when it was one. */
static BOOL event_card_clicked(BYTE* self, const ui_event* event)
{
    int i;

    static const char* const filters[] = { "TrackAll", "TrackBase", "TrackCustom" };
    for (i = 0; i < 3; i++)
        if (clicked(event, filters[i]) && (unsigned) race_kind < 6) {
            { int k; for (k = 0; k < 6; k++) g_track_source[k] = i; }
            sp_event_kind_set(self);
            sp_event_refresh(self);
            ghosts_fill(self);
            return TRUE;
        }
    for (i = 0; i < (int) EVENT_CARDS; i++)
        if (clicked(event, event_cards[i].name)) {
            list_select_data(combo_list(self, SP_EVENT_TYPE), NULL, event_cards[i].kind);
            dialog_notify_controls(self, NULL, 1);
            sp_event_kind_set(self);
            sp_event_refresh(self);
            ghosts_fill(self);
            return TRUE;
        }
    return FALSE;
}

/* The page's gates and seed wells go with their numbers. */
static void gates_show(BYTE* self, BOOL shown)
{
    static const char* const parts[] = {
        "GatesChip", "SeedChip", "RandomGatesLeftArrow", "RandomGatesRightArrow", "SeedLeft", "SeedRight",
    };
    int i;

    if (!dialog_find_control(self, NULL, parts[0], 0))
        return;
    for (i = 0; i < (int) (sizeof parts / sizeof *parts); i++) {
        BYTE* part = dialog_find_control(self, NULL, parts[i], 0);

        if (part)
            control_show(part, NULL, shown, TRUE);
    }
}
#endif

/*
    The kind picked in EventTypeDropDown becomes the race's: race mode 4 for all kinds but 0,
    0 to 10 opponents, laps - or for kind 0 lengths in metres - and the kind's tracks (kind 0
    lists kind 5's too), the one last raced (0xC6C, 128 bytes a kind) picked.
*/
PORT(0x0044F2C0)
void __fastcall sp_event_kind_set(BYTE* self)
{
    int kind = (int) list_data(combo_list(self, SP_EVENT_TYPE), NULL, -1);
    BYTE* modes;
    BYTE* label;
    BYTE* opponents;
    BYTE* laps;
    int ghost;

    race_kind = kind;
    profile_set_event(game_profile, NULL, kind);
    modes = combo_list(self, SP_RACE_MODE);
    ghost = list_find_data(modes, NULL, GHOST_RACE);
    if (!kind) {
        if (ghost != -1) {
            if (list_selected(modes) == ghost) {
                list_select_data(modes, NULL, 0);
                race_mode = 0;
            }
            list_remove(modes, NULL, ghost);
        }
    } else if (ghost == -1) {
        char text[0x80];

        game_FUN_00526fc0(game_main, NULL, 0x13F6, text, sizeof text);
        list_add_text(modes, NULL, text, GHOST_RACE, 0);
    }

    label = dialog_find_control(self, NULL, SP_LAPS_LABEL, 12);
    opponents = dialog_find_control(self, NULL, SP_OPPONENTS, 3);
    laps = dialog_find_control(self, NULL, SP_LAPS, 3);
    dialog_find_control(self, NULL, SP_RACES, 3);      /* looked up, and not used */
    control_load_text(label, NULL, game_language, 0x929);
#ifdef OPENMCM2_DEV
    numbers_fill(opponents, ONE_NUMBER, 0, riders_most() - 1);  /* a series stays at 10 (race_start_loading) */
#else
    numbers_fill(opponents, ONE_NUMBER, 0, 10);
#endif
    numbers_fill(laps, ONE_NUMBER, kind == 2 || kind == 3 ? 2 : 1, 20);
    list_select_data(laps, NULL, FIELD(game_main, 0x2D90, DWORD));
    if (!kind) {
        control_load_text(label, NULL, game_language, 0x92F);
        numbers_fill(laps, METRES, 2, 30);
        list_select_data(laps, NULL, (DWORD) (int) FIELD(game_main, 0x2EB0, float));
    }

    /* Read again: the profile's call may have changed it. */
    if (!race_kind) {
        profile_set_event(game_profile, NULL, 0);
        tracks_list(self, 0);
        profile_set_event(game_profile, NULL, 5);
        tracks_list(self, 1);
    } else {
        tracks_list(self, 0);
    }
    dialog_notify_controls(self, NULL, 0);
    profile_set_event(game_profile, NULL, kind);
    list_select_text(combo_list(self, LIST_TEXT_BOX), NULL,
                     (const char*) game_main + 0xC6C + kind * 0x80);
#ifdef OPENMCM2_DEV
    event_page_show(self);
#endif
}

/* Vtable slot 31: the event page's controls into the settings (out), or the settings into them. */
PORT(0x0044F5A0)
void __fastcall sp_event_sync(BYTE* self, void* edx_unused, DWORD out)
{
    BYTE* settings = race_settings;
    char text[0x80];

    (void) edx_unused;
    if (out) {
        BYTE* list;
        const char* name;
        size_t length;
        int track;
        int ghost;

        if (!event_timed(FIELD(settings, SET_EVENT, int)))
            FIELD(settings, SET_LAPS, DWORD) =
                list_data(dialog_find_control(self, NULL, SP_LAPS, 3), NULL, -1);
        else
            FIELD(settings, SET_TIME, float) =
                (float) (int) list_data(dialog_find_control(self, NULL, SP_LAPS, 3), NULL, -1);
        if (!series_started)
            FIELD(settings, SET_RACES, DWORD) =
                list_data(dialog_find_control(self, NULL, SP_RACES, 3), NULL, -1);
        FIELD(settings, SET_TREES, int) =
            option_current_index(dialog_find_control(self, NULL, SP_TREES, 2));

        text_copy((char*) settings + SET_TRACK, NO_TEXT);
        list = combo_list(self, LIST_TEXT_BOX);
#ifdef OPENMCM2_DEV
        if (list_selected(list) < 0)
            return;
#endif
        track = (int) list_data(list, NULL, -1) * TRACK_ENTRY;
        name = FIELD(game_session, 0x60, char*) + track + 0x14;
        length = text_length(name);
        if ((int) length > 0xFF)
            length = 0xFF;
        game__strncpy((char*) settings + SET_TRACK, name, length);
        settings[SET_TRACK + length] = 0;
        profile_set_event(game_profile, NULL,
                          FIELD(FIELD(game_session, 0x60, BYTE*) + track, 0x8, DWORD));
        FIELD(settings, SET_TRACK_KIND, BYTE) =
            FIELD(FIELD(game_session, 0x60, BYTE*) + track, 0x4, BYTE);
        text_copy((char*) game_main + race_kind * 0x80 + 0xC6C, list_text(list, NULL, -1));
        FIELD(game_main, 0xF6C + race_kind * 4, DWORD) = FIELD(settings, SET_TRACK_KIND, BYTE);

        FIELD(settings, SET_RANDOM_GATES, BYTE) =
            option_current_index(dialog_find_control(self, NULL, SP_RANDOM_GATES, 2)) ? 1 : 0;
        FIELD(settings, SET_GATES, BYTE) =
            (BYTE) list_data(dialog_find_control(self, NULL, SP_GATES, 3), NULL, -1);
        edit_get_text(dialog_find_control(self, NULL, SP_SEED, 11), NULL, text, sizeof text);
        FIELD(settings, SET_SEED, int) = game_FUN_0053b6c7(text);
        FIELD(settings, SET_EVENT, DWORD) = list_data(combo_list(self, SP_EVENT_TYPE), NULL, -1);
        FIELD(settings, SET_MODE, DWORD) = list_data(combo_list(self, SP_RACE_MODE), NULL, -1);

        ghost = (int) list_data(combo_list(self, LIST_GHOSTS), NULL, -1);
        if (FIELD(settings, SET_MODE, int) == GHOST_RACE && ghost != -1) {
            name = FIELD(self, GHOST_PATHS, char**)[ghost];
            length = text_length(name);
            if ((int) length > TRACK_NAME_LENGTH)
                length = TRACK_NAME_LENGTH;
            game__strncpy((char*) game_main + 0x2B64, name, length);
            game_main[0x2B64 + length] = 0;
            __movsd((unsigned long*) (game_session + 0x4B4),
                    (unsigned long*) (FIELD(self, GHOST_CHOSEN, BYTE*) + ghost * RECORD_SIZE),
                    RECORD_SIZE / 4);
        } else {
            FIELD(game_main, 0x2B64, BYTE) = 0;
        }
    } else {
        BYTE* laps;

        list_select_data(dialog_find_control(self, NULL, SP_RACES, 3), NULL,
                         FIELD(settings, SET_RACES, DWORD));
        option_select(dialog_find_control(self, NULL, SP_RANDOM_GATES, 2), NULL,
                      FIELD(settings, SET_RANDOM_GATES, BYTE));
        list_select_data(dialog_find_control(self, NULL, SP_GATES, 3), NULL,
                         FIELD(settings, SET_GATES, BYTE));
        game_FUN_0054aaa5(FIELD(settings, SET_SEED, int), text, 10);
        edit_set_text(dialog_find_control(self, NULL, SP_SEED, 11), NULL, text);
        laps = dialog_find_control(self, NULL, SP_LAPS, 3);
        if (!event_timed(FIELD(settings, SET_EVENT, int)))
            list_select_data(laps, NULL, FIELD(settings, SET_LAPS, DWORD));
        else
            list_select_data(laps, NULL, (DWORD) (int) FIELD(settings, SET_TIME, float));
        option_select(dialog_find_control(self, NULL, SP_TREES, 2), NULL,
                      FIELD(settings, SET_TREES, int));
        list_select_data(combo_list(self, SP_EVENT_TYPE), NULL, FIELD(game_main, 0x2D74, DWORD));
        list_select_data(combo_list(self, SP_RACE_MODE), NULL, FIELD(game_main, 0x2D70, DWORD));
    }
}

/*
    Which of the event page's boxes show for the race mode; the waypoint gates for event type 1
    outside ghost races; and the races list, fixed at the race under way during a series.
*/
PORT(0x0044EEA0)
void __fastcall sp_event_refresh(BYTE* self)
{
    BYTE* record = dialog_find_control(self, NULL, SP_RECORD, 0);
    BYTE* modes = combo_list(self, SP_RACE_MODE);
    DWORD mode = list_data(modes, NULL, -1);
    BYTE* laps_box = dialog_find_control(self, NULL, SP_LAPS_BOX, 5);
    BYTE* races_box = dialog_find_control(self, NULL, SP_RACES_BOX, 5);
    BYTE* ghost_box = dialog_find_control(self, NULL, SP_GHOST_BOX, 5);
    BYTE* opponents_box = dialog_find_control(self, NULL, SP_OPPONENTS_BOX, 5);
    BYTE* waypoints;
    BYTE* races_left;
    BYTE* races_right;
    BYTE* opponents_left;
    BYTE* opponents_right;
    BYTE* races_label;
    BYTE* races;
    BYTE* opponents;
    BOOL free_choice;
    char text[0x20];
    int i;

    control_show(opponents_box, NULL, TRUE, TRUE);
    control_show(record, NULL, TRUE, TRUE);
    control_show(ghost_box, NULL, FALSE, TRUE);
    dialog_enable_controls(self, NULL, 0x3F3, TRUE);
    switch (mode) {
    case 0:
        control_show(laps_box, NULL, FALSE, TRUE);
        control_show(races_box, NULL, FALSE, TRUE);
        break;
    case 1:
        control_show(laps_box, NULL, TRUE, TRUE);
        control_show(races_box, NULL, FALSE, TRUE);
        break;
    case 2:
        control_show(laps_box, NULL, TRUE, TRUE);
        control_show(races_box, NULL, TRUE, TRUE);
        break;
    case 4:
        control_show(opponents_box, NULL, FALSE, TRUE);
        control_show(laps_box, NULL, FALSE, TRUE);
        control_show(races_box, NULL, FALSE, TRUE);
        control_show(record, NULL, FALSE, TRUE);
        control_show(ghost_box, NULL, TRUE, TRUE);
        dialog_enable_controls(self, NULL, 0x3F3, FALSE);
        break;
    }

    waypoints = dialog_find_control(self, NULL, SP_WAYPOINTS, 5);
    if (FIELD(race_settings, SET_EVENT, int) == 1 && list_data(modes, NULL, -1) != GHOST_RACE) {
        BOOL random;

        control_show(waypoints, NULL, TRUE, TRUE);
        random = option_current_index(dialog_find_control(self, NULL, SP_RANDOM_GATES, 2)) != 0;
        dialog_set_controls(self, NULL, 0x6F, random);
        control_show(dialog_find_control(self, NULL, SP_GATES, 0), NULL, random, TRUE);
        control_show(dialog_find_control(self, NULL, SP_SEED, 0), NULL, random, TRUE);
#ifdef OPENMCM2_DEV
        gates_show(self, random);
#endif
    } else {
        control_show(waypoints, NULL, FALSE, TRUE);
    }

    races_left = dialog_find_control(self, NULL, SP_RACES_LEFT, 9);
    races_right = dialog_find_control(self, NULL, SP_RACES_RIGHT, 10);
    opponents_left = dialog_find_control(self, NULL, SP_OPPONENTS_LEFT, 0);
    opponents_right = dialog_find_control(self, NULL, SP_OPPONENTS_RIGHT, 0);
    races_label = dialog_find_control(self, NULL, SP_RACES_LABEL, 12);
    races = dialog_find_control(self, NULL, SP_RACES, 3);
    opponents = dialog_find_control(self, NULL, SP_OPPONENTS, 3);
    list_clear(races);
    for (i = 3; i <= 7; i += 2) {
        game_FUN_0053a11f(text, ONE_NUMBER, i);
        list_add_text(races, NULL, text, i - 1, 0);    /* "3" holds 2, as the original has it */
    }
    list_select_data(races, NULL, FIELD(game_main, 0x2D7C, DWORD));

    free_choice = !(FIELD(race_settings, SET_MODE, int) == SERIES && series_started);
    control_show(opponents_left, NULL, free_choice, TRUE);
    control_show(opponents_right, NULL, free_choice, TRUE);
    VIRTUAL(opponents, 0xC4, method_1)(opponents, NULL, free_choice);     /* enable */
    control_show(races_left, NULL, free_choice, TRUE);
    control_show(races_right, NULL, free_choice, TRUE);
    control_load_text(races_label, NULL, game_language, free_choice ? 0x92A : 0x92B);
    if (!free_choice) {
        game_FUN_0053a11f(text, RACE_OF, FIELD(race_events, 0x48, BYTE) + 1, race_round + 1);
        list_clear(races);
        list_add_text(races, NULL, text, 0, 0);
    }
}

/* The arrows beside EditSeed: 0 down wraps to 999, 999 up to 0. */
static void seed_step(BYTE* self, BOOL back)
{
    BYTE* edit = dialog_find_control(self, NULL, SP_SEED, 11);
    char seed[0x20];
    int value;

    edit_get_text(edit, NULL, seed, sizeof seed);
    value = game_FUN_0053b6c7(seed);
    if (back) {
        if (--value < 0)
            value = SEED_MAX;
    } else if (++value > SEED_MAX) {
        value = 0;
    }
    game_FUN_0054aaa5(value, seed, 10);
    edit_set_text(edit, NULL, seed);
    gui_user_queue_capture(FIELD(self, 0x34, gui_user*), NULL, edit, NULL, FALSE);
}

/* The single-player screen's event page (SPEvent.dtm, vtable 0x00556D68). */
PORT(0x0044E830)
void __fastcall sp_event_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
#ifdef OPENMCM2_DEV
    if (event->type == EVENT_TIMER && event->a == 0x739) {
        if (track_counts_refresh()) {
            sp_event_kind_set(self);
            sp_event_refresh(self);
            ghosts_fill(self);
        }
        return;
    }
#endif
    if (event->type == EVENT_CLICK) {
#ifdef OPENMCM2_DEV
        if (event_card_clicked(self, event))
            return;
        if (clicked(event, "TrackRandom")) {
            track_random(self);
            track_list_follow(self);
            return;
        }
#endif
        if (clicked(event, SP_RANDOM_GATES))
            sp_event_refresh(self);
        if (clicked(event, SP_TRACK_LEFT))
            combo_step(self, LIST_TEXT_BOX, TRUE);
        else if (clicked(event, SP_TRACK_RIGHT))
            combo_step(self, LIST_TEXT_BOX, FALSE);
        else if (clicked(event, SP_GHOST_LEFT))
            combo_step(self, LIST_GHOSTS, TRUE);
        else if (clicked(event, SP_GHOST_RIGHT))
            combo_step(self, LIST_GHOSTS, FALSE);
        else if (clicked(event, SP_SEED_LEFT))
            seed_step(self, TRUE);
        else if (clicked(event, SP_SEED_RIGHT))
            seed_step(self, FALSE);
        else if (clicked(event, SP_OPPONENTS_LEFT) || clicked(event, SP_OPPONENTS_RIGHT))
            sp_event_refresh(self);
    } else if (event->type == EVENT_SELECT) {
        BYTE* list = (BYTE*) event->c;

        if (clicked(event, SP_RACE_MODE)) {
            if (list_data(list, NULL, -1) == GHOST_RACE)
                ghosts_fill(self);
            if (list_data(list, NULL, -1) != SERIES) {
                series_started = 0;
                FIELD(race_events, 0x48, BYTE) = 0;
            }
            sp_event_refresh(self);
        } else if (clicked(event, SP_EVENT_TYPE)) {
            dialog_notify_controls(self, NULL, 1);
            sp_event_kind_set(self);
            sp_event_refresh(self);
            ghosts_fill(self);
        } else if (clicked(event, LIST_TEXT_BOX)) {
            if (list_selected(list) < 0)
                return;
            ghosts_fill(self);
            text_copy((char*) game_main + race_kind * 0x80 + 0xC6C, list_text(list, NULL, -1));
        }
#ifdef OPENMCM2_DEV
        else if (clicked(event, "TrackList")) {
            if (list_selected((BYTE*) event->c) < 0)
                return;
            list = combo_list(self, LIST_TEXT_BOX);
            list_select_all(list, NULL, list_selected(dialog_find_control(self, NULL, "TrackList", 0)));
            ghosts_fill(self);
            text_copy((char*) game_main + race_kind * 0x80 + 0xC6C, list_text(list, NULL, -1));
        }
#endif
    } else if (event->type == EVENT_OPEN) {
#ifdef OPENMCM2_DEV
        if (dialog_find_control(self, NULL, "TrackCount0", 0))
            dialog_add_timer(self, NULL, 0x739, 2000, NULL);
#endif
        BYTE* list = combo_list(self, SP_EVENT_TYPE);
        char number[0x80];
        int i;

        list_add_string(list, 0x13ED, 1);
        list_add_string(list, 0x13EE, 0);
        list_add_string(list, 0x13F0, 5);
        list_add_string(list, 0x13EF, 2);
        list_add_string(list, 0x13F2, 3);
        list_select_data(list, NULL, FIELD(race_settings, SET_EVENT, DWORD));
        list = combo_list(self, SP_RACE_MODE);
        list_add_string(list, 0x13F3, 0);
        list_add_string(list, 0x13F4, 1);
        list_add_string(list, 0x13F5, 2);
        list_add_string(list, 0x13F6, GHOST_RACE);
        list_select_all(list, NULL, FIELD(race_settings, SET_MODE, int));
        control_bind(dialog_find_control(self, NULL, SP_OPPONENTS, 0),
                     race_settings + SET_OPPONENTS);
        control_bind(dialog_find_control(self, NULL, SP_RECORD, 0), game_main + 0x3334);
        control_bind(dialog_find_control(self, NULL, SP_LOD_EASY, 0), game_main + 0x60C);
        list = dialog_find_control(self, NULL, SP_GATES, 3);
        list_clear(list);
        for (i = 10; i <= 30; i++) {
            game_FUN_0054aaa5(i, number, 10);
            list_add_text(list, NULL, number, i, 0);
        }
        edit_set_allowed(dialog_find_control(self, NULL, SP_SEED, 11), NULL, DIGITS);
        dialog_notify_controls(self, NULL, 0);
        sp_event_kind_set(self);
        sp_event_refresh(self);
        FIELD(self, 0x7F5C, int) = 0;
        FIELD(self, GHOST_CHOSEN, void*) = NULL;
        FIELD(self, GHOST_COUNT, int) = 0;
        FIELD(self, GHOST_PATHS, char**) = NULL;
        if (FIELD(race_settings, SET_MODE, int) == GHOST_RACE)
            ghosts_fill(self);
    }
#ifdef OPENMCM2_DEV
    if (event->type == EVENT_CLICK || event->type == EVENT_SELECT)
        track_list_follow(self);
#endif
}

/* ---- Single player: the screen ---- */

int  __fastcall dialog_show(void* dialog, void* edx_unused, DWORD shown);
void __fastcall session_hide_scene(BYTE* self);
void __fastcall rider_mark_changed(BYTE* self);        /* the rider to be dressed again (0x7F88) */
void __fastcall bike_choose(BYTE* self);        /* the bike chosen */

/* Out of line, as the original calls them. */
__declspec(noinline) void __fastcall sp_show_page(BYTE* self, void* edx_unused, int page);
__declspec(noinline) void __fastcall choices_fill(BYTE* self);
__declspec(noinline) void __fastcall ghosts_fill(BYTE* self);

#define SP_TAB_EVENT        NAME(0x0056F8F0)    /* "EventTab" */
#define SP_TAB_BIKE_RIDER   NAME(0x0056F8E0)    /* "BikeRiderTab" */
#define SP_TAB_RACE_INFO    NAME(0x0056F8D4)    /* "RaceInfoTab" */
#define SP_OPTIONS          NAME(0x0056F8BC)    /* "Options" */
#define SP_JOYSTICK         NAME(0x0056F8B0)    /* "Joystick" */
#define SP_SCREEN_RACE_INFO 0x88E               /* the screen opened on its race info */

/* The tabs: words for both their states, centred (0x12). */
static BYTE* sp_tab_setup(BYTE* self, const char* name, UINT id)
{
    BYTE* tab = dialog_find_control(self, NULL, name, TAB_CONTROL);

#ifdef OPENMCM2_DEV
    if (menu_reborn(self))                      /* its tabs' words are in their pictures */
        return tab;
#endif
    option_load_text(tab, NULL, 0, game_language, id);
    option_load_text(tab, NULL, 1, game_language, id);
    control_set_align(tab, NULL, 0x12);
    return tab;
}

/* Each open page's controls into the settings. */
static void sp_pages_store(BYTE* self, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        BYTE* page = FIELD(self, SP_PAGES + 4 * i, BYTE*);

        if (page)
            dialog_notify_controls(page, NULL, 1);
    }
}

/* OptBase.dtm over `parent`, on its page `page` (0 the options, 1 the joystick, 2 the bike). */
static void options_open(BYTE* self, BYTE* parent, int line, DWORD page)
{
    BYTE* options = dialog_new(0x7F70, line, NAME(0x0056F690), (void*) 0x00556BD0); /* OptBase */

    gui_manager_open(FIELD(self, 0x30, gui_manager*), NULL, options, 0, 2, 0, parent, page, 0, 1);
}

/*
    The single-player screen (vtable 0x00556B4C). Its tabs switch the page shown (event, bike
    and rider, race info); Back keeps the settings and goes to screen 100; Start keeps them and
    starts the race - or, with the game's 0x614 set, shows messbox2.dtm instead. A page asks
    (event 9) for the bike and rider page (0x33), or for that page to be refilled (0x51).
*/
PORT(0x0044DF50)
void __fastcall sp_screen_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    switch (event->type) {
    case EVENT_OPEN:
        FIELD(self, SP_PAGES, BYTE*) = NULL;
        FIELD(self, SP_PAGES + 4, BYTE*) = NULL;
        FIELD(self, SP_PAGES + 8, BYTE*) = NULL;
        radio_set(sp_tab_setup(self, SP_TAB_EVENT, 0x13E3), NULL, 0);
        sp_tab_setup(self, SP_TAB_BIKE_RIDER, 0x13E4);
        sp_tab_setup(self, SP_TAB_RACE_INFO, 0x13E5);
        settings_restore();
        profile_set_event(game_profile, NULL, FIELD(race_settings, SET_EVENT, DWORD));
        if (FIELD(self, 0xC4, int) == SP_SCREEN_RACE_INFO) {
            radio_set(dialog_find_control(self, NULL, SP_TAB_RACE_INFO, TAB_CONTROL), NULL, 0);
            sp_show_page(self, NULL, 2);
        } else {
            radio_set(dialog_find_control(self, NULL, SP_TAB_EVENT, TAB_CONTROL), NULL, 0);
            sp_show_page(self, NULL, 0);
        }
        break;
    case EVENT_CLICK:
        if (clicked(event, NAME(0x0056F8CC))) {                                 /* "Back" */
            sp_pages_store(self, 2);
            dialog_notify_controls(self, NULL, 1);
            settings_save();
            go_to(100);
            dialog_end(event_dialog(event), NULL, 0);
            event->e = 1;
        } else if (clicked(event, START)) {
            if (FIELD(game_main, 0x614, int)) {
                BYTE* box = dialog_new(0x7F58, 0x4E1, NAME(0x0056F734),       /* messbox2.dtm */
                                       (void*) 0x00556CE4);

                gui_manager_open(FIELD(self, 0x30, gui_manager*), NULL, box, 0, 4, 0, self, 0, 0,
                                 1);
                return;
            }
            session_hide_scene(game_session);
            FIELD(race_settings, SET_28, DWORD) = 0;
            sp_pages_store(self, 3);
            FIELD(game_main, 0x342C, DWORD) = record_race && race_mode != GHOST_RACE;
            FIELD(game_main, 0x3428, DWORD) = 0;
            settings_save();
            if (race_mode == GHOST_RACE && FIELD(game_main, 0x2B64, BYTE))
                __movsd((unsigned long*) race_settings, (unsigned long*) (game_session + 0x4E8),
                        0x7B);                          /* the ghost's own settings */
            else
                FIELD(game_main, 0x2B64, BYTE) = 0;
            race_start_loading();
            VIRTUAL(self, 0x68, method_0)(self);
        } else if (clicked(event, SP_TAB_EVENT)) {
            sp_show_page(self, NULL, 0);
        } else if (clicked(event, SP_TAB_BIKE_RIDER)) {
            sp_show_page(self, NULL, 1);
        } else if (clicked(event, SP_TAB_RACE_INFO)) {
            sp_show_page(self, NULL, 2);
        } else if (clicked(event, SP_OPTIONS)) {
            options_open(self, self, 0x518, 0);
        } else if (clicked(event, SP_JOYSTICK)) {
            options_open(self, self, 0x51B, 1);
        } else if (clicked(event, HELP)) {
            profile_open_help(game_profile, NULL, HELP_TOPIC, 0);
        }
        break;
    case EVENT_CLOSE:
        if (FIELD(self, 0x2C, BYTE*))
            dialog_show(FIELD(self, 0x2C, BYTE*), NULL, 1);
        break;
    case EVENT_CHILD_ENDED:
        if (event->a == 0x33) {
            radio_set(dialog_find_control(self, NULL, SP_TAB_BIKE_RIDER, TAB_CONTROL), NULL, 0);
            sp_show_page(self, NULL, 1);
        } else if (event->a == 0x51 && FIELD(self, SP_PAGES + 4, BYTE*)) {
            dialog_notify_controls(FIELD(self, SP_PAGES + 4, BYTE*), NULL, 0);
            choices_fill(FIELD(self, SP_PAGES + 4, BYTE*));
            rider_mark_changed(FIELD(self, SP_PAGES + 4, BYTE*));
            bike_choose(FIELD(self, SP_PAGES + 4, BYTE*));
        }
        break;
    }
}

/* ---- Results ---- */

#define TEXT_AND_ASIDE  NAME(0x0056FC94)    /* "%s (%s)" */
#define ROUND_OF        NAME(0x0056FC9C)    /* "%s (%d/%d)" */

int  __fastcall dialog_queue_focus(BYTE* self, void* edx_unused, BYTE* control, BYTE** old,
                                   BOOL now);
void __fastcall button_set_sort_list(BYTE* self, void* edx_unused, BYTE* list);
typedef int (__cdecl* entry_compare)(const BYTE* a, const BYTE* b);
void __fastcall list_set_compare(BYTE* self, void* edx_unused, entry_compare compare);
void __cdecl    game_FUN_0051dd10(char* text, float seconds);   /* a time, as text */

/* A series' title: string 0x93B and the rounds; while rounds remain, 0x93A and screen 0x10E next. */
static void results_title(BYTE* self)
{
    BYTE* title = dialog_find_control(self, NULL, TITLE_TEXT, 12);
    char round[0x80];
    char text[0x80];

    game_FUN_00526fc0(game_main, NULL, 0x93B, round, sizeof round);
    game_FUN_0053a11f(text, ROUND_OF, round, (int) FIELD(race_events, 0x48, BYTE), race_round + 1);
    control_set_text(title, NULL, text);
    if ((int) FIELD(race_events, 0x48, BYTE) > race_round) {
        control_load_text(title, NULL, game_language, 0x93A);
        series_started = 0;
        FIELD(game_session, 0x3C, int) = 0x10E;
    }
}

static UINT points_heading(void)
{
    return race_mode == SERIES ? 0x938 : 0;
}

/* Timed events: the number, the time with the best lap, and points in a series. */
static void results_timed(BYTE* self, ui_event* event, BOOL skip_out)
{
    char number[0x80];
    char time_and_lap[0x80];
    char points[0x80];
    char time[0x80];
    char lap[0x80];
    char heading[0x80];
    char aside[0x80];
    int i;

    stats_load_headings(event, 0x934, 0x935, 0, points_heading());
    game_FUN_00526fc0(game_main, NULL, 0x936, heading, sizeof heading);
    game_FUN_00526fc0(game_main, NULL, 0x13B8, aside, sizeof aside);
    game_FUN_0053a11f(time, TEXT_AND_ASIDE, heading, aside);
    control_set_text(dialog_find_control(self, NULL, stats_buttons[2], 0), NULL, time);

    for (i = 0; i < race_standings_count(); i++) {
        BYTE* rider = race_standing(i);

        if (skip_out && FIELD(rider, STANDING_OUT, DWORD))
            continue;
        game_FUN_0053a11f(number, ONE_NUMBER, FIELD(rider, STANDING_NUMBER, int));
        game_FUN_0051dd10(time, FIELD(rider, STANDING_TIME, float));
        if (FIELD(rider, STANDING_HAS_LAP, DWORD))
            game_FUN_0051dd10(lap, FIELD(rider, STANDING_LAP, float));
        else
            game_FUN_00526fc0(game_main, NULL, 0xFF0, lap, sizeof lap);
        game_FUN_0053a11f(time_and_lap, TEXT_AND_ASIDE, time, lap);
        if (race_mode == SERIES) {
            game_FUN_0053a11f(points, ONE_NUMBER, FIELD(rider, STANDING_POINTS, int));
            stats_add_row(event, (const char*) rider + STANDING_NAME, number, time_and_lap, points);
        } else {
            stats_add_row(event, (const char*) rider + STANDING_NAME, number, time_and_lap, NULL);
        }
    }
}

/* Scored events: the score, and points in a series. */
static void results_scored(ui_event* event, BOOL skip_out)
{
    char score[0x80];
    char points[0x80];
    int i;

    stats_load_headings(event, 0x934, 0, 0x937, points_heading());
    for (i = 0; i < race_standings_count(); i++) {
        BYTE* rider = race_standing(i);

        if (skip_out && FIELD(rider, STANDING_OUT, DWORD))
            continue;
        game_FUN_0053a11f(score, ONE_NUMBER, (int) FIELD(rider, STANDING_SCORE, float));
        if (race_mode == SERIES) {
            game_FUN_0053a11f(points, ONE_NUMBER, FIELD(rider, STANDING_POINTS, int));
            stats_add_row(event, (const char*) rider + STANDING_NAME, NULL, score, points);
        } else {
            stats_add_row(event, (const char*) rider + STANDING_NAME, NULL, score, NULL);
        }
    }
}

/* Tag: the number and two times; in a series the second time gives way to points. */
static void results_tag(ui_event* event)
{
    char first[0x80];
    char second[0x80];
    char third[0x80];
    char fourth[0x80];
    int i;

    if (race_mode == SERIES) {
        stats_load_headings(event, 0x934, 0x939, 0x938, 0);
    } else {
        game_FUN_00526fc0(game_main, NULL, 0x934, first, sizeof first);
        game_FUN_00526fc0(game_main, NULL, 0x935, second, sizeof second);
        game_FUN_00526fc0(game_main, NULL, 0x939, third, sizeof third);
        game_FUN_00526fc0(game_main, NULL, 0x142B, fourth, sizeof fourth);
        stats_set_headings(event, first, second, third, fourth);
    }
    for (i = 0; i < race_standings_count(); i++) {
        BYTE* rider = race_standing(i);

        if (FIELD(rider, STANDING_OUT, DWORD))
            continue;
        game_FUN_0053a11f(first, ONE_NUMBER, FIELD(rider, STANDING_NUMBER, int));
        game_FUN_0051dd10(second, FIELD(rider, STANDING_TIME_1C, float));
        game_FUN_0051dd10(third, FIELD(rider, STANDING_TIME_18, float));
        if (race_mode == SERIES)        /* formatted, then overwritten, as the original has it */
            game_FUN_0053a11f(third, ONE_NUMBER, FIELD(rider, STANDING_POINTS, int));
        stats_add_row(event, (const char*) rider + STANDING_NAME, first, second, third);
    }
}

/* The stats column `column`, sorted by `compare`; with `button`, its heading sorts it too. */
static void column_order(BYTE* self, int column, entry_compare compare, BOOL button, BOOL sort)
{
    BYTE* list = dialog_find_control(self, NULL, stats_lists[column], 0);

    if (button)
        button_set_sort_list(dialog_find_control(self, NULL, stats_buttons[column], 1), NULL, list);
    list_set_compare(list, NULL, compare);
    if (sort)
        list_sort(list, NULL, TRUE);
}

/* The race results screen (vtable 0x0055CC00). */
PORT(0x004521B0)
void __fastcall results_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type != EVENT_OPEN)
        return;
    if (race_mode == SERIES)
        results_title(self);
    dialog_queue_focus(self, NULL, dialog_find_control(self, NULL, stats_lists[0], 0), NULL, FALSE);
    stats_clear(event);

    switch (race_kind) {
    case 1:
    case 2:
    case 3:
    case 5:
        results_timed(self, event, TRUE);
        column_order(self, 3, entries_by_number_falling, TRUE, FALSE);
        column_order(self, 1, entries_by_number, FALSE, TRUE);
        break;
    case 4:
        if (!FIELD(game_main, 0x2EB8, int)) {
            results_tag(event);
            column_order(self, 1, entries_by_number, FALSE, TRUE);
            break;
        }
        /* fall through: scored like the quarry */
    case 0:
        results_scored(event, TRUE);
        column_order(self, 3, entries_by_number_falling, TRUE, FALSE);
        column_order(self, 2, entries_by_number_falling, FALSE, TRUE);
        break;
    }
}

/* ---- Race information ---- */

#define BUTTON_VIEW_REPLAY NAME(0x0056FCA8)     /* "ButViewReplay" */
#define REPLAY_TAPE        NAME(0x0056DC74)     /* "VCRtape.dat": the race just run */

/* ButViewReplay: the tape's race settings become the game's, and the owner takes the track. */
static void replay_watch(BYTE* self, ui_event* event)
{
    BYTE record[RECORD_SIZE];
    size_t length;
    void* file;
    int i;

    for (i = 0; i < 8; i++) {           /* the record's member constructors */
        FIELD(record, 0x180 + i * 8, DWORD) = 0;
        record[0x184 + i * 8] = 0;
    }
    file = game_FUN_0053a35b(REPLAY_TAPE, NAME(0x0056C66C));                  /* "rb" */
    if (!file) {
        control_show((BYTE*) event->c, NULL, FALSE, TRUE);
        return;
    }
    game_FUN_0053a5d5(record, RECORD_SIZE, 1, file);       /* unchecked, as in the original */
    game_FUN_0053a2ad(file);
    __movsd((unsigned long*) race_settings, (unsigned long*) (record + 0x34), 0x7B);
    profile_set_event(game_profile, NULL, race_kind);
    FIELD(game_main, 0x3428, DWORD) = 1;
    FIELD(game_main, 0x342C, DWORD) = 1;
    FIELD(game_main, 0x1550, DWORD) = FIELD(record, 0x33C, DWORD);
    FIELD(game_main, 0x1554, DWORD) = FIELD(record, 0x340, DWORD);
    length = text_length(REPLAY_TAPE);
    if ((int) length > TRACK_NAME_LENGTH)
        length = TRACK_NAME_LENGTH;
    game__strncpy((char*) game_main + 0x2C6C, REPLAY_TAPE, length);
    game_main[0x2C6C + length] = 0;
    race_start_loading();
    track_chosen(self);
}

/* The single-player screen's race information page (SPRInfo.dtm, vtable 0x00556E70). */
PORT(0x004515C0)
void __fastcall race_info_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    if (event->type == EVENT_CLICK) {
        if (clicked(event, BUTTON_VIEW_REPLAY))
            replay_watch(self, event);
        return;
    }
    if (event->type != EVENT_OPEN)
        return;
    control_show(dialog_find_control(self, NULL, BUTTON_VIEW_REPLAY, 1), NULL,
                 FIELD(game_session, 0x40, DWORD) != 0, TRUE);
    if (race_mode == SERIES)
        results_title(self);
    dialog_queue_focus(self, NULL, dialog_find_control(self, NULL, stats_lists[0], 0), NULL, FALSE);
    stats_clear(event);

    switch (race_kind) {
    case 0:
        results_scored(event, FALSE);
        column_order(self, 3, entries_by_number_falling, TRUE, FALSE);
        column_order(self, 2, entries_by_number_falling, FALSE, TRUE);
        break;
    case 1:
    case 2:
    case 3:
    case 5:
        results_timed(self, event, FALSE);
        column_order(self, 3, entries_by_number_falling, TRUE, FALSE);
        column_order(self, 1, entries_by_number, TRUE, TRUE);
        break;
    }
}

/* ---- New profiles ---- */

#define player_name     ((char*) game_main + 0x588)     /* char[0x80] */
#define PROFILE_PROMPT_VTABLE ((void*) 0x00557210)      /* messbox2.dtm, named_choice_handle */
#define PROFILE_CHARACTERS NAME(0x0056FE6C)  /* letters, digits and "_-()$#@" */
#define OK_BUTTON_WORDS NAME(0x0056FF54)     /* "OKButton": OkButton too, names being matched by _stricmp */
#define profile_asking  FIELD(game_session, 0x4B0, DWORD)

void __fastcall profile_defaults(char* self);       /* the profile's defaults */
void __fastcall profile_load_controls(char* self);
void __fastcall game_FUN_00449e20(BYTE* controls, void* edx_unused, const char* path, int a);
void __fastcall game_FUN_00527080(BYTE* game);
void __fastcall game_FUN_004b0e10(BYTE* net, void* edx_unused, DWORD player, char* name);

/*
    A new profile named in EditBox (at most 15 characters): one already listed is asked about
    (messbox2, given the name); otherwise its folder is made, the profile reset with the
    session's default bike (0xFA at 0x8C) and first rider, and ui\preset.ctl copied in as its
    control.ctl. The screen ends with 30.
*/
__declspec(noinline) void __cdecl profile_create(ui_event* event);

PORT(0x00452990)
void __cdecl profile_create(ui_event* event)
{
    char name[0x80];
    char path[0x104];
    char controls[0x104];
    int i;

    edit_get_text(dialog_find_control(event_dialog(event), NULL, EDIT_BOX, 0), NULL, name, 0x10);
    if (!name[0])
        return;
    if (dir_list_has(game_files, NULL, name)) {
        BYTE* prompt = dialog_new(0x7F58, 0xCF9, NAME(0x0056F734),            /* messbox2.dtm */
                                  PROFILE_PROMPT_VTABLE);

        gui_manager_open(session_gui, NULL, prompt, 0, 4, 0, event_dialog(event), (DWORD) name,
                         0, 1);
        return;
    }
    game_FUN_0053a11f(path, NAME(0x0056C7DC), PROFILE_FOLDER, name);           /* %s\%s */
    if (!CreateDirectoryA(path, NULL))
        return;

    profile_defaults(game_profile);
    text_copy_most(game_profile, name, PROFILE_LENGTH);
    FIELD(game_main, 0x614, DWORD) = 1;

    for (i = 0; i < FIELD(game_session, 0x54, int); i++) {
        if (FIELD(FIELD(game_session, 0x50, BYTE*) + i * BIKE_SIZE, 0x8C, int) == 0xFA) {
            BYTE* bike;

            FIELD(game_main, 0x1FB0, int) = i;
            FIELD(game_main, 0x1FAC, int) =
                FIELD(FIELD(game_session, 0x50, BYTE*) + i * BIKE_SIZE, 0, int);
            bike = FIELD(game_session, 0x50, BYTE*) + i * BIKE_SIZE;
            text_copy((char*) game_main + 0x1EEC,
                      (const char*) session_players + FIELD(bike, 0, int) * RIDER_SIZE + 0x40);
            text_copy((char*) game_main + 0x1F2C,
                      (const char*) FIELD(game_session, 0x50, BYTE*) + i * BIKE_SIZE + 0x48);
            break;
        }
    }
    text_copy((char*) game_main + 0x1F6C, FIELD(game_session, 0x58, char*) + 0x40);
    __movsd((unsigned long*) (game_main + 0x207C), (unsigned long*) (game_main + 0x1EEC), 0x32);
    __movsd((unsigned long*) (game_main + 0x1FB4), (unsigned long*) (game_main + 0x207C), 0x32);
    for (i = 0; i < 6; i++) {
        game_main[0xC6C + i * 0x80] = 0;
        FIELD(game_main, 0xF6C + i * 4, DWORD) = 0;
    }
    text_copy(player_name, NO_TEXT);
    text_copy((char*) game_main + 0x2DA6, NO_TEXT);
    race_kind = 0;
    FIELD(game_main, 0x2DA4, BYTE) = 0;
    FIELD(game_main, 0x2D78, DWORD) = 0;

    game_FUN_0053a11f(path, NAME(0x0056C7DC), NAME(0x0056FE48), NAME(0x0056FE4C));  /* ui\preset.ctl */
    game_FUN_0053a11f(controls, NAME(0x0056FE30), PROFILE_FOLDER, name,
                      NAME(0x0056FE3C));                                          /* ...\control.ctl */
    CopyFileA(path, controls, FALSE);
    profile_load_controls(game_profile);
    VIRTUAL(game_files, 4, method_0)(game_files);
    profile_save(game_profile);
    game_FUN_00449e20(FIELD(game_main, 0x33FC, BYTE*), NULL, controls, -1);
    game_FUN_00527080(game_main);
    dialog_end(event_dialog(event), NULL, 0x1E);
    event->e = 1;
}

static void ok_enable(BYTE* self, BOOL enabled)
{
    BYTE* ok = dialog_find_control(self, NULL, OK_BUTTON, 0);

    VIRTUAL(ok, 0xC4, method_1)(ok, NULL, enabled);
}

#ifdef OPENMCM2_DEV
/* A rider named in the launcher, if its name is one a profile may have (as the edit box allows). */
static BOOL first_rider_named(void)
{
    const char* name = dev_first_rider();
    int i, k;

    if (!name || dir_list_has(game_files, NULL, name))
        return FALSE;
    for (i = 0; name[i]; i++) {
        BOOL letter = (name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z')
                      || (name[i] >= '0' && name[i] <= '9');

        for (k = 0; !letter && PROFILE_CHARACTERS[k]; k++)
            letter = name[i] == PROFILE_CHARACTERS[k];
        if (!letter)
            return FALSE;
    }
    return TRUE;
}
#endif

/*
    A new profile's name, typed (OK only once there is one), or in a network game (screen
    0xBBB) the player's own. A yes (31) from the prompt it raises saves it as the profile. In the
    dev build the first one, at screen 0xBBA, may come from the launcher.
*/
PORT(0x00452E00)
void __fastcall profile_name_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    char text[0x80];
    BYTE* edit;

    (void) edx_unused;
    switch (event->type) {
    case EVENT_OPEN: {
        char name[0x10];
        int screen;

        profile_asking = 1;
        load_text(self, TITLE_TEXT, 0, 0x926);
        load_text(self, OK_BUTTON_WORDS, 0, 0x13E9);
        load_text(self, CANCEL_BUTTON, 0, 0x13DC);
        screen = FIELD(game_session, 0x3C, int);
        if (screen == 0xBBA || screen == 0xBBB)
            dialog_enable_controls(self, NULL, 0x65, FALSE);
        load_prompt(self, 0x13AE);
        edit = dialog_find_control(self, NULL, EDIT_BOX, 0);
        edit_set_capacity(edit, NULL, PROFILE_LENGTH);
        edit_set_allowed(edit, NULL, PROFILE_CHARACTERS);
        if (FIELD(game_session, 0x3C, int) == 0xBBB) {
            BYTE* net = FIELD(game_main, 0x8, BYTE*);

            game_FUN_004b0e10(net, NULL, FIELD(net, 0xC, DWORD), name);
            edit_set_text(edit, NULL, name);
            FIELD(edit, 0x5C, int) = 12;            /* the box becomes a label */
            profile_create(event);
            dialog_end(self, NULL, 0);
#ifdef OPENMCM2_DEV
        } else if (screen == 0xBBA && first_rider_named()) {
            /* The first profile, named in the launcher at its install: made as the network's is. */
            edit_set_text(edit, NULL, dev_first_rider());
            FIELD(edit, 0x5C, int) = 12;
            dev_first_rider_done();
            profile_create(event);
            dialog_end(self, NULL, 0);
#endif
        } else {
            gui_user_queue_capture(FIELD(self, 0x34, gui_user*), NULL, edit, NULL, FALSE);
        }
        ok_enable(self, FALSE);
        break;
    }
    case EVENT_CLICK:
        if (clicked(event, CANCEL_BUTTON))
            result_back(0xBB9, event);
        else if (clicked(event, OK_BUTTON_WORDS))
            result_confirm(0xBB9, event);
        break;
    case EVENT_CHILD_ENDED:
        edit = dialog_find_control(self, NULL, EDIT_BOX, 0);
        if (event->a == 0x1F) {
            edit_get_text(edit, NULL, text, sizeof text);
            text_copy_most(game_profile, text, PROFILE_LENGTH);
            profile_load(game_profile);
            dialog_end(self, NULL, 0x1F);
        } else {
            edit_set_text(edit, NULL, NO_TEXT);
            gui_user_queue_capture(FIELD(self, 0x34, gui_user*), NULL, edit, NULL, FALSE);
        }
        break;
    case EVENT_EDIT_CHANGED:
        edit_get_text(dialog_find_control(self, NULL, EDIT_BOX, 11), NULL, text, 0x7F);
        ok_enable(self, text[0] != 0);
        break;
    case EVENT_CLOSE:
        profile_asking = 0;
        break;
    }
}

/* ---- Confirming ---- */

/* What the setup screens confirm into the game (copied whole), and DirectPlay's providers. */
#define confirmed_c24   ((unsigned long*) 0x0068E9D8)  /* 0xDD dwords to game + 0xC24 */
#define confirmed_f98   ((unsigned long*) 0x0068E8D8)  /* 0xB dwords to game + 0xF98 */
#define confirmed_mode  (*(DWORD*) 0x0068E918)         /* then 7 dwords to game + 0xFC4 */
#define serial_settings ((void*) 0x0068E7F8)     /* the cable's COM port, baud and so on */
#define PROVIDER_IPX    (*(const GUID*) 0x0055BA60)    /* DPSPGUID_IPX */
#define PROVIDER_TCPIP  (*(const GUID*) 0x0055BA70)    /* DPSPGUID_TCPIP */
#define PROVIDER_SERIAL (*(const GUID*) 0x0055BA80)    /* DPSPGUID_SERIAL */
#define PROVIDER_MODEM  (*(const GUID*) 0x0055BA90)    /* DPSPGUID_MODEM */
#define game_net        FIELD(game_main, 0x8, BYTE*)

void __cdecl serial_address(const int* settings, DWORD* address);
BOOL __fastcall game_FUN_004b0600(BYTE* net, void* edx_unused, GUID provider, const char* address,
                                  const char* b, void* settings, void** data, DWORD* size);
int  __fastcall game_FUN_004b04e0(BYTE* net, void* edx_unused, void* data, DWORD size, DWORD flags);

/*
    The confirming answer to a screen's result code (see result_back). Those it knows end the
    screen that sent the event (most with 0) and mark the event handled; the connections free
    what they were given.
*/
PORT(0x00453740)
void __cdecl result_confirm(int code, ui_event* event)
{
    char text[0x80];
    char computer[0x10];
    DWORD address[5] = { 0, 0, 0, 0, 0 };
    DWORD size;
    void* data;
    DWORD end = 0;

    switch (code) {
    case 0xC9:
        dialog_notify_controls(event_dialog(event), NULL, 1);
        __movsd((unsigned long*) (game_main + 0xC24), confirmed_c24, 0xDD);
        session_gui->unknown_ec = FIELD(game_main, 0xC40, int);
        go_to(0x66);
        break;
    case 0xCA:
        dialog_notify_controls(event_dialog(event), NULL, 1);
        __movsd((unsigned long*) (game_main + 0xF98), confirmed_f98, 0xB);
        game_FUN_00527080(game_main);
        go_to(0x66);
        break;
    case 0xCB:
        dialog_notify_controls(event_dialog(event), NULL, 1);
        confirmed_mode = list_data(FIELD(dialog_find_control(event_dialog(event), NULL,
                                                             NAME(0x0056FF68), 0),   /* ModeDDL */
                                         COMBO_LIST, BYTE*), NULL, -1);
        __movsd((unsigned long*) (game_main + 0xFC4), &confirmed_mode, 7);
        go_to(0x66);
        break;
    case 0xCD:
        if (!game_main[0x2DA6])
            return;
        go_to(0xD0);
        break;
    case 0xD0:
        go_to(0x68);
        break;
    case 0xD8:
        serial_address(serial_settings, address);    /* a DPCOMPORTADDRESS */
        if (!game_FUN_004b0600(game_net, NULL, PROVIDER_SERIAL, NO_TEXT, NO_TEXT, address, &data,
                               &size)) {
            end = 0xD;
            break;
        }
        if (game_FUN_004b04e0(game_net, NULL, data, size, 8)) {
            game_FUN_004a4450(data, DLGPROCS_CPP, 0xEA4);
            go_to(0x85C);
            return;
        }
        dialog_end(event_dialog(event), NULL, 0xD);
        event->e = 1;
        game_FUN_004a4450(data, DLGPROCS_CPP, 0xEAB);
        return;
    case 0xD9:
        data = NULL;
        edit_get_text(dialog_find_control(event_dialog(event), NULL, EDIT_BOX, 0), NULL, text,
                      sizeof text);
        if (!text[0]) {
            end = 0xE;
            break;
        }
        if (!game_FUN_004b0600(game_net, NULL, PROVIDER_TCPIP, text, NO_TEXT, NULL, &data,
                               &size)) {
            end = 0xD;
            break;
        }
        if (game_FUN_004b04e0(game_net, NULL, data, size, 4)) {
            dialog_end(event_dialog(event), NULL, 0xF);
            event->e = 1;
            game_FUN_004a4450(data, DLGPROCS_CPP, 0xEC3);
        } else {
            dialog_end(event_dialog(event), NULL, 0xD);
            event->e = 1;
            game_FUN_004a4450(data, DLGPROCS_CPP, 0xEC8);
        }
        return;
    case 0x104:
    case 0x105:
        FIELD(game_session, 0x30, int) = FIELD(game_session, 0x3C, int);
        if (race_kind == 3) {
            go_to(0xCD);
            return;
        }
        if (race_kind != 0 && race_kind != 2)
            return;
        break;
    case 0x12C:
        /* The player's name; the computer's, or string 0xBD8, when left empty. */
        edit_get_text(dialog_find_control(event_dialog(event), NULL, EDIT_BOX, 0), NULL,
                      player_name, 0x80);
        if (!player_name[0]) {
            size = sizeof computer;
            GetComputerNameA(computer, &size);
            text_copy_most(player_name, computer, 0x7F);
            if (!player_name[0]) {
                game_FUN_00526fc0(game_main, NULL, 0xBD8, text, sizeof text);
                text_copy_most(player_name, text, 0x7F);
            }
        }
        end = 0x14;
        break;
    case 0x12F:
        end = 0x32;
        break;
    case 0x191:
        game_FUN_00526eb0(game_main, NULL, 0, 0x191, 1);
        return;
    case 0x1F9:
    case 0x1FA:
        break;
    case 0xBB9:
    case 0xBBA:
    case 0xBBB:
        profile_create(event);
        return;
    default:
        return;
    }
    dialog_end(event_dialog(event), NULL, end);
    event->e = 1;
}

/* ---- The record filter panels ---- */

#define DESCRIPTION         0x7F68              /* char[0x20]: the description being typed */
#define DESCRIPTION_LENGTH  0x1F
#define BUTTON_DELETE       NAME(0x005700F8)    /* "ButDelete" */
#define BUTTON_DESCRIPTION  NAME(0x005700D4)    /* "ButDescription" */
#define TEXT_LAPS           NAME(0x00570104)    /* "TxtLaps" */
#define TRACK_AND_DESC      NAME(0x005700E4)    /* "%s, %s" */

int    __cdecl game_FUN_0053a51c(void* file, long offset, int origin);                  /* fseek */
/* fwrite */
size_t __cdecl game_FUN_0053a727(const void* data, size_t size, size_t count, void* file);

/* Out of line, as the original calls them. */
__declspec(noinline) void __fastcall prompt_setup_ids(BYTE* self, void* edx_unused,
    const char* title, UINT title_id, const char* prompt, UINT prompt_id, const char* left,
    UINT left_id, const char* middle, UINT middle_id, const char* right, UINT right_id);
__declspec(noinline) void __fastcall text_entry_setup_ids(BYTE* self, void* edx_unused,
    const char* title, UINT title_id, const char* prompt, UINT prompt_id, char* buffer, int size);
__declspec(noinline) void __fastcall ghost_records_fill(BYTE* self);
__declspec(noinline) void __fastcall replay_records_fill(BYTE* self);

typedef struct record_filter {
    void (__fastcall* fill)(BYTE* self);
    int  ask_line;          /* the delete prompt's allocation */
    int  describe_line;     /* the description entry's */
    int  free_line;         /* the first of the frees on closing */
    BOOL replays;           /* a laps column, and descriptions kept to 31 characters */
} record_filter;

static BYTE* record_picked(BYTE* self, BYTE* list)
{
    return FIELD(self, GHOST_CHOSEN, BYTE*) + list_data(list, NULL, -1) * RECORD_SIZE;
}

/* Delete asks first: messbox2 (three_way_handle), "track, description". */
static void record_ask_delete(BYTE* self, int line)
{
    BYTE* list = dialog_find_control(self, NULL, LIST_TRACK, 3);
    BYTE* record = record_picked(self, list);
    char text[0x100];
    BYTE* prompt;

    game_FUN_0053a11f(text, TRACK_AND_DESC, list_text(list, NULL, -1),
                      (const char*) record + RECORD_DESC);
    prompt = dialog_new(0x7F58, line, NAME(0x0056F734), (void*) 0x005575AC);  /* messbox2.dtm */
    gui_manager_open(FIELD(self, 0x30, gui_manager*), NULL, prompt, 0, 4, 0, self, 0, 0, 1);
    prompt_setup_ids(prompt, NULL, NULL, 0x143C, text, 0, NULL, 0x143E, NULL, 0, NULL, 0x143D);
}

/* The chosen record's description, to edit in messbox1 (text_entry_handle). */
static void record_ask_description(BYTE* self, int line)
{
    char* description = (char*) self + DESCRIPTION;
    BYTE* entry;

    text_copy_most(description, list_text(dialog_find_control(self, NULL, LIST_DESC, 3), NULL, -1),
                   DESCRIPTION_LENGTH);
    entry = dialog_new(0x7F60, line, NAME(0x005700BC), (void*) 0x00557528);   /* messbox1.dtm */
    gui_manager_open(FIELD(self, 0x30, gui_manager*), NULL, entry, 0, 4, 0, self, 0, 0, 1);
    text_entry_setup_ids(entry, NULL, NULL, 0x143F, NULL, 0, description, 0x20);
}

/* The prompts' answers: 101 deletes the file, 201 writes the new description into it. */
static void record_answered(BYTE* self, const ui_event* event, const record_filter* kind)
{
    BYTE* list;

    if (event->a == 0x65) {
        list = dialog_find_control(self, NULL, LIST_TRACK, 3);
        DeleteFileA(FIELD(self, GHOST_PATHS, char**)[list_data(list, NULL, -1)]);
        kind->fill(self);
    } else if (event->a == 0xC9) {
        BYTE* record;
        void* file;

        list = dialog_find_control(self, NULL, LIST_TRACK, 3);
        record = record_picked(self, list);
        if (kind->replays)
            text_copy_most((char*) record + RECORD_DESC, (char*) self + DESCRIPTION,
                           DESCRIPTION_LENGTH);
        else
            text_copy((char*) record + RECORD_DESC, (char*) self + DESCRIPTION);
        file = game_FUN_0053a35b(FIELD(self, GHOST_PATHS, char**)[list_data(list, NULL, -1)],
                                 NAME(0x005700B8));                            /* "rb+" */
        if (file) {
            game_FUN_0053a51c(file, 0, 0);                  /* to the start */
            game_FUN_0053a727(record, RECORD_SIZE, 1, file);
            game_FUN_0053a2ad(file);
        }
        kind->fill(self);
    }
}

static void record_filter_handle(BYTE* self, ui_event* event, const record_filter* kind)
{
    switch (event->type) {
    case EVENT_CLICK:
        if (clicked(event, BUTTON_DELETE)) {
            if (FIELD(self, 0x7F5C, int))
                record_ask_delete(self, kind->ask_line);
        } else if (clicked(event, BUTTON_DESCRIPTION)) {
            if (FIELD(self, 0x7F5C, int))
                record_ask_description(self, kind->describe_line);
        }
        break;
    case EVENT_OPEN:
        FIELD(self, 0x7F5C, DWORD) = 0;
        FIELD(self, GHOST_CHOSEN, void*) = NULL;
        FIELD(self, GHOST_COUNT, int) = 0;
        FIELD(self, GHOST_PATHS, char**) = NULL;
        kind->fill(self);
        if (!kind->replays) {
            control_show(dialog_find_control(self, NULL, LIST_LAPS, 0), NULL, FALSE, TRUE);
            control_show(dialog_find_control(self, NULL, TEXT_LAPS, 0), NULL, FALSE, TRUE);
        }
        break;
    case EVENT_CLOSE:
        records_release(self, kind->free_line);
        break;
    case EVENT_CHILD_ENDED:
        record_answered(self, event, kind);
        break;
    }
}

static const record_filter ghost_filter = { ghost_records_fill, 0x1149, 0x1155, 0x1178, FALSE };
static const record_filter replay_filter = { replay_records_fill, 0x121C, 0x1228, 0x124B, TRUE };

/* GhostFil.dtm's handler (vtable 0x00557420). */
PORT(0x00455030)
void __fastcall ghost_filter_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    record_filter_handle(self, event, &ghost_filter);
}

/* ReplyFil.dtm's handler (vtable 0x005574A4). */
PORT(0x00455A50)
void __fastcall replay_filter_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    record_filter_handle(self, event, &replay_filter);
}

/* ---- The main menu ---- */

/*
    The main menu (vtable 0x0055A070, its slot 10 movie_screen_tick). Three panels drop down from
    it - single player (0x7F60), multiplayer (0x7F64) and the profiles (0x7F68), open at 0x7F6C,
    0x7F70 and 0x7F74 - one at a time, over ui\events.avi playing in a loop (0x7F58).
*/
#define MENU_MOVIE          0x7F58
#define MENU_SINGLE         0x7F60
#define MENU_MULTI          0x7F64
#define MENU_USER           0x7F68
#define MENU_SINGLE_OPEN    0x7F6C
#define MENU_MULTI_OPEN     0x7F70
#define MENU_USER_OPEN      0x7F74
#define MENU_MOVIE_READY    0x7F78
#define MENU_MOVIE_WAITED   0x7F7C

#define SINGLE_ANIMATION    NAME(0x0056F790)    /* "MenuSingleAnimation" */
#define MULTI_ANIMATION     NAME(0x0056F77C)    /* "MenuMultiAnimation" */
#define USER_ANIMATION      NAME(0x0056F768)    /* "MenuUserAnimation" */
#define MESSAGE_BOX         NAME(0x0056F734)    /* "messbox2.dtm" */
#define SP_BASE             NAME(0x0056F660)    /* "SPBase.dtm" */
#define BUT_IPX             NAME(0x0056F6F4)    /* "ButIPX" */
#define BUT_TCP             NAME(0x0056F6EC)    /* "ButTCP" */
#define BUT_ZONE            NAME(0x0056F6E4)    /* "ButZone" */
#define BUT_CABLE           NAME(0x0056F6D8)    /* "ButCable" */
#define BUT_MODEM           NAME(0x0056F6CC)    /* "ButModem" */
#define MOVIE_LOOP          0x0044C190          /* what the movie calls when it ends: start again */

#define session_connecting  FIELD(game_session, 0x4B0, DWORD)
#define profile_removing    (*(int*) 0x005A0F0C)    /* LstProfiles' index of the one to remove */

/* The connections the network lists (Net.cpp): the provider at 0x108, its address at 0x11C. */
#define CONNECTION_SIZE      0x120
#define CONNECTION_COUNT     5
#define CONNECTION_SERVICE   0x108
#define CONNECTION_ADDRESS   0x11C
#define CONNECTION_CONSTRUCT 0x004B23E0
#define CONNECTION_DESTRUCT  0x005292D0

typedef HRESULT (__stdcall* dplay_initialize_connection)(void* dplay, void* connection,
                                                         DWORD flags);

void  __fastcall control_set_link(BYTE* self, void* edx_unused, BYTE* next, DWORD b);

/* Out of line: the original calls these. */
__declspec(noinline) BOOL __cdecl profile_choose(ui_event* event);
__declspec(noinline) void __fastcall profiles_fill(BYTE* self);
__declspec(noinline) void __fastcall profile_show(BYTE* self);

void __fastcall game_FUN_004d8140(BYTE* object);                    /* frees what it holds */
void __fastcall game_FUN_004a3f30(void* video);                     /* stop the movie */
/* the network, made if need be */
BOOL __cdecl net_make(void);
void __stdcall  game_FUN_0053b88e(void* array, size_t size, int count, DWORD construct,
                                  DWORD destruct);                  /* construct an array */
void __stdcall  game_FUN_0053b7b0(void* array, size_t size, int count, DWORD destruct);
int  __fastcall game_FUN_004b2420(BYTE* net, void* edx_unused, BYTE* connections, int* count);
void __fastcall session_quit(BYTE* self);                   /* close the game's window */

static BOOL service_is(const BYTE* connection, const GUID* service)
{
    const DWORD* id = (const DWORD*) (connection + CONNECTION_SERVICE);
    const DWORD* wanted = (const DWORD*) service;

    return id[0] == wanted[0] && id[1] == wanted[1] && id[2] == wanted[2] && id[3] == wanted[3];
}

static void control_enable(BYTE* control, DWORD enabled)
{
    VIRTUAL(control, 0xC4, method_1)(control, NULL, enabled);
}

/* The movie's video (0x1EC), its slot 0x40: paused, it seems (the tick plays it with 0). */
static void menu_movie_pause(BYTE* self, DWORD paused)
{
    BYTE* movie = FIELD(self, MENU_MOVIE, BYTE*);
    void* video = movie ? FIELD(movie, 0x1EC, void*) : NULL;

#ifdef OPENMCM2_DEV
    if (movie)
        video_pause(movie, paused);         /* video.c's, as the stream is never made */
#endif
    if (video)
        VIRTUAL(video, 0x40, method_1)(video, NULL, paused);
}

static void menu_movie_stop(BYTE* self)
{
    BYTE* movie = FIELD(self, MENU_MOVIE, BYTE*);
    void* video = movie ? FIELD(movie, 0x1EC, void*) : NULL;

#ifdef OPENMCM2_DEV
    if (movie)
        video_stop(movie);
#endif
    if (video)
        game_FUN_004a3f30(video);
}

static void panel_close(BYTE* self, int panel, int open)
{
    control_show(FIELD(self, panel, BYTE*), NULL, FALSE, TRUE);
    FIELD(self, open, int) = 0;
}

static void multi_close(BYTE* self)
{
    panel_close(self, MENU_MULTI, MENU_MULTI_OPEN);
    net_leave();
    session_connecting = 0;
}

static void panel_toggle(BYTE* self, int panel, int open)
{
    FIELD(self, open, int) = !FIELD(self, open, int);
    control_show(FIELD(self, panel, BYTE*), NULL, FIELD(self, open, int), TRUE);
}

static void profiles_focus(BYTE* self)
{
    dialog_queue_focus(self, NULL, dialog_find_control(self, NULL, LIST_PROFILES, 3), NULL, FALSE);
}

static void menu_open(BYTE* self, const ui_event* event)
{
    RECT area = { 355, 113, 499, 217 };
    BYTE* old;
    BYTE* movie;

    FIELD(self, MENU_SINGLE, BYTE*) = dialog_find_control(self, NULL, SINGLE_ANIMATION, 5);
    FIELD(self, MENU_MULTI, BYTE*) = dialog_find_control(self, NULL, MULTI_ANIMATION, 5);
    FIELD(self, MENU_USER, BYTE*) = dialog_find_control(self, NULL, USER_ANIMATION, 5);
    control_show(FIELD(self, MENU_SINGLE, BYTE*), NULL, FALSE, FALSE);
    control_show(FIELD(self, MENU_MULTI, BYTE*), NULL, FALSE, FALSE);
    control_show(FIELD(self, MENU_USER, BYTE*), NULL, FALSE, FALSE);
    FIELD(self, MENU_SINGLE_OPEN, int) = 0;
    FIELD(self, MENU_MULTI_OPEN, int) = 0;
    FIELD(self, MENU_USER_OPEN, int) = 0;
    FIELD(self, MENU_MOVIE_WAITED, DWORD) = 0;
    session_connecting = 0;
    series_started = 0;
    profiles_fill(self);
    profile_show(self);

    if (game_files->count < 1 || !FIELD(game_session, 0x48C, DWORD)) {
        go_to(0xBBA);
    } else if (event->param == 99) {
        gui_manager_open(session_gui, NULL,
                         dialog_new(0x7F58, 0xAD, MESSAGE_BOX, (void*) 0x00556C54), 0x1FA, 0xC,
                         0, self, 0, 0, 1);
    }
    old = FIELD(game_main, 0x3440, BYTE*);
    if (old) {
        game_FUN_004d8140(old);
        game_delete(old);
        FIELD(game_main, 0x3440, BYTE*) = NULL;
    }

#ifdef OPENMCM2_DEV
    /*
        The movie plays where ScreenOverCtl's window is: on the single-player panel, which its
        place is relative to - the original's numbers for the original's layout, and the modern
        layout's own.
    */
    if (dev_settings.menus_modern) {
        BYTE* frame = dialog_find_control(self, NULL, NAME(0x0056F724), 0);     /* ScreenOverCtl */
        BYTE* panel = FIELD(self, MENU_SINGLE, BYTE*);

        if (frame && panel) {
            const RECT* at = &FIELD(panel, 0x3C, RECT);

            area = FIELD(frame, 0x3C, RECT);
            area.left += at->left;
            area.right += at->left;
            area.top += at->top;
            area.bottom += at->top;
        }
    }
#endif
    movie = game_new(0x200, DLGPROCS_CPP, 0xBA);
    if (movie)
        movie = movie_construct(movie, NULL, 0, &area, self);
    FIELD(self, MENU_MOVIE, BYTE*) = movie;
    dialog_add(self, NULL, movie, 0, 1);
    object_unlink(FIELD(self, MENU_MOVIE, BYTE*));
    object_move_before(FIELD(self, MENU_MOVIE, BYTE*), NULL,
                       dialog_find_control(self, NULL, NAME(0x0056F724), 0), -1); /* ScreenOverCtl */
    control_set_link(FIELD(self, MENU_MOVIE, BYTE*), NULL,
                     dialog_find_control(self, NULL, SINGLE_ANIMATION, 0), 0);
    FIELD(self, MENU_MOVIE_READY, DWORD) =
        movie_open(movie, NULL, NAME(0x0056F714), FALSE, MOVIE_LOOP, (DWORD) self); /* ui\events.avi */
    if (!FIELD(self, MENU_MOVIE_READY, DWORD))
        log_line("menus: preview video unavailable; continuing without it");
    menu_movie_pause(self, 1);
}

/* Multiplayer: only the Zone, and the services the network offers, can be chosen. */
static void multi_open(BYTE* self)
{
    gui_manager* manager = FIELD(self, 0x30, gui_manager*);
    BYTE connections[CONNECTION_COUNT][CONNECTION_SIZE];
    int count;
    int i;

    gui_manager_set_cursors(manager, NULL, manager->wait_image, 1);
    control_enable(dialog_find_control(self, NULL, BUT_IPX, 1), FALSE);
    control_enable(dialog_find_control(self, NULL, BUT_TCP, 1), FALSE);
    control_enable(dialog_find_control(self, NULL, BUT_ZONE, 1), TRUE);
    control_enable(dialog_find_control(self, NULL, BUT_CABLE, 1), FALSE);
    control_enable(dialog_find_control(self, NULL, BUT_MODEM, 1), FALSE);
    FIELD(game_main, 0x3170, DWORD) = 0;
#ifdef OPENMCM2_DEV
    if (dev_settings.menus_modern) {
        gui_manager_set_cursors(manager, NULL, manager->cursor_image, 0);
        return;                         /* create LAN's network only when LAN is chosen */
    }
    eos_online(FALSE);                  /* the services listed are DirectPlay's own */
#endif
    if (net_make()) {
        game_FUN_0053b88e(connections, CONNECTION_SIZE, CONNECTION_COUNT, CONNECTION_CONSTRUCT,
                          CONNECTION_DESTRUCT);
        game_FUN_004b2420(game_net, NULL, connections[0], &count);
        for (i = 0; i < count; i++) {
            const char* button;

            if (service_is(connections[i], &PROVIDER_IPX))
                button = BUT_IPX;
            else if (service_is(connections[i], &PROVIDER_TCPIP))
                button = BUT_TCP;
            else if (service_is(connections[i], &PROVIDER_SERIAL))
                button = BUT_CABLE;
            else if (service_is(connections[i], &PROVIDER_MODEM))
                button = BUT_MODEM;
            else
                continue;
            control_enable(dialog_find_control(self, NULL, button, 1), TRUE);
        }
        game_FUN_0053b7b0(connections, CONNECTION_SIZE, CONNECTION_COUNT, CONNECTION_DESTRUCT);
    }
    manager = FIELD(self, 0x30, gui_manager*);
    gui_manager_set_cursors(manager, NULL, manager->cursor_image, 0);
}

/* A network address for `service` given to the network as `kind`; the sessions' screen next. */
static void connect_by(GUID service, DWORD kind, int line)
{
    void* address;
    DWORD size;

#ifdef OPENMCM2_DEV
    if (!eos_online(FALSE))              /* switching from Internet may also create LAN's network */
        return;
#endif
    if (!game_net && !net_make())
        return;
    if (game_FUN_004b0600(game_net, NULL, service, NO_TEXT, NO_TEXT, NULL, &address, &size)
        && game_FUN_004b04e0(game_net, NULL, address, size, kind)) {
        game_FUN_004a4450(address, DLGPROCS_CPP, line);
        go_to(0x85C);
    }
}

static void connect_modem(void)
{
    BYTE connections[CONNECTION_COUNT][CONNECTION_SIZE];
    int count;
    int i;

    game_FUN_0053b88e(connections, CONNECTION_SIZE, CONNECTION_COUNT, CONNECTION_CONSTRUCT,
                      CONNECTION_DESTRUCT);
    game_FUN_004b2420(game_net, NULL, connections[0], &count);
    for (i = 0; i < count; i++) {
        if (service_is(connections[i], &PROVIDER_MODEM)) {
            void* dplay = FIELD(game_net, 0x4, void*);

            VIRTUAL(dplay, 0x98, dplay_initialize_connection)(
                dplay, FIELD(connections[i], CONNECTION_ADDRESS, void*), 0);
            go_to(0xFC);
            break;
        }
    }
    game_FUN_0053b7b0(connections, CONNECTION_SIZE, CONNECTION_COUNT, CONNECTION_DESTRUCT);
}

/* A race of `kind` from `folder`, its screen in place of the menu. */
static void event_screen_open(BYTE* self, ui_event* event, int kind, const char* folder, int line)
{
    menu_movie_stop(self);
    FIELD(game_main, 0x2F60, int) = kind;
    FIELD(game_main, 0x2F64, int) = kind;
    FIELD(game_main, 0xC18, const char*) = folder;
    gui_manager_open(session_gui, NULL, dialog_new(0x7F64, line, SP_BASE, (void*) 0x00556B4C), 0,
                     2, 0, NULL, 0, 0, 1);
    dialog_end(event_dialog(event), NULL, 0);
    event->e = 1;
}

#ifdef OPENMCM2_DEV
/*
    The modern menus' Single Player: no event panel, but the event screen straight away, at the
    event last chosen (the panel's buttons' kinds, folders and lines; Supercross the first time).
*/
static void single_player_open(BYTE* self, ui_event* event)
{
    static const struct { int kind; const char* folder; int line; } events[] = {
        { 3, NAME(0x0056F63C), 0x161 },         /* Supercross: Teraform\SX */
        { 2, NAME(0x0056F60C), 0x171 },         /* Nationals */
        { 1, NAME(0x0056CB6C), 0x159 },         /* Baja */
        { 5, NAME(0x0056CB5C), 0x169 },         /* Enduro */
        { 0, NAME(0x0056F66C), 0x151 },         /* Stunt Quarry: Teraform\Quarries */
    };
    int last = FIELD(game_main, 0x2F60, int);
    size_t i, pick = 0;

    for (i = 0; i < sizeof events / sizeof events[0]; i++)
        if (events[i].kind == last)
            pick = i;
    event_screen_open(self, event, events[pick].kind, events[pick].folder, events[pick].line);
}
#endif

/* A screen opened from the menu, which then closes. */
static void screen_open(BYTE* self, size_t size, int line, const char* name, void* vtable,
                        DWORD a)
{
    gui_manager_open(FIELD(self, 0x30, gui_manager*), NULL, dialog_new(size, line, name, vtable),
                     a, 2, 0, NULL, 0, 0, 1);
    dialog_end(self, NULL, 0);
}

/* A dialog over the menu: `a` and `arg` are its own. */
static void box_open(BYTE* self, BYTE* box, DWORD a, DWORD flags, DWORD arg)
{
    gui_manager_open(FIELD(self, 0x30, gui_manager*), NULL, box, a, flags, 0, self, arg, 0, 1);
}

/* The profile picked is removed once a prompt asks; the player's own cannot be. */
static void profile_remove_ask(BYTE* self)
{
    BYTE* list = dialog_find_control(self, NULL, LIST_PROFILES, 0);
    short index = (short) list_selected(list);

    if (!game_FUN_0053b6e0(list_text(list, NULL, -1), game_profile)) {
        box_open(self, dialog_new(0x7F58, 0x198, MESSAGE_BOX, (void*) 0x00556A44), 0, 4, 0);
    } else if (index != -1) {
        BYTE* prompt;

        profile_removing = index;
        prompt = dialog_new(0x7F58, 0x19B, MESSAGE_BOX, (void*) 0x005569C0);
        box_open(self, prompt, 0x12F, 4, (DWORD) list_text(list, NULL, -1));
    }
    dialog_queue_focus(self, NULL, list, NULL, FALSE);
    profiles_fill(self);
}

static void user_panel_after_profile(BYTE* self)
{
    profile_show(self);
    FIELD(self, MENU_USER_OPEN, int) = 0;
    control_show(FIELD(self, MENU_USER, BYTE*), NULL, FALSE, TRUE);
}

#ifdef OPENMCM2_DEV
/*
    MCM2:Reborn's multiplayer is LAN or Internet, each a screen of the look's own (Lan.dtm and
    Online.dtm, made by tools/modern/build_online.py) run by the game's own sessions screen,
    sessions_handle: the games hosted are listed, Host makes one and Join joins the one picked.
    LAN is TCP/IP finding the games on the local network; Internet is Epic Online Services'
    lobbies, through the DirectPlay of code/dev/eosplay.c.
*/
#define ONLINE_VTABLE_SLOTS 0x60
#define CREDITS_VTABLE      ((void**) 0x005567B0)   /* a whole screen's, its handler replaced */
#define sessions_found      (*(int*) 0x0068E80C)    /* sessions_handle's count */

void __fastcall sessions_handle(BYTE* self, void* edx_unused, ui_event* event);

/* The credits' vtable with the handler replaced, and before it, as in any vtable, the pointer to
   its class's RTTI: the game reads screens' class names when it moves between them. */
static void* online_vtable_rtti[1 + ONLINE_VTABLE_SLOTS];
#define online_vtable (online_vtable_rtti + 1)
static BOOL online_screen;          /* the screen open is Online, not LAN */
static BOOL online_internet;        /* its network is EOS's: Online's, but for its Local Network tab */
static BOOL online_failed;          /* its network did not start: only Back works */

/* The network made for the screen and connected through TCP/IP, as LAN's button always did. */
static BOOL online_connect(void)
{
    void* address;
    DWORD size;
    BOOL connected;

    if (!eos_online(online_internet)
        || !game_FUN_004b0600(game_net, NULL, PROVIDER_TCPIP, NO_TEXT, NO_TEXT, NULL, &address, &size))
        return FALSE;
    connected = game_FUN_004b04e0(game_net, NULL, address, size, 4);
    game_FUN_004a4450(address, DLGPROCS_CPP, 0);
    return connected;
}

/* ---- What the screen shows: its tabs, its cards and the game picked ---- */

int __fastcall control_set_colour(BYTE* self, void* edx_unused, COLORREF colour);

#define ONLINE_CARDS        5                       /* the sessions the game keeps */
#define ONLINE_PIPS         8                       /* a card's: how full its lobby is */
#define ONLINE_RIDERS       30                      /* a lobby's names, the most there are */
#define ONLINE_EVENTS       6                       /* race_kind 0 to 5 */
#define session_name(i)     ((const char*) game_main + 0x1014 + (i) * 0x10C)
#define session_instance(i) FIELD(game_main, 0x1118 + (i) * 0x10C, const GUID*)
#define TEXT_PAPER          RGB(244, 240, 230)      /* build_online.py's colours */
#define TEXT_GREY           RGB(150, 150, 156)
#define TEXT_ORANGE         RGB(255, 138, 0)
#define TEXT_INK            RGB(14, 14, 16)
#define TEXT_GREEN          RGB(52, 199, 89)

/*
    Online's tabs and the events each holds, as bits of race_kind; All holds those not known too.
    Local Network is the LAN screen's games: choosing it turns the screen's network to the game's
    own TCP/IP, and choosing another turns it back to EOS's.
*/
#define ONLINE_ALL  -1
#define ONLINE_LAN  -2
static const struct { const char* name; int kinds; } online_tabs[] = {
    { "TabAll", ONLINE_ALL }, { "TabRaces", 1 << 1 | 1 << 2 | 1 << 3 | 1 << 5 }, { "TabQuarry", 1 << 0 },
    { "TabTag", 1 << 4 }, { "TabLan", ONLINE_LAN },
};
#define ONLINE_TABS ((int) (sizeof online_tabs / sizeof online_tabs[0]))

static const char* event_names[ONLINE_EVENTS] = { "STUNT QUARRY", "BAJA", "NATIONALS", "SUPERCROSS", "TAG",
                                                  "ENDURO" };

static int online_tab;                  /* the tab chosen */
static int online_counts[sizeof online_tabs / sizeof online_tabs[0]];  /* each tab's games, -1 not known */
static int online_cards[ONLINE_CARDS];  /* the session each card shows */
static int online_card_count;

static BYTE* online_find(BYTE* self, const char* format, int a, int b)
{
    char name[40];

    wsprintfA(name, format, a, b);
    return dialog_find_control(self, NULL, name, 0);
}

static void online_show(BYTE* self, BOOL shown, const char* format, int a, int b)
{
    BYTE* control = online_find(self, format, a, b);

    if (control)
        control_show(control, NULL, shown, FALSE);
}

static void online_write(BYTE* self, const char* words, COLORREF colour, const char* format, int a)
{
    BYTE* control = online_find(self, format, a, 0);

    if (control) {
        control_set_text(control, NULL, words);
        control_set_colour(control, NULL, colour);
    }
}

/* `from` in capitals, as the look's words are. */
static void capitals(char* to, const char* from, int size)
{
    int i;

    for (i = 0; i < size - 1 && from[i]; i++)
        to[i] = from[i] >= 'a' && from[i] <= 'z' ? from[i] - 'a' + 'A' : from[i];
    to[i] = 0;
}

/* A session's lobby; a LAN game, or a lobby not yet found, is known by its name alone. */
static void online_lobby_of(int session, online_lobby* lobby)
{
    static const online_lobby unknown = { 0, 0, -1 };

    if (online_internet && session_instance(session) && eos_lobby(session_instance(session), lobby))
        return;
    *lobby = unknown;
    lstrcpynA(lobby->host, session_name(session), sizeof lobby->host);
}

/* Whether a tab's games are EOS's (on the LAN screen none are). */
static BOOL online_tab_internet(int tab)
{
    return online_screen && online_tabs[tab].kinds != ONLINE_LAN;
}

/* Whether a game of the network now in use, of `kind`, is one of a tab's. */
static BOOL online_in_tab(int kind, int tab)
{
    int kinds = online_tabs[tab].kinds;

    if (online_tab_internet(tab) != online_internet)
        return FALSE;
    return kinds == ONLINE_ALL || kinds == ONLINE_LAN || (kind >= 0 && (kinds >> kind & 1));
}

/* A lobby's title: its track, or its host's name when the host has not set one up yet. */
static void online_title(const online_lobby* lobby, char* to, int size)
{
    capitals(to, lobby->track[0] ? lobby->track : lobby->host, size);
}

static void online_fill_card(BYTE* self, int card, int session, BOOL chosen)
{
    online_lobby lobby;
    char words[96], host[32];
    int k, pips;

    online_lobby_of(session, &lobby);
    capitals(host, lobby.host, sizeof host);
    online_title(&lobby, words, sizeof words);
    online_write(self, words, TEXT_PAPER, "Lobby%dName", card);
    if (!online_internet)
        lstrcpyA(words, "ON YOUR NETWORK");
    else if (lobby.kind >= 0 && lobby.kind < ONLINE_EVENTS)
        wsprintfA(words, "%s  \xB7  HOSTED BY %s", event_names[lobby.kind], host);
    else
        wsprintfA(words, "HOSTED BY %s", host);
    online_write(self, words, chosen ? TEXT_ORANGE : TEXT_GREY, "Lobby%dInfo", card);
    words[0] = 0;
    if (lobby.most)
        wsprintfA(words, "%d / %d RIDERS", lobby.players, lobby.most);
    online_write(self, words, TEXT_PAPER, "Lobby%dRiders", card);
    online_write(self, lobby.closed ? "RACING" : "", TEXT_ORANGE, "Lobby%dTag", card);
    pips = lobby.most ? (lobby.players * ONLINE_PIPS + lobby.most - 1) / lobby.most : 0;
    for (k = 0; k <= ONLINE_PIPS; k++)
        online_show(self, lobby.most && k == min(pips, ONLINE_PIPS), "Lobby%dPips%d", card, k);
    for (k = 0; k < ONLINE_EVENTS; k++)
        online_show(self, k == lobby.kind, "Lobby%dPic%d", card, k);
    online_show(self, chosen, "Lobby%dChosen", card, 0);
}

static void online_clear_detail(BYTE* self)
{
    static const char* texts[] = { "TxtDetailName", "TxtDetailInfo", "TxtDetailCount", "TxtDetailLaps",
                                   "TxtDetailTrees", "TxtDetailMost" };
    int k;

    for (k = 0; k < ONLINE_EVENTS; k++)
        online_show(self, FALSE, "DetailPic%d", k, 0);
    for (k = 0; k < (int) (sizeof texts / sizeof *texts); k++)
        online_write(self, "", TEXT_PAPER, texts[k], 0);
    for (k = 0; k < ONLINE_RIDERS; k++)
        online_write(self, "", TEXT_PAPER, "TxtRider%d", k);
    for (k = 0; k < 4; k++) {
        online_write(self, "", TEXT_ORANGE, "TxtChatName%d", k);
        online_write(self, "", TEXT_PAPER, "TxtChatText%d", k);
    }
}

static void online_fill_detail(BYTE* self, int session)
{
    online_lobby lobby;
    char words[96], host[32];
    int k, open_at;

    if (session < 0) {
        online_clear_detail(self);
        return;
    }
    online_lobby_of(session, &lobby);
    for (k = 0; k < ONLINE_EVENTS; k++)
        online_show(self, k == lobby.kind, "DetailPic%d", k, 0);
    capitals(host, lobby.host, sizeof host);
    online_title(&lobby, words, sizeof words);
    online_write(self, words, TEXT_PAPER, "TxtDetailName", 0);
    wsprintfA(words, "HOSTED BY %s", host);
    online_write(self, online_internet ? words : "ON YOUR NETWORK", TEXT_GREY, "TxtDetailInfo", 0);
    words[0] = 0;
    if (lobby.most)
        wsprintfA(words, "%d / %d", lobby.players, lobby.most);
    online_write(self, words, TEXT_ORANGE, "TxtDetailCount", 0);
    /* The riders on the gate, then how many places are open. */
    for (k = 0, open_at = -1; k < ONLINE_RIDERS; k++) {
        capitals(words, lobby.riders[k], sizeof words);
        if (words[0]) {
            online_write(self, words, TEXT_PAPER, "TxtRider%d", k);
            continue;
        }
        words[0] = 0;
        if (open_at < 0 && lobby.most > lobby.players) {
            wsprintfA(words, "%d OPEN", lobby.most - lobby.players);
            open_at = k;
        }
        online_write(self, words, TEXT_GREY, "TxtRider%d", k);
    }
    /* Quarry and tag are timed, not raced in laps. */
    words[0] = 0;
    if (lobby.kind == 0 || lobby.kind == 4)
        lstrcpyA(words, "TIMED");
    else if (lobby.kind > 0)
        wsprintfA(words, "%d", lobby.laps);
    online_write(self, words, TEXT_PAPER, "TxtDetailLaps", 0);
    online_write(self, lobby.kind < 0 ? "" : lobby.trees ? "ON" : "OFF", TEXT_PAPER, "TxtDetailTrees", 0);
    words[0] = 0;
    if (lobby.most)
        wsprintfA(words, "%d", lobby.most);
    online_write(self, words, TEXT_PAPER, "TxtDetailMost", 0);
    for (k = 0; k < 4; k++) {
        capitals(words, lobby.chat[k].name, sizeof words);
        online_write(self, words, TEXT_ORANGE, "TxtChatName%d", k);
        online_write(self, lobby.chat[k].text, TEXT_PAPER, "TxtChatText%d", k);
    }
}

static void online_hide_card(BYTE* self, int card)
{
    static const char* parts[] = { "Lobby%d", "Lobby%dName", "Lobby%dInfo", "Lobby%dRiders", "Lobby%dTag",
                                   "Lobby%dChosen" };
    int k;

    for (k = 0; k < (int) (sizeof parts / sizeof *parts); k++)
        online_show(self, FALSE, parts[k], card, 0);
    for (k = 0; k <= ONLINE_PIPS; k++)
        online_show(self, FALSE, "Lobby%dPips%d", card, k);
    for (k = 0; k < ONLINE_EVENTS; k++)
        online_show(self, FALSE, "Lobby%dPic%d", card, k);
}

static void online_show_card(BYTE* self, int card)
{
    static const char* parts[] = { "Lobby%d", "Lobby%dName", "Lobby%dInfo", "Lobby%dRiders", "Lobby%dTag" };
    int k;

    for (k = 0; k < (int) (sizeof parts / sizeof *parts); k++)
        online_show(self, TRUE, parts[k], card, 0);
}

/* The service's state, the rider playing and when the lobbies were found. */
static void online_fill_top(BYTE* self, int state)
{
    char words[64];

    if (!online_internet)
        online_write(self, "LOCAL NETWORK", TEXT_PAPER, "TxtService", 0);
    else if (state > 0)
        online_write(self, "ONLINE  \xB7  EPIC ONLINE SERVICES", TEXT_GREEN, "TxtService", 0);
    else
        online_write(self, state == 0 ? "SIGNING IN  \xB7  EPIC ONLINE SERVICES" : "OFFLINE  \xB7  EPIC ONLINE SERVICES",
                     TEXT_PAPER, "TxtService", 0);
    capitals(words, game_profile, sizeof words);
    online_write(self, words, TEXT_PAPER, "TxtPlayer", 0);
    words[0] = 0;
    if (online_internet && eos_found_ago() >= 0)
        wsprintfA(words, "REFRESHED %d S AGO", eos_found_ago());
    online_write(self, words, TEXT_GREY, "TxtStatus", 0);
}

/* Everything the screen shows, from the sessions the game found. */
static void online_fill(BYTE* self)
{
    BYTE* list = dialog_find_control(self, NULL, "LstSessions", 0);
    int chosen = list ? list_selected(list) : -1;
    int state = online_internet ? eos_state() : 1;
    BOOL chosen_shown = FALSE;
    online_lobby lobby;
    char words[64];
    int i, t;

    online_card_count = 0;
    for (t = 0; t < ONLINE_TABS; t++)
        if (online_tab_internet(t) == online_internet)
            online_counts[t] = 0;
    for (i = 0; i < sessions_found; i++) {
        online_lobby_of(i, &lobby);
        for (t = 0; t < ONLINE_TABS; t++)
            online_counts[t] += online_in_tab(lobby.kind, t);
        if (online_in_tab(lobby.kind, online_tab) && online_card_count < ONLINE_CARDS) {
            chosen_shown |= i == chosen;
            online_cards[online_card_count++] = i;
        }
    }
    /* The game picked is always one shown: the first, when it was not. */
    if (!chosen_shown) {
        chosen = online_card_count ? online_cards[0] : -1;
        if (list && chosen >= 0)
            list_select_data(list, NULL, (DWORD) session_name(chosen));
    }

    for (t = 0; t < ONLINE_TABS; t++) {
        char name[24];

        wsprintfA(name, "%sOn", online_tabs[t].name);
        online_show(self, t == online_tab, name, 0, 0);
        wsprintfA(name, "%sCount", online_tabs[t].name);
        words[0] = 0;
        if (online_counts[t] >= 0)
            wsprintfA(words, "%d", online_counts[t]);
        online_write(self, words, t == online_tab ? TEXT_INK : TEXT_ORANGE, name, 0);
    }
    for (i = 0; i < ONLINE_CARDS; i++) {
        if (i < online_card_count) {
            online_show_card(self, i);
            online_fill_card(self, i, online_cards[i], online_cards[i] == chosen);
        } else {
            online_hide_card(self, i);
        }
    }
    online_fill_detail(self, chosen);

    if (online_failed)
        lstrcpyA(words, online_internet ? "ONLINE PLAY WOULD NOT START" : "THE NETWORK WOULD NOT START");
    else if (state < 0)
        lstrcpyA(words, "EPIC ONLINE SERVICES COULD NOT BE REACHED");
    else if (state == 0)
        lstrcpyA(words, "SIGNING IN...");
    else if (online_card_count)
        words[0] = 0;
    else
        lstrcpyA(words, online_internet ? "NO LOBBIES OPEN  \xB7  HOST ONE" : "NO GAMES FOUND  \xB7  HOST ONE");
    online_write(self, words, TEXT_PAPER, "TxtEmpty", 0);
    online_fill_top(self, state);
}

/* A card or a tab clicked: TRUE when it was one. */
static BOOL online_clicked(BYTE* self, ui_event* event)
{
    char name[24];
    int i;

    for (i = 0; i < online_card_count; i++) {
        wsprintfA(name, "Lobby%d", i);
        if (clicked(event, name)) {
            list_select_data(dialog_find_control(self, NULL, "LstSessions", 0), NULL,
                             (DWORD) session_name(online_cards[i]));
            online_fill(self);
            return TRUE;
        }
    }
    for (i = 0; i < ONLINE_TABS; i++) {
        wsprintfA(name, "%sOn", online_tabs[i].name);
        if (clicked(event, online_tabs[i].name) || clicked(event, name)) {
            online_tab = i;
            if (online_tab_internet(i) != online_internet) {
                online_internet = online_tab_internet(i);
                sessions_found = 0;
                list_clear(dialog_find_control(self, NULL, "LstSessions", 0));
                online_failed = !online_connect();
            }
            online_fill(self);
            return TRUE;
        }
    }
    return FALSE;
}

static void __fastcall online_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    int t;

    if (event->type == EVENT_OPEN) {
        online_tab = 0;
        for (t = 0; t < ONLINE_TABS; t++)
            online_counts[t] = -1;
        online_failed = !online_connect();
    }
    if (event->type == EVENT_CLICK && online_clicked(self, event)) {
        event->e = 1;
        return;
    }
    /* With no network there is nothing to find, host or join: Back works, and the tabs try again. */
    if (online_failed && (event->type == EVENT_TIMER
                          || (event->type == EVENT_CLICK && !clicked(event, "ButCancel")))) {
        online_fill(self);
        return;
    }
    sessions_handle(self, edx_unused, event);
    if (event->type == EVENT_OPEN || event->type == EVENT_TIMER)
        online_fill(self);
}

static void online_open(BYTE* self, BOOL internet)
{
    int i;

    if (!online_vtable[0]) {
        for (i = -1; i < ONLINE_VTABLE_SLOTS; i++)
            online_vtable[i] = CREDITS_VTABLE[i];
        online_vtable[0x74 / 4] = (void*) online_handle;
    }
    online_screen = internet;
    online_internet = internet;
    box_open(self, dialog_new(0x7F5C, 0, internet ? "Online.dtm" : "Lan.dtm", online_vtable), 0, 4, 0);
}

/* The multiplayer panel's LAN and Internet. TRUE when the click was one of them. */
static BOOL multi_clicked(BYTE* self, ui_event* event)
{
    BOOL internet = clicked(event, "ButInternet");

    if (!internet && !clicked(event, BUT_TCP))
        return FALSE;
    log_line("multiplayer: %s", internet ? "Internet" : "LAN");
    session_connecting = 1;
    online_open(self, internet);
    return TRUE;
}
#endif

#ifdef OPENMCM2_DEV
/*
    The lap leaderboards (Leaders.dtm, tools/modern/build_leaders.py; code/dev/leaders.c), in place of
    the main menu's HIGH SCORES when the modern menus have its layout. A board is a track's, an
    event's and a bike class's: each class its own, so riders are measured against their own kind.
    Its riders' best laps from the second lap on, best first. Made on the high scores' vtable, its
    events this screen's own.
*/
#define LEADERS_LAYOUT  "Leaders.dtm"
#define LEADERS_VTABLE  0x00556834      /* HiScoresDlg's: slot 29 the events */
#define LEADERS_SLOTS   32
#define BOARD_ROWS      12
#define TRACKS_MOST     64

static const struct { int kind; const char* name; } leader_kinds[] = {
    { 1, "BAJA" }, { 2, "NATIONALS" }, { 3, "SUPERCROSS" }, { 5, "ENDURO" },
};
static const struct { int cc; const char* name; } leader_classes[] = {
    { 125, "125 2-STROKE" }, { 250, "250 2-STROKE" }, { 400, "400 4-STROKE" }, { 500, "500 2-STROKE" },
    { 600, "600 4-STROKE" },
};

#define KINDS_COUNT     ((int) (sizeof leader_kinds / sizeof *leader_kinds))
#define CLASSES_COUNT   ((int) (sizeof leader_classes / sizeof *leader_classes))

static void* g_leaders_vtable[LEADERS_SLOTS + 1];
static int   g_leaders_kind = 3, g_leaders_cc = 125;
static char  g_leaders_track[64];
static char  g_leaders_tracks[TRACKS_MOST][64];
static int   g_leaders_track_count;
static LONG  g_leaders_seen;               /* the server's answers shown (leaders_version) */

#define LEADERS_TIMER   0x73A              /* looks for the server's answer */

static BOOL leaders_layout(void)
{
    return dev_modern_has("LEADERS.DAT");
}

static void leaders_write(BYTE* self, const char* name, const char* words)
{
    BYTE* text = dialog_find_control(self, NULL, name, 0);

    if (text)
        control_set_text(text, NULL, words);
}

/* A pair of tab buttons, as chosen (NAMEOn) or not (NAME). */
static void leaders_tab(BYTE* self, const char* name, BOOL chosen)
{
    char on[32];
    BYTE* off_button = dialog_find_control(self, NULL, name, 0);
    BYTE* on_button;

    wsprintfA(on, "%sOn", name);
    on_button = dialog_find_control(self, NULL, on, 0);
    if (off_button)
        control_show(off_button, NULL, !chosen, TRUE);
    if (on_button)
        control_show(on_button, NULL, chosen, TRUE);
}

/* The event's tracks in the game, then any with laps here or on the server that it has not (a
   custom track this computer has not got, or one since taken out). */
static void leaders_tracks_fill(void)
{
    int n = track_names(g_leaders_kind, g_leaders_tracks, TRACKS_MOST);

    g_leaders_track_count = leaders_tracks(g_leaders_kind, g_leaders_cc, g_leaders_tracks, n, TRACKS_MOST);
}

/* Opened after a race not yet seen here, the board is that race's: its event, class and track. */
static void leaders_last_race(void)
{
    static char seen[64];
    const char* file = (const char*) game_main + 0x2DA6;
    int kind = FIELD(game_main, 0x2D74, int), cc, i;
    char track[64];

    if (!file[0] || !lstrcmpiA(file, seen))
        return;
    lstrcpynA(seen, file, sizeof seen);
    for (i = 0; i < KINDS_COUNT && leader_kinds[i].kind != kind; i++)
        ;
    if (i == KINDS_COUNT)
        return;
    cc = leaders_race(track);
    g_leaders_kind = kind;
    for (i = 0; i < CLASSES_COUNT; i++)
        if (leader_classes[i].cc == cc)
            g_leaders_cc = cc;
    lstrcpynA(g_leaders_track, track, sizeof g_leaders_track);
}

/* The tabs, the track list (unless `tracks_too` is off: its own pick) and the chosen board. */
static void leaders_refresh(BYTE* self, BOOL tracks_too)
{
    BYTE* list = tracks_too ? dialog_find_control(self, NULL, "LstTracks", 3) : NULL;
    char name[32], words[160];
    leader board[BOARD_MOST];
    const char* kind_name = "";
    const char* class_name = "";
    int state = LEADERS_LOCAL;
    int i, n, mine, chosen = -1;

    for (i = 0; i < KINDS_COUNT; i++) {
        wsprintfA(name, "BtnKind%d", leader_kinds[i].kind);
        leaders_tab(self, name, leader_kinds[i].kind == g_leaders_kind);
        if (leader_kinds[i].kind == g_leaders_kind)
            kind_name = leader_kinds[i].name;
    }
    for (i = 0; i < CLASSES_COUNT; i++) {
        wsprintfA(name, "BtnClass%d", leader_classes[i].cc);
        leaders_tab(self, name, leader_classes[i].cc == g_leaders_cc);
        if (leader_classes[i].cc == g_leaders_cc)
            class_name = leader_classes[i].name;
    }

    /* The tracks with laps for the event and class; the one chosen kept if it is among them. */
    if (tracks_too)
        leaders_tracks_fill();
    for (i = 0; i < g_leaders_track_count; i++)
        if (!lstrcmpiA(g_leaders_tracks[i], g_leaders_track))
            chosen = i;
    if (list) {
        list_clear(list);
        for (i = 0; i < g_leaders_track_count; i++) {
            capitals(words, g_leaders_tracks[i], sizeof words);
            list_add_text(list, NULL, words, (DWORD) i, 0);
        }
    }
    if (chosen < 0 && g_leaders_track_count) {
        chosen = 0;
        lstrcpynA(g_leaders_track, g_leaders_tracks[0], sizeof g_leaders_track);
    }
    if (list && chosen >= 0)
        list_select_data(list, NULL, (DWORD) chosen);

    n = chosen >= 0 ? leaders_board(g_leaders_kind, g_leaders_cc, g_leaders_track, board, BOARD_MOST, &state) : 0;
    capitals(words, chosen >= 0 ? g_leaders_track : "", sizeof words);
    leaders_write(self, "TxtBoard", words);
    mine = leaders_mine(board, n);
    words[0] = 0;
    if (mine >= 0)
        wsprintfA(words, "YOUR BEST  %d:%02d.%03d  \xB7  P%d OF %d%s", board[mine].ms / 60000,
                  board[mine].ms / 1000 % 60, board[mine].ms % 1000, mine + 1, n, n == BOARD_MOST ? "+" : "");
    leaders_write(self, "TxtMine", words);
    n = n < BOARD_ROWS ? n : BOARD_ROWS;
    wsprintfA(words, "%s  \xB7  %s  \xB7  BEST LAPS FROM THE SECOND LAP ON", kind_name, class_name);
    leaders_write(self, "TxtBoardInfo", words);
    leaders_write(self, "TxtSource", state == LEADERS_ONLINE ? "ONLINE LEADERBOARD  \xB7  EVERY RIDER"
                                    : state == LEADERS_LOADING ? "ASKING THE SERVER...  \xB7  THIS PC"
                                    : state == LEADERS_OFFLINE ? "SERVER NOT ANSWERING  \xB7  THIS PC" : "THIS PC'S LAPS");
    for (i = 0; i < BOARD_ROWS; i++) {
        char cell[24];

        wsprintfA(name, "TxtRank%d", i);
        wsprintfA(cell, "%d", i + 1);
        leaders_write(self, name, i < n ? cell : "");
        wsprintfA(name, "TxtName%d", i);
        capitals(words, i < n ? board[i].name : "", sizeof words);
        leaders_write(self, name, words);
        wsprintfA(name, "TxtTime%d", i);
        if (i < n)
            wsprintfA(cell, "%d:%02d.%03d", board[i].ms / 60000, board[i].ms / 1000 % 60, board[i].ms % 1000);
        leaders_write(self, name, i < n ? cell : "");
        wsprintfA(name, "TxtDate%d", i);
        leaders_write(self, name, i < n ? board[i].date : "");
    }
    leaders_write(self, "TxtEmpty", n ? "" : chosen < 0 ? "NO TRACKS FOR THIS EVENT"
                                        : state == LEADERS_LOADING ? "" : "NO LAPS ON THIS BOARD YET");
    g_leaders_seen = leaders_version();
}

static void __fastcall leaders_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    char name[32];
    int i;

    (void) edx_unused;
    if (event->type == EVENT_OPEN) {
        leaders_last_race();
        leaders_refresh(self, TRUE);
        if (g_leaders_url[0])
            dialog_add_timer(self, NULL, LEADERS_TIMER, 250, NULL);
        return;
    }
    if (event->type == EVENT_TIMER && event->a == LEADERS_TIMER) {
        if (leaders_version() != g_leaders_seen)
            leaders_refresh(self, TRUE);
        return;
    }
    if (event->type == EVENT_SELECT && clicked(event, "LstTracks")) {
        DWORD at = list_data((BYTE*) event->c, NULL, -1);

        if (at < (DWORD) g_leaders_track_count) {
            lstrcpynA(g_leaders_track, g_leaders_tracks[at], sizeof g_leaders_track);
            leaders_refresh(self, FALSE);
        }
        return;
    }
    if (event->type != EVENT_CLICK)
        return;
    if (clicked(event, "ButBack")) {
        screen_open(self, 0x7F80, __LINE__, NAME(0x0057392C), (void*) 0x0055A070, 0x64);   /* mainmenu.dtm */
        event->e = 1;
        return;
    }
    for (i = 0; i < KINDS_COUNT; i++) {
        wsprintfA(name, "BtnKind%d", leader_kinds[i].kind);
        if (clicked(event, name)) {
            g_leaders_kind = leader_kinds[i].kind;
            leaders_refresh(self, TRUE);
            return;
        }
    }
    for (i = 0; i < CLASSES_COUNT; i++) {
        wsprintfA(name, "BtnClass%d", leader_classes[i].cc);
        if (clicked(event, name)) {
            g_leaders_cc = leader_classes[i].cc;
            leaders_refresh(self, TRUE);
            return;
        }
    }
}

static void* leaders_vtable(void)
{
    if (!g_leaders_vtable[1]) {
        __movsd((unsigned long*) g_leaders_vtable, (const unsigned long*) (LEADERS_VTABLE - 4), LEADERS_SLOTS + 1);
        g_leaders_vtable[1 + 29] = (void*) leaders_handle;
    }
    return &g_leaders_vtable[1];
}
#endif

static void menu_click(BYTE* self, ui_event* event)
{
#ifdef OPENMCM2_DEV
    if (live_menu_leaving(self, event))
        return;
    if (menu_reborn(self) && multi_clicked(self, event))
        return;
    if (clicked(event, "ButLeaders") && leaders_layout()) {         /* the multiplayer panel's */
        screen_open(self, 0x7F68, __LINE__, LEADERS_LAYOUT, leaders_vtable(), 0x911);
        return;
    }
    /*
        The modern menus start single player (and Pro Circuit) from the menu itself, where the
        original reached them only through panels whose opening closed multiplayer's. Its
        network is closed here instead: a race with one up is a network race, which takes no
        input from this computer's player and cannot be paused.
    */
    if (dev_settings.menus_modern
        && (clicked(event, NAME(0x0056F708)) || clicked(event, NAME(0x0056F5FC)))) {   /* MenuSingle, ButProCircuit */
        if (FIELD(self, MENU_MULTI_OPEN, int))
            multi_close(self);
        else if (game_net)
            net_leave();
    }
#endif
    if (clicked(event, NAME(0x0056F708))) {                                 /* "MenuSingle" */
#ifdef OPENMCM2_DEV
        if (dev_settings.menus_modern) {
            single_player_open(self, event);
            return;
        }
#endif
        panel_toggle(self, MENU_SINGLE, MENU_SINGLE_OPEN);
        FIELD(self, MENU_MOVIE_READY, DWORD) = 1;
        if (FIELD(self, MENU_SINGLE_OPEN, int))
            FIELD(self, MENU_MOVIE_WAITED, DWORD) = 0;
        menu_movie_pause(self, 1);
        control_show(FIELD(self, MENU_MOVIE, BYTE*), NULL, FALSE, TRUE);
        if (FIELD(self, MENU_MULTI_OPEN, int))
            multi_close(self);
        if (FIELD(self, MENU_USER_OPEN, int))
            panel_close(self, MENU_USER, MENU_USER_OPEN);
    } else if (clicked(event, NAME(0x0056F6FC))) {                          /* "MenuMulti" */
        panel_toggle(self, MENU_MULTI, MENU_MULTI_OPEN);
        if (FIELD(self, MENU_SINGLE_OPEN, int))
            panel_close(self, MENU_SINGLE, MENU_SINGLE_OPEN);
        if (FIELD(self, MENU_USER_OPEN, int))
            panel_close(self, MENU_USER, MENU_USER_OPEN);
        if (FIELD(self, MENU_MULTI_OPEN, int)) {
            multi_open(self);
        } else {
            net_leave();
            session_connecting = 0;
        }
    } else if (clicked(event, NAME(0x0056F6BC))                             /* "MenuUserProfile" */
               || clicked(event, NAME(0x0056F6A8))) {                       /* "ButCancelProfile" */
        panel_toggle(self, MENU_USER, MENU_USER_OPEN);
        if (FIELD(self, MENU_MULTI_OPEN, int))
            multi_close(self);
        if (FIELD(self, MENU_USER_OPEN, int))
            profiles_focus(self);
        if (FIELD(self, MENU_SINGLE_OPEN, int))
            panel_close(self, MENU_SINGLE, MENU_SINGLE_OPEN);
    } else if (clicked(event, NAME(0x0056F69C))) {                          /* "MenuOptions" */
        box_open(self, dialog_new(0x7F70, 0x14A, NAME(0x0056F690), (void*) 0x00556BD0), 0, 2, 0);
    } else if (clicked(event, NAME(0x0056F680))) {                          /* "ButStuntQuarry" */
        event_screen_open(self, event, 0, NAME(0x0056F66C), 0x151);         /* Teraform\Quarries */
    } else if (clicked(event, NAME(0x0056F658))) {                          /* "ButBaja" */
        event_screen_open(self, event, 1, NAME(0x0056CB6C), 0x159);         /* Teraform\Baja */
    } else if (clicked(event, NAME(0x0056F648))) {                          /* "ButSupercross" */
        event_screen_open(self, event, 3, NAME(0x0056F63C), 0x161);         /* Teraform\SX */
    } else if (clicked(event, NAME(0x0056F630))) {                          /* "ButEnduro" */
        event_screen_open(self, event, 5, NAME(0x0056CB5C), 0x169);         /* Teraform\Enduro */
    } else if (clicked(event, NAME(0x0056F620))) {                          /* "ButNationals" */
        event_screen_open(self, event, 2, NAME(0x0056F60C), 0x171);         /* Teraform\National */
    } else if (clicked(event, NAME(0x0056F5FC))) {                          /* "ButProCircuit" */
        menu_movie_stop(self);
        gui_manager_open(session_gui, NULL,
                         dialog_new(0x8074, 0x176, NAME(0x0056F5F0), (void*) 0x00556AC8),
                         0, 2, 0, NULL, 0, 0, 1);                           /* PCStart.dtm */
        dialog_end(event_dialog(event), NULL, 0);
        event->e = 1;
    } else if (clicked(event, NAME(0x0056F5E0))) {                          /* "ButLoadProfile" */
        profile_save(game_profile);
        if (profile_choose(event))
            game_FUN_00527080(game_main);
        profiles_focus(self);
        user_panel_after_profile(self);
    } else if (clicked(event, NAME(0x0056F5D0))) {                          /* "ButNewProfile" */
        profile_save(game_profile);
        go_to(0xBB9);
        profiles_focus(self);
        profiles_fill(self);
        user_panel_after_profile(self);
    } else if (clicked(event, NAME(0x0056F5BC))) {                          /* "ButRemoveProfile" */
        profile_remove_ask(self);
    } else if (clicked(event, BUT_IPX)) {
        session_connecting = 1;
        connect_by(PROVIDER_IPX, 1, 0x1AC);
    } else if (clicked(event, BUT_TCP)) {
        session_connecting = 1;
        box_open(self, dialog_new(0x7F58, 0x1B6, MESSAGE_BOX, (void*) 0x0055693C), 0, 4, 0);
    } else if (clicked(event, BUT_ZONE)) {
        FIELD(game_main, 0x343C, DWORD) = 1;
        dialog_end(event_dialog(event), NULL, 0);
        event->e = 1;
        session_quit(game_session);
    } else if (clicked(event, BUT_CABLE)) {
        session_connecting = 1;
        go_to(0xD8);
    } else if (clicked(event, BUT_MODEM)) {
        session_connecting = 1;
        connect_modem();
    } else if (clicked(event, NAME(0x0056F5B0))) {                          /* "MenuReplay" */
        screen_open(self, 0x7F60, 0x1D8, NAME(0x0056F5A4), (void*) 0x005568B8, 0);   /* GRBase */
    } else if (clicked(event, NAME(0x0056F594))) {                          /* "MenuHighScore" */
#ifdef OPENMCM2_DEV
        if (leaders_layout())
            screen_open(self, 0x7F68, __LINE__, LEADERS_LAYOUT, leaders_vtable(), 0x911);
        else
#endif
        screen_open(self, 0x7F68, 0x1DD, NAME(0x0056F584), (void*) 0x00556834, 0x911);
    } else if (clicked(event, NAME(0x0056F578))) {                          /* "MenuCredits" */
        screen_open(self, 0x7F5C, 0x1E6, NAME(0x0056DC98), (void*) 0x005567B0, 0);   /* credits */
    } else if (clicked(event, NAME(0x0056F56C))) {                          /* "MenuHelp" */
        profile_open_help(game_profile, NULL, HELP_TOPIC, 0);
    } else if (clicked(event, NAME(0x0056F554))) {                          /* "MenuExit" */
        session_quit(game_session);
    }
}

/* The profile asked about (profile_removing): its folder deleted, and it is unlisted. */
static void profile_remove(BYTE* list)
{
    char name[PROFILE_LENGTH + 1];
    char path[0x104];

    text_copy_most(name, list_text(list, NULL, profile_removing), PROFILE_LENGTH);
    game_FUN_0053a11f(path, NAME(0x0056C7DC), PROFILE_FOLDER, name);        /* %s\%s */
    if (dir_list_remove_folder(game_files, NULL, path)) {
        VIRTUAL(game_files, 4, method_0)(game_files);
        list_remove(list, NULL, profile_removing);
    }
}

static void menu_child_ended(BYTE* self, ui_event* event)
{
    if (event->a == 0x1E || event->a == 0x32) {
        BYTE* list = dialog_find_control(self, NULL, LIST_PROFILES, 0);

        if (event->a == 0x1E) {                 /* a profile made */
            list_add_text(list, NULL, game_profile, 0, 0);
            list_sort(list, NULL, TRUE);
            list_select_text(list, NULL, game_profile);
        } else if (event->a == 0x32) {          /* removing one agreed to */
            profile_remove(list);
        }
        profiles_fill(self);
        profile_show(self);
        if (FIELD(self, MENU_USER_OPEN, int))
            dialog_queue_focus(self, NULL, list, NULL, FALSE);
    }
    if (event->a == 0x1F)
        profile_show(self);
    if (event->a == 0x63) {
        dialog_end(event_dialog(event), NULL, 0);
        event->e = 1;
    }
    if (event->a == 0x3D)
        go_to(0x85D);
    if (event->a == 0x3C)                       /* TCP/IP chosen */
        connect_by(PROVIDER_TCPIP, 2, 0x240);
}

PORT(0x0044C1D0)
void __fastcall main_menu_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    (void) edx_unused;
    switch (event->type) {
    case EVENT_CLICK:
        menu_click(self, event);
        break;
    case EVENT_OPEN:
        menu_open(self, event);
        break;
    case EVENT_CLOSE:
        FIELD(self, MENU_USER_OPEN, DWORD) = 0;
        FIELD(self, MENU_MULTI_OPEN, DWORD) = 0;
        FIELD(self, MENU_SINGLE_OPEN, DWORD) = 0;
        FIELD(self, MENU_USER, BYTE*) = NULL;
        FIELD(self, MENU_MULTI, BYTE*) = NULL;
        FIELD(self, MENU_SINGLE, BYTE*) = NULL;
        break;
    case EVENT_CHILD_ENDED:
        menu_child_ended(self, event);
        break;
    }
}

#ifdef OPENMCM2_DEV
/*
    The live background: the main menu over a replay - the newest in Record, else the last race's
    tape - played as the replay screens play one, its sound off, going back to the start before
    it ends. (The race setup's own demo, ui\demo.rpl with the credits over it, is unused and
    cannot load: it names a pre-release game's rider models.)

    Started where the main menu would open, a few frames on: the screen before it closes first,
    and it would still use the menus' background, which the race drops. The replay's race
    settings become the game's (those there kept, as Back keeps them), and the race loads as any
    does, behind its loading screen. The replay's controls it opens close, the race going on, and
    the main menu opens over it. A button that leaves the main menu stops the race, brings back
    the plain menus as the controls' Exit does, and is clicked again on their main menu.
*/
#define RECORD_FOLDER   NAME(0x0056FB64)    /* "Record" */
#define RECORD_REPLAYS  NAME(0x0056F848)    /* "Record\*.vcr" */
#define LAST_RACE_TAPE  NAME(0x0056DC74)    /* "VCRtape.dat" */
#define PATH_JOIN       NAME(0x0056C7DC)    /* "%s\%s" */
#define RACE_SOUND      NAME(0x0057B88C)    /* "RaceSound" */
#define VCR_VTABLE      0x00555E4C          /* the replay's controls */
#define VCR_LEAVING     0x7F58              /* theirs: closing keeps the cursor */
#define MAIN_MENU_VTABLE 0x0055A070
#define REPLAY_TIME     0x1B8               /* the race's: where the replay is (float) */
#define REPLAY_SEEK     0x1C4               /* where to go (float), -1 when not going */
#define replay_length(race) FIELD(FIELD(race, 0x1A0, BYTE*), 0x10C, float)

static BOOL live_given_up;                  /* it would not load: the plain menus from now */
static BOOL live_skip;                      /* the next main menu is a plain one */
static int  live_frames;
static const char* live_folder_before;      /* the game's event folder, put back after */
static int live_event_before;               /* and the profile's event */
static char live_replay[0x104];

/* Each event kind's folder, as the main menu's buttons give them; the tag events have none. */
static const char* const live_folders[6] = {
    NAME(0x0056F66C),                   /* Teraform\Quarries */
    NAME(0x0056CB6C),                   /* Teraform\Baja */
    NAME(0x0056F60C),                   /* Teraform\National */
    NAME(0x0056F63C),                   /* Teraform\SX */
    NULL,
    NAME(0x0056CB5C),                   /* Teraform\Enduro */
};

/* The settings, event folder and profile event the flyby replaced, back. */
static void live_settings_back(void)
{
    settings_restore();
    FIELD(game_main, 0xC18, const char*) = live_folder_before;
    profile_set_event(game_profile, NULL, live_event_before);
}

void __fastcall game_FUN_00469cb0(void* object, void* edx_unused, const char* class_name);

/* As the pause does: the race's sounds, under the game's 0x34, stopped. */
static void live_mute(void)
{
    game_FUN_00469cb0(FIELD(game_main, 0x34, void*), NULL, RACE_SOUND);
}

BOOL live_menu_start(void)
{
    if (live_state == LIVE_LOADING || live_state == LIVE_READY) {
        /* The race did not load, and the loading screen came back here. */
        dev_trace("menus", "live background: the replay did not load");
        live_state = LIVE_OFF;
        live_given_up = TRUE;
        live_controls = NULL;
        live_settings_back();
        game_FUN_0045f720(race_events, NULL, 100);
        return TRUE;
    }
    if (live_skip) {
        live_skip = FALSE;
        return FALSE;
    }
    if (!dev_settings.menus_live || live_given_up || live_state != LIVE_OFF
        || FIELD(game_main, 0x8, BYTE*) || FIELD(game_main, 0x18, int) != 1)
        return FALSE;
    live_state = LIVE_WAITING;
    live_frames = 0;
    return TRUE;
}

static BOOL file_newer(const FILETIME* a, const FILETIME* b)
{
    return a->dwHighDateTime != b->dwHighDateTime ? a->dwHighDateTime > b->dwHighDateTime
                                                  : a->dwLowDateTime > b->dwLowDateTime;
}

/* The replay to show: the newest in Record, else the last race's tape; FALSE if neither. */
static BOOL live_find_replay(void)
{
    WIN32_FIND_DATAA found;
    FILETIME newest = { 0, 0 };
    HANDLE search = FindFirstFileA(RECORD_REPLAYS, &found);

    live_replay[0] = 0;
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (!live_replay[0] || file_newer(&found.ftLastWriteTime, &newest)) {
                newest = found.ftLastWriteTime;
                game_FUN_0053a11f(live_replay, PATH_JOIN, RECORD_FOLDER, found.cFileName);
            }
        } while (FindNextFileA(search, &found));
        FindClose(search);
    }
    if (!live_replay[0]) {
        search = FindFirstFileA(LAST_RACE_TAPE, &found);
        if (search == INVALID_HANDLE_VALUE)
            return FALSE;
        FindClose(search);
        text_copy_most(live_replay, LAST_RACE_TAPE, sizeof live_replay - 1);
    }
    return TRUE;
}

/* The replay's settings, and its race loading; the plain main menu if there is none to show. */
static void live_menu_load(void)
{
    BYTE record[RECORD_SIZE];
    void* file = NULL;
    size_t read = 0;
    int kind;
    int i;

    for (i = 0; i < 8; i++) {           /* the record's member constructors */
        FIELD(record, 0x180 + i * 8, DWORD) = 0;
        record[0x184 + i * 8] = 0;
    }
    if (live_find_replay())
        file = game_FUN_0053a35b(live_replay, NAME(0x0056C66C));              /* "rb" */
    if (file) {
        read = game_FUN_0053a5d5(record, RECORD_SIZE, 1, file);
        game_FUN_0053a2ad(file);
    }
    kind = FIELD(record, 0x34 + SET_EVENT, int);
    if (!read || FIELD(record, 0x34 + SET_MODE, int) == GHOST_RACE || (unsigned) kind > 5
        || !live_folders[kind]) {
        dev_trace("menus", "live background: no replay it can show (record a race to have one)");
        live_state = LIVE_OFF;
        live_given_up = TRUE;
        go_to(100);
        return;
    }

    settings_save();
    live_folder_before = FIELD(game_main, 0xC18, const char*);
    FIELD(game_main, 0xC18, const char*) = live_folders[kind];
    live_event_before = FIELD(game_profile, 0x2800, int);
    profile_set_event(game_profile, NULL, kind);
    __movsd((unsigned long*) race_settings, (unsigned long*) (record + 0x34), 0x7B);
    FIELD(game_main, 0x3428, DWORD) = 1;
    FIELD(game_main, 0x342C, DWORD) = 1;
    FIELD(game_main, 0x1550, DWORD) = FIELD(record, 0x33C, DWORD);
    FIELD(game_main, 0x1554, DWORD) = FIELD(record, 0x340, DWORD);
    text_copy_most((char*) game_main + 0x2C6C, live_replay, TRACK_NAME_LENGTH);
    live_state = LIVE_LOADING;
    dev_trace("menus", "live background: loading %s, event kind %d", live_replay, race_kind);
    race_start_loading();
}

void live_menu_opening(BYTE* dialog)
{
    if (live_state == LIVE_LOADING)
        dev_trace("menus", "live background: dialog %08X opens (vtable %08X)", (DWORD) dialog,
                  FIELD(dialog, 0, DWORD));
    if (live_state == LIVE_LOADING && FIELD(dialog, 0, DWORD) == VCR_VTABLE) {
        live_controls = dialog;
        live_state = LIVE_READY;
        dev_trace("menus", "live background: the race is loaded");
    }
}

/* The race runs: the main menu over it. */
static void live_menu_show(void)
{
    live_state = LIVE_SHOWN;
    live_frames = 0;
    if (live_controls) {
        FIELD(live_controls, VCR_LEAVING, DWORD) = 1;
        dialog_end(live_controls, NULL, 0);
        live_controls = NULL;
    }
    session_show_cursors(game_session);
    go_to(100);
    dev_trace("menus", "live background: running");
}

void live_menu_tick(void)
{
    BYTE* race;

    if (live_state == LIVE_WAITING && ++live_frames >= 3) {
        live_menu_load();
        return;
    }
    if (live_state != LIVE_SHOWN || !race_events)
        return;
    race = game_FUN_0045e300(race_events);
    if (!race) {                        /* it ended some other way */
        live_state = LIVE_OFF;
        return;
    }
    /* Not at once: the loading screen just closed is still among the objects, half gone. Then
       again now and then, for the sounds the race makes later. */
    if (++live_frames % 30 == 3)
        live_mute();
    if (FIELD(race, REPLAY_SEEK, float) < 0.0f && replay_length(race) > 2.0f
        && FIELD(race, REPLAY_TIME, float) > replay_length(race) - 1.0f)
        FIELD(race, REPLAY_SEEK, float) = 0.0f;
}

/* The main menu's buttons that stay on it: the panels, the help and quitting. */
static BOOL live_click_stays(const ui_event* event)
{
    static const char* const staying[] = {
        NAME(0x0056F708),               /* "MenuSingle" */
        NAME(0x0056F6FC),               /* "MenuMulti" */
        NAME(0x0056F6BC),               /* "MenuUserProfile" */
        NAME(0x0056F6A8),               /* "ButCancelProfile" */
        NAME(0x0056F56C),               /* "MenuHelp" */
        NAME(0x0056F554),               /* "MenuExit" */
    };
    int i;

    for (i = 0; i < (int) (sizeof staying / sizeof *staying); i++)
        if (clicked(event, staying[i]))
            return TRUE;
    return FALSE;
}

static BOOL live_menu_leaving(BYTE* menu, ui_event* event)
{
    char control[0x40];
    ui_event again;
    BYTE* plain;

    if (live_state != LIVE_SHOWN || live_click_stays(event))
        return FALSE;
    text_copy_most(control, (const char*) event->b, sizeof control - 1);
    dev_trace("menus", "live background: stopped for %s", control);
    live_state = LIVE_OFF;
    live_skip = TRUE;

    dialog_end(menu, NULL, 0);
    event->e = 1;
    live_settings_back();
    game_FUN_0045ddd0(race_events, NULL, 2);
    game_FUN_0045f720(race_events, NULL, 100);
    gui_manager_show_cursors(session_gui, NULL, TRUE);

    plain = gui_manager_active_dialog(session_gui);
    if (plain && FIELD(plain, 0, DWORD) == MAIN_MENU_VTABLE) {
        again = *event;
        again.c = 0;                    /* the control was the old menu's */
        again.e = 0;
        again.b = (DWORD) control;
        again.sender = plain;
        main_menu_handle(plain, NULL, &again);
    }
    return TRUE;
}
#endif

/* ---- Numbers ---- */

PORT(0x00452190)
char* __cdecl number_text(int value, char* text)
{
    return game_FUN_0054aaa5(value, text, 10);
}

/* ---- Startup ---- */

/* The file's statics, set up by the runtime before main: unit vectors and two timers. */
#define vector_zero   (*(vector3*) 0x005A0F10)
#define vector_x      (*(vector3*) 0x005A0F20)
#define vector_y      (*(vector3*) 0x005A0F40)
#define vector_z      (*(vector3*) 0x005A0F00)

void __fastcall game_FUN_004cfdc0(void* timer, void* edx_unused, DWORD period);  /* start it */

/* Out of line, so the entries below stay jumps to them as in the original. */
__declspec(noinline) void __cdecl init_vector_zero(void);
__declspec(noinline) void __cdecl init_vector_x(void);
__declspec(noinline) void __cdecl init_vector_y(void);
__declspec(noinline) void __cdecl init_vector_z(void);
__declspec(noinline) void __cdecl init_timer_a(void);
__declspec(noinline) void __cdecl init_timer_b(void);

PORT(0x00454B30)
void __cdecl init_vector_zero(void)
{
    set_vector(&vector_zero, 0.0f, 0.0f, 0.0f);
}

PORT(0x00454B80)
void __cdecl init_vector_x(void)
{
    set_vector(&vector_x, 1.0f, 0.0f, 0.0f);
}

PORT(0x00454BD0)
void __cdecl init_vector_y(void)
{
    set_vector(&vector_y, 0.0f, 1.0f, 0.0f);
}

PORT(0x00454E50)
void __cdecl init_vector_z(void)
{
    set_vector(&vector_z, 0.0f, 0.0f, 1.0f);
}

PORT(0x00456B70)
void __cdecl init_timer_a(void)
{
    game_FUN_004cfdc0((void*) 0x005A1040, NULL, 5000);
}

PORT(0x00456B90)
void __cdecl init_timer_b(void)
{
    game_FUN_004cfdc0((void*) 0x005A1018, NULL, 5000);
}

/* The runtime's initializer table calls these, which only jump on. */
PORT(0x00454B20)
void __cdecl init_vector_zero_entry(void)
{
    init_vector_zero();
}

PORT(0x00454B70)
void __cdecl init_vector_x_entry(void)
{
    init_vector_x();
}

PORT(0x00454BC0)
void __cdecl init_vector_y_entry(void)
{
    init_vector_y();
}

PORT(0x00454E40)
void __cdecl init_vector_z_entry(void)
{
    init_vector_z();
}

PORT(0x00456B60)
void __cdecl init_timer_a_entry(void)
{
    init_timer_a();
}

PORT(0x00456B80)
void __cdecl init_timer_b_entry(void)
{
    init_timer_b();
}

/* ---- Single player: the bike and rider page ---- */

/*
    The bike picked in DDLBikes: only its maker's model shows, its engine size (chosen in
    DDLEngineSize where the bike allows, 0x88) becomes the game's, a new class brings its
    handling, and the plate number is shown again.
*/
__declspec(noinline) void __fastcall bike_choose(BYTE* self);

PORT(0x00450F00)
void __fastcall bike_choose(BYTE* self)
{
    BYTE* list = combo_list(self, LIST_BIKES);
    BYTE* bike = FIELD(game_session, 0x50, BYTE*) + list_data(list, NULL, -1) * BIKE_SIZE;
    BYTE* maker = session_players + FIELD(bike, 0, int) * RIDER_SIZE;
    BYTE* engine;
    char number[12];
    int old_class;
    int new_class;
    int i;

    for (i = 0; i < FIELD(game_session, 0x4C, int); i++)
        model_show(session_players + i * RIDER_SIZE, FALSE);
    model_show(maker, TRUE);
    FIELD(self, BR_BIKE_CHANGED, int) = 1;

    engine = dialog_find_control(self, NULL, LIST_ENGINE_SIZE, 0);
    if (FIELD(bike, 0x88, DWORD)) {
        if (!FIELD(engine, 0x70, DWORD))
            engine_size_show(self, engine, TRUE);
    } else if (FIELD(engine, 0x70, DWORD)) {
        engine_size_show(self, engine, FALSE);
    }

    FIELD(game_main, 0x1FB0, DWORD) = list_data(list, NULL, -1);
    if (FIELD(bike, 0x88, DWORD)) {
        int size = (int) list_data(combo_list(self, LIST_ENGINE_SIZE), NULL, -1);

        old_class = engine_class(FIELD(game_main, 0x1550, int));
        FIELD(game_main, 0x1550, int) = engine_sizes[size];
        FIELD(game_main, 0x1554, DWORD) = size == 2 || size == 4;       /* the 4-strokes */
    } else {
        bike = FIELD(game_session, 0x50, BYTE*) + list_data(list, NULL, -1) * BIKE_SIZE;
        old_class = engine_class(FIELD(game_main, 0x1550, int));
        FIELD(game_main, 0x1550, DWORD) = FIELD(bike, 0x8C, DWORD);
        FIELD(game_main, 0x1554, DWORD) = FIELD(bike, 0x90, DWORD);
    }
    new_class = engine_class(FIELD(game_main, 0x1550, int));
    if (new_class != old_class)
        engine_class_apply(new_class);

    list = dialog_find_control(self, NULL, EDIT_PLATE, 11);
    game_FUN_0054aaa5(FIELD(game_main, 0x2144, int), number, 10);
    edit_set_text(list, NULL, number);
}

static void bike_rider_open(BYTE* self)
{
    vector3* position = &FIELD(self, BR_POSITION, vector3);
    vector3* target = &FIELD(self, BR_TARGET, vector3);
    RECT* view = &FIELD(self, BR_VIEW, RECT);
    float fov = 50.0f;
    double dx, dy, dz;
    float squared;
    BYTE* list;
    BYTE* camera;

    FIELD(self, BR_MANUAL, int) = 0;
    option_select(dialog_find_control(self, NULL, CHECK_AUTO_ROTATE, 2), NULL, 1);
    FIELD(game_main, 0x614, DWORD) = 0;
    FIELD(self, BR_RIDER_CHANGED, int) = 1;
    FIELD(self, BR_BIKE_CHANGED, int) = 1;
    choices_fill(self);

    list = combo_list(self, LIST_ENGINE_SIZE);
    list_clear(list);
    list_add_text(list, NULL, NAME(0x0056FC28), engine_class(125), 0);     /* "125cc 2-stroke" */
    list_add_text(list, NULL, NAME(0x0056FC18), engine_class(250), 0);     /* "250cc 2-stroke" */
    list_add_text(list, NULL, NAME(0x0056FC08), engine_class(400), 0);     /* "400cc 4-stroke" */
    list_add_text(list, NULL, NAME(0x0056FBF8), engine_class(500), 0);     /* "500cc 2-stroke" */
    list_add_text(list, NULL, NAME(0x0056FBE8), engine_class(600), 0);     /* "600cc 4-stroke" */
    list_select_data(list, NULL, engine_class(FIELD(game_main, 0x1550, int)));
    edit_set_allowed(dialog_find_control(self, NULL, EDIT_PLATE, 11), NULL, DIGITS);
    session_show_scene(game_session, NULL, self);

    view->left = 91;
    view->right = 543;
    view->top = 108;
    view->bottom = 328;
#ifdef OPENMCM2_DEV
    menu_place_view(self, view);         /* where the scaled menus put it */
#endif
    *position = vector_zero;
    position->z = 15.0f;
    position->y = 1.0f;
    *target = session_centre;
    target->y = target->y + 3.0f;
    game_FUN_0042f8d0(session_camera, NULL, position, NULL, NULL, NULL, &fov);
    camera = session_camera;
    VIRTUAL(camera, 0x74, camera_look_at)(camera, NULL, *target);
    game_FUN_004300b0(session_camera, NULL, view->left, view->top, view->right - view->left,
                      view->bottom - view->top);

    dx = (double) position->x - session_centre.x;
    dy = (double) position->y - session_centre.y;
    dz = (double) position->z - session_centre.z;
    squared = (float) (dy * dy + dx * dx + dz * dz);
    FIELD(self, BR_DISTANCE, float) = x87_equal(squared, 1.0f) ? 1.0f : game_FUN_00461ac0(squared);

    rider_mark_changed(self);
    bike_choose(self);
    players_set(self, NULL, FIELD(game_main, 0x2144, DWORD));
    game_FUN_0053a27e(game_FUN_004c41d0());
    if (FIELD(self, 0x110, BYTE*))
        FIELD(self, BR_SLOT, int) = background_new_slot(FIELD(self, 0x110, BYTE*), NULL, 1);
}

#ifdef OPENMCM2_DEV
#define PLATE_LEAST 1               /* a plate, and one taken as it is typed */
#define PLATE_TYPED 1
#else
#define PLATE_LEAST 101
#define PLATE_TYPED 100
#endif

PORT(0x0044FE50)
void __fastcall bike_rider_handle(BYTE* self, void* edx_unused, ui_event* event)
{
    char number[12];
    int plate;

    (void) edx_unused;
    switch (event->type) {
    case EVENT_OPEN:
        bike_rider_open(self);
        break;
    case EVENT_SELECT:
        if (clicked(event, LIST_BIKES))
            bike_choose(self);
        else if (clicked(event, LIST_RIDERS))
            rider_mark_changed(self);
        else if (clicked(event, LIST_ENGINE_SIZE))
            bike_choose(self);
        break;
    case EVENT_CLICK:
        if (clicked(event, NAME(0x0056FBB4)))                                  /* "BikeLeft" */
            combo_step(self, LIST_BIKES, TRUE);
        else if (clicked(event, NAME(0x0056FBA8)))                             /* "BikeRight" */
            combo_step(self, LIST_BIKES, FALSE);
        else if (clicked(event, NAME(0x0056FB9C)))                             /* "RiderLeft" */
            combo_step(self, LIST_RIDERS, TRUE);
        else if (clicked(event, NAME(0x0056FB90)))                             /* "RiderRight" */
            combo_step(self, LIST_RIDERS, FALSE);
        else if (clicked(event, CHECK_AUTO_ROTATE))
            FIELD(self, BR_MANUAL, int) =
                !option_current_index(dialog_find_control(self, NULL, CHECK_AUTO_ROTATE, 2));
        else if (clicked(event, NAME(0x0056FB84)))                             /* "ButWrench" */
            options_open(self, FIELD(self, 0x2C, BYTE*), 0x7F8, 2);
        break;
    case EVENT_EDIT_DONE:
        /* Plates run 101 to 999; under 100 gains 100 (so 100 itself becomes 101). In the dev build
           they run 1 to 999: the plate's picture (0x00418470) already draws one or two digits. */
        if (!clicked(event, EDIT_PLATE))
            break;
        edit_get_text((BYTE*) event->c, NULL, number, 9);
        plate = game_FUN_0053b6c7(number);
#ifndef OPENMCM2_DEV
        if (plate < 100)
            plate += 100;
#endif
        if (plate < PLATE_LEAST)
            plate = PLATE_LEAST;
        else if (plate > 999)
            plate = 999;
        FIELD(game_main, 0x2144, int) = plate;
        game_FUN_0054aaa5(plate, number, 10);
        edit_set_text((BYTE*) event->c, NULL, number);
        players_set(self, NULL, plate);
        break;
    case EVENT_EDIT_CHANGED:
        if (!clicked(event, EDIT_PLATE))
            break;
        edit_get_text((BYTE*) event->c, NULL, number, 9);
        plate = game_FUN_0053b6c7(number);
        if (plate >= PLATE_TYPED && plate <= 999) {
            players_set(self, NULL, plate);
            FIELD(game_main, 0x2144, int) = plate;
        }
        break;
    case EVENT_CLOSE:
        session_hide_scene(game_session);
        if (FIELD(self, 0x110, BYTE*))
            background_free_slot(FIELD(self, 0x110, BYTE*), NULL, FIELD(self, BR_SLOT, int));
        game_FUN_004eda80(object_screen(self), NULL, NULL);
        break;
    }
}

#ifdef OPENMCM2_DEV
#define ZOOM_STEP       0.9f            /* each notch towards the bike */
#define ZOOM_NEAREST    5.0f
#define ZOOM_FARTHEST   24.0f

BOOL preview_zoom(BYTE* page)
{
    int notches = menu_wheel(FIELD(page, 0x34, gui_user*), page);
    float distance = FIELD(page, BR_DISTANCE, float);

    if (!notches)
        return FALSE;
    for (; notches > 0; notches--)
        distance *= ZOOM_STEP;
    for (; notches < 0; notches++)
        distance /= ZOOM_STEP;
    FIELD(page, BR_DISTANCE, float) = min(max(distance, ZOOM_NEAREST), ZOOM_FARTHEST);
    return TRUE;
}
#endif

/* Turn the camera about the session's point by `angle`, keeping its distance; it looks on. */
static void preview_orbit(BYTE* self, float angle)
{
    vector3* position = &FIELD(self, BR_POSITION, vector3);
    float distance;
    vector3 offset;
    vector3 turned;
    BYTE* camera;

    offset.x = position->x - session_centre.x;
    offset.y = position->y - session_centre.y;
    offset.z = position->z - session_centre.z;
    D3DRMVectorRotate(&turned, &offset, &vector_y, angle);
    distance = FIELD(self, BR_DISTANCE, float);
    turned.x = turned.x * distance;
    turned.y = turned.y * distance;
    turned.z = turned.z * distance;
    position->x = turned.x + session_centre.x;
    position->y = turned.y + session_centre.y;
    position->z = turned.z + session_centre.z;
    game_FUN_0042f8d0(session_camera, NULL, position, NULL, NULL, NULL, NULL);
    camera = session_camera;
    VIRTUAL(camera, 0x74, camera_look_at)(camera, NULL, FIELD(self, BR_TARGET, vector3));
}

/*
    Vtable slot 10. Held down, the mouse turns the camera (across) and raises it (down, 1 to
    10 high), which turns ChkAutoRotate off; otherwise it turns by itself at pi/8 a second.
    Now and then the idling rider looks about or stretches.
*/
PORT(0x00450910)
int __fastcall preview_tick(BYTE* self, void* edx_unused, float seconds)
{
    gui_user* user = FIELD(self, 0x34, gui_user*);
    BYTE* model;

    (void) edx_unused;
    if (FIELD(self, 0x110, BYTE*))
        background_invalidate(FIELD(self, 0x110, BYTE*));
    if (user->input && FIELD(self, BR_MANUAL, int) && FIELD(self, BR_HELD, int)) {
        int across = user->input->x - user->cursor_x;
        int down = user->input->y - user->cursor_y;
        double height = (double) down * 0.1f + FIELD(self, BR_POSITION + 4, float);

        FIELD(self, BR_POSITION + 4, float) = (float) height;
        if (!x87_less(height, 10.0))
            height = 10.0;
        else if (1.0 > height)
            height = 1.0;
        FIELD(self, BR_POSITION + 4, float) = (float) height;
        preview_orbit(self, across * TURN_PER_PIXEL);
        FIELD(self, BR_MANUAL, int) = 1;
        option_select(dialog_find_control(self, NULL, CHECK_AUTO_ROTATE, 2), NULL, 0);
    } else if (!FIELD(self, BR_MANUAL, int)) {
        preview_orbit(self, seconds * TURN_PER_SECOND);
    }
#ifdef OPENMCM2_DEV
    if (preview_zoom(self))
        preview_orbit(self, 0.0f);
#endif

    model = session_rider;
    VIRTUAL(model, 0x1C, model_step)(model, NULL, seconds, 0, 0);
    model = session_bike;
    VIRTUAL(model, 0x1C, model_step)(model, NULL, seconds, 0, 0);

    /* (The original reads DDLBikes' choice before each animation, and drops it.) */
    if (FIELD(self, BR_IDLING, int) && game__rand() % 200 == 1) {
        list_data(combo_list(self, LIST_BIKES), NULL, -1);
        switch (game__rand() % 3) {
        case 0:
            game_FUN_004aa170(session_rider, NULL, NAME(0x0056FC60));          /* "LookLeftR" */
            break;
        case 1:
            game_FUN_004aa170(session_rider, NULL, NAME(0x0056FC6C));          /* "LookRightR" */
            break;
        case 2:
            game_FUN_004aa170(session_rider, NULL, NAME(0x0056FC78));          /* "StretchR" */
            break;
        }
        FIELD(self, BR_IDLING, int) = 0;
    }
    if (!FIELD(self, BR_IDLING, int) && FIELD(session_rider, 0xC, int)) {
        list_data(combo_list(self, LIST_BIKES), NULL, -1);
        game_FUN_004aa170(session_rider, NULL, NAME(0x0056FC58));              /* "WaitR" */
        FIELD(self, BR_IDLING, int) = 1;
    }
    if (FIELD(self, 0x110, BYTE*))
        background_slot_redraw(FIELD(self, 0x110, BYTE*), NULL, FIELD(self, BR_SLOT, int),
                               &FIELD(self, BR_VIEW, RECT));
    return dialog_tick(self, NULL, seconds);
}

/*
    Vtable slot 31: the page's choice into the game's rider settings (out only): the bike's
    maker and model, then the rider, each of at most 63 characters; the profile is saved.
*/

PORT(0x004513F0)
void __fastcall bike_rider_sync(BYTE* self, void* edx_unused, DWORD out)
{
    BYTE* bike;

    (void) edx_unused;
    if (!out)
        return;
    text_copy((char*) game_main + 0x1EEC, NO_TEXT);
    bike = FIELD(game_session, 0x50, BYTE*)
           + list_data(combo_list(self, LIST_BIKES), NULL, -1) * BIKE_SIZE;
    text_copy_most((char*) game_main + 0x1EEC,
                   (const char*) session_players + FIELD(bike, 0, int) * RIDER_SIZE + 0x40,
                   RIDER_TEXT_LENGTH);
    text_copy_most((char*) game_main + 0x1F2C, (const char*) bike + 0x48, RIDER_TEXT_LENGTH);
    text_copy_most((char*) game_main + 0x1F6C, NO_TEXT, RIDER_TEXT_LENGTH);
    text_copy_most((char*) game_main + 0x1F6C,
                   FIELD(game_session, 0x58, char*)
                   + list_data(combo_list(self, LIST_RIDERS), NULL, -1) * RIDER_SIZE + 0x40,
                   RIDER_TEXT_LENGTH);
    profile_save(game_profile);
}
