/*
    OpenMCM2 - the menus' GUI objects, ported from the game (GUIManager.cpp in the original).

    Each function here replaces the game's at the address in its PORT line, and was checked against
    the original's instructions. The original's C++ exception frames are not reproduced: they only
    destroy half-built objects when an exception passes through, and the game throws none here.

    The GUIManager runs the menus: it owns the screen's background, the fonts, the input devices
    and up to four GUIUsers. A GUIUser is one player at the menus, with a cursor, a tooltip and the
    controls it has focus on; it claims input devices, at most one user each.
*/

#include "gui.h"
#include <imm.h>

#ifdef OPENMCM2_DEV
#include "../dev/dev.h"

/* Read the active GUI user rather than the startup joystick inventory. This
   remains valid for pause menus and for a pad first connected during a race. */
BOOL pad_menus_active(void)
{
    BYTE* session = FIELD(game_main, 0x56C, BYTE*);
    gui_manager* manager = session ? FIELD(session, 0x2C, gui_manager*) : NULL;
    gui_user* user = manager && manager->user_count ? manager->users[0] : NULL;

    return user && (user->focus || user->capture || gui_manager_active_dialog(manager));
}

BOOL pad_menu_back(void)
{
    BYTE* session = FIELD(game_main, 0x56C, BYTE*);
    gui_manager* manager = session ? FIELD(session, 0x2C, gui_manager*) : NULL;
    gui_user* user = manager && manager->user_count ? manager->users[0] : NULL;

    BYTE* dialog;
    BYTE* active;
    if (!user)
        return FALSE;
    if (pad_close_dropdown(user->focus))
        return TRUE;
    active = gui_manager_active_dialog(manager);
    dialog = user->focus ? FIELD(user->focus, 0xB8, BYTE*) : active;
    if (active && FIELD(active, 0x148, int))
        dialog = active;
    return pad_back_from_dialog(dialog);
}
#endif

/* Two strings, equal: the game inlines strcmp() == 0 like this. */
static BOOL text_equal(const char* a, const char* b)
{
    while (*a == *b) {
        if (!*a)
            return TRUE;
        a++;
        b++;
    }
    return FALSE;
}

/* ---- GUICursor ---------------------------------------------------------------------------- */

PORT(0x00486090)
gui_cursor* __fastcall gui_cursor_construct(gui_cursor* self, void* edx_unused, DWORD arg)
{
    (void) edx_unused;
    game_FUN_0043f9a0(self, NULL, arg);
    *(void***) self = GUICURSOR_VTABLE;
    self->anim = NULL;
    return self;
}

PORT(0x004860D0)
void __fastcall gui_cursor_destruct(gui_cursor* self)
{
    *(void***) self = GUICURSOR_VTABLE;
    game_FUN_0043fb00(self);
}

/* Vtable slot 0: the destructor, freeing the object when bit 0 of `flags` is set. */
PORT(0x004860B0)
gui_cursor* __fastcall gui_cursor_delete(gui_cursor* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    gui_cursor_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

PORT(0x004860E0)
void __fastcall gui_cursor_set_anim(gui_cursor* self, void* edx_unused, ui_anim* anim)
{
    (void) edx_unused;
    self->anim = anim;
    if (!anim)
        self->picture = NULL;
}

/* Vtable slot 15, once a frame: step the animation and draw the picture it reached. */
PORT(0x00486100)
void __fastcall gui_cursor_update(gui_cursor* self)
{
    if (self->anim)
        self->picture = anim_next_picture(self->anim);
    game_vf_15_43FCE0(self);
}

/* ---- GUIManager --------------------------------------------------------------------------- */

PORT(0x00486120)
gui_manager* __fastcall gui_manager_construct(gui_manager* self, void* edx_unused, DWORD flag)
{
    (void) edx_unused;
    object_construct(self, NULL, flag);
    *(void***) self = GUIMANAGER_VTABLE;
    self->message = NULL;
    self->font_object_made = 0;
    self->unknown_d4 = 0;
    self->unknown_d8 = 0;
    self->background_made = 0;
    self->backdrop = NULL;
    self->backdrop_slot = -1;
    self->unknown_e8 = 0;
    self->unknown_ec = 1;
    self->folder[0] = 0;
    self->countdown = 0;
    self->unknown_38 = 0;
    self->unknown_34 = 0;
    self->font_object = NULL;
    self->background = NULL;
    self->font = NULL;
    self->language = NULL;
    self->palette = NULL;
    self->busy = 0;
    self->unknown_1f8 = 0;
    self->background_pending = NULL;
    self->unknown_304 = 0;
    self->cursor_image[0] = 0;
    self->wait_image[0] = 0;
    text_copy(self->wait_image, WAIT_TGA);
    text_copy(self->cursor_image, CURSOR_TGA);
    self->mouse = NULL;
    self->keyboard = NULL;
    __stosd((unsigned long*) self->joysticks, 0, 8);
    self->user_count = 0;
    self->users[0] = self->users[1] = self->users[2] = self->users[3] = NULL;
    self->group = NULL;
    self->swapped = 0;
    self->unknown_34c = 0;
    text_copy(self->font_name, NO_TEXT);
    self->font_grow = 0.0f;
    self->font_bold = self->font_italic = 0;
    self->backdrop_drawn = 0;
    return self;
}

PORT(0x004862B0)
void __fastcall gui_manager_destruct(gui_manager* self)
{
    *(void***) self = GUIMANAGER_VTABLE;
    gui_manager_drop_cursors(self);
    if (self->message)
        object_destroy(self->message);
    gui_manager_drop_backdrop(self);
    if (self->font)
        DeleteObject(self->font);
    if (self->palette)
        self->palette->lpVtbl->Release(self->palette);
    object_destruct(self);
}

PORT(0x00486290)
gui_manager* __fastcall gui_manager_delete(gui_manager* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    gui_manager_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Vtable slots 22 and 23, charged to the UI's memory. */
PORT(0x004867C0)
int __fastcall gui_manager_slot_22(gui_manager* self, void* edx_unused, DWORD a, DWORD b)
{
    void* zone = zone_enter(ZONE_UI);
    int result = game_vf_22_46A4B0(self, NULL, a, b);

    (void) edx_unused;
    zone_leave(zone);
    return result;
}

PORT(0x00486780)
int __fastcall gui_manager_slot_23(gui_manager* self, void* edx_unused, DWORD a, DWORD b)
{
    void* zone = zone_enter(ZONE_UI);
    int result = game_vf_23_46A500(self, NULL, a, b);

    (void) edx_unused;
    zone_leave(zone);
    return result;
}

/* Vtable slot 10, the frame's tick: the end of a busy spell goes to vtable slot 28. */
PORT(0x00486800)
int __fastcall gui_manager_tick(gui_manager* self, void* edx_unused, DWORD time)
{
    void* zone = zone_enter(ZONE_UI);

    (void) edx_unused;
    game_vf_10_46A300(self, NULL, time);
#ifdef OPENMCM2_DEV
    pad_menu_tick(self);
    live_menu_tick();
#endif
    if (self->busy) {
        self->busy = 0;
        game_vf_28_483430(self);
    }
    zone_leave(zone);
    return 1;
}

/* Draw the backdrop behind the menus, through the background if there is one. */
static int draw_backdrop(gui_manager* self, BYTE* backdrop)
{
    RECT area;

    area.left = 0;
    area.top = 0;
    area.right = FIELD(backdrop, 0x14, LONG);
    area.bottom = FIELD(backdrop, 0x18, LONG);
    if (self->background) {
        background_draw(self->background, NULL, backdrop, &area, NULL, 0x1000000,
                        self->backdrop_slot, 1, &self->unknown_e8, 0);
        return 1;
    }
    return VIRTUAL(object_screen(self), 0xC, screen_draw)(object_screen(self), NULL, &area, backdrop,
                                                         NULL, 0x1000000);
}

/* Vtable slot 13: draw, with the backdrop first when it has been drawn before. */
PORT(0x00486860)
int __fastcall gui_manager_draw_over(gui_manager* self)
{
    if (self->countdown == 0) {
        if (self->backdrop && self->backdrop_drawn && !draw_backdrop(self, self->backdrop)) {
            game_vf_13_46A260(self);
            return 0;
        }
        game_vf_13_46A260(self);
    }
    return 1;
}

/* Vtable slot 15: draw, with the backdrop first the first time; count down a busy spell. */
PORT(0x00486900)
int __fastcall gui_manager_draw(gui_manager* self)
{
    if (self->countdown == 0) {
        if (self->backdrop && !self->backdrop_drawn && !draw_backdrop(self, self->backdrop)) {
            game_vf_15_46A2D0(self);
            return 0;
        }
        game_vf_15_46A2D0(self);
    } else {
        int left = self->countdown - 1;

        self->countdown = left < 0 ? 0 : left;
        VIRTUAL(FIELD(game_main, 0x10, void*), 0x30, void (__fastcall*)(void*, void*, DWORD, DWORD))(
            FIELD(game_main, 0x10, void*), NULL, 0, 0);
        if (self->countdown == 0 && self->background_pending) {
            background_invalidate(self->background_pending);
            self->background_pending = NULL;
        }
    }
    if (self->background)
        background_release_dc(self->background);
    return 1;
}

/* Vtable slot 0x6C of a dialog: lay it out and open it. Nonzero when it opened. */
typedef int (__fastcall* dialog_open)(void* dialog, void* edx_unused, void* screen, DWORD a,
                                      DWORD b, DWORD flags, void* font_object, int c,
                                      char* folder, void* parent, BYTE* background,
                                      const char* font, int font_size, gui_manager* manager,
                                      gui_user* user, DWORD d);

#define ARIAL ((const char*) 0x00572444)            /* "Arial" */

BYTE* __fastcall dialog_construct(BYTE* self, void* edx_unused, DWORD flag, const char* name);

/* A walk over an object's descendants of one class, and what is done to each dialog found. */
#define UIDIALOG ((const char*) 0x00572474)         /* "UIDialog" */
void  __fastcall game_FUN_0046a880(void* walk, void* edx_unused, void* root, DWORD deep,
                                   const char* class_name);
BYTE* __fastcall game_FUN_0046a980(void* walk);
void  __fastcall game_FUN_0046a970(void* walk);
void __fastcall dialog_set_background(BYTE* self, void* edx_unused, BYTE* background);
void  __fastcall game_FUN_004eda50(void* video);
void  __fastcall game_FUN_004cfd00(void* video, void* edx_unused, DWORD palette);

/*
    Show `dialog`, opened through gui_manager_open: it goes under its parent, or under the
    manager's group, and the background is redrawn.
*/
PORT(0x00486B60)
void __fastcall gui_manager_show_dialog(gui_manager* self, void* edx_unused, BYTE* dialog,
                                        DWORD arg, DWORD wait)
{
    DWORD shown = FIELD(dialog, 0x25, BYTE) & 1;
    BYTE* parent;

    (void) edx_unused;
    dialog_show(dialog, NULL, 0);
    parent = FIELD(dialog, 0x2C, BYTE*);
    if (parent && (FIELD(dialog, 0x144, BYTE) & 1))
        object_add_sibling(FIELD(parent, 0x7F3C, void*), NULL, dialog, -1);
    else
        object_add_child(self->group, NULL, dialog, -1);
    VIRTUAL(dialog, 0x70, method_1)(dialog, NULL, arg);
    FIELD(dialog, 0xC8, int) = self->unknown_ec;
    if (self->background)
        background_invalidate(self->background);
    if (wait)
        gui_manager_set_cursors(self, NULL, self->cursor_image, self->busy == 0);
    if (self->busy)
        self->countdown = 3;
    dialog_show(dialog, NULL, shown);
}

/*
    Open `dialog`. Bits of `flags`: 2 and 4 keep what is behind it, 4 with 0x10 loads the backdrop
    (8 passed on), 0x10 over the whole screen. With `wait`, the users have the wait cursor
    meanwhile. Returns the dialog, or NULL when it would not open.
*/
PORT(0x00486A00)
BYTE* __fastcall gui_manager_open(gui_manager* self, void* edx_unused, BYTE* dialog, DWORD a,
                                  DWORD flags, DWORD b, BYTE* parent, DWORD arg, DWORD c,
                                  DWORD wait)
{
    (void) edx_unused;
#ifdef OPENMCM2_DEV
    live_menu_opening(dialog);
#endif
    if (!(flags & 4) && !(flags & 2)) {
        if (!gui_manager_active_dialog(self) || !FIELD(gui_manager_active_dialog(self), 0x148, int))
            gui_manager_drop_backdrop(self);
    } else {
        RECT screen;
        RECT* area = NULL;

        if (flags & 0x10) {
            screen.left = 0;
            screen.top = 0;
            screen.right = FIELD(object_screen(self), 0xC, LONG);
            screen.bottom = FIELD(object_screen(self), 0x10, LONG);
            area = &screen;
        }
        if (parent)
            dialog_freeze(parent, NULL, 1, flags & 8, area);
        else if ((flags & 4) && (flags & 0x10))
            gui_manager_load_backdrop(self, NULL, flags & 8);
    }
    if (wait)
        gui_manager_set_cursors(self, NULL, self->wait_image, self->busy == 0);

    if (VIRTUAL(dialog, 0x6C, dialog_open)(dialog, NULL, object_screen(self), b, a, flags,
                                           self->font_object, self->unknown_d8, self->folder,
                                           parent, self->background, self->name, self->font_size,
                                           self, self->users[0], c)) {
        FIELD(dialog, 0x144, DWORD) = flags;
        gui_manager_show_dialog(self, NULL, dialog, arg, wait);
        return dialog;
    }
    if (wait)
        gui_manager_set_cursors(self, NULL, self->cursor_image, self->busy == 0);
    return NULL;
}

PORT(0x00486CE0)
void __fastcall gui_manager_drop_message(gui_manager* self)
{
    if (self->message) {
        object_destroy(self->message);
        self->message = NULL;
    }
}

/* Make the manager's own message dialog, in 14-point Arial. FALSE when it cannot be made. */
PORT(0x00486C10)
BOOL __fastcall gui_manager_make_message(gui_manager* self, void* edx_unused, const char* name)
{
    BYTE* dialog;

    (void) edx_unused;
    gui_manager_drop_message(self);
    dialog = game_new(0x7F58, GUIMANAGER_CPP, 0x20B);
    if (dialog)
        dialog = dialog_construct(dialog, NULL, 0, name);
    self->message = dialog;
    if (!dialog)
        return FALSE;
    VIRTUAL(dialog, 0x6C, dialog_open)(dialog, NULL, object_screen(self), 0, 0, 0,
                                       self->font_object, self->unknown_d8, self->folder, NULL,
                                       NULL, ARIAL, 14, NULL, NULL, 0);
    return TRUE;
}

/* The menus' folder, given a trailing backslash if it has none. */
PORT(0x00486D00)
void __fastcall gui_manager_set_folder(gui_manager* self, void* edx_unused, const char* folder)
{
    size_t last = text_length(folder) - 1;

    (void) edx_unused;
    text_copy(self->folder, folder);
    if (folder[last] != '\\')
        text_append(self->folder, "\\");
}

/* A control of the message dialog, by name. */
PORT(0x00486E50)
int __fastcall gui_manager_find_message(gui_manager* self, void* edx_unused, const char* name)
{
    (void) edx_unused;
    return self->message ? dialog_find(self->message, NULL, name) : 0;
}

PORT(0x00486E70)
int __fastcall gui_manager_swap(gui_manager* self, void* edx_unused, int value)
{
    int old = self->swapped;

    (void) edx_unused;
    self->swapped = value;
    return old;
}

/* Make the background, if there is none, and give every user a tooltip on it. */
PORT(0x00486E80)
void __fastcall gui_manager_make_background(gui_manager* self)
{
    BYTE* background;
    int i;

    if (self->background)
        return;
    background = game_new(0x74, GUIMANAGER_CPP, 0x2A0);
    if (background)
        background = background_construct(background, NULL, 1);
    /* Slot 8, GameObject's set-up: it takes the screen alone. The original pushes the -1 before
       it, but that is object_add_child's. */
    background = VIRTUAL(background, 0x20, void* (__fastcall*)(void*, void*, void*))(
        background, NULL, object_screen(self));
    background = object_add_child(self, NULL, background, -1);
    self->background = background;
    object_move_before(background, NULL, self->group, -1);
    self->background_made = 1;
    gui_manager_set_cursors(self, NULL, NULL, 1);
    for (i = 0; i < self->user_count; i++)
        gui_user_make_tooltip(gui_manager_user(self, NULL, i), NULL, object_screen(self), self);
}

/* Undo gui_manager_make_background: the users' tooltips are made again, without it. */
PORT(0x00486F50)
void __fastcall gui_manager_drop_background(gui_manager* self)
{
    BYTE iterator[0x94];
    BYTE* dialog;
    int i;

    if (!self->background_made)
        return;
    gui_manager_drop_cursors(self);
    if (self->background)
        object_destroy(self->background);
    self->background = NULL;
    gui_manager_set_cursors(self, NULL, NULL, 0);
    game_FUN_0046a880(iterator, NULL, self, 1, UIDIALOG);
    for (dialog = game_FUN_0046a980(iterator); dialog; dialog = game_FUN_0046a980(iterator))
        dialog_set_background(dialog, NULL, 0);
    for (i = 0; i < self->user_count; i++)
        gui_user_make_tooltip(gui_manager_user(self, NULL, i), NULL, object_screen(self), self);
    game_FUN_0046a970(iterator);
}

/* Load the backdrop, and give it a slot in the background. */
PORT(0x00487030)
void __fastcall gui_manager_load_backdrop(gui_manager* self, void* edx_unused, DWORD flag)
{
    (void) edx_unused;
    gui_manager_drop_backdrop(self);
    self->backdrop = gui_manager_capture_screen(self, NULL, flag, NULL);
    if (self->background) {
        self->backdrop_slot = background_new_slot(self->background, NULL, 1);
        background_invalidate(self->background);
        FIELD(self->background, 0x30, int) = 0;
    }
}

PORT(0x00487080)
void __fastcall gui_manager_drop_backdrop(gui_manager* self)
{
    if (self->backdrop)
        object_destroy(self->backdrop);
    if (self->background && self->backdrop_slot >= 0) {
        background_free_slot(self->background, NULL, self->backdrop_slot);
        FIELD(self->background, 0x30, int) = 1;
    }
    self->backdrop = NULL;
    self->backdrop_slot = -1;
    self->unknown_e8 = 0;
}

PORT(0x004870E0)
void __fastcall gui_manager_set_busy(gui_manager* self, void* edx_unused, int value)
{
    (void) edx_unused;
    self->busy = 1;
    self->unknown_34 = value;
}

PORT(0x00487480)
int __fastcall gui_manager_unknown_1f8(gui_manager* self)
{
    return self->unknown_1f8;
}

/* Redraw everything: the background, then the game's screens. */
PORT(0x00487490)
void __fastcall gui_manager_refresh(gui_manager* self)
{
    if (self->background) {
        background_release_dc(self->background);
        background_invalidate(self->background);
    }
    VIRTUAL(game_main, 0x20, method_0)(game_main);
    VIRTUAL(game_main, 0x24, method_0)(game_main);
    game_FUN_004eda50(FIELD(game_main, 0x10, void*));
}

PORT(0x004874D0)
gui_user* __fastcall gui_manager_user(gui_manager* self, void* edx_unused, int index)
{
    (void) edx_unused;
    return index < 4 ? self->users[index] : NULL;
}

PORT(0x004874F0)
void __fastcall gui_manager_set_wait_image(gui_manager* self, void* edx_unused, const char* image)
{
    (void) edx_unused;
    text_copy(self->wait_image, image);
}

/* Give every user `image` as its cursor, or the manager's own cursor image. */
PORT(0x00487570)
void __fastcall gui_manager_set_cursors(gui_manager* self, void* edx_unused, const char* image,
                                        DWORD arg)
{
    int i;

    (void) edx_unused;
    for (i = 0; i < self->user_count; i++) {
        if (self->users[i])
            gui_user_set_cursor(self->users[i], NULL, image ? image : self->cursor_image, arg);
    }
}

/* Show the users' cursors, or hide them: vtable slot 5 or 4 of each. */
PORT(0x004875C0)
void __fastcall gui_manager_show_cursors(gui_manager* self, void* edx_unused, BOOL show)
{
    int count = self->user_count;
    int i;

    (void) edx_unused;
    for (i = 0; i < count; i++) {
        gui_user* user = gui_manager_user(self, NULL, i);

        if (user && user->cursor) {
            if (show)
                VIRTUAL(user->cursor, 0x14, method_0)(user->cursor);
            else
                VIRTUAL(user->cursor, 0x10, method_0)(user->cursor);
        }
    }
}

PORT(0x00487610)
void __fastcall gui_manager_drop_cursors(gui_manager* self)
{
    int i;

    for (i = 0; i < self->user_count; i++) {
        if (self->users[i])
            gui_user_drop_cursor(self->users[i]);
    }
}

/* The menus' font: `face`, 12 pixels high. */
PORT(0x00487650)
void __fastcall gui_manager_set_font(gui_manager* self, void* edx_unused, const char* face)
{
    (void) edx_unused;
    text_copy(self->font_name, face);
    if (self->font)
        DeleteObject(self->font);
    self->font = CreateFontA(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, PROOF_QUALITY, VARIABLE_PITCH, face);
}

/* COM calls on the video objects, by vtable offset. */
typedef HRESULT (__stdcall* com_1)(void* self, DWORD a);
typedef HRESULT (__stdcall* com_2)(void* self, DWORD a, DWORD b);
typedef HRESULT (__stdcall* com_3)(void* self, DWORD a, DWORD b, DWORD c);

static void palette_release(IUnknown* palette)
{
    VIRTUAL(palette, 0x1C, com_2)(palette, 0, 0);
    palette->lpVtbl->Release(palette);
}

/* Use the palette numbered `palette`, or none with 0. */
PORT(0x00487840)
BOOL __fastcall gui_manager_set_palette(gui_manager* self, void* edx_unused, DWORD palette)
{
    void* primary;

    (void) edx_unused;
    if (palette) {
        void* maker;

        if (self->palette)
            palette_release(self->palette);
        maker = FIELD(FIELD(game_main, 0xC, BYTE*), 0x190, void*);
        VIRTUAL(maker, 0x10, com_3)(maker, 0, (DWORD) &self->palette, 0);
        primary = FIELD(FIELD(game_main, 0xC, BYTE*), 0x19C, void*);
        VIRTUAL(primary, 0x70, com_1)(primary, (DWORD) self->palette);
        if (self->palette)
            VIRTUAL(self->palette, 0x20, com_2)(self->palette, 0, FIELD(game_main, 0x31C, DWORD));
    } else {
        if (self->palette) {
            palette_release(self->palette);
            self->palette = NULL;
        }
        primary = FIELD(FIELD(game_main, 0xC, BYTE*), 0x19C, void*);
        VIRTUAL(primary, 0x70, com_1)(primary, 0);
    }
    game_FUN_004cfd00(FIELD(game_main, 0xC, void*), NULL, palette);
    return TRUE;
}

/* ---- ToolTip ------------------------------------------------------------------------------ */

PORT(0x00487920)
tooltip* __fastcall tooltip_construct(tooltip* self, void* edx_unused, DWORD flag)
{
    (void) edx_unused;
    object_construct(self, NULL, flag);
    *(void***) self = TOOLTIP_VTABLE;
    self->shown = 0;
    self->text = NULL;
    self->redraws = 0;
    self->place.bottom = self->place.right = self->place.top = self->place.left = 0;
    self->text_area.bottom = self->text_area.right = 0;
    self->text_area.top = self->text_area.left = 0;
    self->background_slot = -1;
    self->delay = -1.0f;
    self->enabled = 1;
    return self;
}

PORT(0x004879A0)
tooltip* __fastcall tooltip_setup(tooltip* self, void* edx_unused, void* screen,
                                  gui_manager* manager)
{
    (void) edx_unused;
    object_setup(self, NULL, screen);
    self->manager = manager;
    return self;
}

PORT(0x004879C0)
void __fastcall tooltip_destruct(tooltip* self)
{
    *(void***) self = TOOLTIP_VTABLE;
    if (self->text)
        object_destroy(self->text);
    if (self->manager && self->manager->background && self->background_slot > -1)
        background_free_slot(self->manager->background, NULL, self->background_slot);
    object_destruct(self);
}

PORT(0x00487980)
tooltip* __fastcall tooltip_delete(tooltip* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    tooltip_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Vtable slot 10, the frame's tick: count the delay down, and show the text when it runs out. */
PORT(0x00487A40)
int __fastcall tooltip_tick(tooltip* self, void* edx_unused, float seconds)
{
    (void) edx_unused;
    if (!self->enabled) {
        self->shown = 0;
        return 1;
    }
    if (self->delay > 0.0f) {
        self->delay -= seconds;
        if (!(self->delay > 0.0f)) {
            if (!self->text) {
                self->shown = 0;
                return 1;
            }
            self->shown = 1;
        }
    }
    return 1;
}

/* Vtable slot 15: draw the text, or put back what was under it. */
PORT(0x00487D40)
int __fastcall tooltip_draw(tooltip* self)
{
    BYTE* background = self->manager ? self->manager->background : NULL;

    if (self->shown) {
        RECT area;

        if (background) {
            background_draw(background, NULL, self->text, &self->place, &self->text_area,
                            0x1008000, self->background_slot, 0, &self->redraws, 0);
            return 1;
        }
        area.left = self->place.left;
        area.top = self->place.top;
        area.right = self->place.left + self->text_area.right;
        area.bottom = self->place.top + self->text_area.bottom;
        return VIRTUAL(object_screen(self), 0xC, screen_draw)(object_screen(self), NULL, &area,
                                                             self->text, &self->text_area,
                                                             0x1008000) ? 1 : 0;
    }
    if (background && self->background_slot > -1) {
        if (self->redraws > 0) {
            background_slot_redraw(background, NULL, self->background_slot, &self->place);
            self->redraws--;
            return 1;
        }
        background_slot_clear(background, NULL, self->background_slot);
    }
    return 1;
}

/* ---- GUIInputDevice ----------------------------------------------------------------------- */

/* An axis of an input device: its constructor, destructor, release and set-up. */
void  __fastcall game_FUN_0043dbe0(void* axis);
void  __fastcall game_thunk_FUN_0043dd80(void* axis);
void  __fastcall game_FUN_0043dd80(void* axis);

PORT(0x00487E10)
gui_input* __fastcall gui_input_construct(gui_input* self)
{
    object_construct(self, NULL, 1);
    game_FUN_0043dbe0(self->axis_x);
    game_FUN_0043dbe0(self->axis_y);
    self->range[0] = self->range[1] = self->range[2] = self->range[3] = 0.0f;
    self->user = NULL;
    *(void***) self = GUIINPUT_VTABLE;
    return self;
}

PORT(0x00487EB0)
void __fastcall gui_input_destruct(gui_input* self)
{
    *(void***) self = GUIINPUT_VTABLE;
    if (FIELD(self->device, 0xC, int)) {
        game_FUN_0043dd80(self->axis_x);
        game_FUN_0043dd80(self->axis_y);
    }
    game_thunk_FUN_0043dd80(self->axis_y);
    game_thunk_FUN_0043dd80(self->axis_x);
    object_destruct(self);
}

PORT(0x00487E90)
gui_input* __fastcall gui_input_delete(gui_input* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    gui_input_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/*
    Vtable slot 10, the frame's tick: where the device points. A device of type 1 that is a
    user's has its cursor's position instead, unless the game is in its pointer mode (bit 1 of
    its flags at 0x2D4).
*/
PORT(0x004882B0)
int __fastcall gui_input_tick(gui_input* self, void* edx_unused, DWORD time)
{
    int type;

    (void) edx_unused;
    game_vf_10_46A300(self, NULL, time);
    type = FIELD(self->device, 0xC, int);
    if (!type)
        return 1;
#ifdef OPENMCM2_DEV
    if (type == 2 || type == 3) {
        self->x = self->y = 0;           /* modern navigation runs once per GUI user */
        return 1;
    }
#endif
    if (FIELD(game_main, 0x2D4, BYTE) & 2) {
        self->x = (int) FIELD(self->axis_x, 0x24, float);
        self->y = (int) FIELD(self->axis_y, 0x24, float);
        return 1;
    }
    if (type == 1 && self->user && self->user->cursor) {
        self->x = FIELD(self->user->cursor, 0x54, int);
        self->y = FIELD(self->user->cursor, 0x58, int);
    }
    return 1;
}

PORT(0x00488340)
int __fastcall gui_input_slot_25(gui_input* self, void* edx_unused, DWORD a)
{
    (void) edx_unused;
    game_vf_25_46A650(self, NULL, a);
    gui_input_reset(self);
    return 1;
}

/* ---- GUIUser ------------------------------------------------------------------------------ */

PORT(0x00488360)
gui_user* __fastcall gui_user_construct(gui_user* self)
{
    object_construct(self, NULL, 1);
    self->inputs.items = NULL;
    self->inputs.count = 0;
    self->inputs.grow_by = 0;
    self->inputs.capacity = 0;
    self->flags &= ~1;
    *(void***) self = GUIUSER_VTABLE;
    self->manager = NULL;
    self->tooltip = NULL;
    self->cursor = NULL;
    self->own_anim = NULL;
    self->cursor_image[0] = 0;
    self->wait_image[0] = 0;
    self->unknown_c4 = 0;
    text_copy(self->wait_image, WAIT_TGA);
    text_copy(self->cursor_image, CURSOR_TGA);
    self->capture = NULL;
    self->focus = NULL;
    self->next_focus = NULL;
    self->next_capture = NULL;
    self->inputs.items = game_new(4, CONTAINER_LIST_H, 0x3B);
    self->inputs.grow_by = 1;
    self->inputs.capacity = 1;
    self->flags |= 1;
    self->input = NULL;
    self->cursor_y = 0;
    self->cursor_x = 0;
    __stosd((unsigned long*) self->keys, 0, sizeof self->keys / 4);
    return self;
}

PORT(0x004884D0)
void __fastcall gui_user_destruct(gui_user* self)
{
    *(void***) self = GUIUSER_VTABLE;
    game_delete(self->inputs.items);
    object_destruct(self);
}

PORT(0x004884B0)
gui_user* __fastcall gui_user_delete(gui_user* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    gui_user_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Calls a control's vtable takes for focus, by offset. */
typedef int  (__fastcall* control_take)(void* control);
typedef void (__fastcall* control_lose)(void* control, void* edx_unused, void* to);

/* A control that can lose focus: there, not marked (bit 3 of byte 0x25), and not `to`. */
static BOOL loses_focus(BYTE* control, BYTE* to)
{
    return control && !(FIELD(control, 0x25, BYTE) & 8) && control != to;
}

/* Give `control` focus. The old one is told, and returned through `old` if wanted. */
PORT(0x00488790)
int __fastcall gui_user_set_focus(gui_user* self, void* edx_unused, BYTE* control, BYTE** old)
{
    int taken = 1;

    (void) edx_unused;
    if (control && control != self->focus) {
        taken = VIRTUAL(control, 0x7C, control_take)(control);
        if (!taken)
            return 0;
    }
    if (loses_focus(self->focus, control))
        VIRTUAL(self->focus, 0x80, control_lose)(self->focus, NULL, control);
    if (old)
        *old = self->focus;
    self->focus = control;
    return taken;
}

/*
    Give `control` focus and capture. With the game's text input on, a text box (type 11) that
    takes only digits turns it to numbers.
*/
PORT(0x00488800)
int __fastcall gui_user_set_capture(gui_user* self, void* edx_unused, BYTE* control, BYTE** old)
{
    int taken = 1;

    (void) edx_unused;
    if (control && control != self->focus) {
        taken = VIRTUAL(control, 0x7C, control_take)(control);
        if (!taken)
            return 0;
    }
    if (loses_focus(self->focus, control))
        VIRTUAL(self->focus, 0x80, control_lose)(self->focus, NULL, control);
    if (loses_focus(self->capture, control))
        VIRTUAL(self->capture, 0x84, control_lose)(self->capture, NULL, control);
    if (old)
        *old = self->capture;
    self->focus = control;
    self->capture = control;

    if (FIELD(FIELD(game_main, 0xC, BYTE*), 0x6C, int)) {
        if (FIELD(control, 0x5C, int) == 11) {
            const char* allowed = FIELD(control, 0x210, const char*);

            gui_user_set_ime(self, NULL, !(allowed && text_equal(allowed, DIGITS)));
        } else {
            gui_user_set_ime(self, NULL, 0);
        }
    }
    return taken;
}

/* Move focus to the dialog waiting for it; `force` moves it even when none is. */
PORT(0x00488550)
void __fastcall gui_user_apply_focus(gui_user* self, void* edx_unused, BOOL force)
{
    (void) edx_unused;
    if (self->next_capture) {
        gui_user_set_capture(self, NULL, self->next_capture, NULL);
        self->next_focus = NULL;
        self->next_capture = NULL;
    } else if (self->next_focus || force) {
        gui_user_set_focus(self, NULL, self->next_focus, NULL);
        self->next_focus = NULL;
    }
}

/* Vtable slot 10, the frame's tick. */
PORT(0x00488530)
int __fastcall gui_user_tick(gui_user* self, void* edx_unused, DWORD time)
{
    (void) edx_unused;
    gui_user_apply_focus(self, NULL, 0);
    return game_vf_10_46A300(self, NULL, time);
}

/*
    Queue `dialog` for focus. Refused, with 0, while another dialog that is not a drop-down list
    is waiting.
*/
PORT(0x004886C0)
int __fastcall gui_user_queue_focus(gui_user* self, void* edx_unused, BYTE* dialog, BYTE** old,
                                    BOOL now)
{
    (void) edx_unused;
    if (self->next_focus && game_FUN_0053b910(FIELD(self->next_focus, 0x28, const char*), UIDDL))
        return 0;
    if (old)
        *old = self->next_focus;
    self->next_focus = dialog;
    if (now)
        gui_user_apply_focus(self, NULL, 1);
    return 1;
}

PORT(0x00488720)
int __fastcall gui_user_queue_capture(gui_user* self, void* edx_unused, BYTE* dialog, BYTE** old,
                                      BOOL now)
{
    (void) edx_unused;
    if (self->next_capture
        && game_FUN_0053b910(FIELD(self->next_capture, 0x28, const char*), UIDDL))
        return 0;
    if (old)
        *old = self->next_capture;
    self->next_focus = dialog;
    self->next_capture = dialog;
    if (now)
        gui_user_apply_focus(self, NULL, 1);
    return 1;
}

/* Vtable slot 15: the cursor goes where the paired device points. */
PORT(0x004885B0)
int __fastcall gui_user_update(gui_user* self)
{
    game_vf_15_46A2D0(self);
    if (self->input) {
        self->cursor_x = self->input->x;
        self->cursor_y = self->input->y;
    }
    return 1;
}

/* Replace the user's tooltip with a new one. */
PORT(0x00488610)
void __fastcall gui_user_make_tooltip(gui_user* self, void* edx_unused, void* screen,
                                      gui_manager* manager)
{
    tooltip* tip;

    (void) edx_unused;
    gui_user_drop_tooltip(self);
    tip = game_new(sizeof(tooltip), GUIMANAGER_CPP, 0x667);
    if (tip)
        tip = tooltip_construct(tip, NULL, 1);
    self->tooltip = object_add_child(self, NULL, tooltip_setup(tip, NULL, screen, manager), -1);
}

PORT(0x004885E0)
gui_user* __fastcall gui_user_setup(gui_user* self, void* edx_unused, void* screen,
                                    gui_manager* manager)
{
    (void) edx_unused;
    object_setup(self, NULL, screen);
    self->manager = manager;
    gui_user_make_tooltip(self, NULL, screen, manager);
    return self;
}

PORT(0x004886A0)
void __fastcall gui_user_drop_tooltip(gui_user* self)
{
    if (self->tooltip)
        object_destroy(self->tooltip);
    self->tooltip = NULL;
}

/*
    Pair the user with `input`, or unpair it with NULL. TRUE when the two end up paired, and also
    when they already were; FALSE, changing nothing, when `input` already belongs to another user.
*/
PORT(0x004890B0)
BOOL __fastcall gui_user_pair(gui_user* self, void* edx_unused, gui_input* input)
{
    gui_user* owner = input ? input->user : NULL;

    (void) edx_unused;
    if (owner)
        return owner == self;

    self->input = input;
    if (input)
        input->user = self;
    return TRUE;
}

/*
    Claim `input` for the user. FALSE when another user has it. When the list of claimed devices
    cannot grow, the device is still marked as the user's, as the game does.
*/
PORT(0x00488BC0)
BOOL __fastcall gui_user_claim(gui_user* self, void* edx_unused, gui_input* input)
{
    (void) edx_unused;
    if (input->user)
        return input->user == self;

    list_add(&self->inputs, input);
    input->user = self;
    return TRUE;
}

/* Claim the manager's keyboard, if there is one, and then every joystick. */
PORT(0x00488C90)
void __fastcall gui_user_claim_inputs(gui_user* self)
{
    int i;

    if (self->manager->keyboard)
        gui_user_claim(self, NULL, self->manager->keyboard);
    for (i = 0; i < input_count(); i++)
        gui_user_claim(self, NULL, self->manager->joysticks[i]);
}

/* Give every user a cursor, `image` or else the manager's own. `arg` goes to the cursor. */
PORT(0x00487520)
void __fastcall gui_manager_add_cursors(gui_manager* self, void* edx_unused, const char* image,
                                        DWORD arg)
{
    int i;

    (void) edx_unused;
    for (i = 0; i < self->user_count; i++) {
        if (self->users[i])
            gui_user_make_cursor(self->users[i], NULL, image ? image : self->cursor_image, arg);
    }
}

/* ---- GUIManager: set-up ------------------------------------------------------------------- */


/* Make sure the game's sound bank 0x5622 is loaded. */
int  __fastcall game_FUN_004c2cf0(void* sound, void* edx_unused, DWORD bank, DWORD a, DWORD b,
                                  DWORD size, DWORD c);
void __fastcall game_FUN_004c3060(void* sound, void* edx_unused, DWORD bank, DWORD a, DWORD b);

/* The menus' font texture: 72 bytes, made when the caller passes none. */
void* __fastcall game_FUN_00401a60(void* self, void* edx_unused, DWORD flag);

gui_input* __fastcall gui_input_setup(gui_input* self, void* edx_unused, void* screen,
                                      BYTE* device, float x_from, float x_to, float y_from,
                                      float y_to);

/* Make a GUIInputDevice for `device` in `slot`, under the manager and before its dialogs. */
static void add_input(gui_manager* self, gui_input** slot, int line, BYTE* device, float from,
                      float to)
{
    gui_input* input = game_new(sizeof(gui_input), GUIMANAGER_CPP, line);

    if (input)
        input = gui_input_construct(input);
    *slot = object_add_child(self, NULL, gui_input_setup(input, NULL, object_screen(self), device,
                                                           from, to, from, to), -1);
    object_move_before(*slot, NULL, self->group, -1);
    gui_input_reset(*slot);
}

/*
    Set the manager up on `screen`: the font object (made when `font_object` is NULL), the dialog
    container, the input devices and the first user, who claims the devices and is paired with the
    mouse.
*/
PORT(0x00486340)
gui_manager* __fastcall gui_manager_setup(gui_manager* self, void* edx_unused, void* screen,
                                          int a, int b, BYTE* background, DWORD load_sound,
                                          void* font_object, const char* name, short font_size,
                                          const char* cursor_image, int c)
{
    BYTE* inputs;
    void* group;
    gui_user* user;
    int i;

    (void) edx_unused;
    object_setup(self, NULL, screen);
    self->unknown_d8 = b;
    self->font_object = font_object;
    if (load_sound && !game_FUN_004c2cf0(FIELD(game_main, 4, void*), NULL, 0x5622, 1, 0x10,
                                         4000000, 0))
        game_FUN_004c3060(FIELD(game_main, 4, void*), NULL, 0x5622, 1, 0x10);
    if (!font_object) {
        void* made = game_new(0x48, GUIMANAGER_CPP, 0x82);

        if (made)
            made = game_FUN_00401a60(made, NULL, 1);
        self->font_object = made;
        object_add_child(self, NULL, made, -1);
        self->font_object_made = 1;
    }

    group = game_new(sizeof(game_object), GUIMANAGER_CPP, 0x88);
    if (group) {
        object_construct(group, NULL, 1);
        *(void***) group = GROUP_VTABLE;
    }
    self->group = object_add_child(self, NULL, group, -1);
    self->unknown_38 = a;
    self->unknown_34 = a;
    self->background = background;
    text_copy(self->name, name);
    self->font_size = font_size;
    if (cursor_image)
        text_copy(self->cursor_image, cursor_image);
    VIRTUAL(self, 0x48, method_0)(self);
    self->unknown_d4 = c;
    self->font = CreateFontA(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, PROOF_QUALITY, VARIABLE_PITCH, ARIAL);

    inputs = game_inputs;
    if (FIELD(inputs, 0x30, BYTE*))
        add_input(self, &self->mouse, 0xA2, FIELD(inputs, 0x30, BYTE*), 0.0f, 0.0f);
    if (FIELD(inputs, 0x34, BYTE*))
        add_input(self, &self->keyboard, 0xA9, FIELD(inputs, 0x34, BYTE*), 0.0f, 0.0f);
    if (FIELD(inputs, 4, int)) {
        for (i = 0; i < FIELD(inputs, 4, int); i++)
            add_input(self, &self->joysticks[i], 0xB1, FIELD(inputs, 0x10 + 4 * i, BYTE*), -2.0f,
                      2.0f);
    }

    user = game_new(sizeof(gui_user), GUIMANAGER_CPP, 0xBC);
    if (user)
        user = gui_user_construct(user);
    self->users[0] = object_add_child(self, NULL, gui_user_setup(user, NULL, object_screen(self), self),
                                      -1);
    if (self->users[0]) {
        self->user_count = 1;
        gui_user_claim_inputs(self->users[0]);
        if (self->mouse) {
            gui_user_pair(self->users[0], NULL, self->mouse);
            gui_manager_add_cursors(self, NULL, NULL, 1);
            gui_manager_set_cursors(self, NULL, NULL, 0);
        }
    }
    gui_user_set_ime(self->users[0], NULL, 0);
    self->language = LoadLibraryA("uilang.dll");
    return self;
}

/*
    The dialog the users are on: the one marked modal (0x148) if any, else the last one under the
    mouse, else the first.
*/
PORT(0x00486D80)
BYTE* __fastcall gui_manager_active_dialog(gui_manager* self)
{
    BYTE walk[0x94];
    BYTE* found = NULL;
    BYTE* dialog;

    game_FUN_0046a880(walk, NULL, self, 1, UIDIALOG);
    for (dialog = game_FUN_0046a980(walk); dialog; dialog = game_FUN_0046a980(walk)) {
        if (!found)
            found = dialog;
        if (self->mouse) {
            POINT at;

            at.x = self->mouse->x;
            at.y = self->mouse->y;
            if (PtInRect((RECT*) (dialog + 0x160), at))
                found = dialog;
        }
        if (FIELD(dialog, 0x148, int))
            found = dialog;
    }
    game_FUN_0046a970(walk);
    return found;
}

/* ---- ToolTip: placing it ------------------------------------------------------------------ */

void __fastcall tooltip_set_text(tooltip* self, void* edx_unused, const char* text,
                                 const POINT* at, float delay);

/*
    Show `control`'s tooltip text: under the cursor of the user its dialog belongs to, or else
    centred under the control.
*/
PORT(0x00487AA0)
void __fastcall tooltip_show_for(tooltip* self, void* edx_unused, BYTE* control)
{
    gui_user* user = FIELD(FIELD(control, 0xB8, BYTE*), 0x34, gui_user*);
    POINT at;

    (void) edx_unused;
    if (user->input) {
        at.x = user->input->x;
        at.y = user->input->y + 16;
    } else {
        LONG left = FIELD(control, 0x3C, LONG);

        at.x = left + (FIELD(control, 0x44, LONG) - left) / 2;
        at.y = FIELD(control, 0x48, LONG);
    }
    tooltip_set_text(self, NULL, FIELD(control, 0xF0, const char*), &at, 1.0f);
}

/* ---- GUIUser: capture, cursor, devices ---------------------------------------------------- */

/* Release the capture, telling the control; the old one is returned through `old` if wanted. */
PORT(0x00488B80)
int __fastcall gui_user_release_capture(gui_user* self, void* edx_unused, BYTE** old)
{
    (void) edx_unused;
    if (self->capture && !(FIELD(self->capture, 0x25, BYTE) & 8))
        VIRTUAL(self->capture, 0x84, control_lose)(self->capture, NULL, NULL);
    if (old)
        *old = self->capture;
    self->capture = NULL;
    return 1;
}

/* Let go of every claimed device, up to the first empty entry, and empty the list. */
PORT(0x00488CF0)
void __fastcall gui_user_release_inputs(gui_user* self)
{
    int i;

    for (i = 0; self->inputs.count > 0 && self->inputs.items && i < self->inputs.count; i++) {
        gui_input* input = self->inputs.items[i];

        if (!input)
            break;
        input->user = NULL;
    }
    __stosd((unsigned long*) self->inputs.items, 0, self->inputs.count);
    self->inputs.count = 0;
}

/* The cursor kinds GameCursor makes: one that follows a device's axes, and one that does not. */
void* __fastcall game_FUN_0043fa10(void* self, void* edx_unused, void* screen, void* axis_x,
                                   void* axis_y, const char* image, int a, BYTE* background,
                                   DWORD style, void* b);
void* __fastcall game_FUN_0043fa90(void* self, void* edx_unused, void* screen, const char* image,
                                   int a, BYTE* background, DWORD style, void* b);

/*
    Give the user a cursor showing `image` (or its own cursor image), unless it has one or has no
    device. None is made while the game is in a mode below 8, or in mode 8 with nothing set at
    the manager's 0x34. In the game's pointer mode the cursor follows the device's axes.
*/
PORT(0x00488D60)
void __fastcall gui_user_make_cursor(gui_user* self, void* edx_unused, const char* image,
                                     DWORD arg)
{
    gui_manager* manager = self->manager;
    BOOL pointer;
    int mode;
    BYTE* owner;
    DWORD style;
    gui_cursor* cursor;

    (void) edx_unused;
    if (manager && manager->background)
        background_release_dc(manager->background);
    if (self->cursor || !self->input)
        return;

    pointer = (FIELD(game_main, 0x2D4, BYTE) & 2) != 0;
    mode = FIELD(FIELD(game_main, 0x10, BYTE*), 0x28, int);
    if (mode < 8 || (mode == 8 && !self->manager->unknown_34))
        return;
    owner = (BYTE*) self->manager->unknown_34;
    style = owner ? FIELD(owner, 0x708, DWORD) : 0;
    if (!image)
        image = self->cursor_image;

    cursor = game_new(sizeof(gui_cursor), GUIMANAGER_CPP, pointer ? 0x762 : 0x76A);
    if (cursor)
        cursor = gui_cursor_construct(cursor, NULL, arg);
    manager = self->manager;
    if (pointer)
        self->cursor = object_add_child(self, NULL,
                                        game_FUN_0043fa10(cursor, NULL, object_screen(self),
                                                          self->input->axis_x, self->input->axis_y,
                                                          image, manager->unknown_d8,
                                                          manager->background, style,
                                                          (void*) manager->unknown_34), -1);
    else
        self->cursor = object_add_sibling(self, NULL,
                                          game_FUN_0043fa90(cursor, NULL, object_screen(self), image,
                                                            manager->unknown_d8,
                                                            manager->background, style,
                                                            (void*) manager->unknown_34), -1);
}

/*
    Show `anim` on the cursor, or the user's own animation with NULL. With 0xC4 set, an animation
    long enough carries on from the frame the old one had reached.
*/
PORT(0x00488F40)
void __fastcall gui_user_set_cursor_anim(gui_user* self, void* edx_unused, ui_anim* anim)
{
    gui_cursor* cursor = (gui_cursor*) self->cursor;
    ui_anim* old;

    (void) edx_unused;
    if (!cursor)
        return;
    if (!anim && self->own_anim)
        anim = self->own_anim;
    old = cursor->anim;
    if (old && self->unknown_c4 && anim != old && anim->count > old->frame) {
        anim->frame = old->frame;
        anim->restart = old->restart;
        anim->last_step = old->last_step;
    }
    gui_cursor_set_anim((gui_cursor*) self->cursor, NULL, anim);
}

/* The cursor is left alone while bit 1 of the game's flags at 0x2D5 is set. */
#define cursors_locked() (FIELD(game_main, 0x2D5, BYTE) & 2)

PORT(0x00489050)
void __fastcall gui_user_drop_cursor(gui_user* self)
{
    if (!cursors_locked() && self->cursor)
        object_destroy(self->cursor);
    self->cursor = NULL;
}

/*
    Make the user's cursor again with `image` (its own with NULL, then following its own
    device), shown as the old one was. With `refresh`, the whole screen is redrawn.
*/
PORT(0x00488FA0)
void __fastcall gui_user_set_cursor(gui_user* self, void* edx_unused, const char* image,
                                    BOOL refresh)
{
    DWORD shown;

    (void) edx_unused;
    if (cursors_locked() || !self->input)
        return;
    shown = self->cursor ? FIELD(self->cursor, 0x25, BYTE) & 1 : 1;
    gui_user_drop_cursor(self);
    if (FIELD(FIELD(game_main, 0x10, BYTE*), 0x28, int) == 8 && !self->manager->unknown_34)
        return;
    gui_user_make_cursor(self, NULL, image ? image : self->cursor_image, shown);
    if (!image && self->own_anim && self->cursor)
        gui_cursor_set_anim((gui_cursor*) self->cursor, NULL, self->own_anim);
    if (refresh)
        gui_manager_refresh(self->manager);
}

/* Whether the user hears from `device`: its paired one, or one it claimed. No device, all. */
PORT(0x004890F0)
BOOL __fastcall gui_user_hears(gui_user* self, void* edx_unused, BYTE* device)
{
    int count = self->inputs.count;
    int i;

    (void) edx_unused;
    if (!self->input || self->input->device == device)
        return TRUE;
    for (i = 0; count > 0 && self->inputs.items && i < count; i++) {
        gui_input* input = self->inputs.items[i];

        if (!input)
            return FALSE;
        if (input->device == device)
            return TRUE;
    }
    return FALSE;
}

/* The user's GUIInputDevice for `device`, or NULL. */
PORT(0x004891D0)
gui_input* __fastcall gui_user_input_for(gui_user* self, void* edx_unused, BYTE* device)
{
    int count = self->inputs.count;
    int i;

    (void) edx_unused;
    if (!self->input || self->input->device == device)
        return self->input;
    for (i = 0; count > 0 && self->inputs.items && i < count; i++) {
        gui_input* input = self->inputs.items[i];

        if (!input)
            return NULL;
        if (input->device == device)
            return input;
    }
    return NULL;
}

/* The game's device an input event came from: type 0 keyboard, 1 mouse, 2 and 3 a joystick. */
static BYTE* event_device(const BYTE* event, BOOL* known)
{
    int type = FIELD(event, 4, int);

    *known = TRUE;
    if (type == 0)
        return FIELD(game_inputs, 0x34, BYTE*);
    if (type == 1)
        return FIELD(game_inputs, 0x30, BYTE*);
    if (type == 2 || type == 3)
        return FIELD(game_inputs, 0x10 + 4 * FIELD(event, 8, int), BYTE*);
    *known = FALSE;
    return NULL;
}

PORT(0x00489160)
BOOL __fastcall gui_user_hears_event(gui_user* self, void* edx_unused, const BYTE* event)
{
    BOOL known;
    BYTE* device = event_device(event, &known);

    (void) edx_unused;
    return known ? gui_user_hears(self, NULL, device) : FALSE;
}

PORT(0x00489230)
gui_input* __fastcall gui_user_input_for_event(gui_user* self, void* edx_unused,
                                               const BYTE* event)
{
    BOOL known;
    BYTE* device = event_device(event, &known);

    (void) edx_unused;
    return known ? gui_user_input_for(self, NULL, device) : NULL;
}

/* A claimed device by index, or NULL past the end. */
PORT(0x004892A0)
gui_input* __fastcall gui_user_claimed(gui_user* self, void* edx_unused, int index)
{
    (void) edx_unused;
    if (self->inputs.count > 0 && self->inputs.items && index < self->inputs.count)
        return self->inputs.items[index];
    return NULL;
}

/* ---- UIDlgContainer ----------------------------------------------------------------------- */

PORT(0x004890A0)
void __fastcall dialog_container_destruct(game_object* self)
{
    self->vtable = GROUP_VTABLE;
    object_destruct(self);
}

PORT(0x00489080)
game_object* __fastcall dialog_container_delete(game_object* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    dialog_container_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* ---- The static HiResMeter ---------------------------------------------------------------- */

/* A HiResMeter the game keeps at 0x006815B0, made and destroyed by the C runtime's start-up. */
#define HIRES_METER ((void*) 0x006815B0)
void* __fastcall game_FUN_00489310(void* self, void* edx_unused, DWORD flag);
void  __fastcall game_FUN_004893c0(void* self);
int   __cdecl    game_FUN_0053aaa0(void (__cdecl* at_exit)(void));      /* atexit */

static void __cdecl hires_meter_destroy(void)
{
    game_FUN_004893c0(HIRES_METER);
}

PORT(0x004892E0)
void* __cdecl hires_meter_make(void)
{
    return game_FUN_00489310(HIRES_METER, NULL, 0);
}

PORT(0x004892F0)
int __cdecl hires_meter_destroy_at_exit(void)
{
    return game_FUN_0053aaa0(hires_meter_destroy);
}

/* The C runtime's initializer for it. */
PORT(0x004892D0)
int __cdecl hires_meter_init(void)
{
    hires_meter_make();
    return hires_meter_destroy_at_exit();
}

/* ---- Pictures: the screen behind the menus, and the tooltip's box ------------------------- */

/* Hide, and show again, every object of a class under the game's scene (0x34). */
void __fastcall game_FUN_00469cb0(void* scene, void* edx_unused, const char* class_name);
void __fastcall game_FUN_00469df0(void* scene, void* edx_unused, const char* class_name);
#define GUICURSOR_CLASS ((const char*) 0x00572488)  /* "GUICursor" */
#define TOOLTIP_CLASS   ((const char*) 0x00572480)  /* "ToolTip" */

#define LOCK_FLAGS   0x811          /* DDLOCK_WAIT | DDLOCK_READONLY | DDLOCK_NOSYSLOCK */

/*
    A picture of the screen (or `area` of it) without the cursors and tooltips, darkened to half
    when `darken` is set: 8-bit by blanking every other pixel, the rest by halving each channel.
    NULL when the screen cannot be locked.
*/
#ifdef OPENMCM2_DEV
void render_inks(void* picture_surface);

/*
    A picture of the screen darkened as the original darkens it (each colour halved) but for its
    inks (ddraw.c, photo_pixel): what was clear is shadow at half, shadow is half as much again
    clear, and orange and gold are half as strong. Pixels are 565, or 555 with `is_555`.
*/
static void darken_inks(WORD* pixels, int count, BOOL is_555)
{
    int green_bits = is_555 ? 5 : 6;
    int i;

    for (i = 0; i < count; i++) {
        WORD p = pixels[i];
        int red = p >> (5 + green_bits) & 31;
        int green = p >> 5 & ((1 << green_bits) - 1);
        int blue = p & 31;

        if (red >= 29 && !green) {
            if (red == 31 && blue >= 30) {
                if (blue == 30)
                    blue = 15, red = 29;            /* clear: shadow at half */
                else
                    continue;                       /* the colour key */
            } else if (red == 29) {
                blue = (blue + 32) / 2;             /* shadow: half the rest is shadow too */
            } else {
                blue = (blue + 1) / 2 - 1 < 0 ? 0 : (blue + 1) / 2 - 1;
            }
            pixels[i] = (WORD) (red << (5 + green_bits) | blue);
        } else {
            pixels[i] = (WORD) ((p >> 1) & (is_555 ? 0x3DEF : 0x7BEF));
        }
    }
}
#endif

PORT(0x00487100)
void* __fastcall gui_manager_capture_screen(gui_manager* self, void* edx_unused, BOOL darken,
                                            const RECT* area)
{
    int mode = video_mode();
    int depth = game_FUN_00517050(mode);
    BYTE desc[DESC_SIZE];
    BYTE* video;
    void* surface;
    void* picture;
    BYTE* pixels;
    BYTE* to;
    int left, top, width, height, count, row;

    (void) edx_unused;
    if (self->background)
        background_release_dc(self->background);
    game_FUN_00469cb0(FIELD(game_main, 0x34, void*), NULL, GUICURSOR_CLASS);
    game_FUN_00469cb0(FIELD(game_main, 0x34, void*), NULL, TOOLTIP_CLASS);
    VIRTUAL(game_main, 0x20, method_0)(game_main);
    VIRTUAL(game_main, 0x24, method_0)(game_main);
    game_FUN_004eda50(FIELD(game_main, 0x10, void*));
    game_FUN_00469df0(FIELD(game_main, 0x34, void*), NULL, GUICURSOR_CLASS);
    game_FUN_00469df0(FIELD(game_main, 0x34, void*), NULL, TOOLTIP_CLASS);

    __stosd((unsigned long*) desc, 0, DESC_SIZE / 4);
    FIELD(desc, 0, DWORD) = DESC_SIZE;
    video = FIELD(game_main, 0xC, BYTE*);
    surface = FIELD(video, 0x1A8, void*) ? FIELD(video, 0x1A8, void*) : FIELD(video, 0x19C, void*);
    if (VIRTUAL(surface, 0x64, surface_lock)(surface, NULL, desc, LOCK_FLAGS, NULL))
        return NULL;

    if (area) {
        left = area->left;
        top = area->top;
        width = area->right - left;
        height = area->bottom - top;
    } else {
        left = 0;
        top = 0;
        width = FIELD(desc, DESC_WIDTH, int);
        height = FIELD(desc, DESC_HEIGHT, int);
    }
    count = width * height;
    pixels = game_FUN_004a4410(count * depth, GUIMANAGER_CPP, 0x351);
    /*
        Only the part of `area` on the screen is read, and the rest is black. A modern layout's
        dialog is centred at its own shape, so on a screen of another shape it is wider than the
        screen (2133 at 16:9 on a 1920x1200 screen), and copying the whole of it read before each
        row and past the surface's end, which crashed on starting a race.
    */
    to = pixels;
    for (row = 0; row < height; row++) {
        int y = row + top;
        int from = left > 0 ? left : 0;
        int until = left + width < FIELD(desc, DESC_WIDTH, int) ? left + width : FIELD(desc, DESC_WIDTH, int);

        if (y < 0 || y >= FIELD(desc, DESC_HEIGHT, int) || from >= until) {
            __stosb(to, 0, width * depth);
        } else {
            if (from > left || until < left + width)
                __stosb(to, 0, width * depth);
            __movsb(to + (from - left) * depth,
                    FIELD(desc, DESC_PIXELS, BYTE*) + y * FIELD(desc, DESC_PITCH, int) + from * depth,
                    (until - from) * depth);
        }
        to += width * depth;
    }
    VIRTUAL(surface, 0x80, surface_unlock)(surface, NULL);

#ifdef OPENMCM2_DEV
    /* Over the photos the screen's see-through colours stay what they are, darkened as inks. */
    if (darken && depth == 2 && menu_photos_shown()) {
        darken_inks((WORD*) pixels, count, mode == 0x22B);
        darken = FALSE;
    }
#endif
    if (darken) {
        if (depth == 1) {
            unsigned y;

            for (y = 0; y < (unsigned) height; y++) {
                int x;

                for (x = y & 1; x < width; x += 2)
                    pixels[y * width + x] = 0;
            }
        } else if (depth == 2) {
            WORD mask = (WORD) ((mode == 0x22B ? 0x7BDE : 0xF7DE) >> 1);   /* 555 or 565 */
            WORD* pixel = (WORD*) pixels;
            int n;

            for (n = count; n > 0; n--, pixel++)
                *pixel = (WORD) ((*pixel >> 1) & mask);
        } else if (depth == 3 || depth == 4) {
            BYTE* pixel = pixels;
            int n;

            for (n = count; n > 0; n--, pixel += depth) {
                pixel[0] >>= 1;
                pixel[1] >>= 1;
                pixel[2] >>= 1;
            }
        }
    }

    picture = game_new(0x80, GUIMANAGER_CPP, 0x394);
    if (picture)
        picture = game_FUN_004ca650(picture, NULL, self->unknown_d8, 1);
    VIRTUAL(picture, 0x10, picture_fill)(picture, NULL, pixels, width, height, width, width, mode,
                                         mode, 0, 4, 0, 0, 0, 2, 1, 0, 0x80, 0xFF00FF);
    game_FUN_004a4450(pixels, GUIMANAGER_CPP, 0x399);
#ifdef OPENMCM2_DEV
    if (picture && depth == 2 && menu_photos_shown())
        render_inks(FIELD(picture, 0x70, void*));
#endif
    return picture;
}

/* A picture `width` by `height` of one colour, keyed on magenta; for the tooltip's box. */
PORT(0x004876D0)
void* __fastcall gui_manager_make_box(gui_manager* self, void* edx_unused, int width, int height,
                                      COLORREF colour)
{
    void* picture = game_new(0x80, GUIMANAGER_CPP, 0x474);
    BYTE* owner;
    BYTE* pixels;
    BYTE* pixel;
    BYTE* end;

    (void) edx_unused;
    if (picture)
        picture = game_FUN_004ca650(picture, NULL, self->unknown_d8, 1);
    if (!picture)
        return NULL;

    pixels = game_new(width * height * 3, GUIMANAGER_CPP, 0x478);
    end = pixels + width * height * 3;
    for (pixel = pixels; pixel < end; pixel += 3) {          /* 24-bit, blue first */
        pixel[0] = GetBValue(colour);
        pixel[1] = GetGValue(colour);
        pixel[2] = GetRValue(colour);
    }
    owner = (BYTE*) self->unknown_34;
    if (VIRTUAL(picture, 0x10, picture_fill)(picture, NULL, pixels, width, height, width, width,
                                             0x378, video_mode(),
                                             owner ? FIELD(owner, 0x708, DWORD) : 0, 4,
                                             owner ? FIELD(owner, 0x70C, DWORD) : 0, 1, 0, 2, 1, 0,
                                             0x80, 0xFF00FF))
        VIRTUAL(picture, 0x48, method_1)(picture, NULL, 0xFF00FF);
    game_delete(pixels);
    return picture;
}

/*
    Set the tooltip's text, to show at `at` after `delay` seconds: the box is made to fit the
    text in the manager's font, centred on `at` and kept on the screen. NULL hides it.
*/
PORT(0x00487B10)
void __fastcall tooltip_set_text(tooltip* self, void* edx_unused, const char* text,
                                 const POINT* at, float delay)
{
    BYTE* screen;
    BYTE* box;
    void* surface;
    HDC dc;
    HGDIOBJ old;
    SIZE size;
    int width, height, left, top;

    (void) edx_unused;
    if (self->text) {
        object_destroy(self->text);
        self->text = NULL;
    }
    if (!text)
        goto hide;
    if (self->manager->background && self->background_slot == -1) {
        self->background_slot = background_new_slot(self->manager->background, NULL, 0);
        self->redraws = 3;
    }

    surface = FIELD(object_screen(self), 0x48, void*);
    if (VIRTUAL(surface, 0x44, surface_get_dc)(surface, &dc))
        goto hide;
    old = SelectObject(dc, self->manager->font);
    GetTextExtentPoint32A(dc, text, (int) text_length(text), &size);
    SelectObject(dc, old);
    VIRTUAL(FIELD(object_screen(self), 0x48, void*), 0x68, surface_release_dc)(
        FIELD(object_screen(self), 0x48, void*), dc);

    screen = object_screen(self);
    width = size.cx + 2;
    if (width >= FIELD(screen, 0xC, int) - 2)
        width = FIELD(screen, 0xC, int) - 2;
    height = size.cy + 2;
    self->text_area.top = 0;
    self->text_area.left = 0;
    self->text_area.right = width;
    self->text_area.bottom = height;
    self->place.left = at->x;
    self->place.top = at->y;

    left = self->place.left - width / 2;
    if (left < 0)
        left = 0;
    if (FIELD(screen, 0xC, int) - width < left)
        left = FIELD(screen, 0xC, int) - width;
    self->place.left = left;
    top = self->place.top;
    if (FIELD(screen, 0x10, int) - height < top)
        top = FIELD(screen, 0x10, int) - height;
    self->place.top = top;
    self->place.right = left + width;
    self->place.bottom = top + height;

    box = gui_manager_make_box(self->manager, NULL, width, height, RGB(0x80, 0x80, 0x80));
    self->text = (game_object*) box;
    if (!box || !FIELD(box, 0x70, void*)) {
        self->shown = 0;
        self->delay = -1.0f;
        return;
    }
    surface = FIELD(box, 0x70, void*);
    if (!VIRTUAL(surface, 0x44, surface_get_dc)(surface, &dc)) {
        old = SelectObject(dc, self->manager->font);
        SetTextColor(dc, RGB(0xCC, 0xCC, 0xCC));
        SetBkColor(dc, RGB(0x40, 0x40, 0x40));
        DrawTextA(dc, text, (int) text_length(text), &self->text_area,
                  DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        SelectObject(dc, old);
        surface = FIELD(box, 0x70, void*);
        VIRTUAL(surface, 0x68, surface_release_dc)(surface, dc);
    }
    self->shown = 1;
    self->delay = delay;
    return;

hide:
    self->shown = 0;
    self->delay = -1.0f;
}

/* ---- GUIInputDevice: its axes ------------------------------------------------------------- */

int   __cdecl    game_FUN_0043de30(void);                               /* a new axis id */
void* __fastcall game_FUN_0043dbf0(void* axis, void* edx_unused, float from, float to,
                                   float value, int id, int device_axis, float dead_zone);
void  __fastcall game_FUN_0048a9b0(void* device, void* edx_unused, void* axis, float a, float b);
void  __fastcall game_FUN_0048b3b0(void* device, void* edx_unused, void* axis);

/*
    Make `axis` follow the device's axis `device_axis` (0 across, 1 down) from `from` to `to`, at
    `value`. The original builds it in a temporary, copies it and releases the temporary, and so
    does this.
*/
static void axis_make(BYTE* axis, int device_axis, float from, float to, float value)
{
    BYTE made[0x3C];
    int id = game_FUN_0043de30();

    game_FUN_0043dbf0(made, NULL, from, to, value, id, device_axis, 0.0f);
    __movsd((unsigned long*) axis, (unsigned long*) made, sizeof made / 4);
    game_thunk_FUN_0043dd80(made);
}

/* Equal to zero as the x87 compare tests it (C3 set): NaN counts too. */
static BOOL x87_zero(float value)
{
    return !(value < 0.0f || value > 0.0f);
}

/* Register both axes with the device: type 2 with a 0.01 sensitivity, type 1 plainly. */
static void axes_register(gui_input* self)
{
    int type = FIELD(self->device, 0xC, int);

    if (type == 2) {
        game_FUN_0048a9b0(self->device, NULL, self->axis_x, 0.01f, 0.01f);
        game_FUN_0048a9b0(self->device, NULL, self->axis_y, 0.01f, 0.01f);
    } else if (type == 1) {
        game_FUN_0048b3b0(self->device, NULL, self->axis_x);
        game_FUN_0048b3b0(self->device, NULL, self->axis_y);
    }
}

/*
    Set the device up on `device`, its axes running over `x_from`..`x_to` and `y_from`..`y_to`;
    all zero means the whole screen.
*/
PORT(0x00487F40)
gui_input* __fastcall gui_input_setup(gui_input* self, void* edx_unused, void* screen,
                                      BYTE* device, float x_from, float x_to, float y_from,
                                      float y_to)
{
    (void) edx_unused;
    object_setup(self, NULL, screen);
    self->device = device;
    self->range[0] = x_from;
    self->range[1] = x_to;
    self->range[2] = y_from;
    self->range[3] = y_to;
    if (x87_zero(x_from) && x87_zero(x_to) && x87_zero(y_from) && x87_zero(y_to)) {
        x_to = (float) (FIELD(object_screen(self), 0xC, int) - 1);
        y_to = (float) (FIELD(object_screen(self), 0x10, int) - 1);
    }
    axis_make(self->axis_x, 0, self->range[0], x_to, 0.0f);
    axis_make(self->axis_y, 1, self->range[2], y_to, 0.0f);
    axes_register(self);
    return self;
}

/* Make the axes again over the device's range, where they are now, and register them anew. */
PORT(0x004880E0)
void __fastcall gui_input_reset(gui_input* self)
{
    int x = (int) FIELD(self->axis_x, 0x24, float);
    int y = (int) FIELD(self->axis_y, 0x24, float);
    float x_to, y_to;
    int type;

    if (!self->device)
        return;
    type = FIELD(self->device, 0xC, int);
    if (type == 2 || type == 1) {
        VIRTUAL(self->device, 0, method_1)(self->device, NULL, FIELD(self->axis_x, 4, DWORD));
        VIRTUAL(self->device, 0, method_1)(self->device, NULL, FIELD(self->axis_y, 4, DWORD));
    }
    x_to = self->range[1];
    y_to = self->range[3];
    if (x87_zero(self->range[0]) && x87_zero(self->range[1]) && x87_zero(self->range[2])
        && x87_zero(self->range[3])) {
        x_to = (float) (FIELD(object_screen(self), 0xC, int) - 1);
        y_to = (float) (FIELD(object_screen(self), 0x10, int) - 1);
    }
    axis_make(self->axis_x, 0, self->range[0], x_to, (float) x);
    axis_make(self->axis_y, 1, self->range[2], y_to, (float) y);
    axes_register(self);
}

/* ---- GUIUser: the IME --------------------------------------------------------------------- */

#define game_window       FIELD(game_main, 0x31C, HWND)
#define game_ime          FIELD(game_main, 0x53C, HIMC)    /* the game's input context */
#define game_ime_previous FIELD(game_main, 0x540, HIMC)    /* the window's own, put back */

/*
    Turn the IME on for the control with capture, in its font and at its place, or off with
    `on` FALSE (cancelling what was being composed).
*/
PORT(0x00488920)
void __fastcall gui_user_set_ime(gui_user* self, void* edx_unused, BOOL on)
{
    COMPOSITIONFORM form;
    LOGFONTA font;
    POINT status;
    BYTE* control;
    const char* face;
    HWND ime;

    (void) edx_unused;
    if (!on) {
        ime = ImmGetDefaultIMEWnd(game_window);
        if (ime)
            SendMessageA(ime, WM_IME_CONTROL, IMC_CLOSESTATUSWINDOW, 0);
        ImmNotifyIME(game_ime, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        ImmAssociateContext(game_window, game_ime_previous);
        return;
    }

    ImmAssociateContext(game_window, game_ime);
    control = self->capture;
    form.dwStyle = CFS_FORCE_POSITION;
    form.ptCurrentPos.x = FIELD(control, 0x214, LONG);
    form.ptCurrentPos.y = FIELD(control, 0x218, LONG);
    form.rcArea = *(RECT*) (control + 0x214);       /* unused with CFS_FORCE_POSITION */
    ImmSetCompositionWindow(game_ime, &form);

    font.lfWidth = 0;
    font.lfEscapement = 0;
    font.lfOrientation = 0;
    font.lfItalic = 0;
    font.lfUnderline = 0;
    font.lfStrikeOut = 0;
    font.lfCharSet = DEFAULT_CHARSET;
    font.lfOutPrecision = OUT_DEFAULT_PRECIS;
    font.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    font.lfQuality = PROOF_QUALITY;
    font.lfPitchAndFamily = VARIABLE_PITCH;
    if (FIELD(control, 0x130, char)) {                       /* the control's own font */
        face = (const char*) (control + 0x130);
        font.lfHeight = FIELD(control, 0x160, LONG);
        font.lfWeight = FIELD(control, 0x158, int) ? FW_BOLD : FW_MEDIUM;
    } else {
        BYTE* dialog = FIELD(control, 0xB8, BYTE*);

        if (FIELD(dialog, 0xE0, char)) {                     /* its dialog's */
            face = (const char*) (dialog + 0xE0);
            font.lfHeight = FIELD(dialog, 0xDC, LONG);
            font.lfWeight = FIELD(FIELD(control, 0xB8, BYTE*), 0x108, int) ? FW_BOLD : FW_MEDIUM;
        } else {                                             /* the manager's */
            gui_manager* manager = FIELD(control, 0xBC, gui_manager*);

            face = manager->name;
            font.lfHeight = manager->font_size;
            font.lfWeight = FIELD(control, 0xBC, gui_manager*)->font_bold ? FW_BOLD : FW_MEDIUM;
            font.lfItalic = (BYTE) FIELD(control, 0xBC, gui_manager*)->font_italic;
        }
    }
    text_copy(font.lfFaceName, face);
    ImmSetCompositionFontA(game_ime, &font);

    status.x = 550;
    status.y = 450;
    ImmSetStatusWindowPos(game_ime, &status);
    ime = ImmGetDefaultIMEWnd(game_window);
    if (ime)
        SendMessageA(ime, WM_IME_CONTROL, IMC_OPENSTATUSWINDOW, 0);
    ImmSetOpenStatus(game_ime, FALSE);
}
