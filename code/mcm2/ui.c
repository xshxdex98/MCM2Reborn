/*
    OpenMCM2 - the menus' dialogs and controls, ported from the game (gameui.cpp in the original).

    Each function here replaces the game's at the address in its PORT line, and was checked against
    the original's instructions. C++ exception frames are not reproduced (see gui.c).

    A UIDialog (32,600 bytes) is one menu screen, loaded from a dialog file. Its controls live under
    a UICtlContainer; it belongs to the GUIManager and to the GUIUser it was opened for. The layout
    of most of it is not known yet, so its fields are named by offset below.
*/

#include "gui.h"
#include <imm.h>

#ifdef OPENMCM2_DEV
#include "../dev/dev.h"
#include <mmsystem.h>

/*
    Widescreen menus: the 640x480 the menus were made for, scaled evenly to fit `screen` and
    centred on it. The scale, with the area's top left in `left` and `top`.
*/
static float menu_fit(BYTE* screen, int* left, int* top)
{
    int width = FIELD(screen, 0xC, int);
    int height = FIELD(screen, 0x10, int);
    float scale = width * 480 < height * 640 ? width / 640.0f : height / 480.0f;

    *left = (int) ((width - 640 * scale) / 2);
    *top = (int) ((height - 480 * scale) / 2);
    return scale;
}

/* The renderer's photo layer, for a layout that asks for it (Photos), while it is open. */
void render_photos(BOOL on);
void render_inks(void* picture_surface);
/* The dialogs open with the photos under them, the newest last: the photos show while any is. */
#define PHOTOS_MOST 8
static BYTE* photos_dialogs[PHOTOS_MOST];
static int photos_count = 0;

static void photos_add(BYTE* dialog)
{
    int i;

    for (i = 0; i < photos_count; i++)
        if (photos_dialogs[i] == dialog)
            return;
    if (photos_count < PHOTOS_MOST)
        photos_dialogs[photos_count++] = dialog;
    render_photos(TRUE);
}

static void photos_remove(BYTE* dialog)
{
    int i;

    for (i = 0; i < photos_count; i++)
        if (photos_dialogs[i] == dialog) {
            photos_dialogs[i] = photos_dialogs[--photos_count];
            if (!photos_count)
                render_photos(FALSE);
            return;
        }
}

BOOL menu_photos_shown(void)
{
    return photos_count > 0;
}

/* A 640x480 area moved onto the screen's centred one. */
static void menu_place(BYTE* screen, RECT* area)
{
    int left, top;
    float scale = menu_fit(screen, &left, &top);

    area->left = left + (int) (area->left * scale);
    area->top = top + (int) (area->top * scale);
    area->right = left + (int) (area->right * scale);
    area->bottom = top + (int) (area->bottom * scale);
}

static BOOL dialog_view(BYTE* dialog, RECT* view);

#define MOUSE_VTABLE    ((void*) 0x0055B208)
#define MOUSE_WHEEL     (0x2D8 + 8)     /* its DIMOUSESTATE's lZ: the wheel since the last poll */
#define WHEEL_NOTCH     120

int menu_wheel(void* user, void* object)
{
    static DWORD read_in;               /* the frame the wheel was last read in */
    static LONG partial;                /* what a fine-stepping wheel has moved short of a notch */
    static int notches;
    gui_input* input = user ? ((gui_user*) user)->input : NULL;
    BYTE* mouse = input ? input->device : NULL;
    DWORD frame = FIELD(object_screen(object), 0x1C, DWORD);

    if (!mouse || *(void**) mouse != MOUSE_VTABLE)
        return 0;
    if (frame != read_in) {
        read_in = frame;
        partial += FIELD(mouse, MOUSE_WHEEL, LONG);
        notches = partial / WHEEL_NOTCH;
        partial -= notches * WHEEL_NOTCH;
    }
    return notches;
}

/* The same for a rectangle on `dialog`'s screen: the bike previews' views, which the game gives
   in the original's pixels. A modern layout's View is where its own art leaves room for it. */
void menu_place_view(BYTE* dialog, RECT* view)
{
    if (!dialog_view(dialog, view))
        menu_place(object_screen(dialog), view);
}
#endif

/* UIDialog fields, by offset. */
#define DLG_PARENT        0x2C      /* the dialog it was opened over, or NULL */
#define DLG_MANAGER       0x30      /* gui_manager* */
#define DLG_USER          0x34      /* gui_user* */
#define DLG_NAME          0x38      /* char[], its class name ("UIDDL", ...) */
#define DLG_RESOURCE      0xB8      /* its entry in the resource manager */
#define DLG_SHOWN         0xBC
#define DLG_FONT          0xD8      /* HFONT */
#define DLG_SCREEN_SIZE   0xDC
#define DLG_FACE          0xE0      /* char[], its font's face */
#define DLG_BACKGROUND    0x110     /* the manager's BackgroundImage, when it has one */
#define DLG_COLOURS       0x118     /* 10 COLORREFs, white to start with */
#define DLG_BACKDROP      0x140     /* game_object*, shared with the parent when the same */
#define DLG_OPEN_FLAGS    0x144     /* as passed to gui_manager_open */
#define DLG_MODAL         0x148
#define DLG_SWAP          0x14C     /* given to gui_manager_swap when it closes */
#define DLG_SOUND         0x154
#define DLG_SOUND_ID      0x17C
#define DLG_CONTROLS      0x18C     /* game_object*[], DLG_CONTROL_COUNT of them */
#define DLG_CONTROL_COUNT 0x9B0
#define DLG_ENTRY_COUNT   0x7EFC    /* named entries at DLG_ENTRIES, 0x3C bytes each */
#define DLG_ENTRIES       0x9CC
#define DLG_OLD_FONT      0x7F04    /* selected out of the DC by dialog_begin_paint */
#define DLG_CLIP          0x7F00    /* HRGN */
#define DLG_CHILDREN      0x7F24    /* container_list of objects it owns */
#define DLG_CONTAINER     0x7F3C    /* the UICtlContainer its controls are under */

#define UIDIALOG_VTABLE     ((void**) 0x00557B54)
#define UICTLCONTAINER_VTABLE ((void**) 0x00557BD8)
#define UICONTROL_CLASS     ((const char*) 0x005717A0)  /* "UIControl" */

/* The game's resource manager, at 0x00578C84, and its lookup and release. */
#define game_resources (*(void* const*) 0x00578C84)
BYTE* __fastcall game_FUN_004ee0f0(void* resources, void* edx_unused, const char* name, DWORD a);
void  __fastcall game_FUN_004ee5c0(void* resources, void* edx_unused, void* entry);

/* The C library's _stricmp, as the game links it. */
int __cdecl game_FUN_0053b6e0(const char* a, const char* b);

/* A walk over an object's descendants of one class (also in gui.c). */
void  __fastcall game_FUN_0046a880(void* walk, void* edx_unused, void* root, DWORD deep,
                                   const char* class_name);
BYTE* __fastcall game_FUN_0046a980(void* walk);
void  __fastcall game_FUN_0046a970(void* walk);
#define walk_begin game_FUN_0046a880
#define walk_next  game_FUN_0046a980
#define walk_end   game_FUN_0046a970

/* GameObject's own versions of the slots UIDialog overrides. */
int __fastcall game_vf_2_46A5F0(void* self);

/* The rest of UIDialog and UIControl, not ported yet. */
void* __fastcall game_FUN_0046ac10(void* self, void* edx_unused, void* dialog);
int   __fastcall dialog_read(BYTE* self, void* edx_unused, void* data, DWORD size);
void __fastcall dialog_track_pointer(BYTE* self);
void __fastcall dialog_remove_timer(BYTE* self, void* edx_unused, DWORD id);
void __fastcall dialog_tell_parent(BYTE* self, void* edx_unused, DWORD sound, DWORD kind);
void __fastcall control_show(BYTE* self, void* edx_unused, DWORD shown, BOOL animate);
const char* __fastcall control_get_name(BYTE* self);

typedef int (__fastcall* dialog_handle)(void* self, void* edx_unused, ui_event* event);

/* ---- UIDialog: life cycle ----------------------------------------------------------------- */

PORT(0x0046ACE0)
BYTE* __fastcall dialog_construct(BYTE* self, void* edx_unused, DWORD flag, const char* name)
{
    container_list* children = &FIELD(self, DLG_CHILDREN, container_list);
    int i;

    (void) edx_unused;
    object_construct(self, NULL, flag);
    children->items = NULL;
    children->count = 0;
    children->grow_by = 0;
    children->capacity = 0;
    FIELD(self, 0x7F34, BYTE) &= ~1;
    *(void***) self = UIDIALOG_VTABLE;
    game_FUN_0046ac10(self, NULL, self);
    text_copy((char*) self + DLG_NAME, name ? name : NO_TEXT);

    FIELD(self, DLG_PARENT, void*) = NULL;
    FIELD(self, DLG_MANAGER, void*) = NULL;
    FIELD(self, DLG_SOUND_ID, DWORD) = 0;
    FIELD(self, DLG_FONT, HFONT) = NULL;
    FIELD(self, 0x108, DWORD) = 0;
    FIELD(self, 0x10C, DWORD) = 0;
    FIELD(self, DLG_FACE, char) = 0;
    FIELD(self, 0x174, DWORD) = 640;
    FIELD(self, 0x178, DWORD) = 480;
    FIELD(self, DLG_SHOWN, DWORD) = 1;
    FIELD(self, 0xC4, DWORD) = 0;
    FIELD(self, 0xCC, DWORD) = 0;
    FIELD(self, 0xD0, DWORD) = 0;
    FIELD(self, 0xD4, DWORD) = 0;
    FIELD(self, 0x95C, DWORD) = 0;
    FIELD(self, 0x184, DWORD) = 0;
    FIELD(self, 0x188, DWORD) = 0;
    FIELD(self, DLG_CONTROL_COUNT, DWORD) = 0;
    FIELD(self, 0x9B4, DWORD) = 0;
    FIELD(self, 0x7F20, DWORD) = 0;
    FIELD(self, DLG_BACKGROUND, BYTE*) = NULL;
    FIELD(self, 0x114, DWORD) = 0;
    FIELD(self, 0x7F14, DWORD) = 0;
    FIELD(self, 0xC0, DWORD) = 0;
    FIELD(self, DLG_MODAL, DWORD) = 0;
    for (i = 0; i < 10; i++)
        FIELD(self, DLG_COLOURS + 4 * i, COLORREF) = 0xFFFFFF;
    FIELD(self, 0x9C8, float) = 1.0f;
    FIELD(self, 0x9C4, float) = 1.0f;
    FIELD(self, DLG_BACKDROP, void*) = NULL;
    FIELD(self, 0x7F0C, DWORD) = 0;
    FIELD(self, 0x9B8, DWORD) = 0;
    FIELD(self, 0x7F10, DWORD) = 0;
    FIELD(self, 0x158, DWORD) = 1;
    FIELD(self, 0x15C, DWORD) = 0;
    FIELD(self, 0x170, DWORD) = 0x12;
    FIELD(self, 0x180, DWORD) = 7;
    FIELD(self, 0xC8, DWORD) = 1;
    __stosd((unsigned long*) (self + DLG_ENTRIES), 0, 0x1D4C);
    FIELD(self, DLG_ENTRY_COUNT, DWORD) = 0;
    FIELD(self, DLG_SOUND, DWORD) = 1;
    FIELD(self, 0x9C0, DWORD) = 0;
    FIELD(self, 0x7F18, DWORD) = 1;

    children->items = game_new(8, CONTAINER_LIST_H, 0x3B);
    children->grow_by = 1;
    children->capacity = 2;
    FIELD(self, 0x7F34, BYTE) |= 1;
    FIELD(self, 0x7F1C, DWORD) = 1;
    FIELD(self, DLG_USER, void*) = NULL;
    FIELD(self, DLG_RESOURCE, void*) = NULL;
    FIELD(self, DLG_CONTAINER, void*) = NULL;
    FIELD(self, DLG_SWAP, DWORD) = 0;
    FIELD(self, 0x7F44, DWORD) = 0;
    FIELD(self, DLG_OPEN_FLAGS, DWORD) = 0;
    FIELD(self, 0x7F50, DWORD) = 0;
    FIELD(self, 0x7F48, int) = -1;
    return self;
}

PORT(0x0046AFA0)
void __fastcall dialog_destruct(BYTE* self)
{
    gui_manager* manager = FIELD(self, DLG_MANAGER, gui_manager*);
    BYTE* parent;
    BYTE* backdrop;
    container_list* children;
    int i;

    *(void***) self = UIDIALOG_VTABLE;
#ifdef OPENMCM2_DEV
    photos_remove(self);
#endif
    if (manager) {
        if (FIELD(self, DLG_MODAL, int))
            gui_manager_swap(manager, NULL, FIELD(self, DLG_SWAP, int));
        if (FIELD(FIELD(game_main, 0x10, BYTE*), 0x28, int) == 8) {
            manager = FIELD(self, DLG_MANAGER, gui_manager*);
            if (!manager || !gui_manager_unknown_1f8(manager))
                game_vf_28_483430(FIELD(self, DLG_MANAGER, gui_manager*));
        }
        if (FIELD(self, DLG_MODAL, int) && FIELD(self, DLG_PARENT, BYTE*))
            dialog_freeze(FIELD(self, DLG_PARENT, BYTE*), NULL, 0, 0, NULL);
        if ((FIELD(self, DLG_OPEN_FLAGS, DWORD) & 4) && (FIELD(self, DLG_OPEN_FLAGS, DWORD) & 0x10))
            gui_manager_drop_backdrop(FIELD(self, DLG_MANAGER, gui_manager*));
    }
    if (FIELD(self, DLG_BACKGROUND, BYTE*) && !(FIELD(game_main, 0x2D5, BYTE) & 2))
        background_invalidate(FIELD(self, DLG_BACKGROUND, BYTE*));

    parent = FIELD(self, DLG_PARENT, BYTE*);
    if (parent) {
        void* parent_backdrop = FIELD(parent, DLG_BACKDROP, void*);

        if (parent_backdrop && parent_backdrop != FIELD(self, DLG_BACKDROP, void*)
            && FIELD(parent, DLG_MANAGER, gui_manager*)
            && !gui_manager_unknown_1f8(FIELD(parent, DLG_MANAGER, gui_manager*))) {
            parent = FIELD(self, DLG_PARENT, BYTE*);
            gui_manager_set_busy(FIELD(parent, DLG_MANAGER, gui_manager*), NULL,
                                 FIELD(parent, DLG_BACKDROP, int));
        }
        if (FIELD(self, DLG_SOUND, int))
            dialog_tell_parent(self, NULL, FIELD(self, DLG_SOUND_ID, DWORD), 9);
    }
    if (FIELD(self, DLG_FONT, HFONT))
        DeleteObject(FIELD(self, DLG_FONT, HFONT));
    if (FIELD(self, DLG_CONTROL_COUNT, int)) {
        for (i = 0; i < FIELD(self, DLG_CONTROL_COUNT, int); i++) {
            void* control = FIELD(self, DLG_CONTROLS + 4 * i, void*);

            if (control)
                object_destroy(control);
        }
    }
    if (!(FIELD(game_main, 0x2D5, BYTE) & 2) && game_resources && FIELD(self, DLG_RESOURCE, void*))
        game_FUN_004ee5c0(game_resources, NULL, FIELD(self, DLG_RESOURCE, void*));

    backdrop = FIELD(self, DLG_BACKDROP, BYTE*);
    parent = FIELD(self, DLG_PARENT, BYTE*);
    if ((FIELD(self, 0x7F0C, int) && backdrop && !parent)
        || (backdrop && parent && FIELD(parent, DLG_BACKDROP, BYTE*) != backdrop))
        object_destroy(backdrop);

    if (!(FIELD(game_main, 0x2D5, BYTE) & 2) && FIELD(self, 0x7F10, int) && FIELD(self, 0x9B8, int)) {
        object_destroy(FIELD(FIELD(self, DLG_BACKGROUND, BYTE*), 0x2C, void*));
        background_set_picture(FIELD(self, DLG_BACKGROUND, BYTE*), NULL, FIELD(self, 0x9B8, void*));
    }
    parent = FIELD(self, DLG_PARENT, BYTE*);
    if (!(FIELD(game_main, 0x2D5, BYTE) & 2) && parent && FIELD(parent, DLG_BACKGROUND, BYTE*))
        background_invalidate(FIELD(parent, DLG_BACKGROUND, BYTE*));

    children = &FIELD(self, DLG_CHILDREN, container_list);
    for (i = 0; children->count > 0 && children->items && i < children->count; i++) {
        if (!children->items[i])
            break;
        object_destroy(children->items[i]);
    }
    game_delete(children->items);
    object_destruct(self);
}

PORT(0x0046AF20)
BYTE* __fastcall dialog_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    dialog_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

void __fastcall dialog_notify_controls(BYTE* self, void* edx_unused, DWORD flag);

/* Vtable slot 2: close. The controls and the dialog are told (slot 0x7C), then its handler. */
PORT(0x0046AF40)
int __fastcall dialog_close(BYTE* self)
{
    ui_event event;

    dialog_notify_controls(self, NULL, 1);
    VIRTUAL(self, 0x7C, method_1)(self, NULL, 1);
    event.a = 0;
    event.b = 0;
    event.type = 6;
    event.sender = self;
    event.manager = FIELD(self, DLG_MANAGER, gui_manager*);
    event.c = 0;
    event.e = 0;
    VIRTUAL(self, 0x74, dialog_handle)(self, NULL, &event);
    return game_vf_2_46A5F0(self);
}

/* Vtable slot 28: tell the dialog's handler, with `param`. */
PORT(0x0046F7F0)
int __fastcall dialog_slot_28(BYTE* self, void* edx_unused, DWORD param)
{
    ui_event event;

    (void) edx_unused;
    event.e = 0;
    event.a = 0;
    event.b = 0;
    event.c = 0;
    event.manager = FIELD(self, DLG_MANAGER, gui_manager*);
    event.param = param;
    event.type = 5;
    event.sender = self;
    return VIRTUAL(self, 0x74, dialog_handle)(self, NULL, &event);
}

/* ---- UIDialog: input ---------------------------------------------------------------------- */

/* A key or button event: its code, and the device kind (1 is the mouse). */
typedef struct key_event {
    DWORD code;
    DWORD device;
} key_event;

/* Vtable slot 23: a key pressed, recorded in the user's key table unless the dialog is frozen. */
PORT(0x0046B6B0)
int __fastcall dialog_press(BYTE* self, void* edx_unused, key_event* event, DWORD arg)
{
    (void) edx_unused;
    if (event->device && event->code < 32)
        FIELD(self, DLG_USER, gui_user*)->keys[event->code] = 1;
    if (FIELD(self, 0x25, BYTE) & 4)
        return 0;
    if (event->device == 1)
        dialog_track_pointer(self);
    return game_vf_23_46A500(self, NULL, (DWORD) event, arg);
}

/* Vtable slot 22: a key released. */
PORT(0x0046B700)
int __fastcall dialog_release(BYTE* self, void* edx_unused, key_event* event, DWORD arg)
{
    (void) edx_unused;
    if (event->device && event->code < 32)
        FIELD(self, DLG_USER, gui_user*)->keys[event->code] = 0;
    if (FIELD(self, 0x25, BYTE) & 4)
        return 0;
    if (event->device == 1)
        dialog_track_pointer(self);
    if (event->device) {
        dialog_remove_timer(self, NULL, 0x101);
        dialog_remove_timer(self, NULL, 0x102);
    }
    return game_vf_22_46A4B0(self, NULL, (DWORD) event, arg);
}

/* ---- UIDialog: controls ------------------------------------------------------------------- */

/* Give `control` the dialog's screen, manager and size, and the id `id`. */
PORT(0x0046B770)
BYTE* __fastcall dialog_attach(BYTE* self, void* edx_unused, BYTE* control, DWORD id, DWORD keep)
{
    (void) edx_unused;
    if (!control)
        return NULL;
    FIELD(control, 0x18, void*) = object_screen(self);
    FIELD(control, 0x1DC, DWORD) = FIELD(self, 0x7F18, DWORD);
    FIELD(control, 0xBC, void*) = FIELD(self, DLG_MANAGER, void*);
    FIELD(control, 0x78, DWORD) = id;
    FIELD(control, 0x160, DWORD) = FIELD(self, DLG_SCREEN_SIZE, DWORD);
    if (FIELD(self, DLG_BACKGROUND, BYTE*))
        FIELD(control, 0x1B8, int) = background_new_slot(FIELD(self, DLG_BACKGROUND, BYTE*), NULL,
                                                          keep);
    return control;
}

/* Attach `control` and put it under the dialog's control container. */
PORT(0x0046B7D0)
BYTE* __fastcall dialog_add(BYTE* self, void* edx_unused, BYTE* control, DWORD id, DWORD keep)
{
    (void) edx_unused;
    if (!control)
        return NULL;
    dialog_attach(self, NULL, control, id, keep);
    object_add_child(FIELD(self, DLG_CONTAINER, void*), NULL, control, -1);
    return control;
}

/* Load the dialog file named `name` from the game's resources. FALSE when there is none. */
PORT(0x0046B810)
int __fastcall dialog_load(BYTE* self, void* edx_unused, const char* name)
{
    BYTE* entry = game_FUN_004ee0f0(game_resources, NULL, name, 1);

    (void) edx_unused;
    if (!entry)
        return 0;
    return dialog_read(self, NULL, FIELD(entry, 0x14, void*), FIELD(entry, 0x18, DWORD));
}

/* Hidden, or shown: vtable slot 4 or 5 of the object. */
PORT(0x0046F990)
int __fastcall dialog_show(void* dialog, void* edx_unused, DWORD shown)
{
    (void) edx_unused;
    FIELD(dialog, DLG_SHOWN, DWORD) = shown;
    return shown ? game_vf_5_469F90(dialog) : game_vf_4_469F80(dialog);
}

/* The value of the entry named `name`, or of the manager's message dialog's; 0 when none. */
PORT(0x0046F8D0)
int __fastcall dialog_find(void* dialog, void* edx_unused, const char* name)
{
    BYTE* self = dialog;
    int i;

    (void) edx_unused;
    if (!name)
        return 0;
    for (i = 0; i < FIELD(self, DLG_ENTRY_COUNT, int); i++) {
        if (!game_FUN_0053b6e0(name, (const char*) self + DLG_ENTRIES + 0x3C * i))
            return FIELD(self, DLG_ENTRIES + 0x3C * i + 0x34, int);
    }
    if (FIELD(self, DLG_MANAGER, gui_manager*))
        return gui_manager_find_message(FIELD(self, DLG_MANAGER, gui_manager*), NULL, name);
    return 0;
}

/* Every control with id `id`: vtable slot 0xC4 with `value`. */
PORT(0x0046F9B0)
void __fastcall dialog_set_controls(BYTE* self, void* edx_unused, DWORD id, DWORD value)
{
    BYTE walk[0x94];
    BYTE* control;

    (void) edx_unused;
    walk_begin(walk, NULL, FIELD(self, DLG_CONTAINER, void*), 1, UICONTROL_CLASS);
    for (control = walk_next(walk); control; control = walk_next(walk)) {
        if (FIELD(control, 0x78, DWORD) == id)
            VIRTUAL(control, 0xC4, method_1)(control, NULL, value);
    }
    walk_end(walk);
}

/* Enable or disable every control with id `id`; the background is redrawn after disabling. */
PORT(0x0046FA60)
void __fastcall dialog_enable_controls(BYTE* self, void* edx_unused, DWORD id, DWORD enabled)
{
    BYTE walk[0x94];
    BYTE* control;

    (void) edx_unused;
    walk_begin(walk, NULL, FIELD(self, DLG_CONTAINER, void*), 1, UICONTROL_CLASS);
    for (control = walk_next(walk); control; control = walk_next(walk)) {
        if (FIELD(control, 0x78, DWORD) == id)
            control_show(control, NULL, enabled, 1);
    }
    if (!enabled && FIELD(self, DLG_BACKGROUND, BYTE*))
        background_invalidate(FIELD(self, DLG_BACKGROUND, BYTE*));
    walk_end(walk);
}

/*
    The control named `name` (any case), if it is of `type`: any with 0, and type 8 also accepts
    a 7. NULL when there is none, or the first one by that name is of another type.
*/
PORT(0x0046FB20)
BYTE* __fastcall dialog_find_control(BYTE* self, void* edx_unused, const char* name, int type)
{
    BYTE walk[0x94];
    BYTE* control;
    BYTE* found = NULL;

    (void) edx_unused;
    walk_begin(walk, NULL, FIELD(self, DLG_CONTAINER, void*), 1, UICONTROL_CLASS);
    for (control = walk_next(walk); control; control = walk_next(walk)) {
        if (!game_FUN_0053b6e0(control_get_name(control), name)) {
            int kind = FIELD(control, 0x5C, int);

            if (!type || kind == type || (type == 8 && kind == 7))
                found = control;
            break;
        }
    }
    walk_end(walk);
    return found;
}

/* Tell every control that wants it (0x1B4) through its slot 0xEC, then the dialog (slot 0x7C). */
PORT(0x0046FBF0)
void __fastcall dialog_notify_controls(BYTE* self, void* edx_unused, DWORD flag)
{
    BYTE walk[0x94];
    BYTE* control;

    (void) edx_unused;
    walk_begin(walk, NULL, FIELD(self, DLG_CONTAINER, void*), 1, UICONTROL_CLASS);
    for (control = walk_next(walk); control; control = walk_next(walk)) {
        if (FIELD(control, 0x1B4, int))
            VIRTUAL(control, 0xEC, method_1)(control, NULL, flag);
    }
    VIRTUAL(self, 0x7C, method_1)(self, NULL, flag);
    walk_end(walk);
}

/* ---- UIDialog: painting ------------------------------------------------------------------- */

/* The clip rectangle and result of the last dialog_begin_paint, in the game's data. */
#define paint_clip    (*(RECT**) 0x006616D0)
#define paint_area    (*(RECT*) 0x00661738)
#define paint_result  (*(int*) 0x00661734)

/*
    A DC to paint `area` with, into `dc`: the background's, for `control`'s slot when given, or the
    screen's own. With a control, its font (or the dialog's) is selected and the DC clipped to the
    area. Returns what the background said, or 1 for the screen.
*/
PORT(0x0046FCA0)
int __fastcall dialog_begin_paint(BYTE* self, void* edx_unused, HDC* dc, RECT* area, void* out,
                                  BYTE* control)
{
    (void) edx_unused;
    paint_clip = area;
    *(DWORD*) out = 0;
    FIELD(self, DLG_OLD_FONT, HGDIOBJ) = NULL;
    if (FIELD(self, DLG_BACKGROUND, BYTE*)) {
        paint_clip = &paint_area;
        paint_area = *area;
        if (control) {
            paint_result = background_get_dc(FIELD(self, DLG_BACKGROUND, BYTE*), NULL, dc, area,
                                             FIELD(control, 0x1B8, int),
                                             FIELD(control, 0x1C0, DWORD), &FIELD(control, 0x1BC, int),
                                             out, &paint_area);
        } else {
            int whole = 1;

            paint_result = background_get_dc(FIELD(self, DLG_BACKGROUND, BYTE*), NULL, dc, area, -1,
                                             0, &whole, out, &paint_area);
        }
    } else {
        void* surface = FIELD(object_screen(self), 0x48, void*);

        VIRTUAL(surface, 0x44, HRESULT (__stdcall*)(void*, HDC*))(surface, dc);
        paint_result = 1;
    }

    if (*dc && control) {
        HGDIOBJ font = FIELD(control, 0x128, HGDIOBJ);

        FIELD(self, DLG_OLD_FONT, HGDIOBJ) = SelectObject(*dc, font ? font : FIELD(self, DLG_FONT, HGDIOBJ));
        FIELD(self, DLG_CLIP, HRGN) = CreateRectRgn(paint_clip->left, paint_clip->top,
                                                    paint_clip->right, paint_clip->bottom);
        SelectClipRgn(*dc, FIELD(self, DLG_CLIP, HRGN));
        return paint_result;
    }
    FIELD(self, DLG_CLIP, HRGN) = NULL;
    return paint_result;
}

PORT(0x0046FDE0)
void __fastcall dialog_end_paint(BYTE* self, void* edx_unused, HDC dc)
{
    (void) edx_unused;
    if (FIELD(self, DLG_OLD_FONT, HGDIOBJ))
        SelectObject(dc, FIELD(self, DLG_OLD_FONT, HGDIOBJ));
    if (!FIELD(self, DLG_BACKGROUND, BYTE*) && dc) {
        void* surface = FIELD(object_screen(self), 0x48, void*);

        VIRTUAL(surface, 0x68, HRESULT (__stdcall*)(void*, HDC))(surface, dc);
    }
    if (FIELD(self, DLG_CLIP, HRGN))
        DeleteObject(FIELD(self, DLG_CLIP, HRGN));
}

/* ---- UICtlContainer ----------------------------------------------------------------------- */

PORT(0x0046F790)
void __fastcall control_container_destruct(game_object* self)
{
    self->vtable = UICTLCONTAINER_VTABLE;
    object_destruct(self);
}

PORT(0x0046F770)
game_object* __fastcall control_container_delete(game_object* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    control_container_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* ---- The unit vectors, set up by the C runtime's start-up --------------------------------- */

static void set_floats(float* v, float x, float y, float z)
{
    v[0] = x;
    v[1] = y;
    v[2] = z;
}

PORT(0x0046F7B0)
void __cdecl vector_zero_init(void)
{
    set_floats((float*) 0x006616B0, 0.0f, 0.0f, 0.0f);
}

PORT(0x0046F840)
void __cdecl vector_x_init(void)
{
    set_floats((float*) 0x006616C0, 1.0f, 0.0f, 0.0f);
}

PORT(0x0046F890)
void __cdecl vector_y_init(void)
{
    set_floats((float*) 0x006616D8, 0.0f, 1.0f, 0.0f);
}

PORT(0x0046F950)
void __cdecl vector_z_init(void)
{
    set_floats((float*) 0x00661698, 0.0f, 0.0f, 1.0f);
}

/* The start-up table points at these jumps to them. */
PORT(0x0046F7A0)
void __cdecl vector_zero_init_thunk(void)
{
    vector_zero_init();
}

PORT(0x0046F830)
void __cdecl vector_x_init_thunk(void)
{
    vector_x_init();
}

PORT(0x0046F880)
void __cdecl vector_y_init_thunk(void)
{
    vector_y_init();
}

PORT(0x0046F940)
void __cdecl vector_z_init_thunk(void)
{
    vector_z_init();
}

/* ---- UIDialog: opening -------------------------------------------------------------------- */

#define GAMEUI_CPP   ((const char*) 0x005717B8)     /* "...\gameui.cpp" */
#define DAT_FORMAT   ((const char*) 0x005717AC)     /* "%s%s.dat" */
#define PATH_FORMAT  ((const char*) 0x0056ED34)     /* "%s%s" */
#define READ_MODE    ((const char*) 0x0056CA38)     /* "r" */

/* The C library, as the game links it. */
int   __cdecl game_FUN_0053a11f(char* to, const char* format, ...);      /* sprintf */
char* __cdecl game__strrchr(const char* text, int c);
char* __cdecl game__strncpy(char* to, const char* from, size_t count);

/* The resource manager's add, and the game's file object (308 bytes). */
void* __fastcall game_FUN_004eddc0(void* resources, void* edx_unused, const char* path, DWORD a);
void* __fastcall game_FUN_00461c80(void* self, void* edx_unused, void* resources);
int   __fastcall game_FUN_00461ec0(void* self, void* edx_unused, const char* path, const char* mode,
                                   DWORD a);
void  __fastcall game_FUN_00461cd0(void* self);

int __fastcall dialog_queue_focus(BYTE* self, void* edx_unused, BYTE* control, BYTE** old, BOOL now);
int __fastcall dialog_give_up(BYTE* self);

static void file_delete(void* file)
{
    if (file) {
        game_FUN_00461cd0(file);
        game_delete(file);
    }
}

/*
    Vtable slot 27: lay the dialog out and load it. Its .dat file comes from `folder` (named by
    `file`, or after the dialog without its extension), or failing that from the resources. With
    a user that has no pointing device, the first control gets focus. NULL when it cannot load.
*/
#ifdef OPENMCM2_DEV
/*
    The modern menus: the "modern" folder inside `folder` when it has this dialog's layout -
    its resources (name.dat) or a loose layout (name.dtm) - else `folder` itself.
*/
static BOOL file_exists(const char* path)
{
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(path, &found);

    if (search == INVALID_HANDLE_VALUE)
        return FALSE;
    FindClose(search);
    return TRUE;
}

static const char* modern_folder(const char* folder, const char* dialog_name, char* modern)
{
    char path[0x100];
    const char* dot = game__strrchr(dialog_name, '.');
    int length = dot ? (int) (dot - dialog_name) : (int) text_length(dialog_name);

    if (!dev_settings.menus_modern || !folder || !dialog_name[0])
        return folder;
    game_FUN_0053a11f(modern, "%smodern\\", folder);
    game_FUN_0053a11f(path, "%s%.*s.dat", modern, length, dialog_name);
    if (!file_exists(path)) {
        game_FUN_0053a11f(path, PATH_FORMAT, modern, dialog_name);
        if (!file_exists(path))
            return folder;
    }
    dev_trace("menus", "%s from %s", dialog_name, modern);
    return modern;
}
#endif

PORT(0x0046B230)
BYTE* __fastcall dialog_open(BYTE* self, void* edx_unused, void* screen, const RECT* area, DWORD a,
                             DWORD flags, void* font_object, DWORD b, const char* folder,
                             BYTE* parent, BYTE* background, const char* face, int font_size,
                             gui_manager* manager, gui_user* user, const char* file)
{
    char name[0x100];
    char path[0x100];
    char* dialog_name = (char*) self + DLG_NAME;
    game_object* container;
    ui_event event;

    (void) edx_unused;
    object_setup(self, NULL, screen);
    if (area) {
        FIELD(self, 0x160, RECT) = *area;
    } else {
        FIELD(self, 0x164, LONG) = 0;
        FIELD(self, 0x160, LONG) = 0;
        FIELD(self, 0x168, LONG) = FIELD(object_screen(self), 0xC, LONG);
        FIELD(self, 0x16C, LONG) = FIELD(object_screen(self), 0x10, LONG);
    }
#ifdef OPENMCM2_DEV
    /* The areas given are 640x480's; the whole screen becomes the centred 640x480. */
    if (dev_settings.menus_widescreen) {
        RECT* placed = &FIELD(self, 0x160, RECT);

        if (!area)
            SetRect(placed, 0, 0, 640, 480);
        menu_place(object_screen(self), placed);
    }
#endif
    FIELD(self, DLG_PARENT, BYTE*) = parent;
    FIELD(self, DLG_MODAL, DWORD) = (flags & 6) ? 1 : 0;
    FIELD(self, DLG_MANAGER, gui_manager*) = manager;
    if (manager && FIELD(self, DLG_MODAL, DWORD))
        FIELD(self, DLG_SWAP, int) = gui_manager_swap(manager, NULL, (int) self);
    if (parent)
        FIELD(self, DLG_BACKDROP, void*) = FIELD(parent, DLG_BACKDROP, void*);
    FIELD(self, 0xC4, DWORD) = a;
    FIELD(self, 0x114, void*) = font_object;
    FIELD(self, DLG_BACKGROUND, BYTE*) = background;
    FIELD(self, 0x7F20, DWORD) = b;
    FIELD(self, DLG_SCREEN_SIZE, int) = font_size;
    FIELD(self, DLG_USER, gui_user*) = user;
    text_copy((char*) self + DLG_FACE, face);
    FIELD(self, DLG_FONT, HFONT) = CreateFontA(FIELD(self, DLG_SCREEN_SIZE, int), 0, 0, 0, FW_MEDIUM, 0,
                                               0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                               CLIP_DEFAULT_PRECIS, PROOF_QUALITY, VARIABLE_PITCH,
                                               (char*) self + DLG_FACE);
    FIELD(self, 0x108, DWORD) = 0;
    FIELD(self, 0x10C, DWORD) = 0;

    container = game_new(sizeof(game_object), GAMEUI_CPP, 0x16E);
    if (container) {
        object_construct(container, NULL, 1);
        container->vtable = UICTLCONTAINER_VTABLE;
    }
    FIELD(self, DLG_CONTAINER, void*) = object_add_child(self, NULL, container, -1);

    event.e = 0;
    event.type = 4;
    event.a = 0;
    event.b = 0;
    event.sender = self;
    event.c = 0;
    event.manager = FIELD(self, DLG_MANAGER, gui_manager*);
    VIRTUAL(self, 0x74, dialog_handle)(self, NULL, &event);

#ifdef OPENMCM2_DEV
    char modern[0x100];

    folder = modern_folder(folder, dialog_name, modern);
#endif
    if (dialog_name[0]) {
        char* dot = game__strrchr(dialog_name, '.');
        void* data;

        if (!file) {
            if (dot) {
                size_t length = dot - dialog_name;

                game__strncpy(name, dialog_name, length);
                name[length] = 0;
                game_FUN_0053a11f(path, DAT_FORMAT, folder ? folder : NO_TEXT, name);
            } else {
                game_FUN_0053a11f(path, DAT_FORMAT, folder ? folder : NO_TEXT, dialog_name);
            }
        } else {
            game_FUN_0053a11f(path, PATH_FORMAT, folder ? folder : NO_TEXT, file);
        }
        FIELD(self, DLG_RESOURCE, void*) = game_FUN_004eddc0(game_resources, NULL, path, 0);
        game_FUN_0053a11f(path, PATH_FORMAT, folder ? folder : NO_TEXT, dialog_name);

        data = game_new(0x134, GAMEUI_CPP, 0x194);
        if (data)
            data = game_FUN_00461c80(data, NULL, game_resources);
        if (game_FUN_00461ec0(data, NULL, path, READ_MODE, 0)) {
            int loaded = dialog_read(self, NULL, data, 0);

            file_delete(data);
            if (!loaded) {
                dialog_give_up(self);
                return NULL;
            }
        } else {
            file_delete(data);
            if (!dialog_load(self, NULL, dialog_name)) {
                dialog_give_up(self);
                return NULL;
            }
        }
    }

    if (parent && !FIELD(self, DLG_BACKGROUND, BYTE*))
        FIELD(self, DLG_BACKGROUND, BYTE*) = FIELD(parent, DLG_BACKGROUND, BYTE*);
    if (FIELD(self, DLG_BACKGROUND, BYTE*))
        background_invalidate(FIELD(self, DLG_BACKGROUND, BYTE*));
    if (user && !user->input) {
        BYTE walk[0x94];

        walk_begin(walk, NULL, FIELD(self, DLG_CONTAINER, void*), 1, UICONTROL_CLASS);
        dialog_queue_focus(self, NULL, walk_next(walk), NULL, 0);
        walk_end(walk);
    }
    dialog_notify_controls(self, NULL, 0);
    FIELD(self, 0x7F40, DWORD) = 1;
    return self;
}

/* Give up on opening: no closing sound, and vtable slot 0x68. */
PORT(0x00470E90)
int __fastcall dialog_give_up(BYTE* self)
{
    FIELD(self, DLG_SOUND, DWORD) = 0;
    return VIRTUAL(self, 0x68, method_0)(self);
}

/* Queue `control` for focus with the dialog's user. */
PORT(0x00470F30)
int __fastcall dialog_queue_focus(BYTE* self, void* edx_unused, BYTE* control, BYTE** old, BOOL now)
{
    (void) edx_unused;
    return gui_user_queue_focus(FIELD(self, DLG_USER, gui_user*), NULL, control, old, now);
}

/* ---- UIDialog: every frame ---------------------------------------------------------------- */

/* A UITimer under the dialog: counts up to its period, then calls its target or tells the dialog. */
typedef struct ui_timer {
    void**       vtable;
    DWORD        unknown_04;
    DWORD        id;
    DWORD        elapsed;               /* milliseconds */
    DWORD        period;
    BYTE*        target;                /* told through its slot 0xF0, or NULL */
} ui_timer;

#define MILLISECONDS (*(const float*) 0x00555FFC)  /* 1000.0f */

BYTE* __fastcall control_previous(BYTE* self);
BYTE* __fastcall control_next(BYTE* self);
int __fastcall control_hit_test(BYTE* object, void* edx_unused, POINT* at);
void  __fastcall dialog_track_pointer(BYTE* self);

/* Equal to zero as the x87 compare tests it (C3 set): NaN counts too. */
static BOOL x87_zero(float value)
{
    return !(value < 0.0f || value > 0.0f);
}

/*
    Vtable slot 10, the frame's tick: run the timers, follow the focused control with the cursor,
    and move focus between controls with a joystick's left and right.
*/
PORT(0x0046FE30)
int __fastcall dialog_tick(BYTE* self, void* edx_unused, float seconds)
{
    gui_user* user;
    BYTE* focus;
    gui_input* input;
    int i;

    (void) edx_unused;
    if (x87_zero(FIELD(self, 0x9C0, float)))
        FIELD(self, 0x9C0, DWORD) = 0x3A83126F;               /* 0.001: the first frame */
    else
        FIELD(self, 0x9C0, float) = seconds;

    if (FIELD(self, 0x7F40, int)) {
        ui_event event;

        event.manager = FIELD(self, DLG_MANAGER, gui_manager*);
        event.e = 0;
        event.type = 0x12;
        event.b = 0;
        event.sender = self;
        event.c = 0;
        VIRTUAL(self, 0x74, dialog_handle)(self, NULL, &event);
        FIELD(self, 0x7F40, DWORD) = 0;
    }
    if ((FIELD(self, 0x25, BYTE) & 4) || FIELD(self, 0x7F50, int))
        return 1;

    for (i = 0;; i++) {
        container_list* children = &FIELD(self, DLG_CHILDREN, container_list);
        ui_timer* timer;

        if (children->count <= 0 || !children->items || i >= children->count)
            break;
        timer = children->items[i];
        if (!timer)
            break;
        timer->elapsed += (int) ((double) seconds * MILLISECONDS);
        if (timer->elapsed < timer->period)
            continue;
        timer->elapsed = 0;
        if (timer->target) {
            VIRTUAL(timer->target, 0xF0, method_1)(timer->target, NULL, (DWORD) timer);
        } else {
            ui_event event;

            event.a = timer->id;
            event.manager = FIELD(self, DLG_MANAGER, gui_manager*);
            event.e = 0;
            event.type = 7;
            event.b = 0;
            event.sender = self;
            event.c = 0;
            VIRTUAL(self, 0x74, dialog_handle)(self, NULL, &event);
            if (event.e)
                return 1;
        }
    }

    user = FIELD(self, DLG_USER, gui_user*);
    focus = user->focus;
    gui_user_set_cursor_anim(user, NULL, focus && FIELD(focus, 0x180, ui_anim*)
                                             ? FIELD(focus, 0x180, ui_anim*)
                                             : FIELD(self, 0x95C, ui_anim*));
    for (i = 0; (input = gui_user_claimed(FIELD(self, DLG_USER, gui_user*), NULL, i)) != NULL; i++) {
        BYTE* next = NULL;
        int x, y;

        if (FIELD(input->device, 0xC, int) != 2 || !focus)
            continue;
        x = input->x;
        y = input->y;
        if (x <= -1) {
            if (!FIELD(self, 0x7F1C, int))
                continue;
            next = control_previous(focus);
        } else if (x >= 1) {
            if (!FIELD(self, 0x7F1C, int))
                continue;
            next = control_next(focus);
        } else {
            if (y <= -1 || y >= 1) {
                if (FIELD(self, 0x7F1C, int))
                    FIELD(self, 0x7F1C, DWORD) = 0;
            } else {
                FIELD(self, 0x7F1C, DWORD) = 1;
            }
            continue;
        }
        FIELD(self, 0x7F1C, DWORD) = 0;
        if (next)
            dialog_queue_focus(self, NULL, next, NULL, 0);
    }
    dialog_track_pointer(self);
    game_vf_10_46A300(self, NULL, FIELD(self, 0x9C0, DWORD));
    return 1;
}

/*
    With the pointing device off every control, focus goes to the dialog itself: the user's focus
    is queued to NULL when it is on one of this dialog's controls and nothing else is waiting.
*/
PORT(0x00470050)
void __fastcall dialog_track_pointer(BYTE* self)
{
    gui_user* user = FIELD(self, DLG_USER, gui_user*);
    BYTE* container;
    BYTE* focus;
    POINT at;

#ifdef OPENMCM2_DEV
    if (pad_navigation_active)
        return;
#endif
    if (!user->input)
        return;
    container = FIELD(self, DLG_CONTAINER, BYTE*);
    if (!FIELD(container, 0x10, BYTE*))
        return;
    focus = user->focus;
    if (focus && FIELD(focus, 0xB8, BYTE*) != self)
        focus = NULL;
    at.x = user->input->x;
    at.y = user->input->y;
    if (!FIELD(container, 0x10, BYTE*))
        return;
    FIELD(user, 0x1DC, DWORD) = 0;
    if (!control_hit_test(FIELD(FIELD(self, DLG_CONTAINER, BYTE*), 0x10, BYTE*), NULL, &at) && focus
        && !FIELD(self, DLG_USER, gui_user*)->next_focus)
        gui_user_queue_focus(FIELD(self, DLG_USER, gui_user*), NULL, NULL, NULL, 1);
}

/* ---- UIDialog: drawing -------------------------------------------------------------------- */

/* The frozen dialog's picture (0x7F44), drawn at its place through the background or the screen. */
static void draw_frozen(BYTE* self, void* picture)
{
    if (FIELD(self, DLG_BACKGROUND, BYTE*))
        background_draw(FIELD(self, DLG_BACKGROUND, BYTE*), NULL, picture, &FIELD(self, 0x160, RECT),
                        NULL, 0x1000000, FIELD(self, 0x7F48, int), 1, &FIELD(self, 0x7F4C, int), 0);
    else
        VIRTUAL(object_screen(self), 0xC, screen_draw)(object_screen(self), NULL,
                                                      &FIELD(self, 0x160, RECT), picture, NULL,
                                                      0x1000000);
}

/*
    Vtable slot 13: draw over. The controls' background slots are redrawn first. A frozen dialog
    (bit 2 of byte 0x25) draws its picture instead, when it has one and 0x7F18 is clear.
*/
PORT(0x004700F0)
int __fastcall dialog_draw_over(BYTE* self)
{
    if (!(FIELD(self, 0x25, BYTE) & 4)) {
        BYTE walk[0x94];

        walk_begin(walk, NULL, FIELD(self, DLG_CONTAINER, void*), 1, UICONTROL_CLASS);
        if (FIELD(self, DLG_BACKGROUND, BYTE*)) {
            BYTE* control;

            for (control = walk_next(walk); control; control = walk_next(walk)) {
                int slot = FIELD(control, 0x1B8, int);

                if (slot >= 0 && (!FIELD(control, 0x1C0, int) || FIELD(control, 0x1BC, int)))
                    background_slot_redraw(FIELD(self, DLG_BACKGROUND, BYTE*), NULL, slot,
                                           (RECT*) (control + 0x2C));
            }
        }
        game_vf_13_46A260(self);
        walk_end(walk);
    } else if (FIELD(self, 0x7F44, void*) && !FIELD(self, 0x7F18, int)) {
        draw_frozen(self, FIELD(self, 0x7F44, void*));
    }
    return 1;
}

/* Vtable slot 14: nothing while frozen. */
PORT(0x00470230)
int __fastcall dialog_slot_14(BYTE* self)
{
    if (FIELD(self, 0x25, BYTE) & 4)
        return 1;
    return game_vf_14_46A290(self);
}

#ifdef OPENMCM2_DEV
/* A control the pad has chosen shows its own focus picture. One without (a list, a slider, a tab)
   gets the menus' orange rule under it instead. */
static void dialog_pad_mark(BYTE* self)
{
    static HBRUSH orange;
    gui_user* user = FIELD(self, DLG_USER, gui_user*);
    BYTE* focus = user ? user->focus : NULL;
    void* lit;
    void* surface;
    HDC dc = NULL;
    RECT rule;
    int height;

    if (!pad_navigation_active || !focus || FIELD(focus, 0xB8, BYTE*) != self
        || !FIELD(focus, 0x6C, int) || !FIELD(focus, 0x70, int))
        return;
    lit = FIELD(focus, 0x170, void*);
    if (lit && lit != FIELD(focus, 0x16C, void*))
        return;
    rule = FIELD(focus, 0x2C, RECT);
    height = (rule.bottom - rule.top) / 16;
    rule.top = rule.bottom + 2;
    rule.bottom = rule.top + (height < 2 ? 2 : height > 4 ? 4 : height);
    surface = FIELD(object_screen(self), 0x48, void*);
    if (FAILED(VIRTUAL(surface, 0x44, HRESULT (__stdcall*)(void*, HDC*))(surface, &dc)) || !dc)
        return;
    if (!orange)
        orange = CreateSolidBrush(RGB(255, 138, 0));
    FillRect(dc, &rule, orange);
    VIRTUAL(surface, 0x68, HRESULT (__stdcall*)(void*, HDC))(surface, dc);
}
#endif

/* Vtable slot 15: draw; a frozen dialog draws its picture when 0x7F18 is set. Slot 0x68 follows a
   screen change while 0x7F50 is set. */
PORT(0x00470250)
int __fastcall dialog_draw(BYTE* self)
{
    if (!(FIELD(self, 0x25, BYTE) & 4))
        game_vf_15_46A2D0(self);
    else if (FIELD(self, 0x7F44, void*) && FIELD(self, 0x7F18, int))
        draw_frozen(self, FIELD(self, 0x7F44, void*));
#ifdef OPENMCM2_DEV
    if (!(FIELD(self, 0x25, BYTE) & 4))
        dialog_pad_mark(self);
#endif
    if (FIELD(self, 0x7F50, int) && FIELD(self, 0x7F54, DWORD) != FIELD(object_screen(self), 0x1C, DWORD))
        VIRTUAL(self, 0x68, method_0)(self);
    return 1;
}

/* ---- Text --------------------------------------------------------------------------------- */

/*
    A run of a dialog's text, as the markup parser makes them: plain text, a colour change (~N) or
    a tab stop (|X). The array ends with a run whose text is NULL.
*/
typedef struct text_run {
    const char* text;
    int         kind;               /* 1: switch to `colour`, 2: move to `tab` from the start */
    int         length;
    COLORREF    colour;
    int         width;
    int         tab;
} text_run;

_Static_assert(sizeof(text_run) == 24, "a text run is 24 bytes");

/* Where the text being drawn has got to, in the game's data. */
#define text_run_index  (*(int*) 0x0066174C)
#define text_x          (*(int*) 0x006616AC)
#define text_colour     (*(COLORREF*) 0x006616BC)
#define text_size       (*(SIZE*) 0x00661728)
#define text_left       (*(int*) 0x006616F0)
#define text_top        (*(int*) 0x006616CC)

/* `text` in `colour`, over a shadow one pixel up and left when `shadow` is set. */
PORT(0x004702F0)
void __fastcall text_out(void* self, void* edx_unused, HDC dc, int x, int y, const char* text,
                         int length, COLORREF colour, BOOL shadow, COLORREF shadow_colour)
{
    (void) self;
    (void) edx_unused;
    if (shadow) {
        SetTextColor(dc, shadow_colour);
        TextOutA(dc, x - 1, y - 1, text, length);
    }
    SetTextColor(dc, colour);
    TextOutA(dc, x, y, text, length);
}

/*
    Draw text split into runs from (x, y): colour runs change the colour, tab runs move to their
    stop. A shadow goes one pixel down and right of each run when `shadow` is set.
*/
PORT(0x00470350)
void __fastcall text_out_runs(void* self, void* edx_unused, HDC dc, int x, int y, const char* text,
                              int length, const text_run* runs, COLORREF colour, BOOL shadow,
                              COLORREF shadow_colour)
{
    (void) self;
    (void) edx_unused;
    (void) text;
    (void) length;
    text_run_index = 0;
    text_x = x;
    SetTextColor(dc, colour);
    text_colour = colour;
    while (runs[text_run_index].text) {
        const text_run* run = &runs[text_run_index];

        if (run->kind == 2)
            text_x = x + run->tab;
        if (shadow) {
            SetTextColor(dc, shadow_colour);
            run = &runs[text_run_index];
            TextOutA(dc, text_x + 1, y + 1, run->text, run->length);
            SetTextColor(dc, text_colour);
        }
        run = &runs[text_run_index];
        if (run->kind == 1) {
            text_colour = run->colour;
            SetTextColor(dc, text_colour);
        }
        run = &runs[text_run_index];
        TextOutA(dc, text_x, y, run->text, run->length);
        text_x += runs[text_run_index].width;
        text_run_index++;
    }
}

/*
    Draw `text` in `area`, placed by `align`: 2 centred across, 4 right; 0x10 centred down, 0x20 at
    the bottom (`height` high, or the dialog's font size). Align 9 keeps the last measurement.
    `control` measures it, when given; `runs` draws it as runs.
*/
PORT(0x00470480)
void __fastcall dialog_draw_text(BYTE* self, void* edx_unused, HDC dc, const RECT* area,
                                 const char* text, int length, COLORREF colour, int height,
                                 const text_run* runs, BOOL shadow, COLORREF shadow_colour,
                                 DWORD align, BOOL transparent, BYTE* control)
{
    (void) edx_unused;
    SetBkMode(dc, transparent ? TRANSPARENT : OPAQUE);
    if (align != 9) {
        if (control) {
            text_size.cx = VIRTUAL(control, 0x94, SIZE* (__fastcall*)(void*))(control)->cy;
            text_size.cy = FIELD(control, 0x160, LONG);
        } else {
            GetTextExtentPoint32A(dc, text, length, &text_size);
        }
    }
    if (align & 2)
        text_left = area->left + (area->right - area->left) / 2 - text_size.cx / 2;
    else if (align & 4)
        text_left = area->right - text_size.cx;
    else
        text_left = area->left;
    if (!height)
        height = FIELD(self, DLG_SCREEN_SIZE, int);
    if (align & 0x10)
        text_top = area->top + (area->bottom - area->top) / 2 - height / 2;
    else if (align & 0x20)
        text_top = area->bottom - height;
    else
        text_top = area->top;

    if (runs)
        text_out_runs(self, NULL, dc, text_left, text_top, text, length, runs, colour, shadow,
                      shadow_colour);
    else
        text_out(self, NULL, dc, text_left, text_top, text, length, colour, shadow, shadow_colour);
}

/* What a control's text slots give: text, length, runs, and the measured size (0x88 to 0x94). */
#define control_text(c)    VIRTUAL(c, 0x88, const char* (__fastcall*)(void*))(c)
#define control_length(c)  VIRTUAL(c, 0x8C, int (__fastcall*)(void*))(c)
#define control_runs(c)    VIRTUAL(c, 0x90, const text_run* (__fastcall*)(void*))(c)
#define control_extent(c)  VIRTUAL(c, 0x94, SIZE* (__fastcall*)(void*))(c)

#define control_text_left  (*(int*) 0x00661730)
#define control_text_top   (*(int*) 0x00661708)
#define control_text_size  (*(SIZE*) 0x006616E8)

/* Draw `control`'s text inside its padded rectangle, placed by its alignment (0xD8). */
PORT(0x004705F0)
void __fastcall dialog_draw_control_text(BYTE* self, void* edx_unused, HDC dc, BYTE* control,
                                         BOOL transparent)
{
    RECT box = *(RECT*) (control + 0x2C);
    int pad_x = FIELD(control, 0xE8, int);
    int pad_y = FIELD(control, 0xEC, int);
    int left = box.left + pad_x;
    int top = box.top + pad_y;
    int right = box.right - pad_x;
    int bottom = box.bottom - pad_y;
    DWORD align;
    int width;

    (void) edx_unused;
    SetBkMode(dc, transparent ? TRANSPARENT : OPAQUE);
    if (FIELD(control, 0xD8, DWORD) != 9) {
        width = control_extent(control)->cy;
        control_text_size.cx = width;
        control_text_size.cy = FIELD(control, 0x160, LONG);
    } else {
        width = control_text_size.cx;
    }
    align = FIELD(control, 0xD8, DWORD);
    if (align & 2)
        control_text_left = left + (right - left) / 2 - width / 2;
    else if (align & 4)
        control_text_left = right - width;
    else
        control_text_left = left;
    align = FIELD(control, 0xD8, DWORD);
    if (align & 0x10)
        control_text_top = top + (bottom - top) / 2 - FIELD(self, DLG_SCREEN_SIZE, int) / 2;
    else if (align & 0x20)
        control_text_top = bottom - FIELD(self, DLG_SCREEN_SIZE, int);
    else
        control_text_top = top;

    if (control_runs(control)) {
        COLORREF shadow_colour = FIELD(control, 0xC8, COLORREF);
        COLORREF colour = FIELD(control, 0xC4, COLORREF);
        const text_run* runs = control_runs(control);
        int length = control_length(control);

        text_out_runs(self, NULL, dc, control_text_left, control_text_top, control_text(control),
                      length, runs, colour, colour, shadow_colour);
    } else {
        COLORREF shadow_colour = FIELD(control, 0xC8, COLORREF);
        BOOL shadow = FIELD(control, 0xD4, BOOL);
        COLORREF colour = FIELD(control, 0xC4, COLORREF);
        int length = control_length(control);

        text_out(self, NULL, dc, control_text_left, control_text_top, control_text(control),
                 length, colour, shadow, shadow_colour);
    }
}

/* ---- Text markup -------------------------------------------------------------------------- */

char* __cdecl game_FUN_0053b580(const char* text, int c);               /* strchr */

/* Measure `text` in `font` (or the dialog's) on the screen's DC. FALSE when there is no DC. */
static BOOL measure_text(BYTE* self, HFONT font, const char* text, int length, SIZE* size)
{
    void* surface = FIELD(object_screen(self), 0x48, void*);
    HDC dc;
    HGDIOBJ old;

    if (VIRTUAL(surface, 0x44, HRESULT (__stdcall*)(void*, HDC*))(surface, &dc))
        return FALSE;
    old = SelectObject(dc, font ? font : FIELD(self, DLG_FONT, HFONT));
    GetTextExtentPoint32A(dc, text, length, size);
    SelectObject(dc, old);
    surface = FIELD(object_screen(self), 0x48, void*);
    VIRTUAL(surface, 0x68, HRESULT (__stdcall*)(void*, HDC))(surface, dc);
    return TRUE;
}

/*
    Split `text` into runs at its markup: "~N" switches to the dialog's colour N (0-9), "|X" moves
    to tab stop X (0-9, then a-z or A-Z for 10 on) of `tabs`. Each run is measured in `font`. With
    `extent`, [0] gets the length without the markup and [1] the total width. Returns the runs on
    the heap, ended by an empty run, or NULL when the text has no markup (or no tabs for its "|"),
    or while the game's flag 0x538 is set.

    The original is split in two at 0x00470822, which only this function jumps to.
*/
PORT(0x004707C0)
text_run* __fastcall dialog_parse_runs(BYTE* self, void* edx_unused, const char* text,
                                       COLORREF colour, HFONT font, const int* tabs, int* extent)
{
    text_run runs[100];
    const char* pending = NULL;           /* where the markup before the current run began */
    const char* start;
    SIZE size = { 0, 0 };                 /* the original leaves it unset until measured */
    int length, i = 0, count = 0, at = 2;
    text_run* copy;

    (void) edx_unused;
    if (FIELD(game_main, 0x538, int))
        return NULL;
    if (!game_FUN_0053b580(text, '~') && (!game_FUN_0053b580(text, '|') || !tabs))
        return NULL;

    length = (int) text_length(text);
    if (text[0] != '~' && text[0] != '|') {
        runs[0].text = text;
        runs[0].colour = colour;
        runs[0].kind = 1;
        count = 1;
    }
    start = text;
    if (text[0]) {
        do {
            char mark = text[i];
            text_run* run;

            if (mark != '~' && mark != '|') {
                at++;
                i++;
                continue;
            }
            if (!pending)
                pending = &text[i];
            if (at >= length)
                break;
            if (text[i + 2] == '~' || text[i + 2] == '|') {
                i += 2;
                at += 2;
                continue;
            }

            run = &runs[count];
            run->text = &text[i + 2];
            if (mark == '~') {
                int n = text[i + 1] - '0';

                run->kind = 1;
                if (n > 9)
                    n = 9;
                else if (n < 0)
                    n = 0;
                run->colour = FIELD(self, DLG_COLOURS + 4 * n, COLORREF);
            } else {
                run->kind = 2;
                if (tabs) {
                    char stop = text[i + 1];
                    int n;

                    if (stop >= '0' && stop <= '9')
                        n = stop - '0';
                    else if (stop >= 'a' && stop <= 'z')
                        n = stop - 'a' + 10;
                    else if (stop >= 'A' && stop <= 'Z')
                        n = stop - 'A' + 10;
                    else
                        n = 0;
                    run->tab = tabs[n];
                } else {
                    run->tab = 0;
                }
            }
            at += 2;
            i += 2;
            if (count) {
                text_run* previous = &runs[count - 1];

                previous->length = pending ? (int) (pending - start) : (int) (run->text - start) - 2;
                measure_text(self, font, previous->text, previous->length, &size);
                previous->width = size.cx;
            }
            start = run->text;
            count++;
            pending = NULL;
        } while (text[i]);
    }

    if (count) {
        text_run* last = &runs[count - 1];

        last->length = pending ? (int) (pending - last->text) : (int) (text + length - last->text);
        if (measure_text(self, font, last->text, last->length, &size))
            last->width = size.cx;
        else
            last->width = 0;
    }
    if (extent) {
        int n;

        extent[1] = 0;
        extent[0] = length - 2 * count;
        for (n = 0; n < count; n++)
            extent[1] += runs[n].width;
    }
    runs[count].text = NULL;
    runs[count].length = 0;
    runs[count].kind = 0;

    copy = game_FUN_004a4410((count + 1) * sizeof(text_run), GAMEUI_CPP, 0xA5B);
    if (copy)
        __movsb((BYTE*) copy, (BYTE*) runs, (count + 1) * sizeof(text_run));
    return copy;
}

/* ---- UIDialog: timers, children, freezing ------------------------------------------------- */

#define UITIMER_VTABLE ((void**) 0x00557C48)

/* BaseObject's constructor and destructor: UITimer is not a GameObject. */
void __fastcall game_FUN_00405f40(void* self);
void __fastcall game_FUN_00405f70(void* self);
int  __fastcall game_vf_16_46A3B0(void* self, void* edx_unused, DWORD frozen);

/*
    Whether `at` is on a double-byte character of `text` (its lead or trail byte), looking at
    no more than `count` bytes; FALSE once a single-byte character past it is reached.
*/
PORT(0x00470BB0)
BOOL __fastcall text_on_double_byte(void* self, void* edx_unused, const char* text, const char* at,
                                    int count)
{
    const char* p = text;
    int i;

    (void) self;
    (void) edx_unused;
    for (i = 0; i < count; i++, p++) {
        if (IsDBCSLeadByte((BYTE) *p)) {
            if (p == at || text + i + 1 == at)
                return TRUE;
            i++;
            p++;
        } else if (p > at) {
            return FALSE;
        }
    }
    return FALSE;
}

/* Add a timer that fires every `period` milliseconds: at `target` (slot 0xF0), or as event 7. */
PORT(0x00470C10)
void __fastcall dialog_add_timer(BYTE* self, void* edx_unused, DWORD id, DWORD period, BYTE* target)
{
    ui_timer* timer;

    (void) edx_unused;
    if (!period)
        return;
    timer = game_new(sizeof(ui_timer), GAMEUI_CPP, 0xA76);
    if (timer) {
        game_FUN_00405f40(timer);
        timer->vtable = UITIMER_VTABLE;
        timer->id = id;
        timer->period = period;
        timer->elapsed = 0;
        timer->target = target;
    }
    list_add(&FIELD(self, DLG_CHILDREN, container_list), timer);
}

PORT(0x00470D60)
void __fastcall ui_timer_destruct(ui_timer* self)
{
    self->vtable = UITIMER_VTABLE;
    game_FUN_00405f70(self);
}

PORT(0x00470D40)
ui_timer* __fastcall ui_timer_delete(ui_timer* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    ui_timer_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/*
    Take `item` out of the children by moving the last one into its place. The original does not
    destroy it, and the walk then goes on past the moved one.
*/
static void children_remove(container_list* children, void* item)
{
    int j;

    for (j = 0; j < children->count; j++) {
        if (children->items[j] == item) {
            children->items[j] = children->items[children->count - 1];
            children->count--;
            return;
        }
    }
}

/* Take every timer with `id` out of the dialog's children. */
PORT(0x00470D70)
void __fastcall dialog_remove_timer(BYTE* self, void* edx_unused, DWORD id)
{
    container_list* children = &FIELD(self, DLG_CHILDREN, container_list);
    int i;

    (void) edx_unused;
    for (i = 0; children->count > 0 && children->items && i < children->count; i++) {
        ui_timer* timer = children->items[i];

        if (!timer)
            break;
        if (timer->id == id && children->count > 0)
            children_remove(children, timer);
    }
}

/* Take `object` out of the dialog's children. */
PORT(0x00470DF0)
void __fastcall dialog_remove_child(BYTE* self, void* edx_unused, void* object)
{
    container_list* children = &FIELD(self, DLG_CHILDREN, container_list);
    int i;

    (void) edx_unused;
    for (i = 0; children->count > 0 && children->items && i < children->count; i++) {
        void* item = children->items[i];

        if (!item)
            break;
        if (item == object && children->count > 0)
            children_remove(children, object);
    }
}

/* Close at the next screen change, with `sound` (see dialog_draw). Only the first call counts. */
PORT(0x00470E60)
void __fastcall dialog_end(BYTE* self, void* edx_unused, DWORD sound)
{
    (void) edx_unused;
    if (FIELD(self, 0x7F50, int))
        return;
    FIELD(self, DLG_SOUND_ID, DWORD) = sound;
    FIELD(self, 0x7F50, DWORD) = 1;
    FIELD(self, 0x7F54, DWORD) = FIELD(object_screen(self), 0x1C, DWORD);
}

/* Tell the parent dialog's handler: event `kind` from this one, with `sound`. */
PORT(0x00470EA0)
void __fastcall dialog_tell_parent(BYTE* self, void* edx_unused, DWORD sound, DWORD kind)
{
    BYTE* parent = FIELD(self, DLG_PARENT, BYTE*);
    ui_event event;

    (void) edx_unused;
    if (!parent)
        return;
    event.type = kind;
    event.a = sound;
    event.e = 0;
    event.b = 0;
    event.sender = parent;
    event.c = (DWORD) self;
    event.manager = FIELD(parent, DLG_MANAGER, gui_manager*);
    VIRTUAL(parent, 0x74, dialog_handle)(parent, NULL, &event);
}

PORT(0x00470EF0)
void __fastcall dialog_set_170(BYTE* self, void* edx_unused, DWORD value)
{
    (void) edx_unused;
    FIELD(self, 0x170, DWORD) = value;
}

/* The background, for this dialog and every parent up the chain. */
PORT(0x00470F00)
void __fastcall dialog_set_background(BYTE* self, void* edx_unused, BYTE* background)
{
    (void) edx_unused;
    if (FIELD(self, DLG_PARENT, BYTE*))
        dialog_set_background(FIELD(self, DLG_PARENT, BYTE*), NULL, background);
    FIELD(self, DLG_BACKGROUND, BYTE*) = background;
}

/* Vtable slots 29 to 31: the dialog's callbacks, when it has them. */
typedef void (__cdecl* callback_arg)(DWORD arg);
typedef int  (__fastcall* callback_self)(BYTE* self);
typedef void (__cdecl* callback_self_arg)(BYTE* self, DWORD arg);

PORT(0x00470F50)
void __fastcall dialog_slot_29(BYTE* self, void* edx_unused, DWORD arg)
{
    callback_arg callback = FIELD(self, 0xCC, callback_arg);

    (void) edx_unused;
    if (callback)
        callback(arg);
}

PORT(0x00470F70)
int __fastcall dialog_slot_30(BYTE* self)
{
    callback_self callback = FIELD(self, 0xD4, callback_self);

    return callback ? callback(self) : 0;
}

PORT(0x00470F80)
void __fastcall dialog_slot_31(BYTE* self, void* edx_unused, DWORD arg)
{
    callback_self_arg callback = FIELD(self, 0xD0, callback_self_arg);

    (void) edx_unused;
    if (callback)
        callback(self, arg);
}

/* Drop the frozen picture and its background slot. */
PORT(0x00471040)
void __fastcall dialog_thaw_picture(BYTE* self)
{
    BYTE* background;

    if (FIELD(self, 0x7F44, void*))
        object_destroy(FIELD(self, 0x7F44, void*));
    background = FIELD(self, DLG_BACKGROUND, BYTE*);
    if (background && FIELD(self, 0x7F48, int) >= 0) {
        background_free_slot(background, NULL, FIELD(self, 0x7F48, int));
        FIELD(FIELD(self, DLG_BACKGROUND, BYTE*), 0x30, int) = 1;
    }
    FIELD(self, 0x7F44, void*) = NULL;
    FIELD(self, 0x7F48, int) = -1;
    FIELD(self, 0x7F4C, int) = 0;
}

/* Take a picture of the screen behind the dialog (`area`, or its own), darkened with `darken`. */
PORT(0x00470FE0)
void __fastcall dialog_freeze_picture(BYTE* self, void* edx_unused, BOOL darken, const RECT* area)
{
    BYTE* background;

    (void) edx_unused;
    dialog_thaw_picture(self);
    FIELD(self, 0x7F44, void*) = gui_manager_capture_screen(FIELD(self, DLG_MANAGER, gui_manager*),
                                                            NULL, darken,
                                                            area ? area : &FIELD(self, 0x160, RECT));
    background = FIELD(self, DLG_BACKGROUND, BYTE*);
    if (background) {
        FIELD(self, 0x7F48, int) = background_new_slot(background, NULL, 1);
        background_invalidate(FIELD(self, DLG_BACKGROUND, BYTE*));
        FIELD(FIELD(self, DLG_BACKGROUND, BYTE*), 0x30, int) = 0;
    }
}

/* Freeze the dialog behind a picture of itself, or thaw it; GameObject's slot 16 follows. */
PORT(0x00470FA0)
void __fastcall dialog_freeze(void* dialog, void* edx_unused, DWORD frozen, DWORD darken, RECT* area)
{
    BYTE* self = dialog;

    (void) edx_unused;
    if (frozen)
        dialog_freeze_picture(self, NULL, darken, area);
    else
        dialog_thaw_picture(self);
    game_vf_16_46A3B0(self, NULL, frozen);
}

/* ---- UIControl ---------------------------------------------------------------------------- */

/*
    UIControl, the base of every control on a dialog. Fields by offset; 0x2C is its rectangle
    (also copied to 0x3C), 0xB8 the dialog it is on and 0xBC that dialog's manager.
*/
#define CTL_RECT          0x2C
#define CTL_TYPE          0x5C      /* 11 is a text box */
#define CTL_STATE         0x64
#define CTL_ID            0x78
#define CTL_ANIMATION     0xB4      /* 100 to 106 pick a routine at 0x1D0 */
#define CTL_DIALOG        0xB8
#define CTL_MANAGER       0xBC
#define CTL_TEXT          0xC0      /* char*, on the game's heap */
#define CTL_COLOUR        0xC4
#define CTL_SHADOW_COLOUR 0xC8
#define CTL_LENGTH        0xD0
#define CTL_SHADOW        0xD4
#define CTL_ALIGN         0xD8
#define CTL_EXTENT        0xDC      /* int[2]: length without markup, width */
#define CTL_RUNS          0xE4      /* text_run*, on the game's heap */
#define CTL_NAME          0xF4      /* char[0x32] */
#define CTL_FONT          0x128
#define CTL_TABS          0x1C4
#define CTL_SLOT          0x1B8     /* its background slot, or -1 */

#define UICONTROL_VTABLE ((void**) 0x00557C5C)

/*
    The animation a control runs is a pointer to member function, stored at 0x1D0: each is the
    compiler's thunk for one vtable slot, which jumps to the method with its arguments in place.
    These are those thunks, instruction for instruction (the game's own are at 0x0047C540 to
    0x0047C5A0).
*/
#define VCALL_THUNK(name, slot)                                          \
    static __declspec(naked) void name(void)                             \
    {                                                                    \
        __asm mov eax, dword ptr [ecx]                                   \
        __asm jmp dword ptr [eax + slot]                                 \
    }

VCALL_THUNK(animate_hide, 0xBC)      /* 0x0047C540 */
VCALL_THUNK(animate_100, 0xA4)       /* 0x0047C550 */
VCALL_THUNK(animate_101, 0xA8)       /* 0x0047C560 */
VCALL_THUNK(animate_102, 0xAC)       /* 0x0047C570 */
VCALL_THUNK(animate_103, 0xB0)       /* 0x0047C580 */
VCALL_THUNK(animate_104, 0xB4)       /* 0x0047C590 */
VCALL_THUNK(animate_106, 0xB8)       /* 0x0047C5A0 */

int   __cdecl    game__rand(void);
int __fastcall edit_set_text(BYTE* self, void* edx_unused, const char* text);
ui_anim* __fastcall anim_construct(ui_anim* self, void* edx_unused, DWORD texture, DWORD b);
BOOL __fastcall anim_add_file(ui_anim* self, void* edx_unused, const char* name, BYTE* owner);

#define RESOURCE_FAILED      ((const char*) 0x0057180C)  /* "Resource string '%d' load fail\n" */
#define RESOURCE_UNAVAILABLE ((const char*) 0x00571D58)  /* "Resource String Unavailable" */

/* Redraw: vtable slot 0xC8. */
#define control_redraw(c) VIRTUAL(c, 0xC8, method_0)(c)

PORT(0x004710A0)
BYTE* __fastcall control_construct(BYTE* self, void* edx_unused, int type, DWORD a, const RECT* area,
                                   BYTE* dialog)
{
    int i;

    (void) edx_unused;
    object_construct(self, NULL, 1);
    *(void***) self = UICONTROL_VTABLE;
    game_FUN_0046ac10(self, NULL, self);
    FIELD(self, CTL_TYPE, int) = type;
    FIELD(self, 0x74, DWORD) = a;
    if (area) {
        FIELD(self, CTL_RECT, RECT) = *area;
        FIELD(self, 0x3C, RECT) = *area;
    } else {
        FIELD(self, 0x48, LONG) = 0;
        FIELD(self, 0x44, LONG) = 0;
        FIELD(self, 0x40, LONG) = 0;
        FIELD(self, 0x3C, LONG) = 0;
        FIELD(self, CTL_RECT, RECT) = FIELD(self, 0x3C, RECT);
    }
    FIELD(self, CTL_STATE, DWORD) = 0;
    FIELD(self, 0x60, DWORD) = 0;
    FIELD(self, 0x68, DWORD) = 0;
    FIELD(self, 0x6C, DWORD) = 1;
    FIELD(self, 0x70, DWORD) = 1;
    FIELD(self, CTL_ID, DWORD) = 0;
    FIELD(self, 0x7C, DWORD) = 0;
    FIELD(self, CTL_DIALOG, BYTE*) = dialog;
    FIELD(self, CTL_MANAGER, void*) = dialog ? FIELD(dialog, DLG_MANAGER, void*) : NULL;
    FIELD(self, 0x18C, DWORD) = 0;
    FIELD(self, 0x190, DWORD) = 0;
    FIELD(self, 0xA0, DWORD) = 0;
    FIELD(self, 0xA4, DWORD) = 0;
    FIELD(self, 0x1B4, DWORD) = 0;
    FIELD(self, CTL_SLOT, int) = -1;
    FIELD(self, 0x1BC, DWORD) = 0;
    FIELD(self, CTL_TEXT, char*) = NULL;
    FIELD(self, 0xF0, DWORD) = 0;
    FIELD(self, CTL_COLOUR, COLORREF) = 0xFFFF;
    FIELD(self, 0xCC, COLORREF) = 0xFFFF;
    FIELD(self, CTL_SHADOW, DWORD) = 1;
    FIELD(self, CTL_SHADOW_COLOUR, COLORREF) = 0;
    FIELD(self, CTL_ALIGN, DWORD) = 1;
    FIELD(self, CTL_LENGTH, int) = 0;
    FIELD(self, CTL_RUNS, void*) = NULL;
    FIELD(self, 0x1C0, DWORD) = 1;
    FIELD(self, 0x168, DWORD) = 0;
    FIELD(self, 0x1D8, DWORD) = 0;
    FIELD(self, 0x1C8, DWORD) = 0;
    FIELD(self, CTL_TABS, void*) = NULL;
    FIELD(self, 0x180, DWORD) = 0;
    for (i = 0; i < 5; i++) {
        FIELD(self, 0x16C + 4 * i, DWORD) = 0;
        FIELD(self, 0x194 + 4 * i, DWORD) = 0;
    }
    FIELD(self, 0x164, DWORD) = 0;
    FIELD(self, 0xAC, DWORD) = 0;
    FIELD(self, 0xA8, DWORD) = 0;
    FIELD(self, 0x1A8, DWORD) = 0;
    FIELD(self, 0x188, DWORD) = 0;
    FIELD(self, 0x184, DWORD) = 0;
    FIELD(self, 0x1B0, DWORD) = 0;
    FIELD(self, 0x1AC, DWORD) = 0;
    FIELD(self, CTL_NAME, char) = 0;
    FIELD(self, 0x12C, DWORD) = 0;
    FIELD(self, CTL_FONT, HFONT) = NULL;
    FIELD(self, 0x130, char) = 0;
    FIELD(self, 0x160, DWORD) = 0;
    FIELD(self, 0x158, DWORD) = 0;
    FIELD(self, 0x15C, DWORD) = 0;
    FIELD(self, 0xEC, DWORD) = 0;
    FIELD(self, 0xE8, DWORD) = 0;
    FIELD(self, 0x1CC, DWORD) = 0;
    FIELD(self, 0x1D0, void*) = NULL;
    FIELD(self, CTL_ANIMATION, DWORD) = 0;
    FIELD(self, 0x1D4, int) = -1;
    if (dialog) {
        FIELD(self, 0x1DC, DWORD) = FIELD(dialog, 0x7F18, DWORD);
        FIELD(self, 0x18, void*) = object_screen(dialog);
    } else {
        FIELD(self, 0x1DC, DWORD) = 1;
    }
    FIELD(self, 0x1E0, DWORD) = 0;
    FIELD(self, 0x1E8, DWORD) = 0;
    return self;
}

static void heap_free(void* p, int line)
{
    if (p)
        game_FUN_004a4450(p, GAMEUI_CPP, line);
}

/*
    The control is marked dying (bit 3 of byte 0x25) so focus does not come back to it. A user
    holding it in capture or focus lets go, and then its tooltip is hidden.
*/
PORT(0x00471380)
void __fastcall control_destruct(BYTE* self)
{
    BYTE* dialog;

    *(void***) self = UICONTROL_VTABLE;
    dialog = FIELD(self, CTL_DIALOG, BYTE*);
    if (dialog && !(FIELD(game_main, 0x2D5, BYTE) & 2)) {
        gui_user* user = FIELD(dialog, DLG_USER, gui_user*);
        tooltip* tip = NULL;

        FIELD(self, 0x25, BYTE) |= 8;
        if (user) {
            if (user->capture == self) {
                gui_user_release_capture(user, NULL, NULL);
                user = FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_USER, gui_user*);
                tip = (tooltip*) user->tooltip;
            }
            if (user && user->focus == self) {
                dialog_queue_focus(FIELD(self, CTL_DIALOG, BYTE*), NULL, NULL, NULL, 1);
                tip = (tooltip*) FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_USER, gui_user*)->tooltip;
            }
        }
        if (tip)
            tooltip_set_text(tip, NULL, NULL, NULL, 1.0f);
    }
    dialog = FIELD(self, CTL_DIALOG, BYTE*);
    if (FIELD(self, CTL_SLOT, int) != -1 && dialog && FIELD(dialog, DLG_BACKGROUND, BYTE*))
        background_free_slot(FIELD(dialog, DLG_BACKGROUND, BYTE*), NULL, FIELD(self, CTL_SLOT, int));
    heap_free(FIELD(self, CTL_TEXT, void*), 0xBA3);
    heap_free(FIELD(self, 0xF0, void*), 0xBA4);
    if (FIELD(self, 0x12C, int))
        DeleteObject(FIELD(self, CTL_FONT, HFONT));
    heap_free(FIELD(self, CTL_RUNS, void*), 0xBA8);
    heap_free(FIELD(self, CTL_TABS, void*), 0xBA9);
    object_destruct(self);
}

PORT(0x00471360)
BYTE* __fastcall control_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    control_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* The text slots (0x88 to 0x94): text, length, runs, and the measured extent. */
PORT(0x004712F0)
const char* __fastcall control_get_text(BYTE* self)
{
    return FIELD(self, CTL_TEXT, const char*);
}

PORT(0x00471300)
int __fastcall control_get_length(BYTE* self)
{
    return FIELD(self, CTL_LENGTH, int);
}

PORT(0x00471310)
text_run* __fastcall control_get_runs(BYTE* self)
{
    return FIELD(self, CTL_RUNS, text_run*);
}

PORT(0x00471320)
int* __fastcall control_get_extent(BYTE* self)
{
    return &FIELD(self, CTL_EXTENT, int);
}

/* Vtable slot 52. */
PORT(0x00471330)
void __fastcall control_set_7c(BYTE* self, void* edx_unused, DWORD value)
{
    (void) edx_unused;
    FIELD(self, 0x7C, DWORD) = value;
}

/* Vtable slots 61 and 62: its width and height. */
PORT(0x00471340)
int __fastcall control_width(BYTE* self)
{
    return FIELD(self, CTL_RECT, RECT).right - FIELD(self, CTL_RECT, RECT).left;
}

PORT(0x00471350)
int __fastcall control_height(BYTE* self)
{
    return FIELD(self, CTL_RECT, RECT).bottom - FIELD(self, CTL_RECT, RECT).top;
}

PORT(0x00471C70)
int __fastcall control_set_colour(BYTE* self, void* edx_unused, COLORREF colour)
{
    (void) edx_unused;
    FIELD(self, CTL_COLOUR, COLORREF) = colour;
    return control_redraw(self);
}

PORT(0x00471C90)
int __fastcall control_set_shadow(BYTE* self, void* edx_unused, BOOL shadow)
{
    (void) edx_unused;
    FIELD(self, CTL_SHADOW, BOOL) = shadow;
    return control_redraw(self);
}

PORT(0x00471CB0)
int __fastcall control_set_shadow_colour(BYTE* self, void* edx_unused, COLORREF colour)
{
    (void) edx_unused;
    FIELD(self, CTL_SHADOW_COLOUR, COLORREF) = colour;
    return control_redraw(self);
}

PORT(0x00471CD0)
int __fastcall control_set_align(BYTE* self, void* edx_unused, DWORD align)
{
    (void) edx_unused;
    FIELD(self, CTL_ALIGN, DWORD) = align;
    return control_redraw(self);
}

/* Its name, up to 49 characters. */
PORT(0x00471CF0)
void __fastcall control_set_name(BYTE* self, void* edx_unused, const char* name)
{
    (void) edx_unused;
    game__strncpy((char*) self + CTL_NAME, name, 0x31);
    FIELD(self, CTL_NAME + 0x31, char) = 0;
}

PORT(0x00471D20)
const char* __fastcall control_get_name(BYTE* self)
{
    return (const char*) self + CTL_NAME;
}

/*
    Vtable slot 49: enable or disable. Disabled, a control with a disabled look (0x17C) shows
    state 4, and its text goes grey (its colour is kept at 0xCC for when it is enabled again).
*/
PORT(0x00471500)
void __fastcall control_enable(BYTE* self, void* edx_unused, BOOL enabled)
{
    (void) edx_unused;
    if (FIELD(self, 0x6C, BOOL) == enabled)
        return;
    FIELD(self, 0x6C, BOOL) = enabled;
    if (enabled) {
        VIRTUAL(self, 0x74, method_1)(self, NULL, FIELD(self, CTL_STATE, DWORD));
        control_set_colour(self, NULL, FIELD(self, 0xCC, COLORREF));
        return;
    }
    if (FIELD(self, 0x17C, int)) {
        FIELD(self, CTL_STATE, DWORD) = FIELD(self, 0x60, DWORD);
        VIRTUAL(self, 0x74, method_1)(self, NULL, 4);
    }
    FIELD(self, 0xCC, COLORREF) = FIELD(self, CTL_COLOUR, COLORREF);
    control_set_colour(self, NULL, RGB(0x99, 0x99, 0x99));
}

/* Vtable slot 50. */
PORT(0x00471570)
void __fastcall control_slot_50(BYTE* self)
{
    FIELD(self, 0x1C0, DWORD) = 0;
    FIELD(self, 0x1BC, DWORD) = 3;
}

/*
    Show or hide the control. With an animation, showing starts it (106 swaps to the routine at
    0x0047C5A0), and hiding a type 106 one with `animate`, on a dialog that animates (0xC8), runs
    the routine at 0x0047C540 instead of hiding at once.
*/
PORT(0x00471590)
void __fastcall control_show(BYTE* self, void* edx_unused, DWORD shown, BOOL animate)
{
    (void) edx_unused;
    if (FIELD(self, 0x1D0, void*)) {
        if (shown) {
            FIELD(self, 0x68, DWORD) = 1;
            if (FIELD(self, CTL_ANIMATION, int) == 106)
                FIELD(self, 0x1D0, void*) = (void*) animate_106;
            FIELD(self, 0x1CC, DWORD) = 0;
            FIELD(self, 0x70, DWORD) = shown;
            return;
        }
        if (FIELD(self, CTL_ANIMATION, int) == 106 && animate
            && FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0xC8, int)) {
            FIELD(self, 0x1CC, DWORD) = 0;
            FIELD(self, 0x68, DWORD) = 2;
            FIELD(self, 0x1D0, void*) = (void*) animate_hide;
            return;
        }
        FIELD(self, 0x68, DWORD) = 2;
        FIELD(self, 0x70, DWORD) = 0;
        control_redraw(self);
        FIELD(self, 0x1CC, DWORD) = 0;
        return;
    }
    FIELD(self, 0x68, DWORD) = 0;
    FIELD(self, 0x70, DWORD) = shown;
    control_redraw(self);
}

PORT(0x00471650)
BOOL __fastcall control_enabled(BYTE* self)
{
    return FIELD(self, 0x6C, BOOL);
}

/* The picture for state `state` (1 is also kept at 0x178). */
PORT(0x00471660)
int __fastcall control_set_picture(BYTE* self, void* edx_unused, int state, DWORD picture)
{
    (void) edx_unused;
    FIELD(self, 0x16C + 4 * state, DWORD) = picture;
    if (state == 1)
        FIELD(self, 0x178, DWORD) = picture;
    return control_redraw(self);
}

/* Load the picture `file` for state `state`, through the dialog's picture settings. */
PORT(0x00471690)
int __fastcall control_load_picture(BYTE* self, void* edx_unused, int state, const char* file)
{
    BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);

    (void) edx_unused;
    if (dialog) {
        ui_anim* picture = game_new(sizeof(ui_anim), GAMEUI_CPP, 0xC10);

        if (picture)
            picture = anim_construct(picture, NULL, FIELD(dialog, 0x7F20, DWORD), 0);
        anim_add_file(picture, NULL, file, FIELD(dialog, DLG_BACKDROP, BYTE*));
        control_set_picture(self, NULL, state, (DWORD) picture);
    }
    return control_redraw(self);
}

PORT(0x00471740)
void __fastcall control_set_194(BYTE* self, void* edx_unused, int index, DWORD value)
{
    (void) edx_unused;
    FIELD(self, 0x194 + 4 * index, DWORD) = value;
}

PORT(0x00471760)
void __fastcall control_set_link(BYTE* self, void* edx_unused, BYTE* next, DWORD b)
{
    (void) edx_unused;
    FIELD(self, 0x18C, BYTE*) = next;
    FIELD(self, 0x190, DWORD) = b;
}

/* The last control in the chain from this one (0x18C), or `otherwise` when it links to none. */
PORT(0x00471780)
BYTE* __fastcall control_chain_end(BYTE* self, void* edx_unused, BYTE* otherwise)
{
    BYTE* last;
    BYTE* next = FIELD(self, 0x18C, BYTE*);

    (void) edx_unused;
    if (!next)
        return otherwise;
    do {
        last = next;
        next = FIELD(next, 0x18C, BYTE*);
    } while (next);
    return last;
}

/*
    Set the control's animation: kinds 100 to 106 pick a routine (105 none). `start` shows it at
    once; `delay` 0xFFFF means a random one of up to two seconds.
*/
PORT(0x004717A0)
int __fastcall control_set_animation(BYTE* self, void* edx_unused, int kind, DWORD a, BOOL start,
                                     DWORD delay, DWORD b)
{
    (void) edx_unused;
    if (start) {
        control_show(self, NULL, 1, 1);
        FIELD(self, 0x68, DWORD) = 1;
    }
    FIELD(self, CTL_ANIMATION, int) = kind;
    switch (kind) {
    case 100: FIELD(self, 0x1D0, void*) = (void*) animate_100; break;
    case 101: FIELD(self, 0x1D0, void*) = (void*) animate_101; break;
    case 102: FIELD(self, 0x1D0, void*) = (void*) animate_102; break;
    case 103: FIELD(self, 0x1D0, void*) = (void*) animate_103; break;
    case 104: FIELD(self, 0x1D0, void*) = (void*) animate_104; break;
    case 106: FIELD(self, 0x1D0, void*) = (void*) animate_106; break;
    default:  FIELD(self, 0x1D0, void*) = NULL; break;
    }
    FIELD(self, 0xA8, DWORD) = 0;
    FIELD(self, 0xB0, DWORD) = b;
    FIELD(self, 0xAC, DWORD) = delay == 0xFFFF ? (DWORD) (game__rand() % 2000) : delay;
    FIELD(self, 0x1A8, DWORD) = a;
    return control_redraw(self);
}

/* Vtable slot 29: the state shown. */
PORT(0x004718A0)
int __fastcall control_set_state(BYTE* self, void* edx_unused, DWORD state)
{
    (void) edx_unused;
    FIELD(self, 0x60, DWORD) = state;
    return control_redraw(self);
}

/* Vtable slot 28: whether (x, y) is in its rectangle. */
PORT(0x004718C0)
BOOL __fastcall control_contains(BYTE* self, void* edx_unused, LONG x, LONG y, DWORD unused)
{
    RECT area = FIELD(self, CTL_RECT, RECT);
    POINT at;

    (void) edx_unused;
    (void) unused;
    at.x = x;
    at.y = y;
    return PtInRect(&area, at);
}

PORT(0x00471900)
int __fastcall control_set_a0(BYTE* self, void* edx_unused, DWORD value)
{
    (void) edx_unused;
    FIELD(self, 0xA0, DWORD) = value;
    return control_redraw(self);
}

/* Vtable slot 51: hit by (x, y): its rectangle, or its own test (slot 0x70) when 0xA0 is set. */
PORT(0x00471920)
BOOL __fastcall control_hit(BYTE* self, void* edx_unused, LONG x, LONG y)
{
    RECT area = FIELD(self, CTL_RECT, RECT);

    (void) edx_unused;
    if (!FIELD(self, 0xA0, int)) {
        POINT at;

        at.x = x;
        at.y = y;
        return PtInRect(&area, at) ? TRUE : FALSE;
    }
    return VIRTUAL(self, 0x70, int (__fastcall*)(void*, void*, LONG, LONG, DWORD))(
               self, NULL, x, y, FIELD(self, 0x60, DWORD)) ? TRUE : FALSE;
}

/* Vtable slot 54. */
PORT(0x004719A0)
void __fastcall control_set_1b4(BYTE* self, void* edx_unused, DWORD value)
{
    (void) edx_unused;
    FIELD(self, 0x1B4, DWORD) = value;
}

int __fastcall control_set_text(BYTE* self, void* edx_unused, const char* text);

/* The text from string resource `id` of `module`. */
PORT(0x004719B0)
int __fastcall control_load_text(BYTE* self, void* edx_unused, HINSTANCE module, UINT id)
{
    char text[0x400];

    (void) edx_unused;
    if (LoadStringA(module, id, text, sizeof text)) {
        if (FIELD(self, CTL_TYPE, int) == 11)
            edit_set_text(self, NULL, text);
        else
            control_set_text(self, NULL, text);
    } else {
        game_FUN_0053a11f(text, RESOURCE_FAILED, id);
        if (FIELD(self, CTL_TYPE, int) == 11)
            edit_set_text(self, NULL, text);
        else
            control_set_text(self, NULL, RESOURCE_UNAVAILABLE);
    }
    return control_redraw(self);
}

/*
    The control's text: copied to the game's heap and split into runs at its markup; without
    markup it is measured in the control's font (or its dialog's). NULL clears it. A text box
    (type 11) keeps its own.
*/
PORT(0x00471A50)
int __fastcall control_set_text(BYTE* self, void* edx_unused, const char* text)
{
    char* copy;
    void* surface;
    HDC dc;
    SIZE size;

    (void) edx_unused;
    if (!text) {
        heap_free(FIELD(self, CTL_TEXT, void*), 0xCDE);
        heap_free(FIELD(self, CTL_RUNS, void*), 0xCDF);
        FIELD(self, CTL_TEXT, char*) = NULL;
        FIELD(self, CTL_RUNS, void*) = NULL;
        return 0;
    }
    if (FIELD(self, CTL_TYPE, int) == 11) {
        edit_set_text(self, NULL, text);
        return control_redraw(self);
    }
    heap_free(FIELD(self, CTL_TEXT, void*), 0xCC2);
    heap_free(FIELD(self, CTL_RUNS, void*), 0xCC3);
    copy = game_FUN_004a4410(text_length(text) + 1, GAMEUI_CPP, 0xCC4);
    FIELD(self, CTL_TEXT, char*) = copy;
    text_copy(copy, text);
    FIELD(self, CTL_RUNS, text_run*) = dialog_parse_runs(FIELD(self, CTL_DIALOG, BYTE*), NULL,
                                                         FIELD(self, CTL_TEXT, char*),
                                                         FIELD(self, CTL_COLOUR, COLORREF),
                                                         FIELD(self, CTL_FONT, HFONT),
                                                         FIELD(self, CTL_TABS, int*),
                                                         &FIELD(self, CTL_EXTENT, int));
    if (FIELD(self, CTL_RUNS, text_run*))
        return control_redraw(self);

    FIELD(self, CTL_EXTENT, int) = (int) text_length(FIELD(self, CTL_TEXT, char*));
    FIELD(self, CTL_LENGTH, int) = FIELD(self, CTL_EXTENT, int);
    surface = FIELD(FIELD(game_main, 0x10, BYTE*), 0x48, void*);
    if (!VIRTUAL(surface, 0x44, HRESULT (__stdcall*)(void*, HDC*))(surface, &dc)) {
        HFONT font = FIELD(self, CTL_FONT, HFONT);
        HGDIOBJ old;

        if (!font)
            font = FIELD(self, CTL_DIALOG, BYTE*) ? FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_FONT, HFONT)
                                                  : NULL;
        old = SelectObject(dc, font);
        GetTextExtentPoint32A(dc, FIELD(self, CTL_TEXT, char*), FIELD(self, CTL_LENGTH, int), &size);
        SelectObject(dc, old);
        surface = FIELD(FIELD(game_main, 0x10, BYTE*), 0x48, void*);
        VIRTUAL(surface, 0x68, HRESULT (__stdcall*)(void*, HDC))(surface, dc);
        FIELD(self, CTL_EXTENT + 4, int) = size.cx;
        return control_redraw(self);
    }
    FIELD(self, CTL_EXTENT + 4, int) = 0;
    return control_redraw(self);
}

/* ---- UIControl: every frame and drawing --------------------------------------------------- */

/* The last control of this one's chain is still on its way in (shown but not settled). */
static BOOL chain_settled(BYTE* last)
{
    return !last || (FIELD(last, 0x70, int) && !FIELD(last, 0x68, int));
}

ui_frame* __fastcall anim_step(ui_anim* self);
int __fastcall control_animate(BYTE* self);

#define screen_width()  FIELD(FIELD(game_main, 0x10, BYTE*), 0xC, LONG)
#define screen_height() FIELD(FIELD(game_main, 0x10, BYTE*), 0x10, LONG)

/*
    Vtable slot 10, the frame's tick: run the control's own update (slot 0x6C), then its animation
    step unless it is still moving in (0x00472430), and keep its rectangle on the screen.
*/
PORT(0x00471D30)
int __fastcall control_tick(BYTE* self, void* edx_unused, DWORD time)
{
    RECT* area = &FIELD(self, CTL_RECT, RECT);

    (void) edx_unused;
    VIRTUAL(self, 0x6C, method_0)(self);
    if (!FIELD(self, 0x70, int)
        || !chain_settled(control_chain_end(self, NULL, NULL))
        || !control_animate(self)) {
        gui_input* input;

        FIELD(self, 0x1CC, DWORD) = VIRTUAL(self, 0xC0, method_1)(self, NULL, (DWORD) -1);
        VIRTUAL(self, 0x98, method_0)(self);
        VIRTUAL(self, 0x9C, method_0)(self);
        input = FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_USER, gui_user*)->input;
        if (input && FIELD(self, 0x1D4, int) != -1) {
            POINT at;

            input = FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_USER, gui_user*)->input;
            at.x = input->x;
            at.y = input->y;
            VIRTUAL(self, 0xE4, int (__fastcall*)(void*, void*, int, POINT*))(
                self, NULL, FIELD(self, 0x1D4, int), &at);
        }
    }
    if (area->left < 0)
        area->left = 0;
    if (area->top < 0)
        area->top = 0;
    if (screen_width() < area->right)
        area->right = screen_width();
    if (screen_height() < area->bottom)
        area->bottom = screen_height();
    return game_vf_10_46A300(self, NULL, time);
}

#ifdef OPENMCM2_DEV
/*
    The modern menus' pictures are made at ArtScale times a layout's units, but the game's own -
    a track's or a bike's, set by the code at run time - are one pixel to a unit. A control draws
    the part of its picture its layout's size covers, so for the game's pictures in a modern
    dialog that part is taken ArtScale times smaller. The modern pictures are known by the folder
    they are loaded from, and the dialogs by the ArtScale their layout gave.
*/
#define MODERN_MOST 2048

static void* modern_pictures[MODERN_MOST] = { 0 };
static int modern_count = 0;

static struct {
    BYTE* dialog;
    int art;
    RECT view;              /* its View, in its layout's pixels; empty when it gives none */
    BOOL reborn;            /* a MCM2:Reborn layout, its words in its pictures */
} dialog_arts[64] = { 0 };

static void modern_mark(const char* name, void* picture)
{
    if (picture && modern_count < MODERN_MOST
        && (game_FUN_0053b910(name, "_Modern\\") || game_FUN_0053b910(name, "_modern\\"))) {
        modern_pictures[modern_count++] = picture;
        render_inks(FIELD(picture, 0x70, void*));          /* the picture's surface */
    }
}

static void modern_forget(void* picture)
{
    int i;

    for (i = 0; i < modern_count; i++)
        if (modern_pictures[i] == picture) {
            modern_pictures[i] = modern_pictures[--modern_count];
            return;
        }
}

static BOOL modern_marked(void* picture)
{
    int i;

    for (i = 0; i < modern_count; i++)
        if (modern_pictures[i] == picture)
            return TRUE;
    return FALSE;
}

/* A dialog's ArtScale, View and Reborn, kept each time a layout is read: a dialog's address is
   reused. */
static void dialog_art_set(BYTE* dialog, int art, const RECT* view, BOOL reborn)
{
    int i, free = -1;

    for (i = 0; i < 64; i++) {
        if (dialog_arts[i].dialog == dialog)
            break;
        if (free < 0 && dialog_arts[i].art <= 1 && !dialog_arts[i].reborn)
            free = i;
    }
    if (i == 64) {
        if ((art <= 1 && !reborn) || free < 0)      /* nothing to keep */
            return;
        i = free;
        dialog_arts[i].dialog = dialog;
    }
    dialog_arts[i].art = art;
    dialog_arts[i].view = *view;
    dialog_arts[i].reborn = reborn;
}

BOOL menu_reborn(BYTE* dialog)
{
    int i;

    for (i = 0; i < 64; i++)
        if (dialog_arts[i].dialog == dialog)
            return dialog_arts[i].reborn;
    return FALSE;
}

static int dialog_art(BYTE* dialog)
{
    int i;

    for (i = 0; i < 64; i++)
        if (dialog_arts[i].dialog == dialog)
            return dialog_arts[i].art;
    return 1;
}

/*
    How many times over the game's picture `width` pixels across is drawn in `control`: its
    dialog's ArtScale, or fewer when that would not fit across the control (in its layout's
    pixels), so a track's picture fits a box smaller than three times its size.
*/
static int game_picture_art(BYTE* control, int width, int margin)
{
    BYTE* dialog = FIELD(control, CTL_DIALOG, BYTE*);
    int art = dialog_art(dialog);
    int across = (int) ((FIELD(control, CTL_RECT, RECT).right - FIELD(control, CTL_RECT, RECT).left
                         - 2 * margin) / FIELD(dialog, 0x9C4, float) + 0.5f);

    if (width > 0 && across / width < art)
        art = across / width > 1 ? across / width : 1;
    return art;
}

/* The part of `picture` the control draws (its 0x4C), in the picture's own pixels. */
static RECT* control_source(BYTE* self, void* picture)
{
    static RECT scaled;
    RECT* source = &FIELD(self, 0x4C, RECT);
    int art = dialog_art(FIELD(self, CTL_DIALOG, BYTE*));

    if (art <= 1 || modern_marked(picture))
        return source;
    art = game_picture_art(self, FIELD(picture, 0x14, int), 0);
    scaled.left = source->left / art;
    scaled.top = source->top / art;
    scaled.right = source->right / art;
    scaled.bottom = source->bottom / art;
    return &scaled;
}
#define CONTROL_SOURCE(self, picture) control_source(self, picture)
#else
#define CONTROL_SOURCE(self, picture) (&FIELD(self, 0x4C, RECT))
#endif

/*
    Vtable slot 39: lay the control out from its design rectangle (0x3C), scaled by the dialog
    and placed relative to the control it links to, or to the dialog. The parts off the screen
    are clipped: 0x4C is the offset into it, 0x54 by 0x58 the size left.
*/
PORT(0x00471E40)
void __fastcall control_layout(BYTE* self)
{
    BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);
    RECT* area = &FIELD(self, CTL_RECT, RECT);
    RECT design = FIELD(self, 0x3C, RECT);
    BYTE* anchor = FIELD(self, 0x18C, BYTE*);
    int width = design.right - design.left;
    int height = design.bottom - design.top;
    int over;

    /* An int times a float is exact in double here, as in the x87's extended precision. */
    area->left = (int) ((double) design.left * FIELD(dialog, 0x9C4, float));
    area->top = (int) ((double) design.top * FIELD(dialog, 0x9C8, float));
    area->right = (int) ((double) design.right * FIELD(dialog, 0x9C4, float));
    area->bottom = (int) ((double) design.bottom * FIELD(dialog, 0x9C8, float));
    if (anchor && FIELD(self, 0x190, int)) {
        area->top += FIELD(anchor, CTL_RECT + 4, LONG);
        area->bottom += FIELD(anchor, CTL_RECT + 4, LONG);
        area->left += FIELD(anchor, CTL_RECT, LONG);
        area->right += FIELD(anchor, CTL_RECT, LONG);
    } else {
        area->top += FIELD(dialog, 0x164, LONG);
        area->bottom += FIELD(dialog, 0x164, LONG);
        area->left += FIELD(dialog, 0x160, LONG);
        area->right += FIELD(dialog, 0x160, LONG);
    }
    FIELD(self, 0x4C, LONG) = area->left < 0 ? -area->left : 0;
    FIELD(self, 0x50, LONG) = area->top < 0 ? -area->top : 0;
    over = area->right - screen_width();
    if (over > 0)
        width -= over;
    FIELD(self, 0x54, int) = width;
    over = area->bottom - screen_height();
    if (over > 0)
        height -= over;
    FIELD(self, 0x58, int) = height;
}

/* Vtable slot 27: step the pictures, the state's and, while it animates, the moving one. */
PORT(0x00471FA0)
void __fastcall control_step_pictures(BYTE* self)
{
    void* picture = FIELD(self, 0x16C + 4 * FIELD(self, 0x60, int), void*);
    int moving;

    if (picture)
        anim_step(picture);
    moving = FIELD(self, 0x68, int);
    if (moving == 1 || moving == 2) {
        if (FIELD(self, 0x184, void*))
            anim_step(FIELD(self, 0x184, void*));
        if (FIELD(self, 0x188, int))
            anim_step(FIELD(self, 0x184, void*));
    }
}

/*
    Vtable slot 38: whether the control must be drawn again (0x1C0). A settled one asks its
    picture; otherwise it counts down its redraws (0x1BC).
*/
PORT(0x00471FF0)
void __fastcall control_needs_redraw(BYTE* self)
{
    BYTE* last = control_chain_end(self, NULL, NULL);

    if (FIELD(self, 0x70, int) && chain_settled(last)) {
        BYTE* picture = FIELD(self, 0x16C + 4 * FIELD(self, 0x60, int), BYTE*);

        if (!picture)
            FIELD(self, 0x1C0, DWORD) = 1;
        else
            FIELD(self, 0x1C0, DWORD) = FIELD(picture, 0x10, int) == 1 ? 1 : 0;
        return;
    }
    if (--FIELD(self, 0x1BC, int) < 0) {
        FIELD(self, 0x1BC, int) = 0;
        FIELD(self, 0x1C0, DWORD) = 1;
    }
}

/* Vtable slot 13: draw over; a settled, shown control not drawn by the background (0x1DC) draws
   itself through slot 0xA0. */
PORT(0x00472070)
int __fastcall control_draw_over(BYTE* self)
{
    int result = game_vf_13_46A260(self);

    if (chain_settled(control_chain_end(self, NULL, NULL)) && !FIELD(self, 0x1DC, int)) {
        if (FIELD(self, 0x70, int))
            return VIRTUAL(self, 0xA0, method_0)(self);
        FIELD(self, 0x1C0, DWORD) = 1;
    }
    return result;
}

/* Vtable slot 15: draw; then a hidden or unsettled control's background slot is cleared. */
PORT(0x004720D0)
int __fastcall control_draw(BYTE* self)
{
    int result = game_vf_15_46A2D0(self);
    BYTE* last = control_chain_end(self, NULL, NULL);
    BYTE* background;

    if (chain_settled(last) && FIELD(self, 0x1DC, int)) {
        if (FIELD(self, 0x70, int))
            result = VIRTUAL(self, 0xA0, method_0)(self);
        else
            FIELD(self, 0x1C0, DWORD) = 1;
    }
    background = FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_BACKGROUND, BYTE*);
    if (background && FIELD(self, CTL_SLOT, int) > -1
        && (!FIELD(self, 0x70, int)
            || (last && (!FIELD(last, 0x70, int) || FIELD(last, 0x68, int)))))
        background_slot_clear(background, NULL, FIELD(self, CTL_SLOT, int));
    return result;
}

typedef void (__fastcall* control_area)(void* self, void* edx_unused, RECT* from, RECT* to);

/*
    Vtable slot 40: render. The control's picture first (0x1CC), through the background or the
    screen; then its text, in as many passes as the background asks for: with DrawText (wrapped,
    placed by its alignment) when 0x1E8 is set, else as the dialog draws text.
*/
PORT(0x00472170)
int __fastcall control_render(BYTE* self)
{
    BYTE* picture = FIELD(self, 0x1CC, BYTE*);
    int redraws = FIELD(self, 0x1BC, int);
    int kept = -1;
    RECT area;
    HDC dc;
    DWORD more;

    if (picture) {
        BYTE* background;

        VIRTUAL(self, 0xFC, control_area)(self, NULL, &FIELD(self, CTL_RECT, RECT), &area);
        background = FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_BACKGROUND, BYTE*);
        if (background) {
            picture = FIELD(self, 0x1CC, BYTE*);
            background_draw(background, NULL, picture, &area, CONTROL_SOURCE(self, picture),
                            FIELD(picture, 0x30, int) ? 0x1008000 : 0x1000000,
                            FIELD(self, CTL_SLOT, int), FIELD(self, 0x1C0, DWORD),
                            &FIELD(self, 0x1BC, int), 0);
            kept = FIELD(self, 0x1BC, int);
        } else {
            DWORD flags;

            picture = FIELD(self, 0x1CC, BYTE*);
            flags = FIELD(picture, 0x30, int) ? 0x1008000 : 0x1000000;
            if (!VIRTUAL(object_screen(self), 0xC, screen_draw)(object_screen(self), NULL, &area,
                                                                picture,
                                                                CONTROL_SOURCE(self, picture), flags))
                return 0;
        }
    }

    if (FIELD(self, CTL_TEXT, char*) && FIELD(self, CTL_LENGTH, int)) {
        BYTE* dialog;

        FIELD(self, 0x1BC, int) = redraws;
        VIRTUAL(self, 0x100, control_area)(self, NULL, &FIELD(self, CTL_RECT, RECT), &area);
        dialog = FIELD(self, CTL_DIALOG, BYTE*);
        if (dialog_begin_paint(dialog, NULL, &dc, &area, &more, self)) {
            do {
                if (FIELD(self, 0x1E8, int)) {
                    DWORD align = FIELD(self, CTL_ALIGN, DWORD);
                    UINT format = DT_NOPREFIX | DT_WORDBREAK;

                    if (align & 2)
                        format = DT_NOPREFIX | DT_WORDBREAK | DT_CENTER;
                    else if (!(align & 1) && (align & 4))
                        format = DT_NOPREFIX | DT_WORDBREAK | DT_RIGHT;
                    SetBkMode(dc, TRANSPARENT);
                    if (FIELD(self, CTL_SHADOW, int)) {
                        area.left--;
                        area.top--;
                        area.right--;
                        area.bottom--;
                        SetTextColor(dc, FIELD(self, CTL_SHADOW_COLOUR, COLORREF));
                        DrawTextA(dc, FIELD(self, CTL_TEXT, char*), FIELD(self, CTL_LENGTH, int),
                                  &area, format);
                        area.left++;
                        area.top++;
                        area.right++;
                        area.bottom++;
                    }
                    SetTextColor(dc, FIELD(self, CTL_COLOUR, COLORREF));
                    DrawTextA(dc, FIELD(self, CTL_TEXT, char*), FIELD(self, CTL_LENGTH, int), &area,
                              format);
                } else {
                    dialog_draw_text(FIELD(self, CTL_DIALOG, BYTE*), NULL, dc, &area,
                                     FIELD(self, CTL_TEXT, char*), FIELD(self, CTL_LENGTH, int),
                                     FIELD(self, CTL_COLOUR, COLORREF), FIELD(self, 0x160, int),
                                     FIELD(self, CTL_RUNS, text_run*), FIELD(self, CTL_SHADOW, BOOL),
                                     FIELD(self, CTL_SHADOW_COLOUR, COLORREF),
                                     FIELD(self, CTL_ALIGN, DWORD), TRUE, NULL);
                }
                dialog_end_paint(FIELD(self, CTL_DIALOG, BYTE*), NULL, dc);
            } while (more
                     && dialog_begin_paint(FIELD(self, CTL_DIALOG, BYTE*), NULL, &dc, &area, &more,
                                           self));
        }
        if (kept >= 0)
            FIELD(self, 0x1BC, int) = kept;
    }
    return 1;
}

/* ---- UIControl: animations ---------------------------------------------------------------- */

/*
    An animation moves the control from a start box to its design rectangle (0x3C) in a number of
    steps (0xB0, or a default), counting at 0x164. 0x80 to 0x8C is the box now (x, y, width,
    height, as floats) and 0x90 to 0x9C its step. Each returns 1 when done, 0 while running.

    The arithmetic follows the original's x87 code, which runs at the C runtime's 53-bit
    precision (OpenMCM2's renderer never lowers it): there, as here, it is double arithmetic.
*/
#define ANIM_STEP   0x164
#define ANIM_X      0x80
#define ANIM_Y      0x84
#define ANIM_W      0x88
#define ANIM_H      0x8C
#define ANIM_DX     0x90
#define ANIM_DY     0x94
#define ANIM_DW     0x98
#define ANIM_DH     0x9C

/* Each animation's step count, while it runs, in the game's data. */
#define zoom_steps    (*(int*) 0x00661754)
#define left_steps    (*(int*) 0x00661758)
#define top_steps     (*(int*) 0x0066175C)
#define right_steps   (*(int*) 0x00661760)
#define bottom_steps  (*(int*) 0x00661764)

void  __fastcall game_FUN_004c1330(void* sound, void* edx_unused, DWORD volume, DWORD a);
void  __fastcall game_FUN_004c0e00(void* sound, void* edx_unused, DWORD a, DWORD b, DWORD c);
BYTE* __fastcall anim_picture(ui_anim* self);
void __fastcall anim_restart(ui_anim* self);

/* Play `sound` at the manager's volume (0x34C). */
static void play_sound(BYTE* self, void* sound)
{
    game_FUN_004c1330(sound, NULL, FIELD(FIELD(self, CTL_MANAGER, BYTE*), 0x34C, DWORD), 0);
    game_FUN_004c0e00(sound, NULL, 1, 0, 0);
}

/* The steps an animation takes: 0xB0, or `fallback` when that is negative. */
static int animation_steps(BYTE* self, int fallback)
{
    int steps = FIELD(self, 0xB0, int);

    return steps < 0 ? fallback : steps;
}

/* Which control types show their picture's frame while moving in: by type, 1 to 10. */
static const BYTE zoom_shows_frame[10]  = { 1, 0, 0, 1, 1, 0, 0, 0, 1, 1 };
static const BYTE slide_shows_frame[10] = { 1, 1, 0, 1, 1, 0, 0, 0, 1, 1 };

/*
    Draw the frame of the state's picture while moving, for the types that do; for the others,
    and any other type, stop with 1.
*/
static int animation_frame(BYTE* self, const BYTE* shows)
{
    int type = FIELD(self, CTL_TYPE, int);

    if (type < 1 || type > 10 || !shows[type - 1]) {
        FIELD(self, 0x1CC, void*) = NULL;
        return 1;
    }
    FIELD(self, 0x1CC, void*) = anim_picture(FIELD(self, 0x16C + 4 * FIELD(self, 0x60, int), void*));
    FIELD(self, ANIM_STEP, int)++;
    return 0;
}

/* The box toward the design rectangle: x, y, width, height. */
static void animation_place(BYTE* self)
{
    RECT* area = &FIELD(self, CTL_RECT, RECT);

    area->left = (int) FIELD(self, ANIM_X, float);
    area->top = (int) FIELD(self, ANIM_Y, float);
    area->right = (int) ((double) FIELD(self, ANIM_W, float) + FIELD(self, ANIM_X, float));
    area->bottom = (int) ((double) FIELD(self, ANIM_Y, float) + FIELD(self, ANIM_H, float));
}

/* Vtable slot 41 (kind 100): zoom out from a 21 by 11 box at the control's centre. */
PORT(0x00472500)
int __fastcall control_zoom_in(BYTE* self)
{
    if (!FIELD(self, ANIM_STEP, int) && FIELD(self, 0xB0, int) && !(FIELD(self, 0x25, BYTE) & 4)) {
        RECT design = FIELD(self, 0x3C, RECT);
        int width = design.right - design.left;
        int height = design.bottom - design.top;
        double x, y;

        zoom_steps = animation_steps(self, 7);
        FIELD(self, ANIM_W, float) = 21.0f;
        FIELD(self, ANIM_H, float) = 11.0f;
        x = (double) (design.left + width / 2) - 10.5f;
        if (0.0f > x)
            x = 0.0f;
        FIELD(self, ANIM_X, float) = (float) x;
        y = (double) (design.top + height / 2) - 5.5f;
        if (0.0f > y)
            y = 0.0f;
        FIELD(self, ANIM_Y, float) = (float) y;
        FIELD(self, ANIM_DX, float) = (float) (((double) design.left - x) / zoom_steps);
        FIELD(self, ANIM_DY, float) = (float) (((double) design.top - y) / zoom_steps);
        FIELD(self, ANIM_DW, float) = (float) (((double) width - 21.0f) / zoom_steps);
        FIELD(self, ANIM_DH, float) = (float) (((double) height - 11.0f) / zoom_steps);
        if (FIELD(self, 0x1A8, void*))
            play_sound(self, FIELD(self, 0x1A8, void*));
    } else {
        FIELD(self, ANIM_X, float) += FIELD(self, ANIM_DX, float);
        FIELD(self, ANIM_Y, float) += FIELD(self, ANIM_DY, float);
        FIELD(self, ANIM_W, float) += FIELD(self, ANIM_DW, float);
        FIELD(self, ANIM_H, float) += FIELD(self, ANIM_DH, float);
    }
    if (FIELD(self, ANIM_STEP, int) == zoom_steps)
        return 1;
    control_redraw(self);
    if ((FIELD(self, 0x25, BYTE) & 4) && !FIELD(self, ANIM_STEP, int))
        return 0;
    animation_place(self);
    return animation_frame(self, zoom_shows_frame);
}

/*
    The slides: from off one side to the design rectangle. Each keeps the design size and moves
    one coordinate; a step entirely off the screen draws nothing.
*/
static int slide_start(BYTE* self, int* steps, float from, int to, int axis_offset)
{
    *steps = animation_steps(self, 25);
    FIELD(self, axis_offset, float) = from;
    FIELD(self, axis_offset + 0x10, float) = (float) (((double) to - from) / *steps);
    if (FIELD(self, 0x1A8, void*))
        play_sound(self, FIELD(self, 0x1A8, void*));
    return 0;
}

/* Vtable slot 42 (kind 101): slide in from the left. */
PORT(0x00472780)
int __fastcall control_slide_from_left(BYTE* self)
{
    RECT* area = &FIELD(self, CTL_RECT, RECT);

    if (!FIELD(self, ANIM_STEP, int) && FIELD(self, 0xB0, int)) {
        RECT design = FIELD(self, 0x3C, RECT);

        slide_start(self, &left_steps, (float) (design.left - design.right - 1), design.left, ANIM_X);
    } else {
        FIELD(self, ANIM_X, float) += FIELD(self, ANIM_DX, float);
    }
    if (FIELD(self, ANIM_STEP, int) == left_steps)
        return 1;
    control_redraw(self);
    *area = FIELD(self, 0x3C, RECT);
    area->left = (int) FIELD(self, ANIM_X, float);
    area->right = area->left + (FIELD(self, 0x44, LONG) - FIELD(self, 0x3C, LONG));
    if (area->right <= 0) {
        FIELD(self, ANIM_STEP, int)++;
        return 0;
    }
    return animation_frame(self, slide_shows_frame);
}

/* Vtable slot 44 (kind 103): slide in from the top. */
PORT(0x004728F0)
int __fastcall control_slide_from_top(BYTE* self)
{
    RECT* area = &FIELD(self, CTL_RECT, RECT);

    if (!FIELD(self, ANIM_STEP, int) && FIELD(self, 0xB0, int)) {
        RECT design = FIELD(self, 0x3C, RECT);

        slide_start(self, &top_steps, (float) (design.top - design.bottom), design.top, ANIM_Y);
    } else {
        FIELD(self, ANIM_Y, float) += FIELD(self, ANIM_DY, float);
    }
    if (FIELD(self, ANIM_STEP, int) == top_steps)
        return 1;
    control_redraw(self);
    *area = FIELD(self, 0x3C, RECT);
    area->top = (int) FIELD(self, ANIM_Y, float);
    area->bottom = area->top + (FIELD(self, 0x48, LONG) - FIELD(self, 0x40, LONG));
    if (area->bottom <= 0) {
        FIELD(self, ANIM_STEP, int)++;
        return 0;
    }
    return animation_frame(self, slide_shows_frame);
}

/* Vtable slot 43 (kind 102): slide in from the right edge of the dialog's design size (0x174). */
PORT(0x00472A60)
int __fastcall control_slide_from_right(BYTE* self)
{
    RECT* area = &FIELD(self, CTL_RECT, RECT);
    BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);

    if (!FIELD(self, ANIM_STEP, int) && FIELD(self, 0xB0, int))
        slide_start(self, &right_steps, (float) FIELD(dialog, 0x174, int), FIELD(self, 0x3C, LONG),
                    ANIM_X);
    else
        FIELD(self, ANIM_X, float) += FIELD(self, ANIM_DX, float);
    if (FIELD(self, ANIM_STEP, int) == right_steps)
        return 1;
    control_redraw(self);
    *area = FIELD(self, 0x3C, RECT);
    area->left = (int) FIELD(self, ANIM_X, float);
    area->right = area->left + (FIELD(self, 0x44, LONG) - FIELD(self, 0x3C, LONG));
    if (area->left >= FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0x174, LONG)) {
        FIELD(self, ANIM_STEP, int)++;
        return 0;
    }
    return animation_frame(self, slide_shows_frame);
}

/* Vtable slot 45 (kind 104): slide in from the bottom of the dialog's design size (0x178). */
PORT(0x00472BC0)
int __fastcall control_slide_from_bottom(BYTE* self)
{
    RECT* area = &FIELD(self, CTL_RECT, RECT);
    BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);

    if (!FIELD(self, ANIM_STEP, int) && FIELD(self, 0xB0, int))
        slide_start(self, &bottom_steps, (float) FIELD(dialog, 0x178, int), FIELD(self, 0x40, LONG),
                    ANIM_Y);
    else
        FIELD(self, ANIM_Y, float) += FIELD(self, ANIM_DY, float);
    if (FIELD(self, ANIM_STEP, int) == bottom_steps)
        return 1;
    control_redraw(self);
    *area = FIELD(self, 0x3C, RECT);
    area->top = (int) FIELD(self, ANIM_Y, float);
    area->bottom = area->top + (FIELD(self, 0x48, LONG) - FIELD(self, 0x40, LONG));
    if (area->top >= FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0x178, LONG)) {
        FIELD(self, ANIM_STEP, int)++;
        return 0;
    }
    return animation_frame(self, slide_shows_frame);
}

/* The animations a kind 106 plays in (0x184) and out (0x188). */
#define anim_in(c)  FIELD(c, 0x184, ui_anim*)
#define anim_out(c) FIELD(c, 0x188, ui_anim*)

/* Vtable slot 46 (kind 106): play its animation in once through, with its sound (0x1AC). */
PORT(0x00472D20)
int __fastcall control_play_picture(BYTE* self)
{
    ui_anim* anim;

    if (!FIELD(self, ANIM_STEP, int)) {
        anim_in(self)->backwards = 0;
        anim_restart(anim_in(self));
        FIELD(self, ANIM_STEP, int) = 1;
        if (FIELD(self, 0x1AC, void*))
            play_sound(self, FIELD(self, 0x1AC, void*));
    }
    anim = anim_in(self);
    if (FIELD(self, ANIM_STEP, int) == 1 && anim->frame <= anim->count - 1) {
        FIELD(self, 0x1CC, void*) = anim_picture(anim);
        control_redraw(self);
        anim = anim_in(self);
        if (anim->frame == anim->count - 1)
            FIELD(self, ANIM_STEP, int) = 2;
        return 0;
    }
    FIELD(self, ANIM_STEP, int) = 0;
    return 1;
}

/*
    Vtable slot 47 (hiding a kind 106): play its animation out, backwards when it is the one
    played in; then hide the control.
*/
PORT(0x00472DE0)
int __fastcall control_play_picture_out(BYTE* self)
{
    ui_anim* anim;
    int step;

    if (!FIELD(self, ANIM_STEP, int)) {
        if (anim_out(self) == anim_in(self)) {
            anim_out(self)->backwards = 1;
            FIELD(self, ANIM_STEP, int) = 2;
        } else {
            FIELD(self, ANIM_STEP, int) = 1;
        }
        anim_restart(anim_out(self));
        if (FIELD(self, 0x1B0, void*))
            play_sound(self, FIELD(self, 0x1B0, void*));
    }
    step = FIELD(self, ANIM_STEP, int);
    anim = anim_out(self);
    if ((step == 1 && anim->frame < anim->count - 1) || (step == 2 && anim->frame >= 0)) {
        FIELD(self, 0x1CC, void*) = anim_picture(anim);
        control_redraw(self);
        if (!anim_out(self)->frame)
            FIELD(self, ANIM_STEP, int) = 3;
        return 0;
    }
    FIELD(self, ANIM_STEP, int) = 0;
    FIELD(self, 0x70, DWORD) = 0;
    return 1;
}

/* Vtable slot 48: the current frame of state `state`'s picture (-1: the state shown). */
PORT(0x00472ED0)
void* __fastcall control_frame(BYTE* self, void* edx_unused, int state)
{
    void* picture;

    (void) edx_unused;
    if (state == -1)
        state = FIELD(self, 0x60, int);
    picture = FIELD(self, 0x16C + 4 * state, void*);
    return picture ? anim_picture(picture) : NULL;
}

/*
    Run one step of the control's animation (0x1D0) once its delay (0xAC) has passed, on a
    dialog that animates. 1 while it is still running; 0 when it is done, or not animating,
    and the rectangle is back to its design.
*/
PORT(0x00472430)
int __fastcall control_animate(BYTE* self)
{
    BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);
    callback_self step = FIELD(self, 0x1D0, callback_self);
    int type = FIELD(self, CTL_TYPE, int);

    if (FIELD(dialog, 0xC8, int) && step && FIELD(self, 0x68, int) && type != 3 && type != 12) {
        FIELD(self, 0xA8, DWORD) += (int) ((double) FIELD(dialog, 0x9C0, float) * MILLISECONDS);
        if (FIELD(self, 0xA8, DWORD) < FIELD(self, 0xAC, DWORD) || !step(self))
            return 1;
        FIELD(self, ANIM_STEP, int) = 0;
        FIELD(self, 0x68, DWORD) = 0;
    } else if (FIELD(self, 0x68, int)) {
        FIELD(self, 0x68, DWORD) = 0;
        control_redraw(self);
    }
    FIELD(self, CTL_RECT, RECT) = FIELD(self, 0x3C, RECT);
    return 0;
}

/* ---- UIControl: input and focus ----------------------------------------------------------- */

BOOL __fastcall gui_user_hears_event(gui_user* self, void* edx_unused, const BYTE* event);
gui_input* __fastcall gui_user_input_for_event(gui_user* self, void* edx_unused, const BYTE* event);
void __fastcall tooltip_show_for(tooltip* self, void* edx_unused, BYTE* control);
int  __cdecl game_FUN_0043da40(DWORD a, DWORD device, key_event* event, DWORD b);
int  __fastcall game_vf_20_46AB30(void* self, void* edx_unused, DWORD arg);
int  __fastcall game_vf_21_46AB70(void* self, void* edx_unused, DWORD arg);

#define control_user(c) FIELD(FIELD(c, CTL_DIALOG, BYTE*), DLG_USER, gui_user*)

typedef int (__fastcall* control_point)(void* self, void* edx_unused, DWORD code, POINT* at);
typedef int (__fastcall* control_two)(void* self, void* edx_unused, DWORD a, DWORD b);
typedef int (__fastcall* key_down)(void* keyboard, void* edx_unused, DWORD key, DWORD a, DWORD b);

#define KEY_LSHIFT 0x2A
#define KEY_RSHIFT 0x36

/*
    Vtable slot 23: a key or button pressed. The control's hot key (0x1D8), without shift, gives
    it focus and clicks it (slots 0xDC and 0xE0). Buttons go to slot 0xDC with where the user
    points.
*/
PORT(0x00472F00)
int __fastcall control_press(BYTE* self, void* edx_unused, key_event* event, DWORD arg)
{
    (void) edx_unused;
    if (gui_user_hears_event(control_user(self), NULL, (const BYTE*) event)) {
        if (event->device == 0) {
            if (FIELD(self, 0x1D8, DWORD) && FIELD(self, 0x1D8, DWORD) == event->code) {
                void* keyboard = FIELD(game_inputs, 0x34, void*);

                if (!VIRTUAL(keyboard, 0x14, key_down)(keyboard, NULL, KEY_LSHIFT, 0x3F, 0)
                    && !VIRTUAL(FIELD(game_inputs, 0x34, void*), 0x14, key_down)(
                           FIELD(game_inputs, 0x34, void*), NULL, KEY_RSHIFT, 0x3F, 0)
                    && control_enabled(self) && FIELD(self, 0x70, int)) {
                    gui_user_queue_focus(control_user(self), NULL, self, NULL, 1);
                    VIRTUAL(self, 0xDC, control_two)(self, NULL, 0, 0);
                    VIRTUAL(self, 0xE0, control_two)(self, NULL, 0, 0);
                    return 1;
                }
            }
            if (game_FUN_0043da40(0x32, event->device, event, 0x80)
                && FIELD(control_user(self), 0x1DC, BYTE*) == self)
                FIELD(self, 0x168, DWORD) = 1;
        }
        if (event->device != 0) {
            gui_input* input = control_user(self)->input;
            POINT at;

            if (input) {
                at.x = input->x;
                at.y = input->y;
            }
            if (VIRTUAL(self, 0xDC, control_point)(self, NULL, event->code,
                                                   control_user(self)->input ? &at : NULL))
                return 1;
        }
    }
    return game_vf_23_46A500(self, NULL, (DWORD) event, arg);
}

/* Vtable slot 22: a button released, to slot 0xE0 with where the user points. */
PORT(0x00473060)
int __fastcall control_release(BYTE* self, void* edx_unused, key_event* event, DWORD arg)
{
    gui_input* input = gui_user_input_for_event(control_user(self), NULL, (const BYTE*) event);

    (void) edx_unused;
    if (input) {
        POINT at;

        at.x = input->x;
        at.y = input->y;
        if (event->device && VIRTUAL(self, 0xE0, control_point)(self, NULL, event->code, &at))
            return 1;
    }
    return game_vf_22_46A4B0(self, NULL, (DWORD) event, arg);
}

/*
    Vtable slot 55: pressed while it has focus, by `button`. The first button shows state 2 (and
    its sound); the control then takes capture, and the tooltip goes.
*/
PORT(0x004730E0)
int __fastcall control_pressed(BYTE* self, void* edx_unused, DWORD button, DWORD b)
{
    tooltip* tip;

    (void) edx_unused;
    (void) b;
    if (control_user(self)->focus != self)
        return 0;
    if (!button) {
        VIRTUAL(self, 0x74, method_1)(self, NULL, 2);
        VIRTUAL(self, 0xE8, control_two)(self, NULL, 2, FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0x180, DWORD));
    }
    FIELD(self, 0x1D4, DWORD) = button;
    if (button == 1 && FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_BACKGROUND, BYTE*))
        background_invalidate(FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_BACKGROUND, BYTE*));
    gui_user_queue_capture(control_user(self), NULL, self, NULL, 0);
    tip = (tooltip*) control_user(self)->tooltip;
    if (tip) {
        tip->shown = 0;
        tip->delay = -1.0f;
    }
    return 0;
}

/* Vtable slot 56: released; the first button, while pressed (state 2), shows state 3. */
PORT(0x00473180)
int __fastcall control_released(BYTE* self, void* edx_unused, DWORD button, DWORD b)
{
    (void) edx_unused;
    (void) b;
    if (control_user(self)->focus == self && !button && FIELD(self, 0x60, int) == 2) {
        VIRTUAL(self, 0x74, method_1)(self, NULL, 3);
        VIRTUAL(self, 0xE8, control_two)(self, NULL, 3, FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0x180, DWORD));
    }
    FIELD(self, 0x1D4, int) = -1;
    return 0;
}

/* Vtable slot 57: dragged with button 1, when it can be (0x168): move it to `at`. */
PORT(0x004731E0)
void __fastcall control_drag(BYTE* self, void* edx_unused, DWORD button, const POINT* at)
{
    (void) edx_unused;
    if (FIELD(self, 0x168, int) && button == 1) {
        BYTE* dialog;
        int width, height;

        control_redraw(self);
        width = VIRTUAL(self, 0xF4, method_0)(self);
        height = VIRTUAL(self, 0xF8, method_0)(self);
        dialog = FIELD(self, CTL_DIALOG, BYTE*);
        FIELD(self, 0x3C, LONG) = at->x - FIELD(dialog, 0x160, LONG);
        FIELD(self, 0x44, LONG) = FIELD(self, 0x3C, LONG) + width;
        FIELD(self, 0x40, LONG) = at->y - FIELD(dialog, 0x164, LONG);
        FIELD(self, 0x48, LONG) = FIELD(self, 0x40, LONG) + height;
    }
}

/*
    Vtable slots 20 and 21: tell the dialog the control was clicked (event 11) or released (12)
    while it had capture. The dialog's answer in `param`/`e` decides whether the base class hears.
*/
static int control_click_event(BYTE* self, DWORD type, DWORD arg)
{
    if (control_user(self)->capture == self) {
        BYTE* dialog;
        ui_event event;

        event.e = 0;
        event.type = type;
        event.a = FIELD(self, 0x74, DWORD);
        event.b = (DWORD) control_get_name(self);
        dialog = FIELD(self, CTL_DIALOG, BYTE*);
        event.sender = dialog;
        event.manager = FIELD(self, CTL_MANAGER, gui_manager*);
        event.c = (DWORD) self;
        event.d = arg;
        event.param = 1;
        VIRTUAL(dialog, 0x74, dialog_handle)(dialog, NULL, &event);
        if (event.e || !event.param)
            return 1;
    }
    return -1;
}

PORT(0x00473250)
int __fastcall control_clicked(BYTE* self, void* edx_unused, DWORD arg)
{
    int handled = control_click_event(self, 0xB, arg);

    (void) edx_unused;
    return handled >= 0 ? handled : game_vf_20_46AB30(self, NULL, arg);
}

PORT(0x00473300)
int __fastcall control_unclicked(BYTE* self, void* edx_unused, DWORD arg)
{
    int handled = control_click_event(self, 0xC, arg);

    (void) edx_unused;
    return handled >= 0 ? handled : game_vf_21_46AB70(self, NULL, arg);
}

#define UICONTROL_LIST ((const char*) 0x00571D74)   /* "UIControl," */

/*
    Find the control under `at`, from `object` down: its first sibling that is not a plain
    "UIControl,", its first child likewise, then the object itself. A control found gets focus
    when no button is down (or keeps it); returns the control's answer, or 0.
*/
PORT(0x004733B0)
int __fastcall control_hit_test(BYTE* object, void* edx_unused, POINT* at)
{
    BYTE* other;
    int found;

    (void) edx_unused;
    for (other = FIELD(object, 0xC, BYTE*); other; other = FIELD(other, 0xC, BYTE*)) {
        if (game_FUN_0053b910(FIELD(other, 0x28, const char*), UICONTROL_LIST)) {
            found = control_hit_test(other, NULL, at);
            if (found)
                return found;
            break;
        }
    }
    other = FIELD(object, 0x10, BYTE*);
    if (other && game_FUN_0053b910(FIELD(other, 0x28, const char*), UICONTROL_LIST)) {
        found = control_hit_test(FIELD(object, 0x10, BYTE*), NULL, at);
        if (found)
            return found;
    }

    if (VIRTUAL(object, 0xCC, control_two)(object, NULL, at->x, at->y)
        && VIRTUAL(object, 0x78, method_0)(object)) {
        BYTE* last = control_chain_end(object, NULL, NULL);

        if (FIELD(object, 0x6C, int) && FIELD(object, 0x70, int) && chain_settled(last)) {
            gui_user* user = control_user(object);

            if (!user->keys[0] && !user->keys[1]) {
                if (user->focus != object)
                    return dialog_queue_focus(FIELD(object, CTL_DIALOG, BYTE*), NULL, object, NULL, 0);
            } else if (user->focus != object) {
                return 0;
            }
            if (user->next_focus && user->next_focus != object)
                gui_user_queue_focus(user, NULL, object, NULL, 0);
            return 1;
        }
    }
    return 0;
}

/* Vtable slot 31: focus came, when it can take it (slot 0x78): state 1, its picture and sound,
   and its tooltip. */
PORT(0x004734E0)
int __fastcall control_focused(BYTE* self)
{
    if (VIRTUAL(self, 0x78, method_0)(self) && control_user(self)->focus != self) {
        void* picture;
        tooltip* tip;

        VIRTUAL(self, 0x74, method_1)(self, NULL, 1);
        picture = FIELD(self, 0x16C + 4 * FIELD(self, 0x60, int), void*);
        if (picture)
            anim_restart(picture);
        VIRTUAL(self, 0xE8, control_two)(self, NULL, 1, FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0x180, DWORD));
        tip = (tooltip*) control_user(self)->tooltip;
        if (tip)
            tooltip_show_for(tip, NULL, self);
    }
    return 1;
}

/* Vtable slot 32: focus went: back to state 0 (when enabled), and the tooltip goes. */
PORT(0x00473560)
void __fastcall control_unfocused(BYTE* self, void* edx_unused, DWORD to)
{
    tooltip* tip;

    (void) edx_unused;
    (void) to;
    if (FIELD(self, 0x6C, int))
        VIRTUAL(self, 0x74, method_1)(self, NULL, 0);
    tip = (tooltip*) control_user(self)->tooltip;
    if (tip)
        tooltip_set_text(tip, NULL, NULL, NULL, 1.0f);
}

/* Vtable slot 58: play its sound `index` (0x194). */
PORT(0x004735A0)
void __fastcall control_play_sound(BYTE* self, void* edx_unused, int index, DWORD b)
{
    (void) edx_unused;
    (void) b;
    if (FIELD(self, 0x194 + 4 * index, void*))
        play_sound(self, FIELD(self, 0x194 + 4 * index, void*));
}

/* Vtable slot 53: a walk over the controls of its dialog, on the game's heap. */
PORT(0x004735E0)
void* __fastcall control_walk_dialog(BYTE* self)
{
    BYTE* walk = game_new(0x94, GAMEUI_CPP, 0x1267);

    if (!walk)
        return NULL;
    walk_begin(walk, NULL, FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_CONTAINER, void*), 1,
               UICONTROL_CLASS);
    return walk;
}

PORT(0x00473660)
void __fastcall control_walk_end(void* self, void* edx_unused, BYTE* walk)
{
    (void) self;
    (void) edx_unused;
    if (walk) {
        walk_end(walk);
        game_delete(walk);
    }
}

/* The next control of the same group (0x7C), other than this one, in `walk`; NULL at the end. */
PORT(0x00473680)
BYTE* __fastcall control_next_in_group(BYTE* self, void* edx_unused, BYTE* walk)
{
    BYTE* other;

    (void) edx_unused;
    if (!FIELD(self, 0x7C, DWORD))
        return NULL;
    do
        other = walk_next(walk);
    while (other && (FIELD(other, 0x7C, DWORD) != FIELD(self, 0x7C, DWORD) || other == self));
    return other;
}

/* The same, and of the same type too. */
PORT(0x004736C0)
BYTE* __fastcall control_next_alike(BYTE* self, void* edx_unused, BYTE* walk)
{
    BYTE* other = control_next_in_group(self, NULL, walk);

    (void) edx_unused;
    while (other && FIELD(other, CTL_TYPE, int) != FIELD(self, CTL_TYPE, int))
        other = control_next_in_group(self, NULL, walk);
    return other;
}

/* A control focus can move to: enabled, shown, and not of type 5 or 12. */
static BOOL takes_focus(BYTE* control)
{
    int type = FIELD(control, CTL_TYPE, int);

    return FIELD(control, 0x6C, int) && FIELD(control, 0x70, int) && type != 5 && type != 12;
}

/* The control before this one that can take focus, round to the last; NULL when none. */
PORT(0x004736F0)
BYTE* __fastcall control_previous(BYTE* self)
{
    BYTE* control = self;

    if (!self)
        return NULL;
    for (;;) {
        if (control != self && takes_focus(control))
            return control;
        if (FIELD(control, 8, BYTE*)) {
            control = FIELD(control, 8, BYTE*);
        } else {
            while (FIELD(control, 0xC, BYTE*))
                control = FIELD(control, 0xC, BYTE*);
        }
        if (control == self)
            return NULL;
        if (!control)
            return NULL;
    }
}

/* The control after this one that can take focus, round to the dialog's first; NULL when none. */
PORT(0x00473740)
BYTE* __fastcall control_next(BYTE* self)
{
    BYTE* control = self;

    if (!self)
        return NULL;
    for (;;) {
        if (control != self && takes_focus(control))
            return control;
        control = FIELD(control, 0xC, BYTE*);
        if (!control)
            control = FIELD(FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_CONTAINER, BYTE*), 0x10, BYTE*);
        if (control == self)
            return NULL;
        if (!control)
            return NULL;
    }
}

/* Vtable slot 63: the picture's rectangle, sized to the picture (0x1CC) when 0xA4 is set. */
PORT(0x00473790)
void __fastcall control_picture_area(BYTE* self, void* edx_unused, const RECT* from, RECT* to)
{
    BYTE* picture;
    BYTE* dialog;

    (void) edx_unused;
    *to = *from;
    picture = FIELD(self, 0x1CC, BYTE*);
    if (FIELD(self, 0xA4, int) && picture) {
        dialog = FIELD(self, CTL_DIALOG, BYTE*);
        to->right = (int) ((double) FIELD(picture, 0x14, int) * FIELD(dialog, 0x9C4, float)) + to->left;
        picture = FIELD(self, 0x1CC, BYTE*);
        dialog = FIELD(self, CTL_DIALOG, BYTE*);
        to->bottom = (int) ((double) FIELD(picture, 0x18, int) * FIELD(dialog, 0x9C8, float)) + to->top;
    }
}

/* Vtable slot 64: the text's rectangle: padded (0xE8, 0xEC), and a pixel down while pressed. */
PORT(0x00473810)
void __fastcall control_text_area(BYTE* self, void* edx_unused, const RECT* from, RECT* to)
{
    (void) edx_unused;
    *to = *from;
    if (FIELD(self, 0x60, int) == 2) {
        to->top++;
        to->left++;
        to->bottom++;
        to->right++;
    }
    to->left += FIELD(self, 0xE8, LONG);
    to->top += FIELD(self, 0xEC, LONG);
    to->right -= FIELD(self, 0xE8, LONG);
    to->bottom -= FIELD(self, 0xEC, LONG);
}

/* ---- UIFrame: one picture, or one sound ---------------------------------------------------- */

#define UIFRAME_VTABLE ((void**) 0x00557D70)
#define WAV            ((const char*) 0x00571DC8)   /* ".wav" */
#define FRAME_MISSING  ((const char*) 0x00571D98)

/* The game's image loading: from a file, or from memory; and its free. */
BYTE* __cdecl game_FUN_00517ca0(const char* name, DWORD a, void* resources);
BYTE* __cdecl game_FUN_005174b0(const void* data, DWORD a, DWORD size);
void  __cdecl game_FUN_005184b0(BYTE* image);
BOOL  __cdecl picture_has_colour(BYTE* picture, COLORREF key);
void* __fastcall game_FUN_004c0160(void* self, void* edx_unused, DWORD a, DWORD b);
void  __fastcall game_FUN_004c0a70(void* sound, void* edx_unused, const char* name, DWORD a,
                                   DWORD b, DWORD c, DWORD d, int e);
void* __cdecl game_FUN_004bffe0(DWORD a, const char* name, DWORD b, DWORD c, DWORD d, int e);

/* An image's size and pixels. */
#define image_width(i)  FIELD(i, 0xE, short)
#define image_height(i) FIELD(i, 0x10, short)
#define image_pixels(i) FIELD(i, 0x14, BYTE*)

/* Make the frame's picture from `image`, keyed on magenta as the owner's style says. */
static void frame_picture(ui_frame* self, BYTE* image, BYTE* owner, int mode, int line)
{
    BYTE* picture = game_new(0x80, GAMEUI_CPP, line);
    DWORD key_colour, style;

    if (picture)
        picture = game_FUN_004ca650(picture, NULL, self->texture, 1);
    self->picture = picture;
    key_colour = owner ? FIELD(owner, 0x70C, DWORD) : 0;
    style = owner ? FIELD(owner, 0x708, DWORD) : 0;
    VIRTUAL(picture, 0x10, picture_fill)(picture, NULL, image_pixels(image), image_width(image),
                                         image_height(image), image_width(image),
                                         image_width(image), 0x22B, mode, style, 4, key_colour, 0,
                                         0, 2, 1, 0, 0x80, 0xFF00FF);
    if (picture_has_colour(self->picture, 0xFF00FF))
        VIRTUAL(self->picture, 0x48, method_1)(self->picture, NULL, 0xFF00FF);
}

/*
    A frame from the file `name`: a sound when it ends in .wav (with `a` and `b` for the sound),
    else a picture, styled by `owner`.
*/
PORT(0x00473890)
ui_frame* __fastcall frame_construct(ui_frame* self, void* edx_unused, const char* name,
                                     DWORD texture, DWORD a, DWORD b, BYTE* owner)
{
    size_t length;

    (void) edx_unused;
    game_FUN_00405f40(self);
    self->vtable = UIFRAME_VTABLE;
    length = text_length(name);
    if (game_FUN_0053b6e0(WAV, name + length - 4)) {
        int mode = video_mode();
        BYTE* image;

        self->owns_sound = 0;
        self->is_sound = 0;
        self->sound = NULL;
        self->texture = texture;
        image = game_FUN_00517ca0(name, 0, game_resources);
        if (!image) {
            char missing[100];

            self->picture = NULL;
            game_FUN_0053a11f(missing, FRAME_MISSING, name);
            return self;
        }
        self->width = image_width(image);
        self->height = image_height(image);
        frame_picture(self, image, owner, mode, 0x1393);
        game_FUN_005184b0(image);
#ifdef OPENMCM2_DEV
        modern_mark(name, self->picture);
#endif
        return self;
    }

    self->sound = game_new(0x1F8, GAMEUI_CPP, 0x13B9);
    if (self->sound)
        self->sound = game_FUN_004c0160(self->sound, NULL, a, 1);
    game_FUN_004c0a70(self->sound, NULL, name, 0, 1, 3, b, -1);
    self->owns_sound = 1;
    self->is_sound = 1;
    self->width = 0;
    self->height = 0;
    self->picture = NULL;
    return self;
}

/* The picture from an image in memory. FALSE when there is none. */
PORT(0x00473AF0)
BOOL __fastcall frame_load(ui_frame* self, void* edx_unused, const void* data, DWORD size,
                           BYTE* owner)
{
    BYTE* image;
    int mode;

    (void) edx_unused;
    if (!data)
        return FALSE;
    image = game_FUN_005174b0(data, 0, size);
    if (!image)
        return FALSE;
    mode = video_mode();
    self->width = image_width(image);
    self->height = image_height(image);
    frame_picture(self, image, owner, mode, 0x13D6);
    game_FUN_005184b0(image);
    return TRUE;
}

/*
    A frame from the resource `name` of `resources`, or failing that, with `sound_bank`, a sound
    of that name. The original drops the sound it makes yet marks the frame as owning one, so its
    destructor calls through NULL; this keeps that.
*/
PORT(0x00473C30)
ui_frame* __fastcall frame_construct_resource(ui_frame* self, void* edx_unused, const char* name,
                                              void* resources, DWORD texture, DWORD sound_bank,
                                              DWORD skip, DWORD a, BYTE* owner)
{
    (void) edx_unused;
    game_FUN_00405f40(self);
    self->vtable = UIFRAME_VTABLE;
    if (!skip) {
        BYTE* entry;

        self->owns_sound = 0;
        self->is_sound = 0;
        self->sound = NULL;
        self->texture = texture;
        entry = game_FUN_004ee0f0(resources, NULL, name, 1);
        if (!frame_load(self, NULL, FIELD(entry, 0x14, void*), FIELD(entry, 0x18, DWORD), owner)
            && sound_bank) {
            game_FUN_004bffe0(sound_bank, name, 1, 3, a, -1);
            self->owns_sound = 1;
            self->is_sound = 1;
            self->width = 0;
            self->height = 0;
            self->picture = NULL;
        }
#ifdef OPENMCM2_DEV
        modern_mark(name, self->picture);
#endif
    }
    return self;
}

PORT(0x00473CF0)
void __fastcall frame_destruct(ui_frame* self)
{
    self->vtable = UIFRAME_VTABLE;
#ifdef OPENMCM2_DEV
    modern_forget(self->picture);
#endif
    if (self->picture)
        object_destroy(self->picture);
    if (self->owns_sound)
        object_destroy(self->sound);
    game_FUN_00405f70(self);
}

PORT(0x00473AD0)
ui_frame* __fastcall frame_delete(ui_frame* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    frame_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* ---- UIAnim: a run of frames --------------------------------------------------------------- */

#define UIANIM_VTABLE ((void**) 0x00557D84)

PORT(0x00473D60)
ui_anim* __fastcall anim_construct(ui_anim* self, void* edx_unused, DWORD texture, DWORD b)
{
    (void) edx_unused;
    game_FUN_00405f40(self);
    self->texture = texture;
    self->vtable = UIANIM_VTABLE;
    self->frame = 0;
    self->count = 0;
    self->repeats = 0;
    self->repeats_left = 0;
    self->last_step = 0;
    self->restart = 1;
    self->delay = 0;
    self->backwards = 0;
    self->unknown_f4 = b;
    __stosd((unsigned long*) self->frames, 0, 50);
    return self;
}

PORT(0x00473DE0)
void __fastcall anim_destruct(ui_anim* self)
{
    int i;

    self->vtable = UIANIM_VTABLE;
    for (i = 0; i < self->count; i++) {
        if (self->frames[i])
            object_destroy(self->frames[i]);
    }
    game_FUN_00405f70(self);
}

PORT(0x00473DC0)
ui_anim* __fastcall anim_delete(ui_anim* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    anim_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

void __fastcall anim_restart(ui_anim* self);

PORT(0x00473E50)
void __fastcall anim_set_repeats(ui_anim* self, void* edx_unused, int repeats)
{
    (void) edx_unused;
    self->repeats = repeats;
    self->repeats_left = repeats;
    anim_restart(self);
}

PORT(0x00473E70)
void __fastcall anim_set_delay(ui_anim* self, void* edx_unused, int delay)
{
    (void) edx_unused;
    self->delay = delay;
}

/* Back to the first frame, or the last when it plays backwards. */
PORT(0x00473E80)
void __fastcall anim_restart(ui_anim* self)
{
    if (self->backwards) {
        self->last_step = 0;
        self->restart = 1;
        self->frame = self->count - 1;
    } else {
        self->frame = 0;
        self->last_step = 0;
        self->restart = 1;
    }
}

PORT(0x00473EB0)
ui_frame* __fastcall anim_current(ui_anim* self)
{
    return self->frames[self->frame];
}

/* The picture showing, or NULL on a sound frame. */
PORT(0x00473EC0)
BYTE* __fastcall anim_picture(ui_anim* self)
{
    ui_frame* frame = self->frames[self->frame];

    return frame->is_sound == 1 ? NULL : frame->picture;
}

/* Add `frame` at the end. FALSE, and not added, once there are 49. */
PORT(0x00473EE0)
BOOL __fastcall anim_add(ui_anim* self, void* edx_unused, ui_frame* frame)
{
    (void) edx_unused;
    if (self->count >= 49)
        return FALSE;
    self->frames[self->count] = frame;
    self->count++;
    return TRUE;
}

/* The time of the last step of any animation, from timeGetTime. */
DWORD __cdecl game_FUN_004c41d0(void);
#define anim_clock (*(DWORD*) 0x00661710)

#define FOREVER 0x7FFF      /* repeats */

/* Wrapped round to the start: whether it goes on, using up a repeat. */
static BOOL anim_repeat(ui_anim* self)
{
    self->restart = 0;
    if (self->repeats == FOREVER)
        return TRUE;
    return self->repeats && --self->repeats_left > 0;
}

/*
    The frame to show now: the next one once `delay` milliseconds have passed, or at once after a
    sound. At the end it starts over while repeats are left, else it stays on the last. Played
    backwards, the original starts over at `count`, one past the last frame, and so does this.
*/
PORT(0x00473F10)
ui_frame* __fastcall anim_step(ui_anim* self)
{
    DWORD now = game_FUN_004c41d0();
    int passed;

    anim_clock = now;
    passed = (int) (now - self->last_step);
    if (passed < 0)
        passed = -passed;
    if (passed <= self->delay && self->frames[self->frame]->is_sound != 1)
        return self->frames[self->frame];

    self->last_step = now;
    if (self->backwards) {
        if (!self->restart && self->frame > 0)
            return self->frames[--self->frame];
        if (anim_repeat(self))
            self->frame = self->count;
    } else {
        if (!self->restart && self->frame + 1 < self->count)
            return self->frames[++self->frame];
        if (anim_repeat(self))
            self->frame = 0;
    }
    return self->frames[self->frame];
}

void __fastcall game_FUN_004c0e00(void* sound, void* edx_unused, DWORD a, DWORD b, DWORD c);

/* Step, playing each sound frame met on the way; the picture reached. */
PORT(0x00473FE0)
BYTE* __fastcall anim_next_picture(ui_anim* self)
{
    ui_frame* frame = anim_step(self);

    while (frame->is_sound == 1) {
        game_FUN_004c0e00(frame->sound, NULL, 1, 0, 0);
        frame = anim_step(self);
    }
    return frame->picture;
}

/* Add a frame from the file `name`. */
PORT(0x00474010)
BOOL __fastcall anim_add_file(ui_anim* self, void* edx_unused, const char* name, BYTE* owner)
{
    ui_frame* frame = game_new(sizeof(ui_frame), GAMEUI_CPP, 0x14D1);

    (void) edx_unused;
    if (frame)
        frame = frame_construct(frame, NULL, name, self->texture, 0, 0, owner);
    return anim_add(self, NULL, frame);
}

/* Add a frame from the resource `name`, or the sound of that name in `sound_bank`. */
PORT(0x00474090)
BOOL __fastcall anim_add_resource(ui_anim* self, void* edx_unused, const char* name,
                                  void* resources, DWORD sound_bank, BYTE* owner)
{
    ui_frame* frame = game_new(sizeof(ui_frame), GAMEUI_CPP, 0x14E6);

    (void) edx_unused;
    if (frame)
        frame = frame_construct_resource(frame, NULL, name, resources, self->texture, sound_bank,
                                         0, 0, owner);
    return anim_add(self, NULL, frame);
}

/* ---- UIButton: a control drawn from a picture per state --------------------------------------- */

/*
    A UIControl of type 1 with a picture for each state (0x16C on, as control_set_picture keeps
    them) and a list box (0x1EC, the dialog file's SortLBName) it sorts when released. It is hit
    only where its picture is not the colour key.
*/
#define UIBUTTON_VTABLE ((void**) 0x00557D98)
#define BTN_SORTS       0x1EC

void __fastcall list_sort(BYTE* self, void* edx_unused, BOOL outer);

/* Pictures for the states: normal, under the pointer (also kept for 3), pressed and disabled. */
PORT(0x00474200)
int __fastcall button_set_pictures(BYTE* self, void* edx_unused, DWORD normal, DWORD over,
                                   DWORD pressed, DWORD disabled)
{
    (void) edx_unused;
    FIELD(self, 0x16C, DWORD) = normal;
    FIELD(self, 0x170, DWORD) = over;
    FIELD(self, 0x174, DWORD) = pressed;
    FIELD(self, 0x178, DWORD) = over;
    FIELD(self, 0x17C, DWORD) = disabled;
    return control_redraw(self);
}

PORT(0x00474240)
int __fastcall button_set_normal(BYTE* self, void* edx_unused, DWORD picture)
{
    (void) edx_unused;
    FIELD(self, 0x16C, DWORD) = picture;
    return control_redraw(self);
}

PORT(0x00474260)
int __fastcall button_set_over(BYTE* self, void* edx_unused, DWORD picture)
{
    (void) edx_unused;
    FIELD(self, 0x170, DWORD) = picture;
    FIELD(self, 0x178, DWORD) = picture;
    return control_redraw(self);
}

PORT(0x00474280)
int __fastcall button_set_pressed(BYTE* self, void* edx_unused, DWORD picture)
{
    (void) edx_unused;
    FIELD(self, 0x174, DWORD) = picture;
    return control_redraw(self);
}

PORT(0x004742A0)
int __fastcall button_set_disabled(BYTE* self, void* edx_unused, DWORD picture)
{
    (void) edx_unused;
    FIELD(self, 0x17C, DWORD) = picture;
    return control_redraw(self);
}

PORT(0x004742C0)
void __fastcall button_set_sort_list(BYTE* self, void* edx_unused, BYTE* list)
{
    (void) edx_unused;
    FIELD(self, BTN_SORTS, BYTE*) = list;
}

PORT(0x00474120)
BYTE* __fastcall button_construct(BYTE* self, void* edx_unused, DWORD a, const RECT* area,
                                  BYTE* dialog)
{
    int i;

    (void) edx_unused;
    control_construct(self, NULL, 1, a, area, dialog);
    *(void***) self = UIBUTTON_VTABLE;
    FIELD(self, BTN_SORTS, BYTE*) = NULL;
    button_set_pictures(self, NULL, 0, 0, 0, 0);
    for (i = 0; i < 5; i++)
        control_set_194(self, NULL, i, 0);
    return self;
}

PORT(0x004741F0)
void __fastcall button_destruct(BYTE* self)
{
    *(void***) self = UIBUTTON_VTABLE;
    control_destruct(self);
}

PORT(0x004741D0)
BYTE* __fastcall button_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    button_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Vtable slot 49: control_enable, but enabled again it shows state 0, not the one it had. */
PORT(0x004742D0)
void __fastcall button_enable(BYTE* self, void* edx_unused, BOOL enabled)
{
    (void) edx_unused;
    if (FIELD(self, 0x6C, BOOL) == enabled)
        return;
    FIELD(self, 0x6C, BOOL) = enabled;
    if (enabled) {
        VIRTUAL(self, 0x74, method_1)(self, NULL, 0);
        control_set_colour(self, NULL, FIELD(self, 0xCC, COLORREF));
        return;
    }
    if (FIELD(self, 0x17C, int)) {
        FIELD(self, CTL_STATE, DWORD) = FIELD(self, 0x60, DWORD);
        VIRTUAL(self, 0x74, method_1)(self, NULL, 4);
    }
    FIELD(self, 0xCC, COLORREF) = FIELD(self, CTL_COLOUR, COLORREF);
    control_set_colour(self, NULL, RGB(0x99, 0x99, 0x99));
}

/*
    Vtable slot 56: released. Let go of with the first button while pressed, it sorts its list
    and tells the dialog (event 1); the base class hears unless the dialog took it (`e`). The
    original leaves the event's `param` and `d` unset, and so does this.
*/
PORT(0x00474340)
int __fastcall button_released(BYTE* self, void* edx_unused, DWORD button, DWORD b)
{
    (void) edx_unused;
    if (control_user(self)->focus == self && !button && FIELD(self, 0x60, int) == 2) {
        BYTE* dialog;
        ui_event event;

        event.e = 0;
        if (FIELD(self, BTN_SORTS, BYTE*))
            list_sort(FIELD(self, BTN_SORTS, BYTE*), NULL, 1);
        event.a = FIELD(self, 0x74, DWORD);
        event.type = 1;
        event.b = (DWORD) control_get_name(self);
        dialog = FIELD(self, CTL_DIALOG, BYTE*);
        event.sender = dialog;
        event.manager = FIELD(self, CTL_MANAGER, gui_manager*);
        event.c = (DWORD) self;
        VIRTUAL(dialog, 0x74, dialog_handle)(dialog, NULL, &event);
        if (event.e)
            return 1;
    }
    return control_released(self, NULL, button, b);
}

/* Vtable slot 48: the picture for `state`, or for the one showing when it is -1. */
typedef BYTE* (__fastcall* control_state_picture)(void* control, void* edx_unused, int state);

#define DESC_SOURCE_KEY 0x40        /* ddckCKSrcBlt's low value */
#define LOCK_WAIT       1           /* DDLOCK_WAIT */

/*
    Vtable slot 28: whether (x, y) is on the picture showing, scaled from the dialog's size to
    the picture's (0x9C4, 0x9C8): in its rectangle, and not the colour key. 8-bit pictures are
    read a byte a pixel, every other depth as 16 bits. FALSE when it cannot be locked.
*/
PORT(0x004743F0)
BOOL __fastcall button_contains(BYTE* self, void* edx_unused, LONG x, LONG y, DWORD unused)
{
    int mode = video_mode();
    RECT area = FIELD(self, CTL_RECT, RECT);
    BYTE desc[DESC_SIZE];
    BOOL solid = FALSE;
    BYTE* dialog;
    BYTE* picture;
    void* surface;
    POINT at;
    int column, row, pixel;

    (void) edx_unused;
    (void) unused;
    at.x = x;
    at.y = y;
    if (!PtInRect(&area, at))
        return FALSE;

    dialog = FIELD(self, CTL_DIALOG, BYTE*);
    column = (int) ((x - area.left) / (double) FIELD(dialog, 0x9C4, float));
    row = (int) ((y - area.top) / (double) FIELD(dialog, 0x9C8, float));
    FIELD(desc, 0, DWORD) = DESC_SIZE;
    picture = VIRTUAL(self, 0xC0, control_state_picture)(self, NULL, -1);
    surface = FIELD(picture, 0x70, void*);
    if (VIRTUAL(surface, 0x64, surface_lock)(surface, NULL, desc, LOCK_WAIT, NULL))
        return solid;

    pixel = FIELD(desc, DESC_PITCH, int) * row / game_FUN_00517050(mode) + column;
    if (mode == 8)
        solid = FIELD(desc, DESC_PIXELS, BYTE*)[pixel] != FIELD(desc, DESC_SOURCE_KEY, BYTE);
    else
        solid = FIELD(desc, DESC_PIXELS, WORD*)[pixel] != FIELD(desc, DESC_SOURCE_KEY, WORD);
    VIRTUAL(surface, 0x80, surface_unlock)(surface, NULL);
    return solid;
}

/* ---- UIEdit: a text box ------------------------------------------------------------------------ */

/*
    A UIControl of type 11 whose text (CTL_TEXT) is a buffer of `capacity` bytes that it edits,
    optionally bound to an outside buffer (0x1B4, of EDIT_BOUND_SIZE bytes) that it is copied
    back to. It scrolls to keep the caret, a one-pixel box at EDIT_CARET_X, in view.
*/
#define UIEDIT_VTABLE   ((void**) 0x00557EA0)
#define EDIT_BOUND      0x1B4       /* char*: the outside buffer, or NULL */
#define EDIT_CAPACITY   0x1F0
#define EDIT_CARET_X    0x1FC       /* the caret's distance into the text, in pixels */
#define EDIT_SCROLL     0x200       /* how far the text is drawn left of its box, negated */
#define EDIT_CARET_PEN  0x20C       /* HBRUSH for the caret, or white when NULL */
#define EDIT_ALLOWED    0x210       /* char*: the only characters it takes, or NULL for all */
#define EDIT_CARET      0x214       /* RECT: where the caret was last drawn */
#define EDIT_BOUND_SIZE 0x224

#define EDIT_MAX     1000           /* the most a box holds when it starts without text */
#define EMPTY_TEXT   ((const char*) 0x0057D888)     /* "" */

#define ime_in_use() FIELD(FIELD(game_main, 0xC, BYTE*), 0x6C, int)

PORT(0x00474560)
BYTE* __fastcall edit_construct(BYTE* self, void* edx_unused, DWORD a, const RECT* area,
                                BYTE* dialog, int length, DWORD b, DWORD c)
{
    char* text;

    (void) edx_unused;
    control_construct(self, NULL, 11, a, area, dialog);
    *(void***) self = UIEDIT_VTABLE;
    text = FIELD(self, CTL_TEXT, char*);
    if (!text) {
        int capacity = length + 1;

        if (capacity >= EDIT_MAX)
            capacity = EDIT_MAX;
        else if (capacity < 1)
            capacity = 1;
        FIELD(self, EDIT_CAPACITY, int) = capacity;
    } else {
        int held = (int) text_length(text);

        FIELD(self, EDIT_CAPACITY, int) = held > length + 1 ? held : length + 1;
    }
    FIELD(self, 0x204, DWORD) = b;
    FIELD(self, 0x208, DWORD) = c;
    FIELD(self, EDIT_SCROLL, int) = 0;
    FIELD(self, 0x1EC, DWORD) = 0;
    FIELD(self, CTL_LENGTH, int) = 0;
    FIELD(self, EDIT_CARET_X, int) = 0;
    FIELD(self, EDIT_ALLOWED, char*) = NULL;
    FIELD(self, EDIT_CARET_PEN, HBRUSH) = NULL;
    FIELD(self, 0x228, DWORD) = 0;
    FIELD(self, 0x22C, DWORD) = 0;
    if (!text)
        FIELD(self, CTL_TEXT, char*) = game_FUN_004a4410(FIELD(self, EDIT_CAPACITY, int),
                                                         GAMEUI_CPP, 0x15C7);
    FIELD(self, CTL_TEXT, char*)[0] = 0;
    return self;
}

void __fastcall edit_commit(BYTE* self, void* edx_unused, BOOL out);

PORT(0x004746A0)
void __fastcall edit_destruct(BYTE* self)
{
    *(void***) self = UIEDIT_VTABLE;
    if (FIELD(self, EDIT_BOUND, char*))
        edit_commit(self, NULL, TRUE);
    if (FIELD(self, CTL_TEXT, char*)) {
        game_FUN_004a4450(FIELD(self, CTL_TEXT, char*), GAMEUI_CPP, 0x15D2);
        FIELD(self, CTL_TEXT, char*) = NULL;
    }
    if (FIELD(self, EDIT_ALLOWED, char*))
        game_FUN_004a4450(FIELD(self, EDIT_ALLOWED, char*), GAMEUI_CPP, 0x15D6);
    if (FIELD(self, EDIT_CARET_PEN, HBRUSH))
        DeleteObject(FIELD(self, EDIT_CARET_PEN, HBRUSH));
    control_destruct(self);
}

PORT(0x00474680)
BYTE* __fastcall edit_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    edit_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Edit `buffer`, of `size` bytes. */
PORT(0x00474750)
void __fastcall edit_bind(BYTE* self, void* edx_unused, char* buffer, int size)
{
    (void) edx_unused;
    control_set_1b4(self, NULL, (DWORD) buffer);
    FIELD(self, EDIT_BOUND_SIZE, int) = size;
}

/* Vtable slot 59: copy the text out to the bound buffer, or (`out` FALSE) take it from there. */
PORT(0x00474770)
void __fastcall edit_commit(BYTE* self, void* edx_unused, BOOL out)
{
    char* bound = FIELD(self, EDIT_BOUND, char*);
    int size = FIELD(self, EDIT_BOUND_SIZE, int);

    (void) edx_unused;
    if (!bound || !size)
        return;
    if (out) {
        game__strncpy(bound, FIELD(self, CTL_TEXT, char*), size);
        FIELD(self, EDIT_BOUND, char*)[FIELD(self, EDIT_BOUND_SIZE, int) - 1] = 0;
    } else {
        edit_set_text(self, NULL, bound);
    }
}

/* Vtable slot 38: control_needs_redraw, but an empty box keeps counting down. */
PORT(0x004747D0)
void __fastcall edit_needs_redraw(BYTE* self)
{
    BOOL empty = !FIELD(self, CTL_TEXT, char*) || !FIELD(self, CTL_LENGTH, int);
    BYTE* last = control_chain_end(self, NULL, NULL);

    if (FIELD(self, 0x70, int) && !empty && chain_settled(last))
        return;
    if (--FIELD(self, 0x1BC, int) < 0) {
        FIELD(self, 0x1BC, int) = 0;
        FIELD(self, 0x1C0, DWORD) = 1;
    }
}

/*
    Vtable slot 40: render. The picture as control_render draws it, but over the whole rectangle;
    then the text, top left in a box padded by 0xE8/0xEC and scrolled by its alignment so the
    caret shows. While it has capture the caret is framed on the last pass, and an open IME's
    composition window is moved to it. It keeps being redrawn while it has capture.
*/
PORT(0x00474840)
int __fastcall edit_render(BYTE* self)
{
    int width = VIRTUAL(self, 0xF4, method_0)(self);
    int redraws = FIELD(self, 0x1BC, int);
    int kept = -1;
    BYTE* picture;
    RECT area;
    HDC dc;
    DWORD more;

    if (!FIELD(self, CTL_TEXT, char*))
        edit_set_text(self, NULL, EMPTY_TEXT);

    picture = FIELD(self, 0x1CC, BYTE*);
    if (picture) {
        BYTE* background = FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_BACKGROUND, BYTE*);
        DWORD flags = FIELD(picture, 0x30, int) ? 0x1008000 : 0x1000000;

        if (background) {
            background_draw(background, NULL, picture, &FIELD(self, CTL_RECT, RECT),
                            CONTROL_SOURCE(self, picture),
                            flags, FIELD(self, CTL_SLOT, int), FIELD(self, 0x1C0, DWORD),
                            &FIELD(self, 0x1BC, int), 0);
            kept = FIELD(self, 0x1BC, int);
        } else {
            VIRTUAL(object_screen(self), 0xC, screen_draw)(object_screen(self), NULL,
                                                           &FIELD(self, CTL_RECT, RECT), picture,
                                                           CONTROL_SOURCE(self, picture), flags);
        }
    }
    FIELD(self, 0x1BC, int) = redraws;

    do {
        const RECT* box = &FIELD(self, CTL_RECT, RECT);
        DWORD align = FIELD(self, CTL_ALIGN, DWORD);
        int caret = FIELD(self, EDIT_CARET_X, int);
        int scroll;
        LONG top;

        if (align & 1) {
            scroll = width - caret < 0 ? width - caret : 0;
        } else if (align & 2) {
            if (VIRTUAL(self, 0xF4, method_0)(self) > caret)
                scroll = width / 2 - caret / 2;
            else
                scroll = width - caret;
        } else if (align & 4) {
            scroll = width - caret;
        } else {
            scroll = width - caret < 0 ? width - caret : 0;
        }
        FIELD(self, EDIT_SCROLL, int) = scroll;

        if (align & 8)
            top = box->top;
        else if (align & 0x10)
            top = (box->bottom - box->top) / 2 - FIELD(self, 0x160, LONG) / 2 + box->top;
        else if (align & 0x20)
            top = box->bottom - FIELD(self, 0x160, LONG);
        else
            top = box->top;
        area.left = box->left + scroll + FIELD(self, 0xE8, LONG);
        area.top = top + FIELD(self, 0xEC, LONG);
        area.right = box->right - FIELD(self, 0xE8, LONG);
        area.bottom = box->bottom - FIELD(self, 0xEC, LONG);

        if (!dialog_begin_paint(FIELD(self, CTL_DIALOG, BYTE*), NULL, &dc, (RECT*) box, &more,
                                self))
            continue;
        if (FIELD(self, CTL_LENGTH, int))
            dialog_draw_text(FIELD(self, CTL_DIALOG, BYTE*), NULL, dc, &area,
                             FIELD(self, CTL_TEXT, char*), FIELD(self, CTL_LENGTH, int),
                             FIELD(self, CTL_COLOUR, COLORREF), FIELD(self, 0x160, int), NULL,
                             FIELD(self, CTL_SHADOW, BOOL),
                             FIELD(self, CTL_SHADOW_COLOUR, COLORREF), 9, TRUE, NULL);

        if (!more && control_user(self)->capture == self) {
            BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);
            LONG last = FIELD(dialog, 0x160, LONG) + box->left + width - 1;
            LONG height = FIELD(self, 0x160, LONG);
            HBRUSH pen = FIELD(self, EDIT_CARET_PEN, HBRUSH);

            area.left += caret;
            if (area.left > last)
                area.left = last;
            area.top++;
            area.right = area.left + 1;
            if (!height)
                height = FIELD(dialog, 0xDC, LONG);
            area.bottom = area.top + height + 1;
            FIELD(self, EDIT_CARET, RECT) = area;
            FrameRect(dc, &area, pen ? pen : (HBRUSH) GetStockObject(WHITE_BRUSH));
        }
        dialog_end_paint(FIELD(self, CTL_DIALOG, BYTE*), NULL, dc);

        if (ime_in_use() && control_user(self)->capture == self) {
            BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);
            HIMC context;

            if (dialog && FIELD(dialog, DLG_BACKGROUND, BYTE*))
                background_release_dc(FIELD(dialog, DLG_BACKGROUND, BYTE*));
            context = ImmGetContext(FIELD(game_main, 0x31C, HWND));
            if (ImmGetOpenStatus(context)) {
                COMPOSITIONFORM form;

                form.dwStyle = CFS_FORCE_POSITION;
                form.ptCurrentPos.x = FIELD(self, EDIT_CARET, RECT).left;
                form.ptCurrentPos.y = FIELD(self, EDIT_CARET, RECT).top;
                ImmSetCompositionWindow(context, &form);
            }
            if (context)
                ImmReleaseContext(FIELD(game_main, 0x31C, HWND), context);
        }
    } while (more);

    if (kept >= 0)
        FIELD(self, 0x1BC, int) = kept;
    if (control_user(self)->capture == self) {
        control_redraw(self);
        return 1;
    }
    FIELD(self, 0x1C0, DWORD) = 1;
    return 1;
}

#define EDIT_CARET_POS  0x1EC       /* the caret's index into the text */
#define EDIT_TYPE_SOUND 0x204       /* played on every key */
#define EDIT_FULL_SOUND 0x208       /* played when a character does not fit */
#define EDIT_LEAD       0x228       /* the last byte typed led a double-byte character */
#define EDIT_LEAD_LOST  0x22C       /* ...which did not fit, so its trail byte is dropped */

void* __cdecl game_FUN_0053c350(void* to, const void* from, size_t count);      /* memmove */
int   __fastcall game_vf_32_47D940(BYTE* self, void* edx_unused, DWORD a);     /* slot 33's base */

/* The screen's surface, whose DC text is measured on. */
#define video_surface() FIELD(FIELD(game_main, 0x10, BYTE*), 0x48, void*)
#define edit_font(c)    (FIELD(c, CTL_FONT, HFONT) ? FIELD(c, CTL_FONT, HFONT) \
                                                   : FIELD(FIELD(c, CTL_DIALOG, BYTE*), 0xD8, HFONT))

/* Tell the dialog: `type` 10 when editing ends, 19 when the text changed. The dialog's `e`. */
static DWORD edit_event(BYTE* self, DWORD type)
{
    BYTE* dialog;
    ui_event event;

    event.e = 0;
    event.type = type;
    event.a = FIELD(self, 0x74, DWORD);
    event.b = (DWORD) control_get_name(self);
    event.manager = FIELD(self, CTL_MANAGER, gui_manager*);
    dialog = FIELD(self, CTL_DIALOG, BYTE*);
    event.sender = dialog;
    event.c = (DWORD) self;
    VIRTUAL(dialog, 0x74, dialog_handle)(dialog, NULL, &event);
    return event.e;
}

/*
    After an edit: end the text, and measure it whole (CTL_EXTENT's width) and up to the caret, on
    the dialog's screen. A screen without a DC leaves both as they were.
*/
static void edit_measure(BYTE* self)
{
    void* surface;
    HDC dc;

    FIELD(self, CTL_TEXT, char*)[FIELD(self, CTL_LENGTH, int)] = 0;
    surface = FIELD(object_screen(FIELD(self, CTL_DIALOG, BYTE*)), 0x48, void*);
    if (!VIRTUAL(surface, 0x44, surface_get_dc)(surface, &dc)) {
        HGDIOBJ old = SelectObject(dc, edit_font(self));
        SIZE size;

        GetTextExtentPoint32A(dc, FIELD(self, CTL_TEXT, char*), FIELD(self, CTL_LENGTH, int), &size);
        FIELD(self, CTL_EXTENT + 4, LONG) = size.cx;
        GetTextExtentPoint32A(dc, FIELD(self, CTL_TEXT, char*), FIELD(self, EDIT_CARET_POS, int),
                              &size);
        FIELD(self, EDIT_CARET_X, LONG) = size.cx;
        SelectObject(dc, old);
        surface = FIELD(object_screen(FIELD(self, CTL_DIALOG, BYTE*)), 0x48, void*);
        VIRTUAL(surface, 0x68, surface_release_dc)(surface, dc);
    }
    control_redraw(self);
}

/*
    Hold up to `length` characters (1 to 999). The text is kept as far as it fits, but its
    length is left at 0, as the original leaves it. Text over 1000 bytes would overrun the
    original's copy, and this one's.
*/
PORT(0x00474C10)
int __fastcall edit_set_capacity(BYTE* self, void* edx_unused, int length)
{
    char kept[EDIT_MAX];
    char* text = FIELD(self, CTL_TEXT, char*);
    BOOL had = FALSE;
    int capacity;

    (void) edx_unused;
    if (text) {
        text_copy(kept, text);
        game_FUN_004a4450(text, GAMEUI_CPP, 0x169A);
        had = TRUE;
    }
    capacity = length + 1;
    if (capacity >= EDIT_MAX)
        capacity = EDIT_MAX;
    else if (capacity < 1)
        capacity = 1;
    FIELD(self, EDIT_CAPACITY, int) = capacity;
    FIELD(self, EDIT_SCROLL, int) = 0;
    FIELD(self, EDIT_CARET_POS, int) = 0;
    FIELD(self, EDIT_CARET_X, int) = 0;
    FIELD(self, CTL_LENGTH, int) = 0;
    text = game_FUN_004a4410(capacity, GAMEUI_CPP, 0x16A3);
    FIELD(self, CTL_TEXT, char*) = text;
    if (had) {
        int count = (int) text_length(kept);

        if (count >= capacity - 1)
            count = capacity - 1;
        game__strncpy(text, kept, count);
        FIELD(self, CTL_TEXT, char*)[count] = 0;
    }
    return control_redraw(self);
}

PORT(0x00474D20)
void __fastcall edit_set_type_sound(BYTE* self, void* edx_unused, void* sound)
{
    (void) edx_unused;
    FIELD(self, EDIT_TYPE_SOUND, void*) = sound;
}

PORT(0x00474D30)
void __fastcall edit_set_full_sound(BYTE* self, void* edx_unused, void* sound)
{
    (void) edx_unused;
    FIELD(self, EDIT_FULL_SOUND, void*) = sound;
}

/* Set the text (NULL for none), cut to fit, with the caret at its end. */
PORT(0x00474D40)
int __fastcall edit_set_text(BYTE* self, void* edx_unused, const char* text)
{
    void* surface;
    int count;
    HDC dc;

    (void) edx_unused;
    if (!text)
        text = EMPTY_TEXT;
    if (!FIELD(self, CTL_TEXT, char*))
        FIELD(self, CTL_TEXT, char*) = game_FUN_004a4410(FIELD(self, EDIT_CAPACITY, int),
                                                         GAMEUI_CPP, 0x16C0);
    count = (int) text_length(text);
    if (count >= FIELD(self, EDIT_CAPACITY, int) - 1)
        count = FIELD(self, EDIT_CAPACITY, int) - 1;
    game__strncpy(FIELD(self, CTL_TEXT, char*), text, count);
    FIELD(self, CTL_TEXT, char*)[count] = 0;
    FIELD(self, EDIT_CARET_POS, int) = count;
    FIELD(self, CTL_LENGTH, int) = count;

    surface = video_surface();
    if (VIRTUAL(surface, 0x44, surface_get_dc)(surface, &dc)) {
        FIELD(self, CTL_EXTENT + 4, LONG) = 0;
        FIELD(self, EDIT_CARET_X, LONG) = 0;
        return control_redraw(self);
    }
    {
        BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);
        HFONT font = FIELD(self, CTL_FONT, HFONT);
        HGDIOBJ old;
        SIZE size;

        if (!font)
            font = dialog ? FIELD(dialog, 0xD8, HFONT) : NULL;
        old = SelectObject(dc, font);
        GetTextExtentPoint32A(dc, FIELD(self, CTL_TEXT, char*), FIELD(self, CTL_LENGTH, int),
                              &size);
        SelectObject(dc, old);
        surface = video_surface();
        VIRTUAL(surface, 0x68, surface_release_dc)(surface, dc);
        FIELD(self, CTL_EXTENT + 4, LONG) = size.cx;
        FIELD(self, EDIT_CARET_X, LONG) = size.cx;
    }
    return control_redraw(self);
}

/* Copy the text into `buffer` of `size` bytes. */
PORT(0x00474E90)
char* __fastcall edit_get_text(BYTE* self, void* edx_unused, char* buffer, int size)
{
    int count = FIELD(self, CTL_LENGTH, int) + 1;

    (void) edx_unused;
    if (count < size)
        size = count;
    game__strncpy(buffer, FIELD(self, CTL_TEXT, char*), size);
    buffer[size - 1] = 0;
    return buffer;
}

/* Take only the characters in `allowed`, or (NULL) any. The original drops an earlier set. */
PORT(0x00474ED0)
void __fastcall edit_set_allowed(BYTE* self, void* edx_unused, const char* allowed)
{
    (void) edx_unused;
    if (!allowed) {
        if (FIELD(self, EDIT_ALLOWED, char*))
            game_FUN_004a4450(FIELD(self, EDIT_ALLOWED, char*), GAMEUI_CPP, 0x16FB);
        FIELD(self, EDIT_ALLOWED, char*) = NULL;
        return;
    }
    FIELD(self, EDIT_ALLOWED, char*) = game_FUN_004a4410(text_length(allowed) + 1, GAMEUI_CPP,
                                                         0x16F7);
    if (FIELD(self, EDIT_ALLOWED, char*))
        text_copy(FIELD(self, EDIT_ALLOWED, char*), allowed);
}

/* `c` if it is allowed, else the other case of a letter if that is, else 0. */
PORT(0x00474F60)
char __fastcall edit_filter(BYTE* self, void* edx_unused, char c)
{
    const char* allowed = FIELD(self, EDIT_ALLOWED, const char*);
    char other;

    (void) edx_unused;
    if (!allowed)
        return c;
    if (c >= 'A' && c <= 'Z')
        other = c + 0x20;
    else if (c >= 'a' && c <= 'z')
        other = c - 0x20;
    else
        return game_FUN_0053b580(allowed, c) ? c : 0;
    if (game_FUN_0053b580(allowed, c))
        return c;
    return game_FUN_0053b580(FIELD(self, EDIT_ALLOWED, const char*), other) ? other : 0;
}

/* The caret in `colour`, or white when it is 0. */
PORT(0x00475000)
int __fastcall edit_set_caret_colour(BYTE* self, void* edx_unused, COLORREF colour)
{
    (void) edx_unused;
    if (FIELD(self, EDIT_CARET_PEN, HBRUSH))
        DeleteObject(FIELD(self, EDIT_CARET_PEN, HBRUSH));
    FIELD(self, EDIT_CARET_PEN, HBRUSH) = colour ? CreateSolidBrush(colour) : NULL;
    return control_redraw(self);
}

/* Vtable slot 33: losing capture ends the editing (event 10). */
PORT(0x00475050)
int __fastcall edit_lose_capture(BYTE* self, void* edx_unused, DWORD to)
{
    (void) edx_unused;
    edit_event(self, 10);
    control_redraw(self);
    return game_vf_32_47D940(self, NULL, to);
}

/* Vtable slot 55: not pressed while it has capture. */
PORT(0x004750C0)
int __fastcall edit_pressed(BYTE* self, void* edx_unused, DWORD button, DWORD b)
{
    (void) edx_unused;
    if (control_user(self)->capture == self)
        return 0;
    return control_pressed(self, NULL, button, b);
}

/* Room for `bytes` more, keeping the terminator. */
#define edit_room(self, bytes) \
    (FIELD(self, CTL_LENGTH, int) < FIELD(self, EDIT_CAPACITY, int) - (bytes))

/*
    Vtable slot 20: a character typed while it has capture. Backspace deletes before the caret,
    Enter ends the editing unless the dialog takes it, Tab and Escape do nothing, and anything
    else goes in at the caret, filtered by the allowed characters. A double-byte character comes
    as two calls, the lead byte first. Every key plays the type sound, and one that does not fit
    the full sound. The dialog hears of every change (event 19).
*/
PORT(0x004750F0)
int __fastcall edit_char(BYTE* self, void* edx_unused, DWORD key)
{
    char c = (char) key;
    char* text;
    int caret;

    (void) edx_unused;
    if (control_clicked(self, NULL, key) || control_user(self)->capture != self)
        return 0;
    if (FIELD(self, EDIT_TYPE_SOUND, void*))
        play_sound(self, FIELD(self, EDIT_TYPE_SOUND, void*));

    text = FIELD(self, CTL_TEXT, char*);
    caret = FIELD(self, EDIT_CARET_POS, int);
    switch (c) {
    case '\b':
        if (FIELD(self, CTL_LENGTH, int) && caret > 0) {
            char* previous = CharPrevA(text, text + caret);

            if (previous) {
                int width = IsDBCSLeadByte((BYTE) *previous) ? 2 : 1;

                game_FUN_0053c350(previous, text + caret, FIELD(self, CTL_LENGTH, int) - caret + 1);
                FIELD(self, CTL_LENGTH, int) -= width;
                FIELD(self, EDIT_CARET_POS, int) -= width;
            }
        }
        break;
    case '\t':
    case 0x1B:
        break;
    case '\r':
        if (edit_event(self, 10))
            return 1;
        break;
    default: {
        BOOL lead = !FIELD(self, EDIT_LEAD, int) && IsDBCSLeadByte((BYTE) c);
        BOOL fits;

        if (!FIELD(self, EDIT_LEAD, int) && !lead) {
            c = edit_filter(self, NULL, c);
            if (!c)
                break;
        }
        FIELD(self, EDIT_LEAD, int) = 0;
        if (lead) {
            FIELD(self, EDIT_LEAD, int) = 1;
            fits = edit_room(self, 2);
            FIELD(self, EDIT_LEAD_LOST, int) = !fits;
        } else if (FIELD(self, EDIT_LEAD_LOST, int)) {
            FIELD(self, EDIT_LEAD_LOST, int) = 0;
            fits = FALSE;
        } else {
            fits = edit_room(self, 1);
        }
        if (fits) {
            caret = FIELD(self, EDIT_CARET_POS, int);
            text = FIELD(self, CTL_TEXT, char*);
            game_FUN_0053c350(text + caret + 1, text + caret, FIELD(self, CTL_LENGTH, int) - caret + 1);
            FIELD(self, CTL_TEXT, char*)[FIELD(self, EDIT_CARET_POS, int)] = c;
            FIELD(self, EDIT_CARET_POS, int)++;
            FIELD(self, CTL_LENGTH, int)++;
        } else if (FIELD(self, EDIT_FULL_SOUND, void*)) {
            play_sound(self, FIELD(self, EDIT_FULL_SOUND, void*));
        }
        break;
    }
    }

    edit_measure(self);
    FIELD(self, 0x1BC, int)++;
    edit_event(self, 19);
    return 1;
}

/*
    Vtable slot 21: a key pressed while it has capture: End, Home, Left and Right move the caret
    (over double-byte characters whole), Delete deletes after it.
*/
PORT(0x00475510)
int __fastcall edit_key(BYTE* self, void* edx_unused, DWORD key)
{
    char* text;
    int caret, length;

    (void) edx_unused;
    if (control_unclicked(self, NULL, key) || control_user(self)->capture != self)
        return 0;
    if (FIELD(self, EDIT_TYPE_SOUND, void*))
        play_sound(self, FIELD(self, EDIT_TYPE_SOUND, void*));

    text = FIELD(self, CTL_TEXT, char*);
    caret = FIELD(self, EDIT_CARET_POS, int);
    length = FIELD(self, CTL_LENGTH, int);
    switch (key) {
    case VK_END:
        FIELD(self, EDIT_CARET_POS, int) = length;
        break;
    case VK_HOME:
        FIELD(self, EDIT_CARET_POS, int) = 0;
        FIELD(self, EDIT_CARET_X, int) = 0;
        break;
    case VK_LEFT:
        if (caret > 0)
            FIELD(self, EDIT_CARET_POS, int) -=
                text_on_double_byte(FIELD(self, CTL_DIALOG, BYTE*), NULL, text, text + caret - 1,
                                    (int) text_length(text)) ? 2 : 1;
        break;
    case VK_RIGHT:
        if (caret < length)
            FIELD(self, EDIT_CARET_POS, int) += IsDBCSLeadByte((BYTE) text[caret]) ? 2 : 1;
        break;
    case VK_DELETE:
        if (caret < length) {
            if (IsDBCSLeadByte((BYTE) text[caret])) {
                game_FUN_0053c350(text + caret, text + caret + 2, length - caret - 1);
                FIELD(self, CTL_LENGTH, int) -= 2;
            } else {
                game_FUN_0053c350(text + caret, text + caret + 1, length - caret);
                FIELD(self, CTL_LENGTH, int)--;
            }
        }
        break;
    }
    edit_measure(self);
    return 1;
}

/* ---- A UIButton of a given type ------------------------------------------------------------ */

#define UIKINDBUTTON_VTABLE ((void**) 0x00557FA8)

PORT(0x004757C0)
BYTE* __fastcall kind_button_construct(BYTE* self, void* edx_unused, int type, DWORD a,
                                       const RECT* area, BYTE* dialog)
{
    (void) edx_unused;
    button_construct(self, NULL, a, area, dialog);
    *(void***) self = UIKINDBUTTON_VTABLE;
    FIELD(self, CTL_TYPE, int) = type;
    return self;
}

PORT(0x00475810)
void __fastcall kind_button_destruct(BYTE* self)
{
    *(void***) self = UIKINDBUTTON_VTABLE;
    button_destruct(self);
}

PORT(0x004757F0)
BYTE* __fastcall kind_button_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    kind_button_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* ---- The kind buttons: arrows that step the lists of their group ------------------------------ */

/* Vtable slot 53: a walk over the controls of the control's dialog. */
typedef BYTE* (__fastcall* control_walk)(void* control);
#define control_walk_of(c) VIRTUAL(c, 0xD4, control_walk)(c)

#define ARROW_BACK  9               /* a kind button of type 9 steps back; 10, forward */
#define TIMER_HOLD  0x101           /* the first, longer wait while an arrow is held */
#define TIMER_STEP  0x102

void __fastcall list_step(BYTE* self, void* edx_unused, int by);

/* Step every list (type 3) of the group by `by`, from the start (0x23C) when `restart`. */
static void arrow_step(BYTE* self, int by, BOOL restart)
{
    BYTE* walk = control_walk_of(self);
    BYTE* control;

    while ((control = control_next_in_group(self, NULL, walk)) != NULL) {
        if (FIELD(control, CTL_TYPE, int) == 3) {
            if (restart)
                FIELD(control, 0x23C, DWORD) = 0;
            list_step(control, NULL, by);
        }
        if (restart && FIELD(control, CTL_TYPE, int) == 6) {
            BYTE* list = FIELD(control, 0x1FC, BYTE*);

            FIELD(list, 0x23C, DWORD) = 0;
            list_step(list, NULL, by);
        }
    }
    control_walk_end(self, NULL, walk);
}

/*
    Vtable slot 55: pressed with the first button while it has focus, the arrow steps its group
    (back for type 9) and tells the dialog (event 14), and steps again while it is held. The
    original reads the manager for that event from the dialog (0x30), not the control.
*/
PORT(0x00475820)
int __fastcall arrow_pressed(BYTE* self, void* edx_unused, DWORD button, DWORD b)
{
    (void) edx_unused;
    if (control_user(self)->focus == self && !button) {
        BYTE* dialog;
        ui_event event;

        event.e = 0;
        arrow_step(self, FIELD(self, CTL_TYPE, int) == ARROW_BACK ? -1 : 1, TRUE);
        event.a = FIELD(self, 0x74, DWORD);
        event.b = (DWORD) control_get_name(self);
        event.type = 14;
        dialog = FIELD(self, CTL_DIALOG, BYTE*);
        event.sender = dialog;
        event.c = (DWORD) self;
        event.manager = FIELD(dialog, 0x30, gui_manager*);
        VIRTUAL(dialog, 0x74, dialog_handle)(dialog, NULL, &event);
        dialog_add_timer(FIELD(self, CTL_DIALOG, BYTE*), NULL, TIMER_HOLD, 250, self);
    }
    return control_pressed(self, NULL, button, b);
}

/* Vtable slot 60: its timers. After the first wait it steps every 100 ms. */
PORT(0x00475990)
void __fastcall arrow_timer(BYTE* self, void* edx_unused, BYTE* timer)
{
    int by;

    (void) edx_unused;
    if (FIELD(timer, 8, DWORD) == TIMER_HOLD) {
        dialog_remove_child(FIELD(self, CTL_DIALOG, BYTE*), NULL, timer);
        dialog_add_timer(FIELD(self, CTL_DIALOG, BYTE*), NULL, TIMER_STEP, 100, self);
        return;
    }
    if (FIELD(timer, 8, DWORD) != TIMER_STEP)
        return;
    if (FIELD(self, CTL_TYPE, int) == ARROW_BACK)
        by = -1;
    else if (FIELD(self, CTL_TYPE, int) == ARROW_BACK + 1)
        by = 1;
    else
        return;
    arrow_step(self, by, FALSE);
    edit_event(self, 14);
}

/* Vtable slot 56: released after a press, it tells the dialog (event 1). */
PORT(0x00475AB0)
int __fastcall arrow_released(BYTE* self, void* edx_unused, DWORD button, DWORD b)
{
    (void) edx_unused;
    if (control_user(self)->focus == self && !button && FIELD(self, 0x60, int) == 2
        && edit_event(self, 1))
        return 1;
    return control_released(self, NULL, button, b);
}

/* ---- UISlider: a thumb along a track ---------------------------------------------------------- */

/*
    A UIControl of type 8 (across) or other (down). Its value runs from 0 to SLIDER_RANGE; the
    thumb's offset in pixels is SLIDER_AT. It can be bound (0x1B4) to an int.
*/
#define UISLIDER_VTABLE ((void**) 0x005580B0)
#define SLIDER_AT       0x1EC
#define SLIDER_STEPS    0x1F0       /* the values it steps through, or 0 to step by pixels */
#define SLIDER_RANGE    0x200

int __fastcall slider_get_value(BYTE* self);
BOOL __fastcall slider_set_value(BYTE* self, void* edx_unused, int value, unsigned range);
#define slider_value(s)       slider_get_value(s)
#define slider_set(s, v, r)   slider_set_value(s, NULL, v, r)
#define slider_steps(s)       FIELD(s, SLIDER_STEPS, DWORD)

PORT(0x00475B40)
BYTE* __fastcall slider_construct(BYTE* self, void* edx_unused, int type, DWORD a,
                                  const RECT* area, BYTE* dialog)
{
    (void) edx_unused;
    control_construct(self, NULL, type, a, area, dialog);
    *(void***) self = UISLIDER_VTABLE;
    FIELD(self, SLIDER_AT, DWORD) = 0;
    FIELD(self, SLIDER_STEPS, DWORD) = 0;
    FIELD(self, 0x1F4, DWORD) = 0;
    FIELD(self, 0x1F8, DWORD) = 0;
    FIELD(self, 0x1FC, DWORD) = 0;
    FIELD(self, 0x208, DWORD) = 0;
    FIELD(self, 0x204, DWORD) = 0;
    FIELD(self, 0x210, DWORD) = 0;
    FIELD(self, 0x20C, DWORD) = 0;
    FIELD(self, 0x218, DWORD) = 0;
    FIELD(self, 0x21C, DWORD) = 0;
    FIELD(self, SLIDER_RANGE, int) = 100;
    FIELD(self, 0x214, float) = 0.2f;
    return self;
}

/* Vtable slot 59: copy the value out to the bound int, or (`out` FALSE) take it from there. */
#ifdef OPENMCM2_DEV
static int slider_track(BYTE* self);
#endif

PORT(0x00475C50)
void __fastcall slider_commit(BYTE* self, void* edx_unused, BOOL out)
{
    (void) edx_unused;
#ifdef OPENMCM2_DEV
    if (out && slider_track(self) <= 0)     /* put out of sight (no track): its setting kept */
        return;
#endif
    if (out)
        *FIELD(self, EDIT_BOUND, int*) = slider_value(self);
    else
        slider_set(self, *FIELD(self, EDIT_BOUND, int*), FIELD(self, SLIDER_RANGE, int));
}

PORT(0x00475BF0)
void __fastcall slider_destruct(BYTE* self)
{
    *(void***) self = UISLIDER_VTABLE;
    if (FIELD(self, EDIT_BOUND, int*))
        slider_commit(self, NULL, TRUE);
    control_destruct(self);
}

PORT(0x00475BD0)
BYTE* __fastcall slider_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    slider_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Device type 2 (a joystick) whose 0x10 is 4. */
BOOL __fastcall game_FUN_0048a770(void* device, void* edx_unused, int kind);

/*
    Vtable slot 10: tick. While it has capture, each joystick claimed by its user nudges it a step
    along its down axis, once per push (the dialog's 0x7F1C rearms when the stick comes back to
    the middle), between 0 and its steps, or its width when it has none. Then the base tick; a
    list of its group that has run out of entries (0x1EC past 0x200) leaves its picture unset.
*/
PORT(0x00475C90)
int __fastcall slider_tick(BYTE* self, void* edx_unused, DWORD time)
{
    unsigned width = (unsigned) VIRTUAL(self, 0xF4, method_0)(self);
    gui_input* input;
    int index = 0;
    int result;

    (void) edx_unused;
    while ((input = gui_user_claimed(control_user(self), NULL, index++)) != NULL) {
        BYTE* device = input->device;
        BYTE* dialog;
        int push;

        if (FIELD(device, 0xC, int) != 2 || !game_FUN_0048a770(device, NULL, 4))
            continue;
        dialog = FIELD(self, CTL_DIALOG, BYTE*);
        push = input->y;
        if (control_user(self)->capture != self)
            continue;
        if (push == 0) {
            FIELD(dialog, 0x7F1C, DWORD) = 1;
            continue;
        }
        if (!FIELD(dialog, 0x7F1C, DWORD))
            continue;
        if (push < 0) {
            if (slider_value(self))
                slider_set(self, slider_value(self) - 1,
                           slider_steps(self) ? slider_steps(self) : width);
        } else {
            unsigned top = slider_steps(self) ? slider_steps(self) : width;

            if ((unsigned) slider_value(self) < top)
                slider_set(self, slider_value(self) + 1, top);
        }
        FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0x7F1C, DWORD) = 0;
    }

    result = control_tick(self, NULL, time);
    if (result) {
        BYTE* walk = control_walk_of(self);
        BYTE* control;
        BOOL spent = FALSE;

        while ((control = control_next_in_group(self, NULL, walk)) != NULL) {
            if (FIELD(control, CTL_TYPE, int) == 3
                && FIELD(control, 0x200, int) >= FIELD(control, 0x1EC, int))
                spent = TRUE;
        }
        control_walk_end(self, NULL, walk);
        if (spent)
            FIELD(self, 0x1CC, DWORD) = 0;
    }
    return result;
}

/*
    The slider's numbers. SLIDER_AT is the thumb's offset along the track, a float; the track is
    the control's width (type 8) or height less the thumb's (SLIDER_THUMB). Reversed (0x21C), the
    offset counts from the far end. The original works these in x87 doubles from unsigned
    widths, and so do these.
*/
#define SLIDER_THUMB    0x1F4       /* int[2]: the thumb's width and height */
#define SLIDER_FIRST    0x1FC       /* the first entry of a list it follows */
#define SLIDER_REVERSED 0x21C
#define slider_across(s) (FIELD(s, CTL_TYPE, int) == 8)
#define list_first_shown(l) option_current_index(l)   /* its first entry showing (0x1F0) */

int __fastcall option_current_index(BYTE* self);

/* The track's length: the control's size along it, less the thumb's. */
static int slider_track(BYTE* self)
{
    if (slider_across(self))
        return VIRTUAL(self, 0xF4, method_0)(self) - FIELD(self, SLIDER_THUMB, int);
    return VIRTUAL(self, 0xF8, method_0)(self) - FIELD(self, SLIDER_THUMB + 4, int);
}

/* Less as the x87 compare tests it (C0 set): unordered counts too. */
static BOOL x87_less(double a, double b)
{
    return !(a >= b);
}

/* `at` along `track`, or the track's end when `at` reaches it. */
static float slider_clamp(double at, double track)
{
    float rounded = (float) at;

    return x87_less(at, track) ? rounded : (float) track;
}

/* The thumb's size: its picture for `state`, scaled as the dialog is. */
PORT(0x00476100)
void __fastcall slider_measure_thumb(BYTE* self, void* edx_unused, int state)
{
    BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);

    (void) edx_unused;
    FIELD(self, SLIDER_THUMB, int) = (int) (FIELD(FIELD(FIELD(self, 0x16C + 4 * state, BYTE*), 0x28,
                                                        BYTE*), 0xC, int)
                                            * (double) FIELD(dialog, 0x9C4, float));
    FIELD(self, SLIDER_THUMB + 4, int) = (int) (FIELD(FIELD(FIELD(self, 0x16C + 4 * state, BYTE*),
                                                            0x28, BYTE*), 0x10, int)
                                                * (double) FIELD(dialog, 0x9C8, float));
}

PORT(0x00476160)
int __fastcall slider_set_range(BYTE* self, void* edx_unused, int range)
{
    (void) edx_unused;
    FIELD(self, SLIDER_RANGE, int) = range;
    return control_redraw(self);
}

/* How many entries `list` scrolls through. */
PORT(0x00476180)
int __fastcall slider_list_span(BYTE* self, void* edx_unused, BYTE* list)
{
    (void) self;
    (void) edx_unused;
    return FIELD(list, 0x1EC, int) - FIELD(list, 0x204, int);
}

/* Put the thumb where `list` has scrolled to. FALSE when the list does not scroll. */
PORT(0x004761A0)
BOOL __fastcall slider_follow(BYTE* self, void* edx_unused, BYTE* list)
{
    int track, span;
    double length;

    (void) edx_unused;
    control_redraw(self);
    if (FIELD(self, SLIDER_REVERSED, int))
        FIELD(self, SLIDER_FIRST, int) = FIELD(list, 0x1EC, int) - list_first_shown(list);
    else
        FIELD(self, SLIDER_FIRST, int) = list_first_shown(list);
    if (slider_list_span(self, NULL, list) == FIELD(self, SLIDER_FIRST, int))
        return TRUE;

    track = slider_track(self);
    span = FIELD(list, 0x1EC, int) - FIELD(list, 0x204, int);
    if (!span)
        return FALSE;
    length = (double) (unsigned) track;
    FIELD(self, SLIDER_AT, float) = slider_clamp(FIELD(self, SLIDER_FIRST, int) * length / span,
                                                 length);
    return TRUE;
}

/* The value, out of `range`, that the thumb is at; rounded down. */
PORT(0x004762A0)
int __fastcall slider_value_at(BYTE* self, void* edx_unused, int range)
{
    int width = VIRTUAL(self, 0xF4, method_0)(self);
    int height = VIRTUAL(self, 0xF8, method_0)(self);
    int offset, track;

    (void) edx_unused;
    if (!FIELD(self, SLIDER_REVERSED, int))
        offset = (int) FIELD(self, SLIDER_AT, float);
    else
        offset = (int) ((slider_across(self) ? width : height)
                        - (double) FIELD(self, SLIDER_AT, float));
    track = slider_across(self) ? width - FIELD(self, SLIDER_THUMB, int)
                                : height - FIELD(self, SLIDER_THUMB + 4, int);
    if (!track)
        return (int) 0.0;
    return (int) ((double) (unsigned) (offset * range) / track);
}

/* Put the thumb at `value` out of `range` (the offset itself when `range` is 0). */
PORT(0x00476360)
BOOL __fastcall slider_set_value(BYTE* self, void* edx_unused, int value, unsigned range)
{
    unsigned at = FIELD(self, SLIDER_REVERSED, int) ? range - value : (unsigned) value;
    unsigned track;

    (void) edx_unused;
    control_redraw(self);
    if (!slider_steps(self) && (unsigned) slider_value_at(self, NULL, range) == at)
        return TRUE;
    if (!range) {
        FIELD(self, SLIDER_AT, float) = (float) (double) at;
        return TRUE;
    }
    track = (unsigned) slider_track(self);
    FIELD(self, SLIDER_AT, float) = slider_clamp((double) (track * at) / (double) range,
                                                 (double) track);
    return TRUE;
}

/* Step through `count` values, or by pixels when `count` is 0. */
PORT(0x00476470)
int __fastcall slider_set_steps(BYTE* self, void* edx_unused, int count)
{
    (void) edx_unused;
    FIELD(self, SLIDER_STEPS, int) = count > 0 ? count - 1 : 0;
    return control_redraw(self);
}

/*
    The value, out of SLIDER_RANGE when it has one: the offset rounded up (not reversed) or down
    (reversed), scaled to the track.
*/
PORT(0x004764A0)
int __fastcall slider_get_value(BYTE* self)
{
    unsigned width = (unsigned) VIRTUAL(self, 0xF4, method_0)(self);
    unsigned height = (unsigned) VIRTUAL(self, 0xF8, method_0)(self);
    unsigned offset;

    if (!FIELD(self, SLIDER_REVERSED, int)) {
        offset = (unsigned) (int) FIELD(self, SLIDER_AT, float);
        if (x87_less((int) offset, FIELD(self, SLIDER_AT, float)))
            offset++;
    } else {
        offset = (unsigned) (int) ((slider_across(self) ? (int) width : (int) height)
                                   - (double) FIELD(self, SLIDER_AT, float));
    }
    if (!FIELD(self, SLIDER_RANGE, int) || !width)
        return (int) offset;
#ifdef OPENMCM2_DEV
    if (slider_track(self) <= 0)            /* the thumb fills it: the game's divided by 0 */
        return 0;
#endif
    if (slider_across(self))
        return (int) (FIELD(self, SLIDER_RANGE, unsigned) * offset
                      / (width - FIELD(self, SLIDER_THUMB, unsigned)));
    return (int) (FIELD(self, SLIDER_RANGE, unsigned) * offset
                  / (height - FIELD(self, SLIDER_THUMB + 4, unsigned)));
}

#define SLIDER_TRACK    0x204       /* ui_anim*: the track's picture */
#define SLIDER_DRAGGING 0x208
#define SLIDER_HELD_AT  0x20C       /* POINT: where the pointer last stopped */
#define SLIDER_HOLD     0x214       /* seconds there before event 17 */
#define SLIDER_HELD     0x218

BOOL __fastcall list_scroll(BYTE* self, void* edx_unused, int first, BOOL tell);

/* Fill `event` as the control's (keeping its `e`) and tell the dialog. */
static void control_tell(BYTE* self, ui_event* event, DWORD type)
{
    BYTE* dialog;

    event->a = FIELD(self, 0x74, DWORD);
    event->type = type;
    event->b = (DWORD) control_get_name(self);
    event->manager = FIELD(self, CTL_MANAGER, gui_manager*);
    dialog = FIELD(self, CTL_DIALOG, BYTE*);
    event->sender = dialog;
    event->c = (DWORD) self;
#ifdef OPENMCM2_DEV
    dev_trace_event(event->type, (const char*) event->b, dialog);
#endif
    VIRTUAL(dialog, 0x74, dialog_handle)(dialog, NULL, event);
}

/* Draw `picture` in `box`, through the dialog's background or straight to the screen. */
static BOOL slider_blit(BYTE* self, BYTE* picture, RECT* box)
{
    BYTE* background = FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_BACKGROUND, BYTE*);
    DWORD flags = FIELD(picture, 0x30, int) ? 0x1008000 : 0x1000000;

    if (!background)
        return VIRTUAL(object_screen(self), 0xC, screen_draw)(object_screen(self), NULL, box,
                                                             picture, NULL, flags) != 0;
    background_draw(background, NULL, picture, box, NULL, flags, FIELD(self, CTL_SLOT, int),
                    FIELD(self, 0x1C0, DWORD), &FIELD(self, 0x1BC, int), 0);
    return TRUE;
}

/*
    Vtable slot 40: render. The track centred across the control, then the thumb at its offset,
    centred the other way. A thumb with one frame settles; otherwise it is drawn again.
*/
PORT(0x00475E70)
int __fastcall slider_render(BYTE* self)
{
    const RECT* area = &FIELD(self, CTL_RECT, RECT);
    BYTE* thumb;
    RECT box;

    if (FIELD(self, SLIDER_TRACK, ui_anim*) && FIELD(self, 0x1CC, BYTE*)) {
        BYTE* track = anim_picture(FIELD(self, SLIDER_TRACK, ui_anim*));
        int redraws = FIELD(self, 0x1BC, int);
        int margin;

        box = *area;
        if (slider_across(self)) {
            margin = (area->bottom - area->top - FIELD(track, 0x18, int)) / 2;
            box.top += margin < 0 ? 0 : margin;
            box.bottom = FIELD(track, 0x18, int) + box.top;
        } else {
            margin = (area->right - area->left - FIELD(track, 0x14, int)) / 2;
            box.left += margin < 0 ? 0 : margin;
            box.right = FIELD(track, 0x14, int) + box.left;
        }
        if (!slider_blit(self, track, &box))
            return 0;
        FIELD(self, 0x1BC, int) = redraws;
    }

    thumb = FIELD(self, 0x1CC, BYTE*);
    if (!thumb)
        return 1;
    if (slider_across(self)) {
        box.top = (area->bottom - FIELD(self, SLIDER_THUMB + 4, int) - area->top) / 2 + area->top;
        box.left = (int) FIELD(self, SLIDER_AT, float) + area->left;
        box.bottom = FIELD(self, SLIDER_THUMB + 4, int) + box.top;
        box.right = FIELD(self, SLIDER_THUMB, int) + box.left;
    } else {
        box.top = (int) FIELD(self, SLIDER_AT, float) + area->top;
        box.left = (area->right - FIELD(self, SLIDER_THUMB, int) - area->left) / 2 + area->left;
        box.bottom = FIELD(self, SLIDER_THUMB + 4, int) + box.top;
        box.right = FIELD(self, SLIDER_THUMB, int) + box.left;
    }
    if (!FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_BACKGROUND, BYTE*))
        return slider_blit(self, thumb, &box) ? 1 : 0;
    slider_blit(self, thumb, &box);
    if (FIELD(FIELD(self, 0x16C + 4 * FIELD(self, 0x60, int), BYTE*), 0x10, int) == 1)
        FIELD(self, 0x1C0, DWORD) = 1;
    else
        control_redraw(self);
    return 1;
}

/* Scroll every list (type 3) of the group to `first`, or by a page of 0x200 entries. */
static void slider_page(BYTE* self, int direction)
{
    BYTE* walk = control_walk_of(self);
    BYTE* list;

    while ((list = control_next_in_group(self, NULL, walk)) != NULL) {
        if (FIELD(list, CTL_TYPE, int) == 3) {
            int page = FIELD(list, 0x200, int);

            list_scroll(list, NULL, list_first_shown(list) + direction * page, TRUE);
        }
    }
    control_walk_end(self, NULL, walk);
}

/* An offset along the track: no less than 0, no more than the track. */
static int slider_place(int offset, int track)
{
    if (offset < 0)
        offset = 0;
    return track < offset ? track : offset;
}

/*
    Vtable slot 55: pressed with the first button while it has focus. With steps the thumb jumps
    to the pointer; without, a press past the thumb pages its lists forward, before it back.
*/
PORT(0x00476560)
int __fastcall slider_pressed(BYTE* self, void* edx_unused, DWORD button, const POINT* at)
{
    (void) edx_unused;
    if (control_user(self)->focus == self && !button && at) {
        BOOL across = slider_across(self);
        LONG near_edge = across ? FIELD(self, CTL_RECT, RECT).left : FIELD(self, CTL_RECT, RECT).top;
        LONG pointer = across ? at->x : at->y;
        int thumb = FIELD(self, across ? SLIDER_THUMB : SLIDER_THUMB + 4, int);

        if (slider_steps(self)) {
            int track = (across ? VIRTUAL(self, 0xF4, method_0)(self)
                                : VIRTUAL(self, 0xF8, method_0)(self)) - thumb;

            FIELD(self, SLIDER_AT, float) = (float) slider_place(pointer - near_edge, track);
        } else {
            double from = (double) near_edge + FIELD(self, SLIDER_AT, float);

            if ((double) pointer > (double) thumb + near_edge + FIELD(self, SLIDER_AT, float))
                slider_page(self, 1);
            else if (x87_less(pointer, from))
                slider_page(self, -1);
        }
        if (slider_steps(self))
            slider_set_value(self, NULL, slider_value_at(self, NULL, slider_steps(self)),
                             slider_steps(self));
    }
    return control_pressed(self, NULL, button, (DWORD) at);
}

/* Vtable slot 56: released, it ends a drag (event 16), then is released as a kind button is. */
PORT(0x00476810)
int __fastcall slider_released(BYTE* self, void* edx_unused, DWORD button, DWORD b)
{
    ui_event event;

    (void) edx_unused;
    event.e = 0;
    if (FIELD(self, SLIDER_DRAGGING, int))
        control_tell(self, &event, 16);
    FIELD(self, SLIDER_DRAGGING, int) = 0;
    if (control_user(self)->focus == self && !button && FIELD(self, 0x60, int) == 2) {
        control_tell(self, &event, 1);
        if (event.e)
            return 1;
    }
    return control_released(self, NULL, button, b);
}

/*
    Vtable slot 57: dragged with the first button: the thumb follows the pointer (centred on it
    without steps), the dialog hears of the drag starting (15), of each move (3) and of the
    pointer resting SLIDER_HOLD seconds in one place (17), and the lists follow the thumb.
*/
PORT(0x00476900)
void __fastcall slider_drag(BYTE* self, void* edx_unused, DWORD button, const POINT* at)
{
    int width, height, steps, offset;
    ui_event event;

    (void) edx_unused;
    control_drag(self, NULL, button, at);
    event.e = 0;
    if (button || !at)
        return;
    width = VIRTUAL(self, 0xF4, method_0)(self);
    height = VIRTUAL(self, 0xF8, method_0)(self);
    control_redraw(self);
    steps = slider_steps(self);
    if (slider_across(self)) {
        offset = at->x - FIELD(self, CTL_RECT, RECT).left;
        if (!steps)
            offset -= FIELD(self, SLIDER_THUMB, int) / 2;
        offset = slider_place(offset, width - FIELD(self, SLIDER_THUMB, int));
    } else {
        offset = at->y - FIELD(self, CTL_RECT, RECT).top;
        if (!steps)
            offset -= FIELD(self, SLIDER_THUMB + 4, int) / 2;
        offset = slider_place(offset, height - FIELD(self, SLIDER_THUMB + 4, int));
    }
    FIELD(self, SLIDER_AT, float) = (float) offset;
    if (steps)
        slider_set_value(self, NULL, slider_value_at(self, NULL, steps), steps);

    if (!FIELD(self, SLIDER_DRAGGING, int)) {
        control_tell(self, &event, 15);
        FIELD(self, SLIDER_DRAGGING, int) = 1;
    }
    if (event.e)
        return;
    if (at->x == FIELD(self, SLIDER_HELD_AT, LONG) && at->y == FIELD(self, SLIDER_HELD_AT + 4, LONG)) {
        double held = (double) FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0x9C0, float)
                      + FIELD(self, SLIDER_HELD, float);

        FIELD(self, SLIDER_HELD, float) = (float) held;
        if (!x87_less(held, FIELD(self, SLIDER_HOLD, float))) {
            control_tell(self, &event, 17);
            if (event.e)
                return;
        }
    } else {
        control_tell(self, &event, 3);
        FIELD(self, SLIDER_HELD_AT, LONG) = at->x;
        FIELD(self, SLIDER_HELD_AT + 4, LONG) = at->y;
        FIELD(self, SLIDER_HELD, float) = 0.0f;
        if (event.e)
            return;
    }

    {
        BYTE* walk = control_walk_of(self);
        BYTE* list;

        while ((list = control_next_in_group(self, NULL, walk)) != NULL) {
            if (FIELD(list, CTL_TYPE, int) == 3)
                list_scroll(list, NULL,
                                  slider_value_at(self, NULL, FIELD(list, 0x1EC, int)
                                                              - FIELD(list, 0x204, int)),
                                  FALSE);
        }
        control_walk_end(self, NULL, walk);
    }
}

/* ---- UIList: a scrolling list of entries ------------------------------------------------------- */

/* A list entry, 56 bytes: text (kind 1) or a picture, with its row's height and colours. */
typedef struct list_entry {
    int       kind;
    DWORD     b;
    int       length;           /* of the text */
    int       height;           /* 0x0C: its row's */
    int       order;            /* 0x10: its place before a sort */
    char*     text;             /* 0x14, on the game's heap */
    char*     text_2;           /* 0x18, on the game's heap */
    DWORD     data;             /* 0x1C: the caller's */
    ui_anim*  anim;             /* 0x20 */
    text_run* runs;             /* 0x24: the text's markup, on the game's heap */
    COLORREF  colour;           /* 0x28: its text's, or 0 for the list's */
    DWORD     unknown_2c;
    DWORD     drawn;            /* 0x30: drawn since the list was last redrawn */
    HBRUSH    brush;            /* 0x34, possibly shared with other entries */
} list_entry;

_Static_assert(sizeof(list_entry) == 0x38, "a list entry is 56 bytes");

#define UILIST_VTABLE   ((void**) 0x005581B8)
#define LIST_COUNT      0x1EC
#define LIST_FIRST      0x1F0       /* the first entry showing */
#define LIST_SELECTED   0x1F4
#define LIST_CAPACITY   0x1FC
#define LIST_ROWS       0x200       /* the entries that show at once */
#define LIST_HIGHLIGHT  0x20C       /* HBRUSH behind the selection, or none */
#define LIST_BRUSH_2    0x210
#define LIST_ENTRIES    0x214       /* list_entry[LIST_CAPACITY], on the game's heap */
#define NO_COLOUR       0xFF000000

#define list_entries(l) FIELD(l, LIST_ENTRIES, list_entry*)
#define list_count(l)   FIELD(l, LIST_COUNT, int)

/* Vtable slot 65: select an entry. */
typedef int (__fastcall* list_select_slot)(void* list, void* edx_unused, int index);
#define list_select_virtual(l, i) VIRTUAL(l, 0x104, list_select_slot)(l, NULL, i)

void __fastcall list_tell_sliders(BYTE* self);
int __cdecl list_compare_text(const list_entry* a, const list_entry* b);

PORT(0x00476C00)
BYTE* __fastcall list_construct(BYTE* self, void* edx_unused, DWORD a, int capacity,
                                const RECT* area, BYTE* dialog)
{
    LONG height;

    (void) edx_unused;
    control_construct(self, NULL, 3, a, area, dialog);
    *(void***) self = UILIST_VTABLE;
    FIELD(self, LIST_BRUSH_2, HBRUSH) = NULL;
    FIELD(self, LIST_HIGHLIGHT, HBRUSH) = NULL;
    FIELD(self, LIST_SELECTED, int) = 0;
    FIELD(self, LIST_CAPACITY, int) = capacity;
    FIELD(self, 0x208, COLORREF) = RGB(0xFF, 0xFF, 0xFF);
    list_entries(self) = game_FUN_004a4410(capacity * sizeof(list_entry), GAMEUI_CPP, 0x1B50);
    FIELD(self, 0x24C, void*) = (void*) list_compare_text;
    FIELD(self, 0x218, BYTE*) = self;
    FIELD(self, 0x21C, DWORD) = 0;
    FIELD(self, 0x220, DWORD) = 0;
    FIELD(self, 0x224, DWORD) = 1;
    FIELD(self, 0x228, DWORD) = 1;
    FIELD(self, 0x234, DWORD) = 0;
    FIELD(self, 0x230, DWORD) = 0;
    FIELD(self, 0x22C, DWORD) = 0;
    FIELD(self, 0x23C, DWORD) = 0;
    FIELD(self, 0x240, DWORD) = 0;
    list_count(self) = 0;
    FIELD(self, LIST_FIRST, int) = 0;
    FIELD(self, 0x204, DWORD) = 0;
    __stosb((BYTE*) list_entries(self), 0, FIELD(self, LIST_CAPACITY, int) * sizeof(list_entry));
    FIELD(self, 0x244, DWORD) = 1;

    /* As many rows as its text's height (or the dialog's) fits, at least one. */
    height = FIELD(self, 0x160, LONG);
    if (!height && (!dialog || !FIELD(dialog, 0xDC, LONG))) {
        FIELD(self, LIST_ROWS, int) = 1;
    } else {
        int rows;

        if (!height)
            height = FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0xDC, LONG);
        rows = (FIELD(self, CTL_RECT, RECT).bottom - FIELD(self, CTL_RECT, RECT).top) / height;
        FIELD(self, LIST_ROWS, int) = rows < 1 ? 1 : rows;
    }
    return self;
}

void __fastcall list_commit(BYTE* self, void* edx_unused, BOOL out);

/* Entries share brushes; one is deleted once, and cleared from every entry that has it. */
PORT(0x00476DB0)
void __fastcall list_destruct(BYTE* self)
{
    int i;

    *(void***) self = UILIST_VTABLE;
    if (FIELD(self, EDIT_BOUND, int*))
        list_commit(self, NULL, TRUE);
    if (FIELD(self, LIST_HIGHLIGHT, HBRUSH))
        DeleteObject(FIELD(self, LIST_HIGHLIGHT, HBRUSH));
    if (FIELD(self, LIST_BRUSH_2, HBRUSH))
        DeleteObject(FIELD(self, LIST_BRUSH_2, HBRUSH));
    for (i = 0; i < list_count(self); i++) {
        list_entry* entry = &list_entries(self)[i];

        game_FUN_004a4450(entry->text, GAMEUI_CPP, 0x1B79);
        game_FUN_004a4450(list_entries(self)[i].text_2, GAMEUI_CPP, 0x1B7A);
        game_FUN_004a4450(list_entries(self)[i].runs, GAMEUI_CPP, 0x1B7B);
        entry = &list_entries(self)[i];
        if (entry->anim && !entry->anim->unknown_f4)
            object_destroy(entry->anim);
        if (list_entries(self)[i].brush) {
            HBRUSH brush = list_entries(self)[i].brush;
            int j;

            DeleteObject(brush);
            for (j = 0; j < list_count(self); j++) {
                if (list_entries(self)[j].brush == list_entries(self)[i].brush)
                    list_entries(self)[j].brush = NULL;
            }
        }
    }
    game_FUN_004a4450(list_entries(self), GAMEUI_CPP, 0x1B88);
    control_destruct(self);
}

PORT(0x00476D90)
BYTE* __fastcall list_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    list_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Vtable slot 27: step every entry's animation. */
PORT(0x00476F30)
void __fastcall list_animate(BYTE* self)
{
    int i;

    for (i = 0; i < list_count(self); i++) {
        if (list_entries(self)[i].anim)
            anim_step(list_entries(self)[i].anim);
    }
}

/* Show entries from `first`, as far as the list fills; with one row that selects it too. */
PORT(0x004777F0)
BOOL __fastcall list_scroll(BYTE* self, void* edx_unused, int first, BOOL tell)
{
    int last = list_count(self) - FIELD(self, LIST_ROWS, int);

    (void) edx_unused;
    if (first < last)
        last = first;
    FIELD(self, LIST_FIRST, int) = last < 0 ? 0 : last;
    if (FIELD(self, LIST_ROWS, int) == 1)
        list_select_virtual(self, FIELD(self, LIST_FIRST, int));
    control_redraw(self);
    if (tell)
        list_tell_sliders(self);
    return TRUE;
}

/* An entry's data: the selection's when `index` is -1; 0 with nothing selected. */
PORT(0x00477860)
DWORD __fastcall list_data(BYTE* self, void* edx_unused, int index)
{
    (void) edx_unused;
    if (index == -1) {
        index = FIELD(self, LIST_SELECTED, int);
        if (index == -1)
            return 0;
    }
    return list_entries(self)[index].data;
}

/* The entry with `data`, or -1. */
PORT(0x00477890)
int __fastcall list_find_data(BYTE* self, void* edx_unused, DWORD data)
{
    int i;

    (void) edx_unused;
    for (i = 0; i < list_count(self); i++) {
        if (list_entries(self)[i].data == data)
            return i;
    }
    return -1;
}

PORT(0x004778C0)
void __fastcall list_set_data(BYTE* self, void* edx_unused, int index, DWORD data)
{
    (void) edx_unused;
    list_entries(self)[index].data = data;
}

#ifdef OPENMCM2_DEV
/*
    A list's picture entry in a modern dialog that is the game's own (a track's, a bike's): its
    row and picture ArtScale times its pixels, as the layout around it is.
*/
static int list_entry_art(BYTE* self, int index)
{
    list_entry* entry = &list_entries(self)[index];
    int art = dialog_art(FIELD(self, CTL_DIALOG, BYTE*));

    if (art <= 1 || (entry->kind != 2 && entry->kind != 3) || !entry->anim)
        return 1;
    if (modern_marked(anim_current(entry->anim)->picture))
        return 1;
    return game_picture_art(self, anim_current(entry->anim)->width, FIELD(self, 0xE8, LONG));
}
#endif

/* The selected entry, or -1 when the list is empty. */
PORT(0x004778E0)
int __fastcall list_selected(BYTE* self)
{
    return list_count(self) > 0 ? FIELD(self, LIST_SELECTED, int) : -1;
}

PORT(0x00477900)
int __fastcall list_kind(BYTE* self, void* edx_unused, int index)
{
    (void) edx_unused;
    if (list_count(self) && index > -1 && index < list_count(self))
        return list_entries(self)[index].kind;
    return 0;
}

PORT(0x00477930)
int __fastcall list_height(BYTE* self, void* edx_unused, int index)
{
    (void) edx_unused;
    if (list_count(self) && index > -1 && index < list_count(self))
#ifdef OPENMCM2_DEV
        return list_entries(self)[index].height * list_entry_art(self, index);
#else
        return list_entries(self)[index].height;
#endif
    return 0;
}

/*
    Vtable slot 65: select `index` (only 0 in an empty list), scrolling as little as shows it.
*/
PORT(0x00477970)
int __fastcall list_select(BYTE* self, void* edx_unused, int index)
{
    int count = list_count(self);
    int first = FIELD(self, LIST_FIRST, int);

    (void) edx_unused;
    if (!(count && index < count && index >= 0) && (count || index))
        return 1;
    FIELD(self, LIST_SELECTED, int) = index;
    if (index >= first) {
        int last = FIELD(self, LIST_ROWS, int) + first - 1;

        if (last < 0)
            last = 0;
        if (index > last) {
            first = index - FIELD(self, LIST_ROWS, int) + 1;
            FIELD(self, LIST_FIRST, int) = first < 0 ? 0 : first;
        }
    } else {
        FIELD(self, LIST_FIRST, int) = index;
    }
    control_redraw(self);
    return 1;
}

/* Select `index` here and in every list like it on the dialog, and tell its sliders. */
PORT(0x004779F0)
int __fastcall list_select_all(BYTE* self, void* edx_unused, int index)
{
    int result = list_select_virtual(self, index);

    (void) edx_unused;
    list_tell_sliders(self);
    if (FIELD(self, CTL_DIALOG, BYTE*)) {
        BYTE* walk = control_walk_of(self);
        BYTE* other;

        while ((other = control_next_alike(self, NULL, walk)) != NULL)
            list_select_virtual(other, index);
        control_walk_end(self, NULL, walk);
    }
    return result;
}

/* Select the text entry reading `text` (in any case). FALSE when there is none. */
PORT(0x00477A60)
int __fastcall list_select_text(BYTE* self, void* edx_unused, const char* text)
{
    int i;

    (void) edx_unused;
    for (i = 0; i < list_count(self); i++) {
        list_entry* entry = &list_entries(self)[i];

        if (entry->kind == 1 && !game_FUN_0053b6e0(text, entry->text))
            return list_select_all(self, NULL, i);
    }
    return 0;
}

/* Select the entry with `data`. FALSE when there is none. */
PORT(0x00477AC0)
int __fastcall list_select_data(BYTE* self, void* edx_unused, DWORD data)
{
    int i;

    (void) edx_unused;
    for (i = 0; i < list_count(self); i++) {
        if (list_data(self, NULL, i) == data)
            return list_select_all(self, NULL, i);
    }
    return 0;
}

/* Vtable slot 59: copy the selection out to the bound int, or (`out` FALSE) select from it. */
PORT(0x00476F70)
void __fastcall list_commit(BYTE* self, void* edx_unused, BOOL out)
{
    (void) edx_unused;
    if (out) {
        *FIELD(self, EDIT_BOUND, int*) = list_selected(self);
        return;
    }
    FIELD(self, 0x23C, DWORD) = 0;
    list_select_all(self, NULL, *FIELD(self, EDIT_BOUND, int*));
}

PORT(0x00477B10)
int __fastcall list_set_208(BYTE* self, void* edx_unused, DWORD value)
{
    (void) edx_unused;
    FIELD(self, 0x208, DWORD) = value;
    return control_redraw(self);
}

/*
    A background colour: behind the selection (`index` -1) or behind one entry; NO_COLOUR for
    none. An entry's old brush is kept when any entry has it, and the entry itself always does,
    so the original never deletes it; nor does this.
*/
PORT(0x00477B30)
int __fastcall list_set_colour(BYTE* self, void* edx_unused, COLORREF colour, int index)
{
    (void) edx_unused;
    if (index == -1) {
        if (FIELD(self, LIST_HIGHLIGHT, HBRUSH))
            DeleteObject(FIELD(self, LIST_HIGHLIGHT, HBRUSH));
        FIELD(self, LIST_HIGHLIGHT, HBRUSH) = colour != NO_COLOUR ? CreateSolidBrush(colour) : NULL;
        return control_redraw(self);
    }
    if (index < list_count(self)) {
        HBRUSH old = list_entries(self)[index].brush;

        if (old) {
            int i;

            for (i = 0; i < list_count(self); i++) {
                if (list_entries(self)[i].brush == old)
                    break;
            }
            if (i == list_count(self))
                DeleteObject(old);
        }
        list_entries(self)[index].brush = colour == NO_COLOUR ? NULL : CreateSolidBrush(colour);
    }
    return control_redraw(self);
}

/* The text colour: the list's (`index` -1) or one entry's. */
PORT(0x00477C00)
int __fastcall list_set_text_colour(BYTE* self, void* edx_unused, COLORREF colour, int index)
{
    (void) edx_unused;
    if (index == -1) {
        control_set_colour(self, NULL, colour);
        return control_redraw(self);
    }
    if (index < list_count(self))
        list_entries(self)[index].colour = colour;
    return control_redraw(self);
}

PORT(0x00477C60)
int __fastcall list_set_colour_2(BYTE* self, void* edx_unused, COLORREF colour)
{
    (void) edx_unused;
    if (FIELD(self, LIST_BRUSH_2, HBRUSH))
        DeleteObject(FIELD(self, LIST_BRUSH_2, HBRUSH));
    FIELD(self, LIST_BRUSH_2, HBRUSH) = colour != NO_COLOUR ? CreateSolidBrush(colour) : NULL;
    return control_redraw(self);
}

/* An entry's text: the selection's when `index` is -1; NULL when there is none. */
PORT(0x00477CB0)
const char* __fastcall list_text(BYTE* self, void* edx_unused, int index)
{
    (void) edx_unused;
    if (FIELD(self, LIST_SELECTED, int) > -1 && index == -1)
        return list_entries(self)[FIELD(self, LIST_SELECTED, int)].text;
    if (list_count(self) && index > -1 && index < list_count(self))
        return list_entries(self)[index].text;
    return NULL;
}

void* __cdecl ui_realloc(void* block, size_t size);

/* Room for `capacity` entries, the new ones zeroed. FALSE when it cannot grow. */
PORT(0x00477EE0)
BOOL __fastcall list_reserve(BYTE* self, void* edx_unused, int capacity)
{
    list_entry* entries;

    (void) edx_unused;
    if (capacity <= FIELD(self, LIST_CAPACITY, int))
        return TRUE;
    entries = ui_realloc(list_entries(self), capacity * sizeof(list_entry));
    if (!entries)
        return FALSE;
    __stosb((BYTE*) &entries[FIELD(self, LIST_CAPACITY, int)], 0,
            (capacity - FIELD(self, LIST_CAPACITY, int)) * sizeof(list_entry));
    FIELD(self, LIST_CAPACITY, int) = capacity;
    list_entries(self) = entries;
    return TRUE;
}

/* How many entries, from the first showing, fit its height; one at least when 0x240 is set. */
PORT(0x00477E70)
int __fastcall list_rows_fitting(BYTE* self)
{
    int room = FIELD(self, CTL_RECT, RECT).bottom - FIELD(self, CTL_RECT, RECT).top;
    int i = FIELD(self, LIST_FIRST, int);
    int used = 0, rows = 0;

    if (i >= list_count(self))
        return 0;
    for (; i < list_count(self); i++) {
        used += list_entries(self)[i].height;
        if (used > room)
            return !rows && FIELD(self, 0x240, int) ? 1 : rows;
        rows++;
    }
    return rows;
}

/* Add a text entry with `data`, growing the list when full. FALSE without text or room. */
PORT(0x00477D10)
BOOL __fastcall list_add_text(BYTE* self, void* edx_unused, const char* text, DWORD data, DWORD b)
{
    list_entry* entry;
    int height;

    (void) edx_unused;
    if (!text)
        return FALSE;
    if (list_count(self) >= FIELD(self, LIST_CAPACITY, int)
        && !list_reserve(self, NULL, list_count(self) + 1))
        return FALSE;
    entry = &list_entries(self)[list_count(self)];
    entry->length = (int) text_length(text);
    height = FIELD(self, 0x160, int);
    if (!height)
        height = FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0xDC, int);
    entry->anim = NULL;
    entry->text_2 = NULL;
    entry->colour = 0;
    entry->brush = NULL;
    entry->height = height;
    entry->kind = 1;
    entry->b = b;
    entry->text = game_FUN_004a4410(entry->length + 1, GAMEUI_CPP, 0x1DC7);
    text_copy(entry->text, text);
    entry->runs = dialog_parse_runs(FIELD(self, CTL_DIALOG, BYTE*), NULL, entry->text,
                                    FIELD(self, CTL_COLOUR, COLORREF), FIELD(self, CTL_FONT, HFONT),
                                    FIELD(self, CTL_TABS, const int*), NULL);
    list_set_data(self, NULL, list_count(self), data);
    entry->drawn = 0;
    list_count(self)++;
    FIELD(self, LIST_ROWS, int) = list_rows_fitting(self);
    list_tell_sliders(self);
    if (FIELD(self, 0x21C, int))
        list_sort(self, NULL, 1);
    return TRUE;
}

/* Replace an entry's text. */
PORT(0x00477F80)
int __fastcall list_set_text(BYTE* self, void* edx_unused, int index, const char* text)
{
    (void) edx_unused;
    if (index < list_count(self) && index > -1) {
        list_entry* entry = &list_entries(self)[index];

        if (entry->text)
            game_FUN_004a4450(entry->text, GAMEUI_CPP, 0x1E0B);
        if (list_entries(self)[index].runs)
            game_FUN_004a4450(list_entries(self)[index].runs, GAMEUI_CPP, 0x1E0C);
        list_entries(self)[index].length = (int) text_length(text);
        list_entries(self)[index].text = game_FUN_004a4410(list_entries(self)[index].length + 1,
                                                           GAMEUI_CPP, 0x1E0E);
        text_copy(list_entries(self)[index].text, text);
        list_entries(self)[index].runs =
            dialog_parse_runs(FIELD(self, CTL_DIALOG, BYTE*), NULL, list_entries(self)[index].text,
                              FIELD(self, CTL_COLOUR, COLORREF), FIELD(self, CTL_FONT, HFONT),
                              FIELD(self, CTL_TABS, const int*), NULL);
    }
    return control_redraw(self);
}

#define LIST_WRAP 0x244             /* stepping past either end comes round */

/* Vtable slot 66: after a one-row list steps, whether that is all (`done`). */
typedef void (__fastcall* list_stepped_slot)(void* list, void* edx_unused, int* done);

/* Step the first entry showing by `by`. A one-row list selects it; slot 66 may end it there. */
PORT(0x004786C0)
void __fastcall list_step(BYTE* self, void* edx_unused, int by)
{
    int count = list_count(self);
    int done = 0;

    (void) edx_unused;
    if (!count)
        return;
    FIELD(self, LIST_FIRST, int) += by;
    if (FIELD(self, LIST_WRAP, int)) {
        int first;

        if (FIELD(self, LIST_FIRST, int) < 0)
            FIELD(self, LIST_FIRST, int) += count;
        first = FIELD(self, LIST_FIRST, int) % count;
        FIELD(self, LIST_FIRST, int) = first < 0 ? -first : first;
    } else {
        int first = FIELD(self, LIST_FIRST, int);
        int last = count - FIELD(self, LIST_ROWS, int);

        if (last < first)
            first = last;
        FIELD(self, LIST_FIRST, int) = first < 0 ? 0 : first;
    }
    if (FIELD(self, LIST_ROWS, int) == 1) {
        list_select_virtual(self, FIELD(self, LIST_FIRST, int));
        VIRTUAL(self, 0x108, list_stepped_slot)(self, NULL, &done);
        if (done)
            return;
    }
    control_redraw(self);
    list_tell_sliders(self);
}

/* Add `anim` (which it takes a reference on) as a picture entry with `data`. */
PORT(0x00478330)
BOOL __fastcall list_add_anim(BYTE* self, void* edx_unused, ui_anim* anim, DWORD data, DWORD b)
{
    list_entry entry;

    (void) edx_unused;
    if (list_count(self) >= FIELD(self, LIST_CAPACITY, int)
        && !list_reserve(self, NULL, list_count(self) + 1))
        return FALSE;
    entry.kind = 2;
    entry.text = NULL;
    entry.anim = anim;
    VIRTUAL(anim, 4, method_0)(anim);
    entry.b = b;
    entry.length = anim->frames[0]->width;
    entry.height = anim->frames[0]->height;
    entry.text_2 = NULL;
    entry.runs = NULL;
    entry.drawn = 0;
    list_entries(self)[list_count(self)] = entry;
    list_set_data(self, NULL, list_count(self), data);
    list_count(self)++;
    FIELD(self, LIST_ROWS, int) = list_rows_fitting(self);
    list_tell_sliders(self);
    if (FIELD(self, 0x21C, int))
        list_sort(self, NULL, 1);
    return TRUE;
}

/*
    Remove an entry, the later ones moving up. The original copies one entry past the last into
    place, and keeps a brush any entry has, which is always the removed one's own, so it never
    deletes it; this does the same.
*/
PORT(0x00478420)
BOOL __fastcall list_remove(BYTE* self, void* edx_unused, int index)
{
    HBRUSH brush;
    int i;

    (void) edx_unused;
    if (index >= list_count(self) || index < 0)
        return FALSE;
    game_FUN_004a4450(list_entries(self)[index].text, GAMEUI_CPP, 0x1E80);
    game_FUN_004a4450(list_entries(self)[index].runs, GAMEUI_CPP, 0x1E81);
    game_FUN_004a4450(list_entries(self)[index].text_2, GAMEUI_CPP, 0x1E82);
    brush = list_entries(self)[index].brush;
    if (brush) {
        for (i = 0; i < list_count(self); i++) {
            if (list_entries(self)[i].brush == brush)
                break;
        }
        if (i == list_count(self))
            DeleteObject(brush);
    }
    if (index < list_count(self) - 1 && index < list_count(self)) {
        for (i = index; i < list_count(self); i++)
            list_entries(self)[i] = list_entries(self)[i + 1];
    }
    __stosd((unsigned long*) &list_entries(self)[list_count(self) - 1], 0,
            sizeof(list_entry) / 4);
    list_count(self) = list_count(self) - 1 > 0 ? list_count(self) - 1 : 0;
    if (FIELD(self, LIST_SELECTED, int) == index
        && FIELD(self, LIST_SELECTED, int) == list_count(self)) {
        int previous = FIELD(self, LIST_SELECTED, int) - 1;

        list_select_virtual(self, previous > 0 ? previous : 0);
    }
    list_tell_sliders(self);
    return TRUE;
}

/* How its entries sort: a qsort compare, given two entries. */
PORT(0x00478780)
void __fastcall list_set_compare(BYTE* self, void* edx_unused, void* compare)
{
    (void) edx_unused;
    FIELD(self, 0x24C, void*) = compare;
}

/* The default sort: by text, in any case; entries without text last. */
PORT(0x00478AF0)
int __cdecl list_compare_text(const list_entry* a, const list_entry* b)
{
    if (!a->text)
        return 1;
    if (!b->text)
        return -1;
    return game_FUN_0053b6e0(a->text, b->text);
}

PORT(0x00478B20)
void __fastcall list_set_21c(BYTE* self, void* edx_unused, DWORD value)
{
    (void) edx_unused;
    FIELD(self, 0x21C, DWORD) = value;
}

PORT(0x00478B30)
void __fastcall list_set_228(BYTE* self, void* edx_unused, DWORD value)
{
    (void) edx_unused;
    FIELD(self, 0x228, DWORD) = value;
}

PORT(0x00478B40)
void __fastcall list_set_224(BYTE* self, void* edx_unused, DWORD value)
{
    (void) edx_unused;
    FIELD(self, 0x224, DWORD) = value;
}

#define LIST_LAST_PAGE 0x204        /* how many entries at its end fit its height together */

/*
    Count the entries at its end that fit its height, and move the thumb of every slider (types
    7 and 8) of its group to where it has scrolled.
*/
PORT(0x00478B50)
void __fastcall list_tell_sliders(BYTE* self)
{
    BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);
    BYTE walk[0x94];
    BYTE* control;
    int room, used = 0, i;

    if (!dialog)
        return;
    FIELD(self, LIST_LAST_PAGE, int) = 0;
    room = FIELD(self, CTL_RECT, RECT).bottom - FIELD(self, CTL_RECT, RECT).top;
    for (i = list_count(self) - 1; i >= 0; i--) {
        used += list_entries(self)[i].height;
        if (used > room)
            break;
        FIELD(self, LIST_LAST_PAGE, int)++;
    }
    if (FIELD(self, LIST_LAST_PAGE, int) < 0)
        FIELD(self, LIST_LAST_PAGE, int) = 0;

    walk_begin(walk, NULL, FIELD(dialog, DLG_CONTAINER, void*), 1, UICONTROL_CLASS);
    for (control = walk_next(walk); control; control = walk_next(walk)) {
        int type = FIELD(control, CTL_TYPE, int);

        if (FIELD(control, 0x7C, DWORD) == FIELD(self, 0x7C, DWORD) && (type == 8 || type == 7))
            slider_follow(control, NULL, self);
    }
    control_redraw(self);
    walk_end(walk);
}

/* Remove every entry. Brushes are deleted here, once each, and cleared where shared. */
PORT(0x00478580)
void __fastcall list_clear(BYTE* self)
{
    int i;

    for (i = 0; i < list_count(self); i++) {
        list_entry* entry;

        game_FUN_004a4450(list_entries(self)[i].text, GAMEUI_CPP, 0x1ECF);
        game_FUN_004a4450(list_entries(self)[i].text_2, GAMEUI_CPP, 0x1ED0);
        game_FUN_004a4450(list_entries(self)[i].runs, GAMEUI_CPP, 0x1ED1);
        if (list_entries(self)[i].brush) {
            int j;

            DeleteObject(list_entries(self)[i].brush);
            for (j = 0; j < list_count(self); j++) {
                if (list_entries(self)[j].brush == list_entries(self)[i].brush)
                    list_entries(self)[j].brush = NULL;
            }
        }
        entry = &list_entries(self)[i];
        if (entry->anim && !entry->anim->unknown_f4)
            object_destroy(entry->anim);
    }
    list_count(self) = 0;
    FIELD(self, LIST_FIRST, int) = 0;
    list_select_all(self, NULL, 0);
    FIELD(self, LIST_LAST_PAGE, int) = 0;
    __stosb((BYTE*) list_entries(self), 0, FIELD(self, LIST_CAPACITY, int) * sizeof(list_entry));
    list_tell_sliders(self);
}

/* A resource archive (0x134 bytes): opened on the game's resources, and searched for a file. */
void* __fastcall game_FUN_00461c80(void* self, void* edx_unused, void* resources);
BOOL  __fastcall game_FUN_00461ec0(void* self, void* edx_unused, const char* name, const char* kind,
                                   DWORD a);
void  __fastcall game_FUN_00461cd0(void* self);
BYTE* __cdecl    game_FUN_005173e0(const char* name, DWORD a, void* resources);   /* load an image */
#define ARCHIVE_KIND ((const char*) 0x0056CA38)

/*
    Add an entry from the file `name`: an animation (kind 2) when `animated`, found through a
    resource archive, else a picture (kind 3) whose name is kept and whose size is read from
    the image. The original leaves the fields it does not set as they were on its stack.
*/
PORT(0x004780A0)
BOOL __fastcall list_add_file(BYTE* self, void* edx_unused, const char* name, DWORD data,
                              BOOL animated, DWORD b)
{
    list_entry entry;

    (void) edx_unused;
    if (list_count(self) >= FIELD(self, LIST_CAPACITY, int)
        && !list_reserve(self, NULL, list_count(self) + 1))
        return FALSE;

    if (animated) {
        void* archive = game_new(0x134, GAMEUI_CPP, 0x1E20);
        ui_anim* anim;

        if (archive)
            archive = game_FUN_00461c80(archive, NULL, game_resources);
        if (!game_FUN_00461ec0(archive, NULL, name, ARCHIVE_KIND, 0)) {
            if (archive) {
                game_FUN_00461cd0(archive);
                game_delete(archive);
            }
            return FALSE;
        }
        entry.kind = 2;
        entry.text = NULL;
        entry.text_2 = NULL;
        entry.runs = NULL;
        anim = game_new(sizeof(ui_anim), GAMEUI_CPP, 0x1E27);
        if (anim)
            anim = anim_construct(anim, NULL, FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0x7F20, DWORD), 0);
        entry.anim = anim;
        anim_add_file(anim, NULL, name, NULL);
        entry.b = b;
        entry.length = anim->frames[0]->width;
        entry.height = anim->frames[0]->height;
        entry.drawn = 0;
        if (archive) {
            game_FUN_00461cd0(archive);
            game_delete(archive);
        }
    } else {
        BYTE* image;

        entry.kind = 3;
        entry.text = NULL;
        entry.anim = NULL;
        entry.text_2 = game_FUN_004a4410(text_length(name) + 1, GAMEUI_CPP, 0x1E37);
        entry.b = b;
        text_copy(entry.text_2, name);
        entry.runs = NULL;
        entry.drawn = 0;
        image = game_FUN_005173e0(name, 0, game_resources);
        if (!image)
            return FALSE;
        entry.length = image_width(image);
        entry.height = image_height(image);
        game_FUN_005184b0(image);
    }

    list_entries(self)[list_count(self)] = entry;
    list_set_data(self, NULL, list_count(self), data);
    list_count(self)++;
    FIELD(self, LIST_ROWS, int) = list_rows_fitting(self);
    list_tell_sliders(self);
    if (FIELD(self, 0x21C, int))
        list_sort(self, NULL, 1);
    return TRUE;
}

/*
    Sorting. The list being sorted is kept on its dialog (0x9BC), and that dialog in a global,
    for the compare to find; lists of the same group with as many entries are sorted the same
    way, entries tying in one being ordered by the next.
*/
#define sort_dialog    (*(BYTE**) 0x00661750)
#define sort_list()    FIELD(FIELD(sort_dialog, 0x9BC, BYTE*), 0x218, BYTE*)
#define LIST_SORTING   0x220

typedef int (__cdecl* list_compare)(const list_entry* a, const list_entry* b);
#define list_compare_of(l) FIELD(l, 0x24C, list_compare)

void __cdecl game_FUN_00539a96(void* base, size_t count, size_t size,
                               int (__cdecl* compare)(const void*, const void*));    /* qsort */

/* qsort's compare: the list's own, then each like list's in turn on the entries' first places. */
PORT(0x00478790)
int __cdecl list_sort_compare(const list_entry* a, const list_entry* b)
{
    int first_a, first_b, result;
    BYTE* walk;
    BYTE* other;

    if (!FIELD(sort_dialog, 0x9BC, BYTE*))
        return 0;
    result = list_compare_of(sort_list())(a, b);
    if (result)
        return result;
    first_a = a->order;
    first_b = b->order;
    walk = control_walk_of(sort_list());
    while ((other = control_next_alike(sort_list(), NULL, walk)) != NULL) {
        if (list_count(other) != list_count(sort_list())) {
            result = -1;
            break;
        }
        result = list_compare_of(other)(&list_entries(other)[first_a], &list_entries(other)[first_b]);
        if (result)
            break;
    }
    control_walk_end(sort_list(), NULL, walk);
    return result;
}

/*
    Sort the entries, and with them every list of its group with as many, keeping the selection
    on the same entry. A nested sort (`outer` FALSE) leaves the dialog's sorting list as it is.
*/
PORT(0x00478890)
void __fastcall list_sort(BYTE* self, void* edx_unused, BOOL outer)
{
    int selected, i;

    (void) edx_unused;
    if (outer || !FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0x9BC, BYTE*) || !sort_dialog) {
        FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0x9BC, BYTE*) = self;
        sort_dialog = FIELD(self, CTL_DIALOG, BYTE*);
    }
    if (!list_count(self))
        return;
    FIELD(self, LIST_SORTING, DWORD) = 1;
    for (i = 0; i < list_count(self); i++)
        list_entries(self)[i].order = i;
    selected = FIELD(self, LIST_SELECTED, int);
    game_FUN_00539a96(list_entries(self), list_count(self), sizeof(list_entry),
                      (int (__cdecl*)(const void*, const void*)) list_sort_compare);
    control_redraw(self);

    if (FIELD(self, 0x7C, int) > 0) {
        BYTE walk[0x94];
        BYTE* other;

        walk_begin(walk, NULL, FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_CONTAINER, void*), 1,
                   UICONTROL_CLASS);
        for (other = walk_next(walk); other; other = walk_next(walk)) {
            list_entry* sorted;

            if (FIELD(other, 0x7C, int) != FIELD(self, 0x7C, int) || FIELD(other, CTL_TYPE, int) != 3
                || other == self || list_count(other) != list_count(self))
                continue;
            sorted = game_FUN_004a4410(FIELD(other, LIST_CAPACITY, int) * sizeof(list_entry),
                                       GAMEUI_CPP, 0x1F87);
            for (i = 0; i < list_count(self); i++)
                sorted[i] = list_entries(other)[list_entries(self)[i].order];
            game_FUN_004a4450(list_entries(other), GAMEUI_CPP, 0x1F8E);
            list_entries(other) = sorted;
            control_redraw(other);
        }
        walk_end(walk);
    }

    for (i = 0; i < list_count(self); i++) {
        if (list_entries(self)[i].order == selected) {
            list_select_all(self, NULL, i);
            break;
        }
    }
    FIELD(self, LIST_SORTING, DWORD) = 0;
}

#define LIST_CLICKABLE  0x224       /* a press selects the entry under it */
#define LIST_DRAGGABLE  0x228       /* the first button drags it across */
#define LIST_DRAG_FROM  0x22C       /* where a drag started */
#define LIST_SHIFT      0x230       /* how far it has been dragged, drawn negative */
#define LIST_DRAGGED    0x234       /* the drag under way */
#define LIST_LAST_CLICK 0x238       /* the entry and time of the last selection, for double clicks */
#define LIST_CLICK_TIME 0x23C
#define ENTRY_FIXED     1           /* an entry's `b`: it cannot be selected */

#define double_click_time (*(UINT*) 0x0066170C)
#define double_click_read (*(BYTE*) 0x006616A4)
#define click_clock       (*(DWORD*) 0x006616E4)

/* Tell the dialog of the selection as the list's (manager 0x30 of the dialog). */
static void list_tell(BYTE* self, ui_event* event, DWORD type)
{
    BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);

    event->a = FIELD(self, 0x74, DWORD);
    event->b = (DWORD) control_get_name(self);
    event->type = type;
    event->sender = dialog;
    event->c = (DWORD) self;
    event->manager = FIELD(dialog, 0x30, gui_manager*);
#ifdef OPENMCM2_DEV
    dev_trace_event(event->type, (const char*) event->b, dialog);
#endif
    VIRTUAL(dialog, 0x74, dialog_handle)(dialog, NULL, event);
}

/* Vtable slot 50: redraw; with a background each entry is drawn again too. */
PORT(0x00478C70)
void __fastcall list_redraw(BYTE* self)
{
    int i;

    control_slot_50(self);
    if (FIELD(self, CTL_DIALOG, BYTE*) && FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_BACKGROUND, BYTE*)) {
        for (i = 0; i < list_count(self); i++)
            list_entries(self)[i].drawn = 0;
    }
}

/*
    Vtable slot 66: the selection moved. The dialog hears (event 2), and of a second selection of
    the same entry within the double-click time (event 13); `done` says the dialog took either.
*/
PORT(0x00478CC0)
void __fastcall list_selection_moved(BYTE* self, void* edx_unused, int* done)
{
    ui_event event;

    (void) edx_unused;
    event.e = 0;
    list_tell(self, &event, 2);
    if (done)
        *done = 0;
    if (event.e) {
        if (done)
            *done = 1;
        return;
    }
    if (!(double_click_read & 1)) {
        double_click_read |= 1;
        double_click_time = GetDoubleClickTime();
    }
    click_clock = game_FUN_004c41d0();
    if (FIELD(self, LIST_SELECTED, int) != FIELD(self, LIST_LAST_CLICK, int)
        || click_clock - FIELD(self, LIST_CLICK_TIME, DWORD) > double_click_time) {
        FIELD(self, LIST_CLICK_TIME, DWORD) = click_clock;
        FIELD(self, LIST_LAST_CLICK, int) = FIELD(self, LIST_SELECTED, int);
        return;
    }
    list_tell(self, &event, 13);
    if (!event.e) {
        FIELD(self, LIST_CLICK_TIME, DWORD) = 0;
        return;
    }
    if (done)
        *done = 1;
}

PORT(0x00478DF0)
void __fastcall list_set_240(BYTE* self, void* edx_unused, DWORD value)
{
    (void) edx_unused;
    FIELD(self, 0x240, DWORD) = value;
    if (!FIELD(self, LIST_ROWS, int))
        FIELD(self, LIST_ROWS, int) = 1;
}

/*
    Vtable slot 55: pressed while it has focus. The first button selects the selectable entry
    under the pointer, in a clickable list; the second starts dragging a draggable one.
*/
PORT(0x00478E20)
int __fastcall list_pressed(BYTE* self, void* edx_unused, DWORD button, const POINT* at)
{
    (void) edx_unused;
    if (control_user(self)->focus == self && at) {
        if (!button) {
            RECT row;
            int done = 0;
            int i;

            if (!FIELD(self, LIST_CLICKABLE, int))
                return control_pressed(self, NULL, 0, (DWORD) at);
            row = FIELD(self, CTL_RECT, RECT);
            for (i = FIELD(self, LIST_FIRST, int);
                 i < FIELD(self, LIST_FIRST, int) + FIELD(self, LIST_ROWS, int); i++) {
                row.bottom = list_height(self, NULL, i) + row.top;
                if (PtInRect(&row, *at) && !(list_entries(self)[i].b & ENTRY_FIXED)) {
                    list_select_all(self, NULL, i);
                    VIRTUAL(self, 0x108, list_stepped_slot)(self, NULL, &done);
                    break;
                }
                if (!done)
                    row.top += list_height(self, NULL, i);
            }
            if (done)
                return 1;
        } else if (button == 1 && FIELD(self, LIST_DRAGGABLE, int)) {
            FIELD(self, LIST_DRAG_FROM, LONG) = at->x;
            return control_pressed(self, NULL, 1, (DWORD) at);
        }
    }
    return control_pressed(self, NULL, button, (DWORD) at);
}

/* Vtable slot 56: letting go of a drag keeps how far it went. */
PORT(0x00478F80)
int __fastcall list_released(BYTE* self, void* edx_unused, DWORD button, DWORD b)
{
    (void) edx_unused;
    if (control_user(self)->focus == self && button == 1) {
        int dragged = FIELD(self, LIST_DRAGGED, int);

        FIELD(self, LIST_DRAGGED, int) = 0;
        FIELD(self, LIST_SHIFT, int) += dragged;
    }
    return control_released(self, NULL, button, b);
}

/*
    Vtable slot 21: a key while it has capture, in a clickable list: Page Up and Down move the
    selection a page, Home and End to either end, and the arrows by one.
*/
PORT(0x00478FD0)
int __fastcall list_key(BYTE* self, void* edx_unused, DWORD key)
{
    int done;
    int to;

    (void) edx_unused;
    if (control_unclicked(self, NULL, key) || control_user(self)->capture != self
        || !FIELD(self, LIST_CLICKABLE, int))
        return 0;
    switch (key) {
    case VK_PRIOR:
        to = list_selected(self) - FIELD(self, LIST_ROWS, int);
        if (to < 0)
            to = 0;
        if (to == list_selected(self))
            return 0;
        break;
    case VK_NEXT:
        to = list_selected(self) + FIELD(self, LIST_ROWS, int);
        if (list_count(self) - 1 < to)
            to = list_count(self) - 1;
        if (to == list_selected(self))
            return 0;
        break;
    case VK_END:
        to = list_count(self) - 1;
        if (list_selected(self) >= to)
            return 0;
        break;
    case VK_HOME:
        if (list_selected(self) <= 0)
            return 0;
        to = 0;
        break;
    case VK_LEFT:
    case VK_UP:
        if (list_selected(self) <= 0)
            return 0;
        to = list_selected(self) - 1;
        break;
    case VK_RIGHT:
    case VK_DOWN:
        if (list_selected(self) >= list_count(self) - 1)
            return 0;
        to = list_selected(self) + 1;
        break;
    default:
        return 0;
    }
    list_select_all(self, NULL, to);
    VIRTUAL(self, 0x108, list_stepped_slot)(self, NULL, &done);
    return 1;
}

/* Vtable slot 57: dragged with the second button, it slides across, no further than its start. */
PORT(0x00479120)
void __fastcall list_drag(BYTE* self, void* edx_unused, DWORD button, const POINT* at)
{
    (void) edx_unused;
    if (button == 1 && at) {
        int dragged = at->x - FIELD(self, LIST_DRAG_FROM, LONG);
        int limit = -FIELD(self, LIST_SHIFT, int);

        FIELD(self, LIST_DRAGGED, int) = dragged;
        FIELD(self, LIST_DRAGGED, int) = dragged < limit ? dragged : limit;
        control_redraw(self);
    }
    control_drag(self, NULL, button, at);
}

#define LIST_HOVER 0x248            /* the selectable entry under the pointer, or -1 */

/*
    Vtable slot 10: tick. While it has capture a claimed joystick moves the selection up or
    down; then the entry under the pointer is found, for the hover frame.
*/
PORT(0x00476FB0)
int __fastcall list_tick(BYTE* self, void* edx_unused, DWORD time)
{
    gui_input* input;
    gui_input* pointer;
    int index = 0;

    (void) edx_unused;
    while ((input = gui_user_claimed(control_user(self), NULL, index++)) != NULL) {
        int push;

        if (FIELD(input->device, 0xC, int) != 2 || !game_FUN_0048a770(input->device, NULL, 4))
            continue;
        push = input->y;
        if (control_user(self)->capture != self)
            continue;
        if (push <= -1) {
            if (list_selected(self) > 0)
                list_select_all(self, NULL, list_selected(self) - 1);
        } else if (push >= 1) {
            if (list_selected(self) < list_count(self) - 1)
                list_select_all(self, NULL, list_selected(self) + 1);
        }
    }

#ifdef OPENMCM2_DEV
    /* The wheel scrolls the list under the pointer, a row a notch. */
    if (control_user(self)->focus == self && list_count(self) > FIELD(self, LIST_ROWS, int)) {
        int notches = menu_wheel(control_user(self), self);

        if (notches)
            list_scroll(self, NULL, FIELD(self, LIST_FIRST, int) - notches, TRUE);
    }
#endif
    FIELD(self, LIST_HOVER, int) = -1;
    pointer = (gui_input*) control_user(self)->input;
    if (pointer && control_user(self)->focus == self && FIELD(self, LIST_CLICKABLE, int)) {
        POINT at;
        RECT row = FIELD(self, CTL_RECT, RECT);
        int i;

        at.x = pointer->x;
        at.y = pointer->y;
        for (i = FIELD(self, LIST_FIRST, int);
             i < FIELD(self, LIST_FIRST, int) + FIELD(self, LIST_ROWS, int); i++) {
            row.bottom = list_height(self, NULL, i) + row.top;
            if (PtInRect(&row, at) && !(list_entries(self)[i].b & ENTRY_FIXED)) {
                FIELD(self, LIST_HOVER, int) = i;
                break;
            }
            row.top += list_height(self, NULL, i);
        }
    }
    return control_tick(self, NULL, time);
}

/* A file entry (kind 3) becomes an animation (kind 2) the first time it is drawn. */
static void list_load_file(BYTE* self, int index)
{
    list_entry* entry = &list_entries(self)[index];

    if (entry->text_2) {
        ui_anim* anim;

        if (entry->anim)
            object_destroy(entry->anim);
        anim = game_new(sizeof(ui_anim), GAMEUI_CPP, 0x1C62);
        if (anim)
            anim = anim_construct(anim, NULL, FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0x7F20, DWORD), 0);
        list_entries(self)[index].anim = anim;
        entry = &list_entries(self)[index];
        anim_add_file(entry->anim, NULL, entry->text_2, FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0x140, BYTE*));
        list_entries(self)[index].length = list_entries(self)[index].anim->frames[0]->width;
        list_entries(self)[index].height = list_entries(self)[index].anim->frames[0]->height;
    } else {
        entry->anim = NULL;
    }
    list_entries(self)[index].kind = 2;
    control_redraw(self);
}

/*
    Vtable slot 40: render. Its picture, then the entries from the first showing, as many as
    fit (a part one when 0x240 is set and none has been drawn): text in a paint pass (the
    selection on its own brush and colour, all shifted by a drag), then pictures straight to the
    screen. The rows drawn become LIST_ROWS; the entry under the pointer is framed.
*/
PORT(0x00477180)
int __fastcall list_render(BYTE* self)
{
    BYTE* picture = FIELD(self, 0x1CC, BYTE*);
    int redraws = FIELD(self, 0x1BC, int);
    int kept = -1, drawn = 0, pass;
    DWORD settled = 1;
    DWORD more;
    RECT area;
    HDC dc = NULL;          /* set by the paint pass, the only one to use it */

    if (picture) {
        BYTE* background = FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_BACKGROUND, BYTE*);
        DWORD flags = FIELD(picture, 0x30, int) ? 0x1008000 : 0x1000000;

        if (background) {
            background_draw(background, NULL, picture, &FIELD(self, CTL_RECT, RECT),
                            CONTROL_SOURCE(self, picture),
                            flags, FIELD(self, CTL_SLOT, int), FIELD(self, 0x1C0, DWORD),
                            &FIELD(self, 0x1BC, int), 0);
            FIELD(self, 0x1BC, int) = redraws;
        } else if (!VIRTUAL(object_screen(self), 0xC, screen_draw)(object_screen(self), NULL,
                                                                  &FIELD(self, CTL_RECT, RECT),
                                                                  picture,
                                                                  CONTROL_SOURCE(self, picture),
                                                                  flags)) {
            return 0;
        }
    }

    area = FIELD(self, CTL_RECT, RECT);
    area.left += FIELD(self, 0xE8, LONG);
    area.right -= FIELD(self, 0xE8, LONG);
    area.top += FIELD(self, 0xEC, LONG);
    area.bottom -= FIELD(self, 0xEC, LONG);

    for (pass = 0; pass < 2; pass++) {
        do {
            LONG y = area.top;
            int i;

            if (!pass) {
                dialog_begin_paint(FIELD(self, CTL_DIALOG, BYTE*), NULL, &dc, &area, &more, self);
                if (FIELD(self, LIST_HIGHLIGHT, HBRUSH))
                    FillRect(dc, &area, FIELD(self, LIST_HIGHLIGHT, HBRUSH));
            } else {
                more = 0;
                kept = FIELD(self, 0x1BC, int);
                FIELD(self, 0x1BC, int) = redraws;
            }

            for (i = FIELD(self, LIST_FIRST, int); i < list_count(self); i++) {
                int height = list_height(self, NULL, i);
                BOOL whole = FALSE;
                RECT cell;

                if (!FIELD(self, 0x240, int) || drawn > 0) {
                    if (height + y > area.bottom)
                        break;
                    whole = TRUE;
                }

                switch (list_kind(self, NULL, i)) {
                case 1: {
                    list_entry* entry = &list_entries(self)[i];
                    COLORREF colour;

                    if (pass || !entry->text)
                        break;
                    cell.left = area.left;
                    cell.top = y;
                    cell.right = area.right;
                    cell.bottom = height + y;
                    SetBkMode(dc, TRANSPARENT);
                    if (i == FIELD(self, LIST_SELECTED, int) && FIELD(self, LIST_CLICKABLE, int)) {
                        if (FIELD(self, LIST_BRUSH_2, HBRUSH))
                            FillRect(dc, &cell, FIELD(self, LIST_BRUSH_2, HBRUSH));
                        colour = FIELD(self, 0x208, COLORREF);
                    } else {
                        if (list_entries(self)[i].brush)
                            FillRect(dc, &cell, list_entries(self)[i].brush);
                        colour = list_entries(self)[i].colour;
                        if (!colour)
                            colour = FIELD(self, CTL_COLOUR, COLORREF);
                    }
                    entry = &list_entries(self)[i];
                    if (entry->length) {
                        int shift = FIELD(self, LIST_DRAGGED, int) + FIELD(self, LIST_SHIFT, int);

                        cell.left += shift;
                        cell.right += shift;
                        dialog_draw_text(FIELD(self, CTL_DIALOG, BYTE*), NULL, dc, &cell, entry->text,
                                         entry->length, colour, FIELD(self, 0x160, int), entry->runs,
                                         FIELD(self, CTL_SHADOW, BOOL),
                                         FIELD(self, CTL_SHADOW_COLOUR, COLORREF),
                                         FIELD(self, CTL_ALIGN, DWORD), TRUE, NULL);
                    }
                    if (!more)
                        drawn++;
                    break;
                }
                case 3:
                    if (pass == 1) {
                        list_load_file(self, i);
                        height = list_entries(self)[i].height;
                    }
                    if (whole && height + y > area.bottom)
                        break;
                    /* fall through: drawn as the animation it now is */
                case 2: {
                    ui_anim* anim = list_entries(self)[i].anim;
                    BYTE* shown;
                    LONG width, tall;

                    if (pass != 1 || !anim)
                        break;
                    width = anim_current(anim)->width;
                    tall = anim_current(list_entries(self)[i].anim)->height;
#ifdef OPENMCM2_DEV
                    width *= list_entry_art(self, i);
                    tall *= list_entry_art(self, i);
#endif
                    cell.left = FIELD(self, LIST_SHIFT, int) + area.left;
                    cell.top = y;
                    cell.right = cell.left + width < area.right ? cell.left + width : area.right;
                    if (FIELD(self, 0x240, int))
                        cell.bottom = tall + y < area.bottom ? tall + y : area.bottom;
                    else
                        cell.bottom = tall + y;
                    shown = anim_current(list_entries(self)[i].anim)->picture;
                    if (FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_BACKGROUND, BYTE*)) {
                        background_draw(FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_BACKGROUND, BYTE*),
                                        NULL, shown, &cell, NULL,
                                        FIELD(shown, 0x30, int) ? 0x1008000 : 0x1000000,
                                        FIELD(self, CTL_SLOT, int), FIELD(self, 0x1C0, DWORD),
                                        &FIELD(self, 0x1BC, int), 0);
                        if (list_entries(self)[i].anim->count > 1) {
                            settled = 0;
                            kept = FIELD(self, 0x1BC, int);
                        }
                    } else if (!VIRTUAL(object_screen(self), 0xC, screen_draw)(
                                   object_screen(self), NULL, &cell, shown, NULL,
                                   FIELD(shown, 0x30, int) ? 0x1008000 : 0x1000000)) {
                        return 0;
                    }
                    drawn++;
                    break;
                }
                }

                if (FIELD(self, LIST_HOVER, int) == i && !pass && FIELD(self, LIST_ROWS, int) > 1) {
                    HBRUSH frame = FIELD(self, LIST_HIGHLIGHT, HBRUSH);

                    cell.left = area.left;
                    cell.top = y;
                    cell.bottom = height + y;
                    cell.right = area.right;
                    FrameRect(dc, &cell, frame ? frame : (HBRUSH) GetStockObject(WHITE_BRUSH));
                    control_redraw(self);
                }
                y += height;
            }
            if (!pass)
                dialog_end_paint(FIELD(self, CTL_DIALOG, BYTE*), NULL, dc);
        } while (more);
    }

    if (kept >= 0)
        FIELD(self, 0x1BC, int) = kept;
    FIELD(self, LIST_ROWS, int) = drawn;
    FIELD(self, 0x1C0, DWORD) = settled;
    return 1;
}

/* ---- UIOption: a control that steps through options ------------------------------------------- */

/* An option, 32 bytes: its pictures (normal, and disabled for state 4) and its text. */
typedef struct ui_option {
    ui_anim*  anim;
    ui_anim*  anim_disabled;
    char*     text;             /* on the game's heap */
    int       length;
    DWORD     enabled;          /* stepped to by option_next */
    int       extent[2];        /* as CTL_EXTENT: length without markup, width */
    text_run* runs;             /* on the game's heap */
} ui_option;

_Static_assert(sizeof(ui_option) == 32, "an option is 32 bytes");

#define UIOPTION_VTABLE ((void**) 0x005582C8)
#define OPTION_COUNT    0x1EC
#define OPTION_CURRENT  0x1F0
#define OPTION_LIST     0x1F4

#define option_count(o)   FIELD(o, OPTION_COUNT, int)
#define option_list(o)    FIELD(o, OPTION_LIST, ui_option*)
#define option_current(o) (&option_list(o)[FIELD(o, OPTION_CURRENT, int)])

/* Vtable slots 34 to 37: the text of the option showing, its length, markup and extent. */
PORT(0x004791F0)
const char* __fastcall option_text(BYTE* self)
{
    return option_current(self)->text;
}

PORT(0x00479210)
int __fastcall option_length(BYTE* self)
{
    return option_current(self)->length;
}

PORT(0x00479230)
text_run* __fastcall option_runs(BYTE* self)
{
    return option_current(self)->runs;
}

PORT(0x00479250)
int* __fastcall option_extent(BYTE* self)
{
    return option_current(self)->extent;
}

/* The option showing. */
PORT(0x00479D00)
int __fastcall option_current_index(BYTE* self)
{
    return FIELD(self, OPTION_CURRENT, int);
}

/* Show option `index`. */
PORT(0x00479C80)
int __fastcall option_select(BYTE* self, void* edx_unused, int index)
{
    (void) edx_unused;
    if (index < option_count(self))
        FIELD(self, OPTION_CURRENT, int) = index;
    return control_redraw(self);
}

/* Step to the next enabled option, coming round; stays when none other is. */
PORT(0x00479CA0)
void __fastcall option_next(BYTE* self)
{
    ui_option* options = option_list(self);
    int start, at;

    if (!options)
        return;
    start = FIELD(self, OPTION_CURRENT, int);
    at = (start + 1) % option_count(self);
    FIELD(self, OPTION_CURRENT, int) = at;
    while (!options[at].enabled && at != start) {
        at = (at + 1) % option_count(self);
        FIELD(self, OPTION_CURRENT, int) = at;
    }
    control_redraw(self);
}

/* Free option `index`'s parts; the original frees its pictures without destroying them. */
static void option_free(BYTE* self, int index, int line)
{
    ui_option* option = &option_list(self)[index];

    if (option->anim && !option->anim->unknown_f4)
        game_FUN_004a4450(option->anim, GAMEUI_CPP, line);
    option = &option_list(self)[index];
    if (option->anim_disabled && !option->anim_disabled->unknown_f4)
        game_FUN_004a4450(option->anim_disabled, GAMEUI_CPP, line + 1);
    if (option_list(self)[index].text)
        game_FUN_004a4450(option_list(self)[index].text, GAMEUI_CPP, line + 2);
    if (option_list(self)[index].runs)
        game_FUN_004a4450(option_list(self)[index].runs, GAMEUI_CPP, line + 3);
}

/* Hold `count` options: the ones past it freed, new ones empty and enabled; the first shows. */
PORT(0x004797F0)
BOOL __fastcall option_set_count(BYTE* self, void* edx_unused, int count)
{
    int i;

    (void) edx_unused;
    if (count < option_count(self)) {
        for (i = count; i < option_count(self); i++)
            option_free(self, i, 0x21B6);
        option_list(self) = ui_realloc(option_list(self), count * sizeof(ui_option));
    } else if (count > option_count(self)) {
        option_list(self) = ui_realloc(option_list(self), count * sizeof(ui_option));
        for (i = option_count(self); i < count; i++) {
            option_list(self)[i].anim = NULL;
            option_list(self)[i].anim_disabled = NULL;
            option_list(self)[i].text = NULL;
            option_list(self)[i].runs = NULL;
            option_list(self)[i].enabled = 1;
        }
    }
    FIELD(self, OPTION_CURRENT, int) = 0;
    option_count(self) = count;
    control_redraw(self);
    return TRUE;
}

PORT(0x00479180)
BYTE* __fastcall option_construct(BYTE* self, void* edx_unused, DWORD a, const RECT* area,
                                  BYTE* dialog)
{
    (void) edx_unused;
    control_construct(self, NULL, 2, a, area, dialog);
    *(void***) self = UIOPTION_VTABLE;
    option_count(self) = 0;
    FIELD(self, OPTION_CURRENT, int) = 0;
    option_list(self) = NULL;
    option_set_count(self, NULL, 2);
    return self;
}

/* The options are only freed when the dialog is not being torn down whole (0x9B0). */
PORT(0x00479290)
void __fastcall option_destruct(BYTE* self)
{
    BYTE* dialog;
    int i;

    *(void***) self = UIOPTION_VTABLE;
    dialog = FIELD(self, CTL_DIALOG, BYTE*);
    if (option_count(self) && (!dialog || !FIELD(dialog, 0x9B0, int))) {
        for (i = 0; i < option_count(self); i++)
            option_free(self, i, 0x210E);
    }
    if (option_list(self))
        game_FUN_004a4450(option_list(self), GAMEUI_CPP, 0x2114);
    control_destruct(self);
}

PORT(0x00479270)
BYTE* __fastcall option_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    option_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* The option's picture for the state showing: disabled (4) when it has one. */
static ui_anim* option_anim(BYTE* self)
{
    ui_option* option = option_current(self);

    if (FIELD(self, 0x60, int) == 4 && option->anim_disabled)
        return option->anim_disabled;
    return option_current(self)->anim;
}

/* Vtable slot 38: whether it must be drawn again, as control_needs_redraw but for its option. */
PORT(0x004793E0)
void __fastcall option_needs_redraw(BYTE* self)
{
    BYTE* last = control_chain_end(self, NULL, NULL);

    if (!FIELD(self, 0x70, int) || !chain_settled(last)) {
        if (--FIELD(self, 0x1BC, int) >= 0)
            return;
        FIELD(self, 0x1BC, int) = 0;
    } else if (option_list(self) && option_current(self)->anim) {
        FIELD(self, 0x1C0, DWORD) = option_current(self)->anim->count == 1 ? 1 : 0;
        return;
    }
    FIELD(self, 0x1C0, DWORD) = 1;
}

/* Vtable slot 27: step the option's picture. */
PORT(0x00479470)
void __fastcall option_animate(BYTE* self)
{
    ui_anim* anim;

    if (!option_list(self))
        return;
    anim = FIELD(self, 0x60, int) == 4 ? option_current(self)->anim_disabled
                                       : option_current(self)->anim;
    if (anim)
        anim_step(anim);
}

/* Vtable slot 29: show state `state`, drawn over three frames. */
PORT(0x004794B0)
void __fastcall option_set_state(BYTE* self, void* edx_unused, DWORD state)
{
    (void) edx_unused;
    FIELD(self, 0x1BC, int) = 3;
    FIELD(self, 0x60, DWORD) = state;
}

/* Vtable slot 59: copy the option showing out to the bound int, or (`out` FALSE) show it. */
PORT(0x004794D0)
void __fastcall option_commit(BYTE* self, void* edx_unused, BOOL out)
{
    (void) edx_unused;
    if (out)
        *FIELD(self, EDIT_BOUND, int*) = option_current_index(self);
    else
        option_select(self, NULL, *FIELD(self, EDIT_BOUND, int*));
}

/* Vtable slot 48: the picture showing, or NULL. */
PORT(0x004797A0)
BYTE* __fastcall option_picture(BYTE* self, void* edx_unused, int state)
{
    ui_anim* anim = option_anim(self);

    (void) edx_unused;
    (void) state;
    return anim ? anim_picture(anim) : NULL;
}

/* Option `index`'s text: kept, its markup parsed, and measured when it has none. */
PORT(0x00479A60)
int __fastcall option_set_text(BYTE* self, void* edx_unused, int index, const char* text)
{
    int showing = FIELD(self, OPTION_CURRENT, int);

    (void) edx_unused;
    if (index < option_count(self)) {
        ui_option* option;

        FIELD(self, OPTION_CURRENT, int) = index;
        if (option_list(self)[index].text)
            game_FUN_004a4450(option_list(self)[index].text, GAMEUI_CPP, 0x2210);
        if (option_list(self)[index].runs)
            game_FUN_004a4450(option_list(self)[index].runs, GAMEUI_CPP, 0x2211);
        if (!text) {
            option_list(self)[index].text = NULL;
            option_list(self)[index].runs = NULL;
        } else {
            option_list(self)[index].text = game_FUN_004a4410(text_length(text) + 1, GAMEUI_CPP,
                                                              0x2213);
            text_copy(option_list(self)[index].text, text);
            option_list(self)[index].runs =
                dialog_parse_runs(FIELD(self, CTL_DIALOG, BYTE*), NULL, option_list(self)[index].text,
                                  FIELD(self, CTL_COLOUR, COLORREF), FIELD(self, CTL_FONT, HFONT),
                                  FIELD(self, CTL_TABS, const int*), option_list(self)[index].extent);
            option = &option_list(self)[index];
            if (!option->runs) {
                void* surface;
                HDC dc;

                option->extent[0] = (int) text_length(option->text);
                option_list(self)[index].length = option_list(self)[index].extent[0];
                surface = video_surface();
                if (!VIRTUAL(surface, 0x44, surface_get_dc)(surface, &dc)) {
                    BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);
                    HFONT font = FIELD(self, CTL_FONT, HFONT);
                    HGDIOBJ old;
                    SIZE size;

                    if (!font)
                        font = dialog ? FIELD(dialog, 0xD8, HFONT) : NULL;
                    old = SelectObject(dc, font);
                    GetTextExtentPoint32A(dc, option_list(self)[index].text,
                                          option_list(self)[index].length, &size);
                    SelectObject(dc, old);
                    surface = video_surface();
                    VIRTUAL(surface, 0x68, surface_release_dc)(surface, dc);
                    option_list(self)[index].extent[1] = size.cx;
                } else {
                    option_list(self)[index].extent[1] = 0;
                }
            }
        }
        FIELD(self, OPTION_CURRENT, int) = showing;
    }
    return control_redraw(self);
}

/* Option `index`'s picture (when given, the old one destroyed) and text (when given). */
PORT(0x00479980)
void __fastcall option_set(BYTE* self, void* edx_unused, int index, ui_anim* anim,
                           const char* text)
{
    (void) edx_unused;
    if (index < option_count(self) && anim) {
        ui_anim* old = option_list(self)[index].anim;

        if (old && !old->unknown_f4)
            object_destroy(old);
        option_list(self)[index].anim = anim;
    }
    if (text)
        option_set_text(self, NULL, index, text);
}

/* Option `index`'s text from string resource `id` of `module`. */
PORT(0x004799E0)
int __fastcall option_load_text(BYTE* self, void* edx_unused, int index, HINSTANCE module, UINT id)
{
    char text[0x400];
    const char* shown = text;

    (void) edx_unused;
    if (!LoadStringA(module, id, text, sizeof text)) {
        game_FUN_0053a11f(text, RESOURCE_FAILED, id);
        shown = RESOURCE_UNAVAILABLE;
    }
    option_set_text(self, NULL, index, shown);
    return control_redraw(self);
}

/* Vtable slot 55: pressed with the first button while it has focus, it steps to the next option. */
PORT(0x00479D10)
int __fastcall option_pressed(BYTE* self, void* edx_unused, DWORD button, DWORD b)
{
    (void) edx_unused;
    if (control_user(self)->focus == self && !button) {
        ui_event event;

        event.e = 0;
        option_next(self);
        control_tell(self, &event, 1);
        if (event.e)
            return 1;
    }
    return control_pressed(self, NULL, button, b);
}

/* Vtable slot 28: the button's test, as the original repeats it. */
PORT(0x00479DB0)
BOOL __fastcall option_contains(BYTE* self, void* edx_unused, LONG x, LONG y, DWORD unused)
{
    return button_contains(self, edx_unused, x, y, unused);
}

/* ---- UIPicture: a control that only shows its picture ---------------------------------------- */

#define UIPICTURE_VTABLE ((void**) 0x005583D0)

PORT(0x00479F00)
BYTE* __fastcall picture_control_construct(BYTE* self, void* edx_unused, DWORD a, const RECT* area,
                                           BYTE* dialog)
{
    (void) edx_unused;
    control_construct(self, NULL, 5, a, area, dialog);
    *(void***) self = UIPICTURE_VTABLE;
    return self;
}

PORT(0x00479F50)
void __fastcall picture_control_destruct(BYTE* self)
{
    *(void***) self = UIPICTURE_VTABLE;
    control_destruct(self);
}

PORT(0x00479F30)
BYTE* __fastcall picture_control_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    picture_control_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Vtable slot 48: its picture, whatever the state. */
PORT(0x00479F60)
BYTE* __fastcall picture_control_picture(BYTE* self, void* edx_unused, int state)
{
    ui_anim* anim = FIELD(self, 0x16C, ui_anim*);

    (void) edx_unused;
    (void) state;
    return anim ? anim_picture(anim) : NULL;
}

/* ---- UILabel: a control of text alone (type 12) ------------------------------------------------ */

#define UILABEL_VTABLE ((void**) 0x005584D8)

PORT(0x00479F80)
BYTE* __fastcall label_construct(BYTE* self, void* edx_unused, DWORD a, const RECT* area,
                                 BYTE* dialog, const char* text, COLORREF colour)
{
    (void) edx_unused;
    control_construct(self, NULL, 12, a, area, dialog);
    *(void***) self = UILABEL_VTABLE;
    FIELD(self, CTL_COLOUR, COLORREF) = colour;
    if (!text) {
        FIELD(self, CTL_TEXT, char*) = NULL;
        FIELD(self, CTL_SHADOW, DWORD) = 0;
        return self;
    }
    FIELD(self, CTL_TEXT, char*) = game_FUN_004a4410(text_length(text) + 1, GAMEUI_CPP, 0x22EC);
    text_copy(FIELD(self, CTL_TEXT, char*), text);
    FIELD(self, CTL_SHADOW, DWORD) = 0;
    return self;
}

PORT(0x0047A080)
void __fastcall label_destruct(BYTE* self)
{
    *(void***) self = UILABEL_VTABLE;
    control_destruct(self);
}

PORT(0x0047A060)
BYTE* __fastcall label_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    label_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Vtable slot 40: render as a control does, then settle. */
PORT(0x0047A090)
int __fastcall label_render(BYTE* self)
{
    int result = control_render(self);

    FIELD(self, 0x1C0, DWORD) = 1;
    return result;
}

/* Vtable slot 55: a press with the first button while it has focus tells the dialog (event 1). */
PORT(0x0047A0B0)
int __fastcall label_pressed(BYTE* self, void* edx_unused, DWORD button, DWORD b)
{
    (void) edx_unused;
    if (control_user(self)->focus == self && !button) {
        ui_event event;

        event.e = 0;
        control_tell(self, &event, 1);
        if (event.e)
            return 1;
    }
    return control_pressed(self, NULL, button, b);
}

/* Vtable slot 29: show state `state`, settled at once. */
PORT(0x0047A140)
void __fastcall label_set_state(BYTE* self, void* edx_unused, DWORD state)
{
    (void) edx_unused;
    FIELD(self, 0x1C0, DWORD) = 1;
    FIELD(self, 0x60, DWORD) = state;
}

/* Vtable slot 30: never takes focus. */
PORT(0x0047A160)
BOOL __fastcall label_slot_30(BYTE* self)
{
    (void) self;
    return FALSE;
}

/* ---- UIRadioButton: options of which one of a group is on ---------------------------------- */

/*
    A UIOption of type 4 whose option 1 is "on": of the radio buttons sharing its group (0x7C),
    one is on at a time.
*/
#define UIRADIO_VTABLE ((void**) 0x005585E0)
#define RADIO_CLASS    ((const char*) 0x00571EE4)   /* "UIRadioButton" */


PORT(0x0047A170)
BYTE* __fastcall radio_construct(BYTE* self, void* edx_unused, DWORD a, const RECT* area,
                                 BYTE* dialog)
{
    (void) edx_unused;
    option_construct(self, NULL, a, area, dialog);
    *(void***) self = UIRADIO_VTABLE;
    game_FUN_0046ac10(self, NULL, self);
    FIELD(self, CTL_TYPE, int) = 4;
    return self;
}

PORT(0x0047A200)
void __fastcall radio_destruct(BYTE* self)
{
    *(void***) self = UIRADIO_VTABLE;
    option_destruct(self);
}

PORT(0x0047A1E0)
BYTE* __fastcall radio_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    radio_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/*
    Turn on the `number`th radio button of the group (counting from 1) and the rest off; with 0,
    this one on and the others off and in state 0.
*/
PORT(0x0047A240)
BOOL __fastcall radio_set(BYTE* self, void* edx_unused, int number)
{
    BYTE walk[0x94];
    BYTE* radio;
    int counted = 0;

    (void) edx_unused;
    walk_begin(walk, NULL, FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_CONTAINER, void*), 1,
               RADIO_CLASS);
    if (!number)
        option_select(self, NULL, 1);
    for (radio = walk_next(walk); radio; radio = walk_next(walk)) {
        if (FIELD(radio, 0x7C, DWORD) != FIELD(self, 0x7C, DWORD))
            continue;
        counted++;
        if (number) {
            option_select(radio, NULL, counted == number ? 1 : 0);
        } else if (radio != self) {
            VIRTUAL(radio, 0x74, method_1)(radio, NULL, 0);
            option_select(radio, NULL, 0);
        }
    }
    walk_end(walk);
    return TRUE;
}

/* Which of the group (counting from 1) is on, or 0 when none is. */
PORT(0x0047A320)
int __fastcall radio_number(BYTE* self)
{
    BYTE walk[0x94];
    BYTE* radio;
    int counted = 0;

    walk_begin(walk, NULL, FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_CONTAINER, void*), 1,
               RADIO_CLASS);
    for (radio = walk_next(walk); radio; radio = walk_next(walk)) {
        if (FIELD(radio, 0x7C, DWORD) != FIELD(self, 0x7C, DWORD))
            continue;
        counted++;
        if (option_current_index(radio) == 1) {
            walk_end(walk);
            return counted;
        }
    }
    walk_end(walk);
    return 0;
}

/* Vtable slot 59: copy which is on out to the bound int, or (`out` FALSE) turn it on. */
PORT(0x0047A210)
void __fastcall radio_commit(BYTE* self, void* edx_unused, BOOL out)
{
    (void) edx_unused;
    if (out)
        *FIELD(self, EDIT_BOUND, int*) = radio_number(self);
    else
        radio_set(self, NULL, *FIELD(self, EDIT_BOUND, int*));
}

/*
    Vtable slot 55: pressed with the first button while it has focus, an off radio button turns
    on and the others of its kind off (state 0); the dialog hears (event 1, its own manager).
*/
PORT(0x0047A3F0)
int __fastcall radio_pressed(BYTE* self, void* edx_unused, DWORD button, DWORD b)
{
    (void) edx_unused;
    if (control_user(self)->focus == self && !button) {
        ui_event event;

        event.e = 0;
        if (!option_current_index(self)) {
            BYTE* walk;
            BYTE* radio;

            option_select(self, NULL, 1);
            walk = control_walk_of(self);
            while ((radio = control_next_alike(self, NULL, walk)) != NULL) {
                if (radio != self && option_current_index(radio) == 1) {
                    VIRTUAL(radio, 0x74, method_1)(radio, NULL, 0);
                    option_select(radio, NULL, 0);
                }
            }
            control_walk_end(self, NULL, walk);
        }
        list_tell(self, &event, 1);
        if (event.e)
            return 1;
    }
    return control_pressed(self, NULL, button, b);
}

/* Vtable slot 53: a walk over the radio buttons of its dialog, on the game's heap. */
PORT(0x0047A4F0)
BYTE* __fastcall radio_walk(BYTE* self)
{
    BYTE* walk = game_new(0x94, GAMEUI_CPP, 0x23B9);

    if (!walk)
        return NULL;
    walk_begin(walk, NULL, FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_CONTAINER, void*), 1,
               RADIO_CLASS);
    return walk;
}

/* Shared by many vtables: a slot that answers TRUE. */
PORT(0x0047BD10)
BOOL __fastcall vtable_true(void* self)
{
    (void) self;
    return TRUE;
}

/* ---- UIComboBox: a label that drops a list ----------------------------------------------- */

/*
    A UILabel of type 6 built from pieces: an arrow button beside the text, and below it a list
    with its scroll bar, each over a background picture (the text has one too). Shut, only the
    button and the text's background are its children; open, the list and scroll bar and their
    backgrounds go under the dialog's container, over everything else. Each piece knows the
    combo box, and losing capture to anything that is not part of it shuts it.
*/
#define UICOMBO_VTABLE    ((void**) 0x00558B10)
#define COMBO_BUTTON      0x1EC
#define COMBO_LIST_BACK   0x1F0     /* the pictures behind the list, the scroll bar and the text */
#define COMBO_SLIDER_BACK 0x1F4
#define COMBO_TEXT_BACK   0x1F8
#define COMBO_LIST        0x1FC
#define COMBO_SLIDER      0x200
#define COMBO_DROP        0x204     /* the list's height */
#define COMBO_OPEN        0x208
#define COMBO_OPEN_AREA   0x20C     /* RECT: all it covers open */
#define COMBO_GROUP       0x4D43    /* the list and scroll bar's group when it has none */
#define CTL_DESIGN        0x3C      /* RECT: the control's design rectangle */

#define COMBO_NAME_BUTTON      ((const char*) 0x00571FC0)  /* "%s_BUT" */
#define COMBO_NAME_LIST        ((const char*) 0x00571FB8)  /* "%s_LB" */
#define COMBO_NAME_SLIDER      ((const char*) 0x00571FB0)  /* "%s_SB" */
#define COMBO_NAME_LIST_BACK   ((const char*) 0x00571FA8)  /* "%s_LBK" */
#define COMBO_NAME_SLIDER_BACK ((const char*) 0x00571FA0)  /* "%s_SBK" */
#define COMBO_NAME_TEXT_BACK   ((const char*) 0x00571F98)  /* "%s_TBK" */

/* The pieces that drop down with the list, in the order the original walks them. */
static const int combo_dropped[] = { COMBO_LIST_BACK, COMBO_SLIDER_BACK, COMBO_LIST, COMBO_SLIDER };

int  __fastcall game_FUN_0046a0d0(void* self);    /* unlink from its parent; 0 while locked */
#define object_unlink game_FUN_0046a0d0

/* Open or shut the list. */
PORT(0x0047B210)
void __fastcall combo_open(BYTE* self, void* edx_unused, BOOL open)
{
    BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);
    int i;

    (void) edx_unused;
    for (i = 0; i < 4; i++)
        control_show(FIELD(self, combo_dropped[i], BYTE*), NULL, open, TRUE);
    if (!FIELD(self, COMBO_OPEN, BOOL) && open) {
        for (i = 0; i < 4; i++)
            object_add_child(FIELD(dialog, DLG_CONTAINER, void*), NULL,
                             FIELD(self, combo_dropped[i], BYTE*), -1);
        list_tell_sliders(FIELD(self, COMBO_LIST, BYTE*));
    } else if (FIELD(self, COMBO_OPEN, BOOL) && !open) {
        for (i = 0; i < 4; i++)
            object_unlink(FIELD(self, combo_dropped[i], BYTE*));
        if (FIELD(dialog, DLG_BACKGROUND, BYTE*))
            background_invalidate(FIELD(dialog, DLG_BACKGROUND, BYTE*));
    }
    FIELD(self, COMBO_OPEN, BOOL) = open;
}

/* Whether `control` is the combo box or one of its pieces. */
PORT(0x0047B740)
BOOL __fastcall combo_owns(BYTE* self, void* edx_unused, BYTE* control)
{
    (void) edx_unused;
    return control == self || control == FIELD(self, COMBO_SLIDER, BYTE*)
        || control == FIELD(self, COMBO_BUTTON, BYTE*) || control == FIELD(self, COMBO_LIST, BYTE*)
        || control == FIELD(self, COMBO_LIST_BACK, BYTE*)
        || control == FIELD(self, COMBO_SLIDER_BACK, BYTE*)
        || control == FIELD(self, COMBO_TEXT_BACK, BYTE*);
}

/* Vtable slot 33 of the combo box and its pieces: capture lost outside it shuts the list. */
static void combo_lose_capture(BYTE* self, BYTE* combo, DWORD to)
{
    game_vf_32_47D940(self, NULL, to);
    if (!combo_owns(combo, NULL, (BYTE*) to))
        combo_open(combo, NULL, FALSE);
}

/* Tell the dialog as a piece of `combo`: the event is the piece's, under the combo box's name. */
static void combo_tell(BYTE* self, BYTE* combo, ui_event* event, DWORD type, gui_manager* manager)
{
    BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);

    event->a = FIELD(self, 0x74, DWORD);
    event->type = type;
    event->b = (DWORD) control_get_name(combo);
    event->sender = dialog;
    event->manager = manager;
    event->c = (DWORD) self;
#ifdef OPENMCM2_DEV
    dev_trace_event(event->type, (const char*) event->b, dialog);
#endif
    VIRTUAL(dialog, 0x74, dialog_handle)(dialog, NULL, event);
}

/* Copy a control's box to its design rectangle. */
static void control_keep_design(BYTE* control)
{
    FIELD(control, CTL_DESIGN, RECT) = FIELD(control, CTL_RECT, RECT);
}

/* ---- The combo box's scroll bar ---- */

#define COMBO_SLIDER_VTABLE ((void**) 0x005586E8)
#define COMBO_SLIDER_OWNER  0x220

PORT(0x0047A570)
BYTE* __fastcall combo_slider_construct(BYTE* self, void* edx_unused, int type, DWORD a,
                                        const RECT* area, BYTE* dialog, BYTE* combo)
{
    (void) edx_unused;
    slider_construct(self, NULL, type, a, area, dialog);
    FIELD(self, COMBO_SLIDER_OWNER, BYTE*) = combo;
    *(void***) self = COMBO_SLIDER_VTABLE;
    game_FUN_0046ac10(self, NULL, self);
    return self;
}

PORT(0x0047A600)
void __fastcall combo_slider_destruct(BYTE* self)
{
    slider_destruct(self);
}

PORT(0x0047A5E0)
BYTE* __fastcall combo_slider_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    combo_slider_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

PORT(0x0047A610)
void __fastcall combo_slider_lose_capture(BYTE* self, void* edx_unused, DWORD to)
{
    (void) edx_unused;
    combo_lose_capture(self, FIELD(self, COMBO_SLIDER_OWNER, BYTE*), to);
}

/*
    Vtable slot 57: dragged, as a slider is (events 15, 3 and 17 under the combo box's name),
    but the list it scrolls is the combo box's own.
*/
PORT(0x0047A640)
void __fastcall combo_slider_drag(BYTE* self, void* edx_unused, DWORD button, const POINT* at)
{
    BYTE* combo = FIELD(self, COMBO_SLIDER_OWNER, BYTE*);
    gui_manager* manager = FIELD(self, CTL_MANAGER, gui_manager*);
    int width, height, steps, offset;
    ui_event event;
    BYTE* list;

    (void) edx_unused;
    control_drag(self, NULL, button, at);
    event.e = 0;
    if (button || !at)
        return;
    width = VIRTUAL(self, 0xF4, method_0)(self);
    height = VIRTUAL(self, 0xF8, method_0)(self);
    control_redraw(self);
    steps = slider_steps(self);
    if (slider_across(self)) {
        offset = at->x - FIELD(self, CTL_RECT, RECT).left;
        if (!steps)
            offset -= FIELD(self, SLIDER_THUMB, int) / 2;
        offset = slider_place(offset, width - FIELD(self, SLIDER_THUMB, int));
    } else {
        offset = at->y - FIELD(self, CTL_RECT, RECT).top;
        if (!steps)
            offset -= FIELD(self, SLIDER_THUMB + 4, int) / 2;
        offset = slider_place(offset, height - FIELD(self, SLIDER_THUMB + 4, int));
    }
    FIELD(self, SLIDER_AT, float) = (float) offset;
    if (steps)
        slider_set_value(self, NULL, slider_value_at(self, NULL, steps), steps);

    if (!FIELD(self, SLIDER_DRAGGING, int)) {
        combo_tell(self, combo, &event, 15, manager);
        FIELD(self, SLIDER_DRAGGING, int) = 1;
    }
    if (event.e)
        return;
    if (at->x == FIELD(self, SLIDER_HELD_AT, LONG) && at->y == FIELD(self, SLIDER_HELD_AT + 4, LONG)) {
        double held = (double) FIELD(FIELD(self, CTL_DIALOG, BYTE*), 0x9C0, float)
                      + FIELD(self, SLIDER_HELD, float);

        FIELD(self, SLIDER_HELD, float) = (float) held;
        if (!x87_less(held, FIELD(self, SLIDER_HOLD, float))) {
            combo_tell(self, combo, &event, 17, manager);
            if (event.e)
                return;
        }
    } else {
        combo_tell(self, combo, &event, 3, manager);
        FIELD(self, SLIDER_HELD_AT, LONG) = at->x;
        FIELD(self, SLIDER_HELD_AT + 4, LONG) = at->y;
        FIELD(self, SLIDER_HELD, float) = 0.0f;
        if (event.e)
            return;
    }

    list = FIELD(combo, COMBO_LIST, BYTE*);
    if (list)
        list_scroll(list, NULL,
                    slider_value_at(self, NULL, list_count(list) - FIELD(list, LIST_LAST_PAGE, int)),
                    FALSE);
}

/*
    Vtable slot 55: pressed with the first button while it has focus. With steps the thumb jumps
    to the pointer; without, a press below the thumb pages the list on, above it back. It runs
    down only.
*/
PORT(0x0047A930)
int __fastcall combo_slider_pressed(BYTE* self, void* edx_unused, DWORD button, const POINT* at)
{
    (void) edx_unused;
    if (control_user(self)->focus == self && !button && at) {
        BYTE* list = FIELD(FIELD(self, COMBO_SLIDER_OWNER, BYTE*), COMBO_LIST, BYTE*);
        LONG top = FIELD(self, CTL_RECT, RECT).top;
        int thumb = FIELD(self, SLIDER_THUMB + 4, int);

        if (slider_steps(self)) {
            int track = VIRTUAL(self, 0xF8, method_0)(self) - thumb;

            FIELD(self, SLIDER_AT, float) = (float) slider_place(at->y - top, track);
        } else if ((double) at->y > (double) thumb + top + FIELD(self, SLIDER_AT, float)) {
            list_scroll(list, NULL, list_first_shown(list) + FIELD(list, LIST_ROWS, int), TRUE);
        } else if (x87_less(at->y, (double) top + FIELD(self, SLIDER_AT, float))) {
            list_scroll(list, NULL, list_first_shown(list) - FIELD(list, LIST_ROWS, int), TRUE);
        }
    }
    if (slider_steps(self))
        slider_set_value(self, NULL, slider_value_at(self, NULL, slider_steps(self)),
                         slider_steps(self));
    return control_pressed(self, NULL, button, (DWORD) at);
}

/* ---- The combo box's background pictures ---- */

#define COMBO_PICTURE_VTABLE ((void**) 0x005587F0)
#define COMBO_PICTURE_OWNER  0x1EC

PORT(0x0047AA80)
BYTE* __fastcall combo_picture_construct(BYTE* self, void* edx_unused, DWORD a, const RECT* area,
                                         BYTE* dialog, BYTE* combo)
{
    (void) edx_unused;
    picture_control_construct(self, NULL, a, area, dialog);
    FIELD(self, COMBO_PICTURE_OWNER, BYTE*) = combo;
    *(void***) self = COMBO_PICTURE_VTABLE;
    game_FUN_0046ac10(self, NULL, self);
    return self;
}

PORT(0x0047AAF0)
void __fastcall combo_picture_destruct(BYTE* self)
{
    picture_control_destruct(self);
}

#define UIMOVIE_VTABLE ((void**) 0x00558C18)

/* Also the slot 0 of other UIPicture kinds, which destroy no more than it - UIMovie's among them. */
PORT(0x0047BDB0)
BYTE* __fastcall combo_picture_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
#ifdef OPENMCM2_DEV
    if (*(void***) self == UIMOVIE_VTABLE)
        video_close(self);          /* the movie video.c plays goes with its control */
#endif
    combo_picture_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

PORT(0x0047AB00)
void __fastcall combo_picture_lose_capture(BYTE* self, void* edx_unused, DWORD to)
{
    (void) edx_unused;
    combo_lose_capture(self, FIELD(self, COMBO_PICTURE_OWNER, BYTE*), to);
}

/* ---- The combo box's arrow button ---- */

#define COMBO_BUTTON_VTABLE ((void**) 0x005588F8)
#define COMBO_BUTTON_OWNER  0x1F0

PORT(0x0047AB30)
BYTE* __fastcall combo_button_construct(BYTE* self, void* edx_unused, DWORD a, const RECT* area,
                                        BYTE* dialog, BYTE* combo)
{
    (void) edx_unused;
    button_construct(self, NULL, a, area, dialog);
    FIELD(self, COMBO_BUTTON_OWNER, BYTE*) = combo;
    *(void***) self = COMBO_BUTTON_VTABLE;
    game_FUN_0046ac10(self, NULL, self);
    return self;
}

PORT(0x0047ABC0)
void __fastcall combo_button_destruct(BYTE* self)
{
    button_destruct(self);
}

PORT(0x0047ABA0)
BYTE* __fastcall combo_button_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    combo_button_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Vtable slot 55: pressed while it has focus, it opens or shuts the list. */
PORT(0x0047ABD0)
int __fastcall combo_button_pressed(BYTE* self, void* edx_unused, DWORD button, DWORD b)
{
    (void) edx_unused;
    control_pressed(self, NULL, button, b);
    if (control_user(self)->focus == self) {
        BYTE* combo = FIELD(self, COMBO_BUTTON_OWNER, BYTE*);

        combo_open(combo, NULL, 1 - FIELD(combo, COMBO_OPEN, int));
    }
    return 0;
}

PORT(0x0047AC20)
void __fastcall combo_button_lose_capture(BYTE* self, void* edx_unused, DWORD to)
{
    (void) edx_unused;
    combo_lose_capture(self, FIELD(self, COMBO_BUTTON_OWNER, BYTE*), to);
}

/* ---- The combo box's list ---- */

#define COMBO_LIST_VTABLE ((void**) 0x00558A00)
#define COMBO_LIST_OWNER  0x250

PORT(0x0047AC50)
BYTE* __fastcall combo_list_construct(BYTE* self, void* edx_unused, DWORD a, int capacity,
                                      const RECT* area, BYTE* dialog, BYTE* combo)
{
    (void) edx_unused;
    list_construct(self, NULL, a, capacity, area, dialog);
    FIELD(self, COMBO_LIST_OWNER, BYTE*) = combo;
    *(void***) self = COMBO_LIST_VTABLE;
    game_FUN_0046ac10(self, NULL, self);
    return self;
}

PORT(0x0047ACE0)
void __fastcall combo_list_destruct(BYTE* self)
{
    list_destruct(self);
}

PORT(0x0047ACC0)
BYTE* __fastcall combo_list_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    combo_list_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Vtable slot 65: select entry `index`, and the combo box shows its text. */
PORT(0x0047ACF0)
int __fastcall combo_list_select(BYTE* self, void* edx_unused, int index)
{
    int result = list_select(self, NULL, index);

    (void) edx_unused;
    if (result)
        control_set_text(FIELD(self, COMBO_LIST_OWNER, BYTE*), NULL, list_text(self, NULL, index));
    return result;
}

/* Vtable slot 66: an entry chosen. The combo box shows it and shuts, and the dialog hears (2). */
PORT(0x0047AD30)
void __fastcall combo_list_choose(BYTE* self, void* edx_unused, DWORD unused)
{
    BYTE* combo = FIELD(self, COMBO_LIST_OWNER, BYTE*);
    ui_event event;

    (void) edx_unused;
    (void) unused;
    event.e = 0;
    control_set_text(combo, NULL, list_text(self, NULL, list_selected(self)));
    combo_open(combo, NULL, FALSE);
    combo_tell(self, combo, &event, 2,
               FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_MANAGER, gui_manager*));
}

PORT(0x0047ADB0)
void __fastcall combo_list_lose_capture(BYTE* self, void* edx_unused, DWORD to)
{
    (void) edx_unused;
    combo_lose_capture(self, FIELD(self, COMBO_LIST_OWNER, BYTE*), to);
}

/* ---- The combo box itself ---- */

PORT(0x0047ADE0)
BYTE* __fastcall combo_construct(BYTE* self, void* edx_unused, DWORD a, const RECT* area,
                                 BYTE* dialog, const char* text, COLORREF colour)
{
    static const int linked[] = { COMBO_LIST_BACK, COMBO_SLIDER_BACK, COMBO_TEXT_BACK,
                                  COMBO_BUTTON, COMBO_LIST, COMBO_SLIDER };
    static const int attached[] = { COMBO_BUTTON, COMBO_LIST, COMBO_SLIDER,
                                    COMBO_LIST_BACK, COMBO_TEXT_BACK, COMBO_SLIDER_BACK };
    static const int backs[] = { COMBO_LIST_BACK, COMBO_SLIDER_BACK, COMBO_TEXT_BACK };
    RECT box = { 0, 0, 10, 10 };
    BYTE* piece;
    int i;

    (void) edx_unused;
    label_construct(self, NULL, a, area, dialog, text, colour);
    *(void***) self = UICOMBO_VTABLE;
    game_FUN_0046ac10(self, NULL, self);
    FIELD(self, CTL_TYPE, int) = 6;
    FIELD(self, COMBO_DROP, int) = 20;

    piece = game_new(0x1F4, GAMEUI_CPP, 0x24E3);
    FIELD(self, COMBO_BUTTON, BYTE*) =
        piece ? combo_button_construct(piece, NULL, 0, &box, dialog, self) : NULL;
    piece = game_new(0x254, GAMEUI_CPP, 0x24E4);
    FIELD(self, COMBO_LIST, BYTE*) =
        piece ? combo_list_construct(piece, NULL, 0, 100, &box, dialog, self) : NULL;
    piece = game_new(0x224, GAMEUI_CPP, 0x24E5);
    FIELD(self, COMBO_SLIDER, BYTE*) =
        piece ? combo_slider_construct(piece, NULL, 7, 0, &box, dialog, self) : NULL;
    for (i = 0; i < 3; i++) {
        piece = game_new(0x1F0, GAMEUI_CPP, 0x24E6 + i);
        FIELD(self, backs[i], BYTE*) =
            piece ? combo_picture_construct(piece, NULL, 0, &box, dialog, self) : NULL;
    }

    for (i = 0; i < 6; i++)
        control_set_link(FIELD(self, linked[i], BYTE*), NULL, self, 1);
    VIRTUAL(FIELD(self, COMBO_LIST, BYTE*), 0xD0, method_1)(FIELD(self, COMBO_LIST, BYTE*), NULL,
                                                           COMBO_GROUP);
    VIRTUAL(FIELD(self, COMBO_SLIDER, BYTE*), 0xD0, method_1)(FIELD(self, COMBO_SLIDER, BYTE*),
                                                             NULL, COMBO_GROUP);
    object_add_child(self, NULL, FIELD(self, COMBO_BUTTON, BYTE*), -1);
    object_add_child(self, NULL, FIELD(self, COMBO_TEXT_BACK, BYTE*), -1);
    FIELD(self, COMBO_OPEN, BOOL) = FALSE;
    combo_open(self, NULL, FALSE);
    for (i = 0; i < 6; i++)
        dialog_attach(dialog, NULL, FIELD(self, attached[i], BYTE*), 0, 0);
    return self;
}

/* Vtable slot 59: copy the selection out to the bound int, or (`out` FALSE) choose from it. */
PORT(0x0047BCC0)
void __fastcall combo_commit(BYTE* self, void* edx_unused, BOOL out)
{
    BYTE* list = FIELD(self, COMBO_LIST, BYTE*);

    (void) edx_unused;
    if (out) {
        *FIELD(self, EDIT_BOUND, int*) = list_selected(list);
        return;
    }
    list_select_all(list, NULL, *FIELD(self, EDIT_BOUND, int*));
    VIRTUAL(list, 0x108, method_1)(list, NULL, 0);
}

/* Shut, the pieces that drop down are not under the dialog, so the combo box destroys them. */
PORT(0x0047B130)
void __fastcall combo_destruct(BYTE* self)
{
    *(void***) self = UICOMBO_VTABLE;
    if (FIELD(self, EDIT_BOUND, void*))
        combo_commit(self, NULL, TRUE);
    if (!FIELD(self, COMBO_OPEN, BOOL)) {
        int i;

        for (i = 0; i < 4; i++)
            object_destroy(FIELD(self, combo_dropped[i], BYTE*));
    }
    label_destruct(self);
}

PORT(0x0047B110)
BYTE* __fastcall combo_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    combo_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Vtable slot 52: its group (0x7C), which the list and scroll bar share, or theirs alone. */
PORT(0x0047B1D0)
void __fastcall combo_set_group(BYTE* self, void* edx_unused, DWORD group)
{
    (void) edx_unused;
    FIELD(self, 0x7C, DWORD) = group;
    if (!group)
        group = COMBO_GROUP;
    VIRTUAL(FIELD(self, COMBO_LIST, BYTE*), 0xD0, method_1)(FIELD(self, COMBO_LIST, BYTE*), NULL,
                                                           group);
    VIRTUAL(FIELD(self, COMBO_SLIDER, BYTE*), 0xD0, method_1)(FIELD(self, COMBO_SLIDER, BYTE*),
                                                             NULL, group);
}

/* Name `piece` after the combo box ("<name>_BUT" ...) and place it at `box`, its design too. */
static void combo_place(BYTE* self, BYTE* piece, const char* format, LONG left, LONG top,
                        LONG right, LONG bottom)
{
    char name[0x34];

    game_FUN_0053a11f(name, format, control_get_name(self));
    control_set_name(piece, NULL, name);
    FIELD(piece, CTL_RECT, RECT).left = left;
    FIELD(piece, CTL_RECT, RECT).top = top;
    FIELD(piece, CTL_RECT, RECT).right = right;
    FIELD(piece, CTL_RECT, RECT).bottom = bottom;
    control_keep_design(piece);
}

/*
    Lay the pieces out from its box: the text's background over it, the arrow just past it as
    wide as its picture (hidden when it has none), and below, the list and its background the
    combo box's width and the scroll bar under the arrow, COMBO_DROP tall. The list takes its
    margins and colours.
*/
PORT(0x0047B340)
void __fastcall combo_layout(BYTE* self)
{
    const RECT* area = &FIELD(self, CTL_RECT, RECT);
    BYTE* button = FIELD(self, COMBO_BUTTON, BYTE*);
    BYTE* list = FIELD(self, COMBO_LIST, BYTE*);
    ui_anim* arrow = FIELD(button, 0x16C, ui_anim*);
    LONG width = area->right - area->left;
    LONG height = area->bottom - area->top;
    LONG drop = FIELD(self, COMBO_DROP, int);
    LONG side = 0, right = 0, tall = 0;

    if (arrow) {
        side = width + 1;
        right = arrow->frames[0]->width + side;
        tall = height;
    }
    FIELD(self, COMBO_OPEN_AREA, RECT).left = area->left;
    FIELD(self, COMBO_OPEN_AREA, RECT).top = area->top;
    FIELD(self, COMBO_OPEN_AREA, RECT).right = area->left + right;
    FIELD(self, COMBO_OPEN_AREA, RECT).bottom = area->top + height + drop - 1;

    combo_place(self, button, COMBO_NAME_BUTTON, side, 0, right, tall);
    if (!side)
        VIRTUAL(button, 0x10, method_0)(button);   /* hide it */
    combo_place(self, list, COMBO_NAME_LIST, 0, height + 1, width, height + 1 + drop);
    combo_place(self, FIELD(self, COMBO_SLIDER, BYTE*), COMBO_NAME_SLIDER, side, height + 3, right,
                height + drop - 1);
    combo_place(self, FIELD(self, COMBO_LIST_BACK, BYTE*), COMBO_NAME_LIST_BACK, 0, height + 1,
                width, height + 1 + drop);
    combo_place(self, FIELD(self, COMBO_SLIDER_BACK, BYTE*), COMBO_NAME_SLIDER_BACK, side,
                height + 3, right, height + drop - 1);
    combo_place(self, FIELD(self, COMBO_TEXT_BACK, BYTE*), COMBO_NAME_TEXT_BACK, 0, 0, width,
                height);

    FIELD(list, 0xE8, LONG) = FIELD(self, 0xE8, LONG);
    FIELD(list, 0xEC, LONG) = FIELD(self, 0xEC, LONG);
    list_set_text_colour(list, NULL, FIELD(self, CTL_COLOUR, COLORREF), -1);
    if (FIELD(self, CTL_COLOUR, COLORREF) != 0xFFFFFF)
        list_set_208(list, NULL, 0xFFFFFF);
    control_set_shadow_colour(list, NULL, FIELD(self, CTL_SHADOW_COLOUR, COLORREF));
}

PORT(0x0047B710)
void __fastcall combo_lose_capture_slot(BYTE* self, void* edx_unused, DWORD to)
{
    (void) edx_unused;
    combo_lose_capture(self, self, to);
}

/* Vtable slots 61 and 62: its width, to the arrow's far side when it has one, and its height open. */
PORT(0x0047B790)
int __fastcall combo_width(BYTE* self)
{
    BYTE* button = FIELD(self, COMBO_BUTTON, BYTE*);

    if (!FIELD(button, CTL_DESIGN, RECT).left)
        return FIELD(self, CTL_DESIGN, RECT).right - FIELD(self, CTL_DESIGN, RECT).left;
    return FIELD(button, CTL_DESIGN, RECT).right - FIELD(self, CTL_DESIGN, RECT).left;
}

PORT(0x0047B7B0)
int __fastcall combo_height(BYTE* self)
{
    return FIELD(FIELD(self, COMBO_LIST, BYTE*), CTL_DESIGN, RECT).bottom
         - FIELD(self, CTL_DESIGN, RECT).top;
}

/* Make the list `drop` tall, with its background and the scroll bar (inset 2 at each end). */
PORT(0x0047B7C0)
void __fastcall combo_set_drop(BYTE* self, void* edx_unused, int drop)
{
    BYTE* slider = FIELD(self, COMBO_SLIDER, BYTE*);
    RECT* box;

    (void) edx_unused;
    box = &FIELD(FIELD(self, COMBO_LIST, BYTE*), CTL_RECT, RECT);
    box->bottom = box->top + drop;
    control_keep_design(FIELD(self, COMBO_LIST, BYTE*));
    box = &FIELD(FIELD(self, COMBO_LIST_BACK, BYTE*), CTL_RECT, RECT);
    box->bottom = box->top + drop;
    control_keep_design(FIELD(self, COMBO_LIST_BACK, BYTE*));
    box = &FIELD(slider, CTL_RECT, RECT);
    box->bottom = box->top + drop;
    box->top += 2;
    box->bottom -= 2;
    control_keep_design(slider);
    box = &FIELD(FIELD(self, COMBO_SLIDER_BACK, BYTE*), CTL_RECT, RECT);
    box->bottom = box->top + drop;
    control_keep_design(FIELD(self, COMBO_SLIDER_BACK, BYTE*));
    FIELD(self, COMBO_DROP, int) = drop;
}

/* Give background piece `back` the picture `anim`, sized to it. */
static void combo_set_back(BYTE* back, ui_anim* anim)
{
    RECT* box = &FIELD(back, CTL_RECT, RECT);

    control_set_picture(back, NULL, 0, (DWORD) anim);
    box->right = anim_current(anim)->width + box->left;
    box->bottom = anim_current(anim)->height + box->top;
    control_keep_design(back);
}

/* The list moves to its background's box (by way of its design rectangle). */
static void combo_list_to_back(BYTE* self)
{
    BYTE* list = FIELD(self, COMBO_LIST, BYTE*);

    FIELD(list, CTL_DESIGN, RECT) = FIELD(FIELD(self, COMBO_LIST_BACK, BYTE*), CTL_RECT, RECT);
    FIELD(list, CTL_RECT, RECT) = FIELD(list, CTL_DESIGN, RECT);
}

/* The scroll bar moves to its background's box, inset 2 at each end. */
static void combo_slider_to_back(BYTE* self)
{
    BYTE* slider = FIELD(self, COMBO_SLIDER, BYTE*);

    FIELD(slider, CTL_RECT, RECT) = FIELD(FIELD(self, COMBO_SLIDER_BACK, BYTE*), CTL_RECT, RECT);
    FIELD(slider, CTL_RECT, RECT).top += 2;
    FIELD(slider, CTL_RECT, RECT).bottom -= 2;
    control_keep_design(slider);
}

/* The list's background picture; the list drops as tall as it. */
PORT(0x0047B8B0)
void __fastcall combo_set_list_back(BYTE* self, void* edx_unused, ui_anim* anim)
{
    (void) edx_unused;
    combo_set_back(FIELD(self, COMBO_LIST_BACK, BYTE*), anim);
    combo_list_to_back(self);
    combo_set_drop(self, NULL, anim_current(anim)->height);
}

/* The scroll bar's background picture. */
PORT(0x0047B980)
void __fastcall combo_set_slider_back(BYTE* self, void* edx_unused, ui_anim* anim)
{
    (void) edx_unused;
    combo_set_back(FIELD(self, COMBO_SLIDER_BACK, BYTE*), anim);
    combo_slider_to_back(self);
}

/*
    The text's background picture: the combo box takes its size, and the other pieces go round
    it, the list below, the arrow (when placed) and the scroll bar past its right edge.
*/
PORT(0x0047BA60)
void __fastcall combo_set_text_back(BYTE* self, void* edx_unused, ui_anim* anim)
{
    BYTE* text_back = FIELD(self, COMBO_TEXT_BACK, BYTE*);
    const RECT* back;
    RECT* area = &FIELD(self, CTL_RECT, RECT);
    BYTE* button;

    (void) edx_unused;
    control_set_picture(text_back, NULL, 0, (DWORD) anim);
    FIELD(text_back, CTL_RECT, RECT).top = 0;
    FIELD(text_back, CTL_RECT, RECT).left = 0;
    FIELD(text_back, CTL_RECT, RECT).right = anim_current(anim)->width;
    FIELD(text_back, CTL_RECT, RECT).bottom = anim_current(anim)->height;
    control_keep_design(text_back);
    back = &FIELD(FIELD(self, COMBO_TEXT_BACK, BYTE*), CTL_RECT, RECT);

    area->right = back->right + area->left;
    area->bottom = back->bottom + area->top;
    control_keep_design(self);

    FIELD(FIELD(self, COMBO_LIST_BACK, BYTE*), CTL_RECT, RECT).left = 0;
    FIELD(FIELD(self, COMBO_LIST_BACK, BYTE*), CTL_RECT, RECT).top = back->bottom;
    control_keep_design(FIELD(self, COMBO_LIST_BACK, BYTE*));
    combo_list_to_back(self);

    button = FIELD(self, COMBO_BUTTON, BYTE*);
    if (FIELD(button, CTL_DESIGN, RECT).left) {
        RECT* box = &FIELD(button, CTL_RECT, RECT);
        BYTE* arrow;

        box->top = 0;
        box->left = back->right;
        arrow = VIRTUAL(button, 0xC0, control_state_picture)(button, NULL, -1);
        box->right = FIELD(arrow, 0x14, LONG) + box->left;
        arrow = VIRTUAL(button, 0xC0, control_state_picture)(button, NULL, -1);
        box->bottom = FIELD(arrow, 0x18, LONG) + box->top;
        control_keep_design(button);
    }

    FIELD(FIELD(self, COMBO_SLIDER_BACK, BYTE*), CTL_RECT, RECT).left = back->right;
    FIELD(FIELD(self, COMBO_SLIDER_BACK, BYTE*), CTL_RECT, RECT).top = back->bottom;
    control_keep_design(FIELD(self, COMBO_SLIDER_BACK, BYTE*));
    combo_slider_to_back(self);
}

/* Vtable slot 55: pressed while it has focus, it opens or shuts and takes capture. */
PORT(0x0047BD20)
int __fastcall combo_pressed(BYTE* self, void* edx_unused, DWORD button, DWORD b)
{
    (void) edx_unused;
    if (control_user(self)->focus == self) {
        combo_open(self, NULL, !FIELD(self, COMBO_OPEN, BOOL));
        gui_user_queue_capture(control_user(self), NULL, self, NULL, 0);
        return 1;
    }
    return control_pressed(self, NULL, button, b);
}

/* ---- UIMovie: a picture control that plays a movie ------------------------------------------ */

/*
    The movie is a 0x80-byte stream object (a DirectShow multimedia stream drawing to a
    DirectDraw surface, 0x2C) made a child of the control; MOVIE_SOURCE is the part of its
    surface shown. The dev build never makes one: video.c plays the launcher's MPEG-1 copy of
    the movie instead, and MOVIE_STREAM stays NULL.
*/
#define MOVIE_STREAM   0x1EC
#define MOVIE_SOURCE   0x1F0       /* RECT */

BYTE* __fastcall game_FUN_004a3980(void* self, void* edx_unused, DWORD a);
BYTE* __fastcall game_FUN_004a3ad0(void* self, void* edx_unused, void* screen, const char* file,
                                   DWORD a, DWORD b);
BOOL  __fastcall game_FUN_004a3ef0(void* self);
#define stream_construct game_FUN_004a3980
#define stream_open      game_FUN_004a3ad0   /* destroys the stream and gives NULL when it fails */
#define stream_play      game_FUN_004a3ef0

typedef HRESULT (__stdcall* surface_blt)(void* surface, RECT* to, void* from, RECT* source,
                                         DWORD flags, void* effects);
#define BLT_WAIT 0x1000000

PORT(0x0047BD80)
BYTE* __fastcall movie_construct(BYTE* self, void* edx_unused, DWORD a, const RECT* area,
                                 BYTE* dialog)
{
    (void) edx_unused;
    picture_control_construct(self, NULL, a, area, dialog);
    *(void***) self = UIMOVIE_VTABLE;
    FIELD(self, MOVIE_STREAM, BYTE*) = NULL;
    return self;
}

/* Open movie `file` in place of the one it had, showing all of it, and start it if `play`. */
PORT(0x0047BDD0)
BOOL __fastcall movie_open(BYTE* self, void* edx_unused, const char* file, BOOL play, DWORD a,
                           DWORD b)
{
#ifndef OPENMCM2_DEV
    BYTE* stream;
#endif

    (void) edx_unused;
    if (FIELD(self, MOVIE_STREAM, BYTE*))
        object_destroy(FIELD(self, MOVIE_STREAM, BYTE*));
#ifdef OPENMCM2_DEV
    /* Never the original (Indeo 5): the launcher's MPEG-1 copy of it, played by video.c, or none. */
    FIELD(self, MOVIE_STREAM, BYTE*) = NULL;
    if (!video_open(self, file, play, a, b))
        return FALSE;
    FIELD(self, MOVIE_SOURCE, RECT).left = 0;
    FIELD(self, MOVIE_SOURCE, RECT).top = 0;
    video_size(self, &FIELD(self, MOVIE_SOURCE, RECT).right, &FIELD(self, MOVIE_SOURCE, RECT).bottom);
    return TRUE;
#else
    stream = game_new(0x80, GAMEUI_CPP, 0x264D);
    if (stream)
        stream = stream_construct(stream, NULL, 1);
    FIELD(self, MOVIE_STREAM, BYTE*) = stream;
    if (!stream || !stream_open(stream, NULL, object_screen(self), file, a, b)) {
        FIELD(self, MOVIE_STREAM, BYTE*) = NULL;
        return FALSE;
    }
    object_add_child(self, NULL, FIELD(self, MOVIE_STREAM, BYTE*), -1);
    stream = FIELD(self, MOVIE_STREAM, BYTE*);
    FIELD(self, MOVIE_SOURCE, RECT).left = 0;
    FIELD(self, MOVIE_SOURCE, RECT).top = 0;
    FIELD(self, MOVIE_SOURCE, RECT).right = FIELD(stream, 0x30, LONG);
    FIELD(self, MOVIE_SOURCE, RECT).bottom = FIELD(stream, 0x34, LONG);
    if (play)
        stream_play(stream);
    return TRUE;
#endif
}

/* Vtable slot 10: tick, as a control does. */
PORT(0x0047BED0)
int __fastcall movie_tick(BYTE* self, void* edx_unused, DWORD time)
{
    (void) edx_unused;
#ifdef OPENMCM2_DEV
    video_tick(self);
#endif
    return control_tick(self, NULL, time);
}

/*
    Vtable slot 40: render the movie's frame into its picture box, straight to the back buffer
    (the screen's 0x48). Over a background the background's DC goes first and the control
    is drawn again next frame.
*/
PORT(0x0047BEE0)
BOOL __fastcall movie_render(BYTE* self)
{
    BYTE* background;
    void* back;
    void* frame;
    RECT area;

#ifdef OPENMCM2_DEV
    frame = video_picture(self);
    if (!frame)
        return FALSE;
#else
    if (!FIELD(self, MOVIE_STREAM, BYTE*))
        return FALSE;
    frame = FIELD(FIELD(self, MOVIE_STREAM, BYTE*), 0x2C, void*);
#endif
    background = FIELD(FIELD(self, CTL_DIALOG, BYTE*), DLG_BACKGROUND, BYTE*);
    if (background) {
        background_release_dc(background);
        control_redraw(self);
    }
    VIRTUAL(self, 0xFC, control_area)(self, NULL, &FIELD(self, CTL_RECT, RECT), &area);
    back = FIELD(object_screen(self), 0x48, void*);
    return VIRTUAL(back, 0x14, surface_blt)(back, &area, frame, &FIELD(self, MOVIE_SOURCE, RECT),
                                            BLT_WAIT, NULL) == 0;
}

/* ---- UIProgress: a bar that fills as steps are done ------------------------------------------ */

/*
    Its picture is drawn cut to the part done, and the part done is written over it as a
    percentage when PROGRESS_SHOW_TEXT is set.
*/
#define UIPROGRESS_VTABLE  ((void**) 0x00558D20)
#define PROGRESS_DONE      0x1EC   /* float: the part done, 0 to 1 */
#define PROGRESS_STEPS     0x1F0
#define PROGRESS_STEP      0x1F4
#define PROGRESS_PICTURE   0x1F8
#define PROGRESS_SHOW_TEXT 0x1FC
#define PROGRESS_OWNS      0x200   /* the picture is destroyed with it */
#define PROGRESS_FORMAT    ((const char*) 0x00572004)   /* "%3.0f%%" */

PORT(0x0047BF60)
BYTE* __fastcall progress_construct(BYTE* self, void* edx_unused, DWORD a, const RECT* area,
                                    BYTE* dialog)
{
    (void) edx_unused;
    picture_control_construct(self, NULL, a, area, dialog);
    *(void***) self = UIPROGRESS_VTABLE;
    FIELD(self, PROGRESS_DONE, float) = 0.0f;
    FIELD(self, PROGRESS_STEPS, int) = 0;
    FIELD(self, PROGRESS_STEP, int) = 0;
    FIELD(self, PROGRESS_PICTURE, BYTE*) = NULL;
    FIELD(self, PROGRESS_SHOW_TEXT, BOOL) = TRUE;
    return self;
}

PORT(0x0047BFD0)
void __fastcall progress_destruct(BYTE* self)
{
    *(void***) self = UIPROGRESS_VTABLE;
    if (FIELD(self, PROGRESS_PICTURE, BYTE*) && FIELD(self, PROGRESS_OWNS, BOOL))
        object_destroy(FIELD(self, PROGRESS_PICTURE, BYTE*));
    picture_control_destruct(self);
}

PORT(0x0047BFB0)
BYTE* __fastcall progress_delete(BYTE* self, void* edx_unused, BYTE flags)
{
    (void) edx_unused;
    progress_destruct(self);
    if (flags & 1)
        game_delete(self);
    return self;
}

/* Vtable slot 48: its picture, whatever the state. */
PORT(0x0047C040)
BYTE* __fastcall progress_picture(BYTE* self, void* edx_unused, int state)
{
    (void) edx_unused;
    (void) state;
    return FIELD(self, PROGRESS_PICTURE, BYTE*);
}

/* Its picture, and whether it is destroyed with it. */
PORT(0x0047C310)
void __fastcall progress_set_picture(BYTE* self, void* edx_unused, BYTE* picture, BOOL owned)
{
    (void) edx_unused;
    FIELD(self, PROGRESS_PICTURE, BYTE*) = picture;
    FIELD(self, PROGRESS_OWNS, BOOL) = owned;
}

/* One more step done (no more than all of them); with `refresh` the manager redraws now. */
PORT(0x0047C2B0)
void __fastcall progress_step(BYTE* self, void* edx_unused, BOOL refresh)
{
    int steps = FIELD(self, PROGRESS_STEPS, int);

    (void) edx_unused;
    if (steps) {
        int step = FIELD(self, PROGRESS_STEP, int) + 1;

        if (step >= steps)
            step = steps;
        FIELD(self, PROGRESS_STEP, int) = step;
        FIELD(self, PROGRESS_DONE, float) = (float) ((double) step / steps);
    }
    if (refresh && FIELD(self, CTL_MANAGER, gui_manager*))
        gui_manager_refresh(FIELD(self, CTL_MANAGER, gui_manager*));
}

/*
    Vtable slot 40: render. The picture cut to the part done, scaled as the dialog is, then the
    percentage in its text box (slot 64), centred.
*/
PORT(0x0047C050)
int __fastcall progress_render(BYTE* self)
{
    BYTE* picture = FIELD(self, PROGRESS_PICTURE, BYTE*);
    BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);
    int redraws = FIELD(self, 0x1BC, int);
    RECT area;

    if (picture && !x87_zero(FIELD(self, PROGRESS_DONE, float))) {
        RECT source = *CONTROL_SOURCE(self, picture);
        DWORD flags = FIELD(picture, 0x30, int) ? 0x1008000 : 0x1000000;

        VIRTUAL(self, 0xFC, control_area)(self, NULL, &FIELD(self, CTL_RECT, RECT), &area);
        source.right = (int) ((double) (source.right - source.left)
                              * FIELD(self, PROGRESS_DONE, float));
        area.right = (int) ((double) source.right * FIELD(dialog, 0x9C4, float) + area.left);
        area.bottom = (int) ((double) source.bottom * FIELD(dialog, 0x9C8, float) + area.top);
        if (FIELD(dialog, DLG_BACKGROUND, BYTE*)) {
            background_draw(FIELD(dialog, DLG_BACKGROUND, BYTE*), NULL, picture, &area, &source,
                            flags, FIELD(self, CTL_SLOT, int), FIELD(self, 0x1C0, DWORD),
                            &redraws, 0);
        } else if (!VIRTUAL(object_screen(self), 0xC, screen_draw)(object_screen(self), NULL, &area,
                                                                   picture, &source, flags)) {
            return 0;
        }
    }

    if (FIELD(self, PROGRESS_SHOW_TEXT, BOOL)) {
        char text[16];
        DWORD more;
        HDC dc;

        redraws = FIELD(self, 0x1BC, int);
        VIRTUAL(self, 0x100, control_area)(self, NULL, &FIELD(self, CTL_RECT, RECT), &area);
        game_FUN_0053a11f(text, PROGRESS_FORMAT, (double) FIELD(self, PROGRESS_DONE, float) * 100.0);
        if (dialog_begin_paint(FIELD(self, CTL_DIALOG, BYTE*), NULL, &dc, &area, &more, self)) {
            do {
                dialog_draw_text(FIELD(self, CTL_DIALOG, BYTE*), NULL, dc, &area, text,
                                 (int) text_length(text), FIELD(self, CTL_COLOUR, COLORREF),
                                 FIELD(self, 0x160, int), NULL, FIELD(self, CTL_SHADOW, BOOL),
                                 FIELD(self, CTL_SHADOW_COLOUR, COLORREF), 2, TRUE, NULL);
                dialog_end_paint(FIELD(self, CTL_DIALOG, BYTE*), NULL, dc);
            } while (more && dialog_begin_paint(FIELD(self, CTL_DIALOG, BYTE*), NULL, &dc, &area,
                                                &more, self));
        }
        FIELD(self, 0x1BC, int) = redraws;
    }
    return 1;
}

/* ---- UICtlContainer: input ------------------------------------------------------------------- */

/*
    Vtable slots 22 and 23: a key released or pressed goes to the children from the last (the
    one on top) back, until one takes it. An object's first child is at 0x10, and siblings link
    on at 0x0C and back at 0x08.
*/
static int container_pass_key(BYTE* self, int slot, key_event* event, DWORD arg)
{
    BYTE* child = FIELD(self, 0x10, BYTE*);

    if (!child)
        return 0;
    while (FIELD(child, 0x0C, BYTE*))
        child = FIELD(child, 0x0C, BYTE*);
    for (; child; child = FIELD(child, 0x08, BYTE*)) {
        if (VIRTUAL(child, slot, control_two)(child, NULL, (DWORD) event, arg))
            return 1;
    }
    return 0;
}

PORT(0x0047C380)
int __fastcall container_release(BYTE* self, void* edx_unused, key_event* event, DWORD arg)
{
    (void) edx_unused;
    return container_pass_key(self, 0x58, event, arg);
}

PORT(0x0047C330)
int __fastcall container_press(BYTE* self, void* edx_unused, key_event* event, DWORD arg)
{
    (void) edx_unused;
    return container_pass_key(self, 0x5C, event, arg);
}

/* ---- Odds and ends ----------------------------------------------------------------------------- */

/* The game's picture surfaces, by vtable offset: lock (giving the pixels) and unlock. */
typedef WORD* (__fastcall* picture_lock)(void* picture, void* edx_unused, RECT* area, DWORD a,
                                         DWORD flags);
typedef BOOL  (__fastcall* picture_unlock)(void* picture, void* edx_unused, RECT* area);

#define PIXELS_565 0x235           /* the picture's pixel format (0x20) */
#define PIXELS_555 0x22B

/*
    Whether 16-bit `picture` has colour `key` anywhere (for a colour key). A key of black, or a
    format other than 565 or 555, is never found. The rows are taken as packed.
*/
PORT(0x0047C3D0)
BOOL __cdecl picture_has_colour(BYTE* picture, COLORREF key)
{
    WORD* pixels = VIRTUAL(picture, 0x34, picture_lock)(picture, NULL, NULL, 0, 0x11);
    BOOL found = FALSE;
    int width, rows, x;
    unsigned pixel;

    if (!pixels)
        return FALSE;
    if (FIELD(picture, 0x20, int) == PIXELS_565)
        pixel = (key >> 8 & 0xF800) | (key >> 5 & 0x7E0) | (key >> 3 & 0x1F);
    else if (FIELD(picture, 0x20, int) == PIXELS_555)
        pixel = (key >> 9 & 0x7C00) | (key >> 6 & 0x3E0) | (key >> 3 & 0x1F);
    else
        pixel = 0;
    if (pixel) {
        width = FIELD(picture, 0x14, int);
        for (rows = FIELD(picture, 0x18, int); rows > 0; rows--, pixels += width) {
            for (x = 0; x < width; x++) {
                if (pixels[x] == pixel) {
                    found = TRUE;
                    break;
                }
            }
        }
    }
    if (!VIRTUAL(picture, 0x38, picture_unlock)(picture, NULL, NULL))
        return FALSE;
    return found;
}

size_t __cdecl game_FUN_0053a860(void* block);    /* _msize */

/* realloc on the game's heap: a size of 0 frees the block. */
PORT(0x0047C4B0)
void* __cdecl ui_realloc(void* block, size_t size)
{
    BYTE* grown;

    if (block && !size) {
        game_FUN_004a4450(block, GAMEUI_CPP, 0x2782);
        return NULL;
    }
    grown = game_FUN_004a4410(size, GAMEUI_CPP, 0x2787);
    if (grown && block) {
        size_t kept = game_FUN_0053a860(block);

        if ((int) size <= (int) kept)
            kept = size;
        __movsb(grown, block, kept);
        game_FUN_004a4450(block, GAMEUI_CPP, 0x278E);
    }
    return grown;
}

/*
    UIOption's vtable slot 40: render. Its picture (sized to itself when 0xA4 is set), then the
    option's text as a control's is drawn; sized to its picture, the text is the control's own,
    drawn to the right of the option's picture within the padding.
*/
PORT(0x00479500)
int __fastcall option_render(BYTE* self)
{
    BYTE* dialog = FIELD(self, CTL_DIALOG, BYTE*);
    BYTE* picture = FIELD(self, 0x1CC, BYTE*);
    int redraws = FIELD(self, 0x1BC, int);
    int kept = -1;
    ui_option* option;
    DWORD more;
    RECT box;
    HDC dc;

    if (option_list(self) && picture) {
        DWORD flags = FIELD(picture, 0x30, int) ? 0x1008000 : 0x1000000;

        control_picture_area(self, NULL, &FIELD(self, CTL_RECT, RECT), &box);
        if (FIELD(dialog, DLG_BACKGROUND, BYTE*)) {
            background_draw(FIELD(dialog, DLG_BACKGROUND, BYTE*), NULL, picture, &box, NULL, flags,
                            FIELD(self, CTL_SLOT, int), FIELD(self, 0x1C0, DWORD),
                            &FIELD(self, 0x1BC, int), 0);
            kept = FIELD(self, 0x1BC, int);
        } else if (!VIRTUAL(object_screen(self), 0xC, screen_draw)(object_screen(self), NULL, &box,
                                                                   picture, NULL, flags)) {
            return 0;
        }
    }

    option = option_list(self) ? option_current(self) : NULL;
    if (!(option && option->text && option->length)
        && !(FIELD(self, 0xA4, int) && FIELD(self, CTL_TEXT, char*) && FIELD(self, CTL_LENGTH, int)))
        return 1;
    FIELD(self, 0x1BC, int) = redraws;
    do {
        if (dialog_begin_paint(FIELD(self, CTL_DIALOG, BYTE*), NULL, &dc, &FIELD(self, CTL_RECT, RECT),
                               &more, self)) {
            if (FIELD(self, 0xA4, int)) {
                box = FIELD(self, CTL_RECT, RECT);
                box.left += anim_current(option_current(self)->anim)->width + FIELD(self, 0xE8, LONG);
                box.right -= FIELD(self, 0xE8, LONG);
                box.top += FIELD(self, 0xEC, LONG);
                box.bottom -= FIELD(self, 0xEC, LONG);
                dialog_draw_text(FIELD(self, CTL_DIALOG, BYTE*), NULL, dc, &box,
                                 FIELD(self, CTL_TEXT, char*), FIELD(self, CTL_LENGTH, int),
                                 FIELD(self, CTL_COLOUR, COLORREF), FIELD(self, 0x160, int),
                                 FIELD(self, CTL_RUNS, text_run*), FIELD(self, CTL_SHADOW, BOOL),
                                 FIELD(self, CTL_SHADOW_COLOUR, COLORREF), 0x11, TRUE, NULL);
            } else {
                dialog_draw_control_text(FIELD(self, CTL_DIALOG, BYTE*), NULL, dc, self, TRUE);
            }
            dialog_end_paint(FIELD(self, CTL_DIALOG, BYTE*), NULL, dc);
        }
    } while (more);
    if (kept >= 0)
        FIELD(self, 0x1BC, int) = kept;
    return 1;
}

/* ---- UIDialog: reading a dialog file ---------------------------------------------------------- */

/*
    A dialog file is in Parameter.cpp's INI-style format: a section per anim, sound and control,
    grouped under Set_Anim, Set_Sound and Set_Control headings, with Set_Info describing the
    dialog and Set_Default sections giving the keys' defaults for the controls after them. The
    sections are read in four passes: the dialog, its anims, its sounds, then its controls.
*/

/* A strtok that keeps its place in the object. */
typedef struct text_tokens {
    char* token;
    char* rest;
} text_tokens;

void  __fastcall game_FUN_0051b440(text_tokens* self, void* edx_unused, char* text);
char* __fastcall game_FUN_0051b470(text_tokens* self, void* edx_unused, const char* delimiters);
#define tokens_begin game_FUN_0051b440
#define tokens_next  game_FUN_0051b470

int   __cdecl game_FUN_0054cd80(const char* a, const char* b, size_t count);  /* _strnicmp */
int   __cdecl game_FUN_0053a97b(const char* text, const char* format, ...);  /* sscanf */
void* __fastcall game_FUN_004bb350(void* self, void* edx_unused, const BYTE* data);  /* Palette8 */

/* The file's words. */
#define KEY(address) ((const char*) (address))
#define SECTION_END     KEY(0x00571D24)     /* "?" */
#define FILE_SEPARATOR  KEY(0x00570574)     /* "," */
#define NUMBERED_KEY    KEY(0x00571C64)     /* "%s%d" */
#define TIP_FAILED      KEY(0x0057180C)     /* "Resource string '%d' load fail\n" */
#define BACKDROP_NAME   KEY(0x00571BE8)     /* "FRMBIStatic" */
#define XY_FORMAT       KEY(0x00571BA0)     /* "%d%*[,.] %d" */
#define XYXY_FORMAT     KEY(0x00571B78)     /* "%d%*[,.] %d%*[,.] %d%*[,.] %d" */
#define WORD_TOP        KEY(0x00571CA4)
#define WORD_BOTTOM     KEY(0x00571C9C)
#define WORD_LEFT       KEY(0x00571C78)
#define WORD_RIGHT      KEY(0x00571C80)
#define WORD_CENTER     KEY(0x00571C94)
#define WORD_MIDDLE     KEY(0x00571CB4)
#define WORD_STATE      KEY(0x00571ADC)     /* "State" */
#define WORD_STATE_TEXT KEY(0x00571AD0)     /* "StateText" */
#define WORD_ITEM       KEY(0x00571A60)     /* "Item" */
#define WORD_TEXT_COLOR KEY(0x00571C6C)     /* "TextColor" */

/* The entry table: each section's name, what it made, and what it is. */
#define ENTRY_SIZE      0x3C
#define ENTRY_VALUE     0x34
#define ENTRY_KIND      0x38
#define DLG_ANIM_LIMIT  500
#define DLG_SOUNDS      0x960       /* void*[DLG_SOUND_LIMIT] */
#define DLG_SOUND_COUNT 0x9B4
#define DLG_SOUND_LIMIT 20
#define DLG_SCALED      0x158       /* scaled from its design size (Set_Info's NoScale off) */
#define DLG_POPUP       0x15C       /* placed on the screen by DLG_ALIGN */
#define DLG_AREA        0x160       /* RECT */
#define DLG_ALIGN       0x170       /* 1 left, 2 centre, 4 right; 8 top, 0x10 middle, 0x20 bottom */
#define DLG_DESIGN_W    0x174       /* the screen size it was designed for */
#define DLG_DESIGN_H    0x178
#define CTL_TOOLTIP     0xF0        /* char*, on the game's heap */
#define CTL_FONT_HEIGHT 0x160

enum { KIND_INFO, KIND_ANIM, KIND_SOUND, KIND_DEFAULT, KIND_CONTROL };

#define STATE_LIMIT 50              /* the MULTISTATE states and list items that have defaults */

#define WEIGHT(bold) ((bold) ? FW_BOLD : FW_MEDIUM)

/*
    A control's keys, as Set_Default leaves them for the controls after it or as one control
    reads them (falling back on the defaults).
*/
typedef struct control_keys {
    int  group;                     /* GroupId */
    char type[0x80];
    char font_name[0x80];
    int  font_height;
    char anchor[0x80];
    int  rel_anchor;
    int  moveable;
    char text_align[0x80];
    char text_align_v[0x80];
    int  fx_delay;
    char fx[0x80];
    char fx_sound[0x80];
    char fx_anim_in[0x80];
    char fx_sound_in[0x80];
    char fx_anim_out[0x80];
    char fx_sound_out[0x80];
    char mouse_anim[0x80];
    char sound_norm[0x80];
    char sound_focus[0x80];
    char sound_push[0x80];
    char sound_click[0x80];
    char text[0x80];
    int  attach_id;
    int  colour;
    int  drop_colour;
    int  key_bind;
    int  italic;
    int  bold;
    int  text_drop;
    int  source_blit;
    int  enable;
    int  show;
    int  pre_3d;
    int  permanent;
    int  shape_bounds;
} control_keys;

/* Defaults of the per-type keys: a control reads these into the shared text, or as numbers. */
typedef struct type_defaults {
    char anim_norm[0x80];
    char anim_focus[0x80];
    char anim_push[0x80];
    char anim_disable[0x80];
    char sort_list[0x80];
    char anim_off[0x80];
    char anim_on[0x80];
    char anim_thumb[0x80];
    char anim_text_back[0x80];
    char anim_list_back[0x80];
    char anim_scroll_back[0x80];
    char sound_end[0x80];
    char states[STATE_LIMIT][0x80];
    char state_texts[STATE_LIMIT][0x80];
    char items[STATE_LIMIT][0x80];
    int  num_states;
    int  value;                     /* Default */
    int  auto_sort;
    int  max_items;                 /* kept, but MaxItems falls back on 100 */
    int  select_colour;
    int  selectable;
    int  allow_scroll;
    int  select_box_colour;
    int  item_box_colour;
    int  tick_num;
    int  scale;
    int  max_length;
    int  list_height;
} type_defaults;

static const COLORREF default_text_colours[10] = {
    0x000000, 0x008800, 0x0066FF, 0xFF6600, 0x00FFFF,
    0xFFFFFF, 0xBBBBBB, 0xBB55CC, 0x880000, 0x999999,
};

/* The reader's state across the sections. */
typedef struct dialog_reader {
    BYTE*         dialog;
    param_file    file;
    int           video_mode;
    BOOL          is_default;       /* reading Set_Default rather than a control */
    RECT          area;             /* Xy, Xywh or Xyxy */
    control_keys  defaults;
    control_keys  keys;
    type_defaults more;
    char          background_image[0x80];
    char          tooltip[0x80];
    char          state[0x80];      /* a MULTISTATE state's anim and text */
    char          state_text[0x80];
    char          text[0x2800];     /* the value just read */
} dialog_reader;

static BOOL reader_string(dialog_reader* r, const char* key, const char* fallback, char* value)
{
    return game_FUN_004bc610(&r->file, NULL, key, fallback, value, -1);
}

static BOOL reader_int(dialog_reader* r, const char* key, int fallback, int* value)
{
    return game_FUN_004bc660(&r->file, NULL, key, fallback, value);
}

#ifdef OPENMCM2_DEV
/* A dev key's "x, y, w, h" as a rectangle; FALSE, and `rect` as it was, when there is none. */
static BOOL reader_rect(dialog_reader* r, const char* key, RECT* rect)
{
    if (!reader_string(r, key, NO_TEXT, r->text) || !r->text[0])
        return FALSE;
    game_FUN_0053a97b(r->text, XYXY_FORMAT, &rect->left, &rect->top, &rect->right, &rect->bottom);
    rect->right += rect->left;
    rect->bottom += rect->top;
    return TRUE;
}

/* Where a modern dialog's View is on the screen; FALSE when it gives none. */
static BOOL dialog_view(BYTE* dialog, RECT* view)
{
    const RECT* area = &FIELD(dialog, DLG_AREA, RECT);
    float scale = FIELD(dialog, 0x9C4, float);
    int i;

    for (i = 0; i < 64; i++)
        if (dialog_arts[i].dialog == dialog)
            break;
    if (i == 64 || dialog_arts[i].view.right <= dialog_arts[i].view.left)
        return FALSE;
    view->left = area->left + (int) (dialog_arts[i].view.left * scale);
    view->top = area->top + (int) (dialog_arts[i].view.top * scale);
    view->right = area->left + (int) (dialog_arts[i].view.right * scale);
    view->bottom = area->top + (int) (dialog_arts[i].view.bottom * scale);
    return TRUE;
}
#endif

/* String `key`: into its default in Set_Default, else into `value` (missing, the default). */
static BOOL keys_string(dialog_reader* r, const char* key, char* fallback, char* value)
{
    return reader_string(r, key, fallback, r->is_default ? fallback : value);
}

static BOOL keys_int(dialog_reader* r, const char* key, int* fallback, int* value)
{
    return reader_int(r, key, *fallback, r->is_default ? fallback : value);
}

/* Key "<name><n>", whose default (and Set_Default's place) is defaults[n] below STATE_LIMIT. */
static BOOL keys_numbered(dialog_reader* r, const char* name, int n, char defaults[][0x80],
                          char* value)
{
    char key[0x80];

    game_FUN_0053a11f(key, NUMBERED_KEY, name, n);
    return reader_string(r, key, n < STATE_LIMIT ? defaults[n] : NO_TEXT,
                         r->is_default ? defaults[n] : value);
}

/* The anim, sound or control a name stands for, or 0. */
static DWORD reader_find(dialog_reader* r, const char* name)
{
    return (DWORD) dialog_find(r->dialog, NULL, name);
}

static BOOL is_type(dialog_reader* r, const char* type)
{
    return !game_FUN_0053b6e0(r->keys.type, type);
}

/* Picture key `key` for the normal, over and pressed states of `control`, and state `last`. */
static void reader_pictures(dialog_reader* r, BYTE* control, const char* key, char* fallback,
                            int last)
{
    keys_string(r, key, fallback, r->text);
    if (r->is_default)
        return;
    control_set_picture(control, NULL, 0, reader_find(r, r->text));
    control_set_picture(control, NULL, 1, reader_find(r, r->text));
    control_set_picture(control, NULL, 2, reader_find(r, r->text));
    control_set_picture(control, NULL, last, reader_find(r, r->text));
}

/* Items "Item1" on, while there are any, each to `add` (Set_Default keeps the first 50). */
static void reader_items(dialog_reader* r, BYTE* list, BOOL anims)
{
    int n = 1;

    if (!keys_numbered(r, WORD_ITEM, n, r->more.items, r->text))
        return;
    do {
        if (!r->is_default) {
            if (anims && r->text[0] == '@')
                list_add_anim(list, NULL, (ui_anim*) reader_find(r, r->text + 1), 0, 0);
            else
                list_add_text(list, NULL, r->text, 0, 0);
        }
        n++;
        if (r->is_default && n >= STATE_LIMIT)
            return;
    } while (keys_numbered(r, WORD_ITEM, n, r->more.items, r->text));
}

/* ---- Set_Info, anims and sounds ---- */

/* Set_Info: the dialog's design size and scale, placing, colours, cursor, backdrop and font. */
static void reader_info(dialog_reader* r)
{
    BYTE* self = r->dialog;
    BYTE* screen = FIELD(game_main, 0x10, BYTE*);
    gui_manager* manager = FIELD(self, DLG_MANAGER, gui_manager*);
    char face[0x80];
    float size;
    int height, bold, italic, i;
#ifdef OPENMCM2_DEV
    int art = 1;
    RECT place;
#endif

    reader_int(r, KEY(0x00571CDC), FIELD(screen, 0xC, int), &FIELD(self, DLG_DESIGN_W, int));
    reader_int(r, KEY(0x00571CCC), FIELD(screen, 0x10, int), &FIELD(self, DLG_DESIGN_H, int));
#ifdef OPENMCM2_DEV
    /* A dialog that names no size was made for the 640x480 the original always ran at. */
    if (dev_settings.menus_widescreen) {
        reader_int(r, KEY(0x00571CDC), 640, &FIELD(self, DLG_DESIGN_W, int));
        reader_int(r, KEY(0x00571CCC), 480, &FIELD(self, DLG_DESIGN_H, int));
    }
#endif
    reader_int(r, KEY(0x00571CC4), 0, &FIELD(self, DLG_SCALED, int));       /* NoScale */
    FIELD(self, DLG_SCALED, int) = !FIELD(self, DLG_SCALED, int);
    reader_int(r, KEY(0x00571CBC), 1, &FIELD(self, DLG_POPUP, int));        /* Popup */
    if (FIELD(self, DLG_SCALED, int)) {
        FIELD(self, 0x9C4, float) = (float) ((double) FIELD(object_screen(self), 0xC, int)
                                             / FIELD(self, DLG_DESIGN_W, int));
        FIELD(self, 0x9C8, float) = (float) ((double) FIELD(object_screen(self), 0x10, int)
                                             / FIELD(self, DLG_DESIGN_H, int));
#ifdef OPENMCM2_DEV
        /* Evenly, by the smaller, so nothing stretches. */
        if (dev_settings.menus_widescreen) {
            if (FIELD(self, 0x9C4, float) > FIELD(self, 0x9C8, float))
                FIELD(self, 0x9C4, float) = FIELD(self, 0x9C8, float);
            else
                FIELD(self, 0x9C8, float) = FIELD(self, 0x9C4, float);
        }
#endif
    } else if (FIELD(self, DLG_DESIGN_W, int) * 2 > FIELD(object_screen(self), 0xC, int)
               || FIELD(self, DLG_DESIGN_H, int) * 2 > FIELD(object_screen(self), 0x10, int)) {
        FIELD(self, 0x9C8, float) = 1.0f;
        FIELD(self, 0x9C4, float) = 1.0f;
    }
#ifdef OPENMCM2_DEV
    /* NoScale is 1:1 on the 640x480 the menus were made for: in widescreen, the menus' scale. */
    if (dev_settings.menus_widescreen && !FIELD(self, DLG_SCALED, int)) {
        int left, top;
        float scale = menu_fit(object_screen(self), &left, &top);

        FIELD(self, 0x9C4, float) = scale;
        FIELD(self, 0x9C8, float) = scale;
    }

    /*
        A modern screen's layout and pictures are made at ArtScale times the original's size, so
        they are drawn at the menus' scale divided by it: 1:1 when the menus are scaled by that.
    */
    if (dev_settings.menus_widescreen) {
        int photos = 0, reborn = 0;
        RECT view = { 0 };

        reader_int(r, "ArtScale", 1, &art);
        reader_rect(r, "View", &view);
        reader_int(r, "Reborn", 0, &reborn);
        dialog_art_set(self, art, &view, reborn);
        reader_int(r, "Photos", 0, &photos);
        if (photos) {
            photos_add(self);
        }
        if (art > 1) {
            int left, top;
            float scale = menu_fit(object_screen(self), &left, &top) / art;

            FIELD(self, 0x9C4, float) = scale;
            FIELD(self, 0x9C8, float) = scale;
        }
    }

    /*
        A screen made wider than 4:3 (the modern menus' 1920x1080) is its own area: centred when
        it is the whole screen, and where it was placed when it is drawn in another, whose window
        for it is there (moved, it would leave part of that window that nothing draws).
    */
    if (dev_settings.menus_widescreen && (FIELD(self, DLG_SCALED, int) || art > 1)
        && !FIELD(self, DLG_POPUP, int)
        && FIELD(self, DLG_DESIGN_W, int) * 3 > FIELD(self, DLG_DESIGN_H, int) * 4) {
        RECT* area = &FIELD(self, DLG_AREA, RECT);
        int screen_w = FIELD(object_screen(self), 0xC, int);
        int screen_h = FIELD(object_screen(self), 0x10, int);
        int across = (int) (FIELD(self, DLG_DESIGN_W, int) * FIELD(self, 0x9C4, float));
        int down = (int) (FIELD(self, DLG_DESIGN_H, int) * FIELD(self, 0x9C8, float));

        if (across >= screen_w || down >= screen_h) {
            area->left = (screen_w - across) / 2;
            area->top = (screen_h - down) / 2;
        }
        area->right = area->left + across;
        area->bottom = area->top + down;
    }

    /*
        Place: where a modern page goes on the screen it opens over, in that screen's layout
        pixels, in place of the 640x480 window the game opens it in.
    */
    if (dev_settings.menus_widescreen && FIELD(self, DLG_PARENT, BYTE*) && reader_rect(r, "Place", &place)) {
        BYTE* parent = FIELD(self, DLG_PARENT, BYTE*);
        RECT* area = &FIELD(self, DLG_AREA, RECT);
        float scale = FIELD(parent, 0x9C4, float);

        area->left = FIELD(parent, DLG_AREA, RECT).left + (int) (place.left * scale);
        area->top = FIELD(parent, DLG_AREA, RECT).top + (int) (place.top * scale);
        area->right = FIELD(parent, DLG_AREA, RECT).left + (int) (place.right * scale);
        area->bottom = FIELD(parent, DLG_AREA, RECT).top + (int) (place.bottom * scale);
    }

    if (dev_settings.trace_display)
        dev_trace("dialog", "%s: screen %dx%d, made for %dx%d, scaled %d by %d/1000 x %d/1000, "
                  "area %d,%d-%d,%d", (char*) self + DLG_NAME, FIELD(object_screen(self), 0xC, int),
                  FIELD(object_screen(self), 0x10, int), FIELD(self, DLG_DESIGN_W, int),
                  FIELD(self, DLG_DESIGN_H, int), FIELD(self, DLG_SCALED, int),
                  (int) (FIELD(self, 0x9C4, float) * 1000), (int) (FIELD(self, 0x9C8, float) * 1000),
                  FIELD(self, DLG_AREA, RECT).left, FIELD(self, DLG_AREA, RECT).top,
                  FIELD(self, DLG_AREA, RECT).right, FIELD(self, DLG_AREA, RECT).bottom);
#endif

    if (FIELD(self, DLG_POPUP, int)) {
        DWORD align;

        reader_string(r, KEY(0x00571CA8), WORD_MIDDLE, r->text);            /* DlgAlignV */
        if (!game_FUN_0053b6e0(r->text, WORD_TOP))
            align = 8;
        else
            align = game_FUN_0053b6e0(r->text, WORD_BOTTOM) ? 0x10 : 0x20;
        reader_string(r, KEY(0x00571C88), WORD_CENTER, r->text);            /* DlgAlignH */
        if (!game_FUN_0053b6e0(r->text, WORD_RIGHT))
            dialog_set_170(self, NULL, align | 4);
        else if (!game_FUN_0053b6e0(r->text, WORD_LEFT))
            dialog_set_170(self, NULL, align | 1);
        else
            dialog_set_170(self, NULL, align | 2);
    } else {
        FIELD(self, DLG_ALIGN, DWORD) = 9;
    }

    for (i = 0; i < 10; i++) {
        char key[0x80];
        int colour;

        game_FUN_0053a11f(key, NUMBERED_KEY, WORD_TEXT_COLOR, i);
        reader_int(r, key, (int) default_text_colours[i], &colour);
        FIELD(self, DLG_COLOURS + 4 * i, COLORREF) = colour;
    }

    reader_string(r, KEY(0x00571C54), NO_TEXT, r->text);                    /* MouseCursorAnim */
    if (r->text[0])
        FIELD(self, 0x95C, int) = dialog_find(self, NULL, r->text);

    reader_string(r, KEY(0x00571C44), NO_TEXT, r->text);                    /* BackgroundFile */
    if (r->text[0] && FIELD(self, DLG_BACKGROUND, BYTE*)) {
        BYTE* image = game_FUN_00517ca0(r->text, 0, game_resources);

        if (image) {
            BYTE* palette;
            void* picture;

            FIELD(self, 0x7F10, DWORD) = 1;
            if (FIELD(self, DLG_PARENT, BYTE*))
                FIELD(self, 0x9B8, DWORD) = FIELD(FIELD(self, DLG_BACKGROUND, BYTE*), 0x2C, DWORD);
            picture = game_new(0x80, GAMEUI_CPP, 0x367);
            if (picture)
                picture = game_FUN_004ca650(picture, NULL, FIELD(self, 0x7F20, int), 1);
            FIELD(FIELD(self, DLG_BACKGROUND, BYTE*), 0x2C, void*) = picture;
            palette = FIELD(self, DLG_BACKDROP, BYTE*);
            picture = FIELD(FIELD(self, DLG_BACKGROUND, BYTE*), 0x2C, void*);
            VIRTUAL(picture, 0x10, picture_fill)(picture, NULL, image_pixels(image),
                                                 image_width(image), image_height(image),
                                                 image_width(image), image_width(image), 0x22B,
                                                 r->video_mode,
                                                 palette ? FIELD(palette, 0x708, DWORD) : 0, 4,
                                                 palette ? FIELD(palette, 0x70C, DWORD) : 0, 0, 0,
                                                 2, 1, 0, 0x80, 0xFF00FF);
            background_invalidate(FIELD(self, DLG_BACKGROUND, BYTE*));
            game_FUN_005184b0(image);
        }
    } else if (FIELD(self, DLG_PARENT, BYTE*)) {
        FIELD(self, DLG_BACKGROUND, BYTE*) = FIELD(FIELD(self, DLG_PARENT, BYTE*), DLG_BACKGROUND,
                                                   BYTE*);
    }

    /* The font: PointSize, or FontHeight over it, rounded up; the manager may override it. */
    reader_string(r, KEY(0x00571C38), NO_TEXT, face);                       /* FontName */
    game_FUN_004bc690(&r->file, NULL, KEY(0x00571C2C), 0.0f, &size);        /* PointSize */
    height = (int) size;
    if (x87_less(height, size))
        height++;
    game_FUN_004bc690(&r->file, NULL, KEY(0x00571C20), 0.0f, &size);        /* FontHeight */
    if (size > 0.0f) {
        height = (int) size;
        if (x87_less(height, size))
            height++;
    }
    if (face[0] && height) {
        if (FIELD(self, DLG_FONT, HFONT))
            DeleteObject(FIELD(self, DLG_FONT, HFONT));
        FIELD(self, DLG_SCREEN_SIZE, int) = (int) ((double) height * FIELD(self, 0x9C8, float));
        text_copy((char*) self + DLG_FACE, face);
        reader_int(r, KEY(0x00571C18), 0, &bold);                           /* Bold */
        reader_int(r, KEY(0x00571C10), 0, &italic);                         /* Italic */
        if ((manager ? manager->font_name : NO_TEXT)[0]) {
            text_copy((char*) self + DLG_FACE, manager->font_name);
            bold = manager->font_bold;
            italic = manager->font_italic;
        }
        height = (int) ((double) (manager ? manager->font_grow : 0.0f)
                        + FIELD(self, DLG_SCREEN_SIZE, int));
        FIELD(self, DLG_SCREEN_SIZE, int) = height;
        FIELD(self, DLG_FONT, HFONT) = CreateFontA(height, 0, 0, 0, WEIGHT(bold), italic, 0, 0,
                                                   DEFAULT_CHARSET, 0, 0, PROOF_QUALITY,
                                                   VARIABLE_PITCH, (char*) self + DLG_FACE);
        FIELD(self, 0x10C, int) = italic;
        FIELD(self, 0x108, int) = bold;
    }

    /* In 8-bit modes, a palette of its own (PaletteFile) unless the manager has one. */
    if (FIELD(FIELD(game_main, 0x10, BYTE*), 0x28, int) == 8) {
        BOOL own_palette = TRUE;

        if (manager) {
            if (gui_manager_unknown_1f8(manager))
                own_palette = FALSE;
            FIELD(self, DLG_BACKDROP, void*) = FIELD(manager, 0x34, void*);
        }
        reader_string(r, KEY(0x00571C04), NO_TEXT, r->text);                /* PaletteFile */
        if (r->text[0] && own_palette) {
            BYTE* entry = game_FUN_004ee0f0(game_resources, NULL, r->text, 1);

            if (entry) {
                void* palette = game_new(0x718, GAMEUI_CPP, 0x3B6);

                if (palette)
                    palette = game_FUN_004bb350(palette, NULL, FIELD(entry, 0x14, BYTE*));
                FIELD(self, DLG_BACKDROP, void*) = palette;
                FIELD(self, 0x7F0C, DWORD) = 1;
            }
        }
        if (manager && own_palette)
            gui_manager_set_busy(manager, NULL, FIELD(self, DLG_BACKDROP, int));
    }

    /* BackgroundImage: a picture control behind the others, fitted to it once all is read. */
    reader_string(r, KEY(0x00571BF4), NO_TEXT, r->background_image);
    if (r->background_image[0]) {
        RECT area;
        BYTE* backdrop;

        area.left = 0;
        area.top = 0;
        area.right = FIELD(self, DLG_AREA, RECT).right - FIELD(self, DLG_AREA, RECT).left;
        area.bottom = FIELD(self, DLG_AREA, RECT).bottom - FIELD(self, DLG_AREA, RECT).top;
        backdrop = game_new(0x1EC, GAMEUI_CPP, 0x3CD);
        if (backdrop)
            backdrop = picture_control_construct(backdrop, NULL, 0, &area, self);
        control_set_name(backdrop, NULL, BACKDROP_NAME);
        FIELD(backdrop, 0x1E0, DWORD) = 1;
        object_add_child(FIELD(self, DLG_CONTAINER, void*), NULL, backdrop, -1);
        dialog_attach(self, NULL, backdrop, 0, 0);
    }
}

/* An anim: MSecDelay, LoopCount, and its frames (Files1), as resources or files. */
static void reader_anim(dialog_reader* r, BYTE* entry)
{
    BYTE* self = r->dialog;
    const char* name = (const char*) entry;
    text_tokens files;
    ui_anim** anim;
    int delay, repeats;
    char* file;

    if (FIELD(self, DLG_CONTROL_COUNT, int) >= DLG_ANIM_LIMIT)
        return;
    anim = &FIELD(self, DLG_CONTROLS + 4 * FIELD(self, DLG_CONTROL_COUNT, int), ui_anim*);
    *anim = game_new(sizeof(ui_anim), GAMEUI_CPP, 0x3DF);
    if (*anim)
        *anim = anim_construct(*anim, NULL, FIELD(self, 0x7F20, DWORD), 1);
    reader_int(r, KEY(0x00571BDC), 0, &delay);                              /* MSecDelay */
    reader_int(r, KEY(0x00571BD0), 0, &repeats);                            /* LoopCount */
    anim_set_delay(*anim, NULL, delay);
    anim_set_repeats(*anim, NULL, repeats);
    reader_string(r, KEY(0x00571BC8), NO_TEXT, r->text);                    /* Files1 */
    tokens_begin(&files, NULL, r->text);
    for (file = tokens_next(&files, NULL, FILE_SEPARATOR); file;
         file = tokens_next(&files, NULL, FILE_SEPARATOR)) {
        if (game_FUN_004ee0f0(game_resources, NULL, name, 1))
            anim_add_resource(*anim, NULL, name, game_resources, FIELD(self, 0x114, DWORD),
                              FIELD(self, DLG_BACKDROP, BYTE*));
        else if (game_FUN_004ee0f0(game_resources, NULL, file, 1))
            anim_add_resource(*anim, NULL, file, game_resources, FIELD(self, 0x114, DWORD),
                              FIELD(self, DLG_BACKDROP, BYTE*));
        else
            anim_add_file(*anim, NULL, file, FIELD(self, DLG_BACKDROP, BYTE*));
    }
    FIELD(entry, ENTRY_VALUE, ui_anim*) = *anim;
    FIELD(self, DLG_CONTROL_COUNT, int)++;
}

/* A sound: File (or the section's own resource), Static, Copies. */
static void reader_sound(dialog_reader* r, BYTE* entry)
{
    BYTE* self = r->dialog;
    const char* name = (const char*) entry;
    void** sound;
    int is_static, copies, mode;

    if (FIELD(self, DLG_SOUND_COUNT, int) >= DLG_SOUND_LIMIT)
        return;
    reader_int(r, KEY(0x00571BC0), 1, &is_static);                          /* Static */
    reader_int(r, KEY(0x00571BB8), 0, &copies);                             /* Copies */
    reader_string(r, KEY(0x00571BB0), NO_TEXT, r->text);                    /* File */
    mode = is_static ? 1 : 2;
    sound = &FIELD(self, DLG_SOUNDS + 4 * FIELD(self, DLG_SOUND_COUNT, int), void*);
    if (game_FUN_004ee0f0(game_resources, NULL, name, 1)) {
        *sound = game_FUN_004bffe0(FIELD(self, 0x114, DWORD), name, mode, 3, copies, -1);
    } else if (game_FUN_004ee0f0(game_resources, NULL, r->text, 1)) {
        *sound = game_FUN_004bffe0(FIELD(self, 0x114, DWORD), r->text, mode, 3, copies, -1);
    } else {
        *sound = game_new(0x1F8, GAMEUI_CPP, 0x41E);
        if (*sound)
            *sound = game_FUN_004c0160(*sound, NULL, FIELD(self, 0x114, DWORD), 1);
        game_FUN_004c0a70(*sound, NULL, r->text, 0, mode, 3, copies, -1);
    }
    FIELD(entry, ENTRY_VALUE, void*) = *sound;
    FIELD(self, DLG_SOUND_COUNT, int)++;
}

/* ---- Controls ---- */

/*
    Each type's keys. Reading Set_Default every type's keys are read, into their defaults;
    reading a control only its own type's, once the control is made. Each gives the control.
*/
static BYTE* reader_button(dialog_reader* r)
{
    BYTE* self = r->dialog;
    BYTE* button = NULL;

    if (!r->is_default) {
        BYTE* made;

        if (is_type(r, KEY(0x00571B58))) {                                  /* BUTTON */
            made = game_new(0x1F0, GAMEUI_CPP, 0x45B);
            if (made)
                made = button_construct(made, NULL, 0, &r->area, self);
        } else if (is_type(r, KEY(0x00571B40))) {                           /* SCROLLDOWN */
            made = game_new(0x1F0, GAMEUI_CPP, 0x45C);
            if (made)
                made = kind_button_construct(made, NULL, 10, 0, &r->area, self);
        } else {                                                            /* SCROLLUP */
            made = game_new(0x1F0, GAMEUI_CPP, 0x45D);
            if (made)
                made = kind_button_construct(made, NULL, 9, 0, &r->area, self);
        }
        button = dialog_add(self, NULL, made, r->keys.group, 0);
    }
    keys_string(r, KEY(0x00571B34), r->more.anim_norm, r->text);            /* AnimNorm */
    if (!r->is_default)
        button_set_normal(button, NULL, reader_find(r, r->text));
    keys_string(r, KEY(0x00571B28), r->more.anim_focus, r->text);           /* AnimFocus */
    if (!r->is_default)
        button_set_over(button, NULL, reader_find(r, r->text));
    keys_string(r, KEY(0x00571B1C), r->more.anim_push, r->text);            /* AnimPush */
    if (!r->is_default)
        button_set_pressed(button, NULL, reader_find(r, r->text));
    keys_string(r, KEY(0x00571B10), r->more.anim_disable, r->text);         /* AnimDisable */
    if (!r->is_default)
        button_set_disabled(button, NULL, reader_find(r, r->text));
    keys_string(r, KEY(0x00571B04), r->more.sort_list, r->text);            /* SortLBName */
    if (!r->is_default)
        button_set_sort_list(button, NULL, (BYTE*) reader_find(r, r->text));
    return button;
}

/* MULTISTATE: NumStates, Default, and State<n>/StateText<n> from 0 while either is there. */
static BYTE* reader_multistate(dialog_reader* r)
{
    BYTE* option = NULL;
    int count, value, n = 0;

    if (!r->is_default) {
        option = game_new(0x1F8, GAMEUI_CPP, 0x474);
        if (option)
            option = option_construct(option, NULL, 0, &r->area, r->dialog);
        option = dialog_add(r->dialog, NULL, option, r->keys.group, 0);
    }
    keys_int(r, KEY(0x00571AEC), &r->more.num_states, &count);             /* NumStates */
    if (!r->is_default)
        option_set_count(option, NULL, count);
    keys_int(r, KEY(0x00571AE4), &r->more.value, &value);                  /* Default */
    if (!r->is_default)
        option_select(option, NULL, value);
    while (keys_numbered(r, WORD_STATE, n, r->more.states, r->state)
           | keys_numbered(r, WORD_STATE_TEXT, n, r->more.state_texts, r->state_text)) {
        if (!r->is_default) {
            /* The original keeps the text in a global on its way. */
            const char* text = r->state_text[0] ? r->state_text : NULL;

            *(const char**) 0x00661748 = text;
            option_set(option, NULL, n, (ui_anim*) reader_find(r, r->state), text);
        }
        n++;
        if (r->is_default && n >= STATE_LIMIT)
            break;
    }
    return option;
}

/* LISTBOX: its capacity, selection, sorting, colours, items (an '@' item is an anim), picture. */
static BYTE* reader_list(dialog_reader* r)
{
    BYTE* list = NULL;
    int value = 0;

    reader_int(r, KEY(0x00571ABC), 100, r->is_default ? &r->more.max_items : &value);  /* MaxItems */
    if (!r->is_default) {
        list = game_new(0x250, GAMEUI_CPP, 0x4A3);
        if (list)
            list = list_construct(list, NULL, 0, value, &r->area, r->dialog);
        list = dialog_add(r->dialog, NULL, list, r->keys.group, 0);
    }
    keys_int(r, KEY(0x00571AE4), &r->more.value, &value);                  /* Default */
    if (!r->is_default)
        list_select_all(list, NULL, value);
    /* AutoSort falls back on Default's default, as the original has it. */
    reader_int(r, KEY(0x00571AB0), r->more.value, r->is_default ? &r->more.auto_sort : &value);
    if (!r->is_default)
        list_set_21c(list, NULL, value);
    keys_int(r, KEY(0x00571AA4), &r->more.select_colour, &value);          /* SelectColor */
    if (!r->is_default)
        list_set_208(list, NULL, value);
    keys_int(r, KEY(0x00571A98), &r->more.selectable, &value);             /* Selectable */
    if (!r->is_default)
        list_set_224(list, NULL, value);
    keys_int(r, KEY(0x00571A88), &r->more.allow_scroll, &value);           /* AllowWScroll */
    if (!r->is_default)
        list_set_228(list, NULL, value);
    keys_int(r, KEY(0x00571A78), &r->more.select_box_colour, &value);      /* SelectBoxColor */
    if (!r->is_default)
        list_set_colour_2(list, NULL, value);
    keys_int(r, KEY(0x00571A68), &r->more.item_box_colour, &value);        /* ItemBoxColor */
    if (!r->is_default)
        list_set_colour(list, NULL, value, -1);
    reader_items(r, list, TRUE);
    reader_pictures(r, list, KEY(0x00571B34), r->more.anim_norm, 4);       /* AnimNorm */
    return list;
}

/* RADIOBUTTON: Default, and its off and on pictures. */
static BYTE* reader_radio(dialog_reader* r)
{
    BYTE* radio = NULL;
    int value;

    if (!r->is_default) {
        radio = game_new(0x1F8, GAMEUI_CPP, 0x4E3);
        if (radio)
            radio = radio_construct(radio, NULL, 0, &r->area, r->dialog);
        radio = dialog_add(r->dialog, NULL, radio, r->keys.group, 0);
    }
    keys_int(r, KEY(0x00571AE4), &r->more.value, &value);                  /* Default */
    if (!r->is_default)
        option_select(radio, NULL, value);
    keys_string(r, KEY(0x00571A4C), r->more.anim_off, r->text);            /* AnimOff */
    if (!r->is_default)
        option_set(radio, NULL, 0, (ui_anim*) reader_find(r, r->text), NULL);
    keys_string(r, KEY(0x00571A44), r->more.anim_on, r->text);             /* AnimOn */
    if (!r->is_default)
        option_set(radio, NULL, 1, (ui_anim*) reader_find(r, r->text), NULL);
    return radio;
}

/* STATIC and STATICTEXT: a picture, or a label, with one picture. */
static BYTE* reader_picture(dialog_reader* r, BOOL label, int line)
{
    BYTE* control = NULL;

    if (!r->is_default) {
        control = game_new(0x1EC, GAMEUI_CPP, line);
        if (control && label)
            control = label_construct(control, NULL, 0, &r->area, r->dialog, NULL, 0xFFFF);
        else if (control)
            control = picture_control_construct(control, NULL, 0, &r->area, r->dialog);
        control = dialog_add(r->dialog, NULL, control, r->keys.group, 0);
    }
    keys_string(r, KEY(0x00571B34), r->more.anim_norm, r->text);            /* AnimNorm */
    if (!r->is_default)
        control_set_picture(control, NULL, 0, reader_find(r, r->text));
    return control;
}

/*
    VSCROLLBAR and HSCROLLBAR: TickNum, Default, Scale, track and thumb pictures. Without a
    size the slider takes its thumb's; without ticks it steps by pixels.
*/
static BYTE* reader_scroll_bar(dialog_reader* r)
{
    BYTE* slider = NULL;
    int ticks, value, scale;

    if (!r->is_default) {
        slider = game_new(0x220, GAMEUI_CPP, 0x500);
        if (slider)
            slider = slider_construct(slider, NULL, 7, 0, &r->area, r->dialog);
        slider = dialog_add(r->dialog, NULL, slider, r->keys.group, 0);
    }
    /* Set_Default gets here with no control, where the original faults. */
    if (is_type(r, KEY(0x00571A24)) && slider)                             /* HSCROLLBAR */
        FIELD(slider, CTL_TYPE, int) = 8;
    keys_int(r, KEY(0x00571A1C), &r->more.tick_num, &ticks);               /* TickNum */
    keys_int(r, KEY(0x00571AE4), &r->more.value, &value);                  /* Default */
    keys_int(r, KEY(0x00571A14), &r->more.scale, &scale);                  /* Scale */
    if (!r->is_default) {
        slider_set_steps(slider, NULL, ticks);
        slider_set_range(slider, NULL, scale);
    }
    keys_string(r, KEY(0x00571B34), r->more.anim_norm, r->text);            /* AnimNorm */
    if (!r->is_default)
        FIELD(slider, SLIDER_TRACK, DWORD) = reader_find(r, r->text);
    reader_pictures(r, slider, KEY(0x00571A08), r->more.anim_thumb, 3);    /* AnimThumb */
    if (!r->is_default) {
        RECT* design = &FIELD(slider, CTL_DESIGN, RECT);

        if (design->right == design->left) {
            ui_anim* thumb = FIELD(slider, 0x16C, ui_anim*);

            design->right = thumb->frames[0]->width + design->left;
            design->bottom = design->top + thumb->frames[0]->height;
        }
        slider_set_value(slider, NULL, value,
                         ticks ? ticks : design->right - design->left + 1);
        slider_measure_thumb(slider, NULL, 0);
    }
    return slider;
}

/* EDITBOX: MaxLen, its key and full sounds, pictures. */
static BYTE* reader_edit(dialog_reader* r)
{
    BYTE* edit = NULL;
    int value;

    if (!r->is_default) {
        edit = game_new(0x230, GAMEUI_CPP, 0x52C);
        if (edit)
            edit = edit_construct(edit, NULL, 0, &r->area, r->dialog, 100, 0, 0);
        edit = dialog_add(r->dialog, NULL, edit, r->keys.group, 0);
    }
    keys_int(r, KEY(0x005719F8), &r->more.max_length, &value);             /* MaxLen */
    if (!r->is_default)
        edit_set_capacity(edit, NULL, value);
    keys_string(r, KEY(0x005719EC), r->defaults.sound_click, r->text);     /* SoundClick */
    if (!r->is_default)
        edit_set_type_sound(edit, NULL, (void*) reader_find(r, r->text));
    keys_string(r, KEY(0x005719E0), r->more.sound_end, r->text);           /* SoundEnd */
    if (!r->is_default)
        edit_set_full_sound(edit, NULL, (void*) reader_find(r, r->text));
    reader_pictures(r, edit, KEY(0x00571B34), r->more.anim_norm, 4);       /* AnimNorm */
    return edit;
}

/* DROPDOWNLIST: the arrow's pictures and sounds, list height and items, colours, backgrounds. */
static BYTE* reader_combo(dialog_reader* r, const char* name)
{
    char sounds[4][0x80];
    BYTE* combo = NULL;
    BYTE* list = NULL;
    int margin[2], colour, drop_colour, value, i;

    if (!r->is_default) {
        combo = game_new(0x21C, GAMEUI_CPP, 0x54F);
        if (combo)
            combo = combo_construct(combo, NULL, 0, &r->area, r->dialog, NULL, 0xFFFF);
        combo = dialog_add(r->dialog, NULL, combo, r->keys.group, 0);
    }
    keys_string(r, KEY(0x00571B34), r->more.anim_norm, r->text);            /* AnimNorm */
    if (!r->is_default)
        button_set_normal(FIELD(combo, COMBO_BUTTON, BYTE*), NULL, reader_find(r, r->text));
    keys_string(r, KEY(0x00571B28), r->more.anim_focus, r->text);           /* AnimFocus */
    if (!r->is_default)
        button_set_over(FIELD(combo, COMBO_BUTTON, BYTE*), NULL, reader_find(r, r->text));
    keys_string(r, KEY(0x00571B1C), r->more.anim_push, r->text);            /* AnimPush */
    if (!r->is_default)
        button_set_pressed(FIELD(combo, COMBO_BUTTON, BYTE*), NULL, reader_find(r, r->text));
    keys_string(r, KEY(0x00571A08), r->more.anim_thumb, r->text);           /* AnimThumb */
    if (!r->is_default) {
        BYTE* slider = FIELD(combo, COMBO_SLIDER, BYTE*);

        for (i = 0; i < 4; i++)
            control_set_picture(slider, NULL, i, reader_find(r, r->text));
        slider_measure_thumb(slider, NULL, 0);
    }
    keys_int(r, KEY(0x005719B8), &r->more.list_height, &value);            /* ListHeight */
    if (!r->is_default) {
        combo_set_drop(combo, NULL, value);
        list = FIELD(combo, COMBO_LIST, BYTE*);
    }
    reader_items(r, list, FALSE);
    /* Set_Default gets here with no control, where the original faults. */
    if (combo)
        control_set_name(combo, NULL, name);

    reader_int(r, KEY(0x005719AC), 0, &margin[0]);                          /* TextLMargin */
    reader_int(r, KEY(0x005719A0), 0, &margin[1]);                          /* TextTMargin */
    reader_int(r, WORD_TEXT_COLOR, r->defaults.colour, &colour);
    reader_int(r, KEY(0x00571994), r->defaults.drop_colour, &drop_colour);  /* DropColor */
    reader_string(r, KEY(0x00571988), r->defaults.sound_norm, sounds[0]);   /* SoundNorm */
    reader_string(r, KEY(0x0057197C), r->defaults.sound_focus, sounds[1]);  /* SoundFocus */
    reader_string(r, KEY(0x00571970), r->defaults.sound_push, sounds[2]);   /* SoundPush */
    reader_string(r, KEY(0x005719EC), r->defaults.sound_click, sounds[3]);  /* SoundClick */
    if (!r->is_default) {
        for (i = 0; i < 4; i++)
            control_set_194(FIELD(combo, COMBO_BUTTON, BYTE*), NULL, i, reader_find(r, sounds[i]));
        FIELD(combo, 0xE8, int) = margin[0];
        FIELD(combo, 0xEC, int) = margin[1];
        control_set_colour(combo, NULL, colour);
        control_set_shadow_colour(combo, NULL, drop_colour);
        combo_layout(combo);
    }
    keys_int(r, KEY(0x00571AE4), &r->more.value, &value);                  /* Default */
    if (!r->is_default) {
        int index = value - 1 < 0 ? 0 : value - 1;

        list_select_all(list, NULL, index);
        control_set_text(combo, NULL, list_text(list, NULL, index));
    }
    keys_string(r, KEY(0x00571960), r->more.anim_text_back, r->text);      /* AnimTextBack */
    if (!r->is_default && r->text[0])
        combo_set_text_back(combo, NULL, (ui_anim*) reader_find(r, r->text));
    keys_string(r, KEY(0x00571950), r->more.anim_list_back, r->text);      /* AnimListBack */
    if (!r->is_default && r->text[0])
        combo_set_list_back(combo, NULL, (ui_anim*) reader_find(r, r->text));
    keys_string(r, KEY(0x00571940), r->more.anim_scroll_back, r->text);    /* AnimScrollBack */
    if (!r->is_default && r->text[0])
        combo_set_slider_back(combo, NULL, (ui_anim*) reader_find(r, r->text));
    return combo;
}

/* The keys every control has; Set_Default reads them into the defaults. */
static void reader_common(dialog_reader* r, int margin[2])
{
    control_keys* d = &r->defaults;
    control_keys* k = &r->keys;
    float height;
    int rounded;

    keys_string(r, KEY(0x00571C38), d->font_name, k->font_name);           /* FontName */
    game_FUN_004bc690(&r->file, NULL, KEY(0x00571C20), (float) d->font_height, &height);
    rounded = (int) height;                                                 /* FontHeight, up */
    if (x87_less(rounded, height))
        rounded++;
    if (r->is_default)
        d->font_height = rounded;
    else
        k->font_height = rounded;
    keys_string(r, KEY(0x00571938), d->anchor, k->anchor);                 /* Anchor */
    keys_int(r, KEY(0x0057192C), &d->rel_anchor, &k->rel_anchor);          /* RelAnchor */
    keys_int(r, KEY(0x00571920), &d->moveable, &k->moveable);              /* Moveable */
    keys_string(r, KEY(0x00571914), d->text_align, k->text_align);         /* TextAlign */
    keys_string(r, KEY(0x00571908), d->text_align_v, k->text_align_v);     /* TextAlignV */
    reader_int(r, KEY(0x005719AC), 0, &margin[0]);                          /* TextLMargin */
    reader_int(r, KEY(0x005719A0), 0, &margin[1]);                          /* TextTMargin */
    keys_int(r, KEY(0x00571900), &d->fx_delay, &k->fx_delay);              /* FXDelay */
    keys_string(r, KEY(0x005718FC), d->fx, k->fx);                         /* FX */
    keys_string(r, KEY(0x005718F4), d->fx_sound, k->fx_sound);             /* FXSound */
    keys_string(r, KEY(0x005718E8), d->fx_anim_in, k->fx_anim_in);         /* FXAnimIn */
    keys_string(r, KEY(0x005718DC), d->fx_sound_in, k->fx_sound_in);       /* FXSoundIn */
    keys_string(r, KEY(0x005718D0), d->fx_anim_out, k->fx_anim_out);       /* FXAnimOut */
    keys_string(r, KEY(0x005718C4), d->fx_sound_out, k->fx_sound_out);     /* FXSoundOut */
    keys_string(r, KEY(0x005718B8), d->mouse_anim, k->mouse_anim);         /* MouseAnim */
    keys_string(r, KEY(0x00571988), d->sound_norm, k->sound_norm);         /* SoundNorm */
    keys_string(r, KEY(0x0057197C), d->sound_focus, k->sound_focus);       /* SoundFocus */
    keys_string(r, KEY(0x00571970), d->sound_push, k->sound_push);         /* SoundPush */
    keys_string(r, KEY(0x005719EC), d->sound_click, k->sound_click);       /* SoundClick */
    keys_string(r, KEY(0x005718B0), d->text, k->text);                     /* Text */
    keys_int(r, KEY(0x005718A4), &d->attach_id, &k->attach_id);            /* AttachId */
    keys_int(r, WORD_TEXT_COLOR, &d->colour, &k->colour);
    keys_int(r, KEY(0x00571994), &d->drop_colour, &k->drop_colour);        /* DropColor */
    keys_int(r, KEY(0x0057189C), &d->key_bind, &k->key_bind);              /* KeyBind */
    keys_int(r, KEY(0x00571C10), &d->italic, &k->italic);                  /* Italic */
    keys_int(r, KEY(0x00571C18), &d->bold, &k->bold);                      /* Bold */
    keys_int(r, KEY(0x00571890), &d->text_drop, &k->text_drop);            /* TextDrop */
    keys_int(r, KEY(0x00571884), &d->source_blit, &k->source_blit);        /* SourceBlit */
    keys_int(r, KEY(0x0057187C), &d->enable, &k->enable);                  /* Enable */
    keys_int(r, KEY(0x00571874), &d->show, &k->show);                      /* Show */
    keys_int(r, KEY(0x0057186C), &d->pre_3d, &k->pre_3d);                  /* Pre3D */
    keys_int(r, KEY(0x00571860), &d->permanent, &k->permanent);            /* Permanent */
    keys_int(r, KEY(0x00571854), &d->shape_bounds, &k->shape_bounds);      /* ShapeBounds */
    reader_string(r, KEY(0x00571848), NO_TEXT, r->tooltip);                 /* ToolTipText */
}

/* Which way a control's text goes when TextAlign is not RIGHT or CENTER: by type (1 to 10). */
static const BYTE left_aligned_types[10] = { 0, 0, 0, 1, 0, 0, 1, 1, 1, 0 };

static DWORD reader_alignment(unsigned control_type, const char* horizontal, const char* vertical)
{
    DWORD align;
    if (!game_FUN_0053b6e0(vertical, WORD_TOP))
        align = 8;
    else
        align = game_FUN_0053b6e0(vertical, WORD_BOTTOM) ? 0x10 : 0x20;
    if (!game_FUN_0053b6e0(horizontal, WORD_RIGHT))
        align |= 4;
    else if (!game_FUN_0053b6e0(horizontal, WORD_CENTER))
        align |= 2;
    else if (!game_FUN_0053b6e0(horizontal, WORD_LEFT))
        align |= 1;
    else {
        unsigned type = control_type - 1;

        align |= type < 10 && !left_aligned_types[type] ? 2 : 1;
    }
    return align;
}

/* The control's own font, when it names one or a size and it differs from the dialog's. */
static void reader_font(dialog_reader* r, BYTE* control)
{
    BYTE* self = r->dialog;
    gui_manager* manager = FIELD(self, DLG_MANAGER, gui_manager*);
    control_keys* k = &r->keys;

    if (!k->font_name[0] && !(k->font_height && manager))
        return;
    FIELD(control, 0x12C, DWORD) = 1;
    if (FIELD(control, CTL_FONT, HFONT))
        DeleteObject(FIELD(control, CTL_FONT, HFONT));
    FIELD(control, CTL_FONT, HFONT) = NULL;
    if (k->font_height)
        FIELD(control, CTL_FONT_HEIGHT, int) = k->font_height;
    if (k->bold > FW_MEDIUM)
        k->bold = 1;
    else if (k->bold > 1)
        k->bold = 0;
    if ((manager ? manager->font_name : NO_TEXT)[0]) {
        text_copy((char*) control + 0x130, manager->font_name);
        k->bold = manager->font_bold;
        k->italic = manager->font_italic;
    }
    FIELD(control, CTL_FONT_HEIGHT, int) =
        (int) ((double) (manager ? manager->font_grow : 0.0f) + FIELD(control, CTL_FONT_HEIGHT, int));
    if (FIELD(control, CTL_FONT_HEIGHT, int) == FIELD(self, DLG_SCREEN_SIZE, int)
        && k->bold == FIELD(self, 0x108, int) && k->italic == FIELD(self, 0x10C, int))
        return;
    FIELD(control, CTL_FONT_HEIGHT, int) =
        (int) ((double) FIELD(control, CTL_FONT_HEIGHT, int) * FIELD(self, 0x9C8, float));
    FIELD(control, CTL_FONT, HFONT) = CreateFontA(FIELD(control, CTL_FONT_HEIGHT, int), 0, 0, 0,
                                                  WEIGHT(k->bold), k->italic, 0, 0,
                                                  DEFAULT_CHARSET, 0, 0, PROOF_QUALITY,
                                                  VARIABLE_PITCH, (char*) self + DLG_FACE);
    text_copy((char*) control + 0x130, (char*) self + DLG_FACE);
    FIELD(control, 0x158, int) = k->bold;
    FIELD(control, 0x15C, int) = k->italic;
}

/* Its text and tooltip: from the manager's language module when it has one, else the file's. */
static void reader_texts(dialog_reader* r, BYTE* control)
{
    gui_manager* manager = FIELD(r->dialog, DLG_MANAGER, gui_manager*);
    char tip[0x400];
    int text_id, tip_id;

    /* The original reads the manager's module without a manager, and faults. */
    if (!manager || !manager->language) {
        if (r->tooltip[0]) {
            FIELD(control, CTL_TOOLTIP, char*) =
                game_FUN_004a4410(text_length(r->tooltip) + 1, GAMEUI_CPP, 0x656);
            text_copy(FIELD(control, CTL_TOOLTIP, char*), r->tooltip);
        }
        if (r->keys.text[0])
            control_set_text(control, NULL, r->keys.text);
        return;
    }
    reader_int(r, KEY(0x0057183C), 0, &text_id);                            /* LocalTextId */
    reader_int(r, KEY(0x0057182C), 0, &tip_id);                             /* LocalToolTipId */
    if (text_id)
        control_load_text(control, NULL, manager->language, text_id);
    if (!tip_id)
        return;
    if (LoadStringA(manager->language, tip_id, tip, sizeof tip)) {
        if (FIELD(control, CTL_TOOLTIP, char*))
            game_FUN_004a4450(FIELD(control, CTL_TOOLTIP, char*), GAMEUI_CPP, 0x646);
        FIELD(control, CTL_TOOLTIP, char*) = game_FUN_004a4410(text_length(tip) + 1, GAMEUI_CPP, 0x647);
        text_copy(FIELD(control, CTL_TOOLTIP, char*), tip);
    } else {
        game_FUN_0053a11f(tip, TIP_FAILED, tip_id);
        if (FIELD(control, CTL_TOOLTIP, char*))
            game_FUN_004a4450(FIELD(control, CTL_TOOLTIP, char*), GAMEUI_CPP, 0x64D);
        FIELD(control, CTL_TOOLTIP, char*) = NULL;
    }
}

/* Give a made control its common keys. */
static void reader_apply(dialog_reader* r, BYTE* control, const char* name, const int margin[2])
{
    control_keys* k = &r->keys;
    RECT* design = &FIELD(control, CTL_DESIGN, RECT);
    DWORD align;

    VIRTUAL(control, 0xD0, method_1)(control, NULL, k->attach_id);
    FIELD(control, CTL_ID, DWORD) = k->group;
    control_set_link(control, NULL, (BYTE*) reader_find(r, k->anchor), k->rel_anchor);
    control_set_name(control, NULL, name);
    FIELD(control, 0x168, int) = k->moveable;
    if (design->right == design->left) {
        ui_anim* anim = is_type(r, KEY(0x00571AF8)) || is_type(r, KEY(0x00571A54))  /* MULTISTATE, RADIOBUTTON */
                            ? option_current(control)->anim
                            : FIELD(control, 0x16C, ui_anim*);

        design->right = anim->frames[0]->width + design->left;
        design->bottom = design->top + anim->frames[0]->height;
    }
    reader_font(r, control);
    if (k->source_blit)
        FIELD(control, 0xA4, DWORD) = 1;
    control_set_a0(control, NULL, k->shape_bounds);
    reader_texts(r, control);
    FIELD(control, 0xE8, int) = margin[0];
    FIELD(control, 0xEC, int) = margin[1];
    control_set_shadow(control, NULL, k->text_drop != 0);
    control_set_colour(control, NULL, k->colour);
    control_set_shadow_colour(control, NULL, k->drop_colour);

    align = reader_alignment(FIELD(control, CTL_TYPE, unsigned), k->text_align, k->text_align_v);
    control_set_align(control, NULL, align);
    if (FIELD(control, CTL_TYPE, unsigned) == 6)
        control_set_align(FIELD(control, COMBO_LIST, BYTE*), NULL, align);

    /* FX: how it comes and goes. */
    {
        static const struct { const char* name; int kind; } slides[] = {
            { KEY(0x00571804), 100 },                                       /* GROW */
            { KEY(0x005717FC), 101 },                                       /* LSLIDE */
            { KEY(0x005717F4), 103 },                                       /* TSLIDE */
            { KEY(0x005717EC), 102 },                                       /* RSLIDE */
            { KEY(0x005717E4), 104 },                                       /* BSLIDE */
        };
        int i;

        for (i = 0; i < 5; i++) {
            if (!game_FUN_0053b6e0(k->fx, slides[i].name)) {
                control_set_animation(control, NULL, slides[i].kind, reader_find(r, k->fx_sound),
                                      TRUE, k->fx_delay, (DWORD) -1);
                break;
            }
        }
        if (i == 5 && !game_FUN_0053b6e0(k->fx, KEY(0x005717DC))) {          /* ANIM */
            control_set_animation(control, NULL, 106, 0, TRUE, 0, (DWORD) -1);
            FIELD(control, 0x184, DWORD) = reader_find(r, k->fx_anim_in);
            FIELD(control, 0x188, DWORD) = reader_find(r, k->fx_anim_out);
            FIELD(control, 0x1AC, DWORD) = reader_find(r, k->fx_sound_in);
            FIELD(control, 0x1B0, DWORD) = reader_find(r, k->fx_sound_out);
        }
    }

    control_set_194(control, NULL, 0, reader_find(r, k->sound_norm));
    control_set_194(control, NULL, 1, reader_find(r, k->sound_focus));
    control_set_194(control, NULL, 2, reader_find(r, k->sound_push));
    control_set_194(control, NULL, 3, reader_find(r, k->sound_click));
    VIRTUAL(control, 0xC4, method_1)(control, NULL, k->enable != 0);
    control_show(control, NULL, k->show != 0, TRUE);
    FIELD(control, 0x180, DWORD) = reader_find(r, k->mouse_anim);
    FIELD(control, 0x1D8, DWORD) = k->key_bind;
    FIELD(control, 0x1DC, DWORD) = !k->pre_3d;
    FIELD(control, 0x1E0, DWORD) = k->permanent;
    FIELD(control, 0x1CC, BYTE*) = VIRTUAL(control, 0xC0, control_state_picture)(control, NULL, -1);
    VIRTUAL(control, 0x9C, method_0)(control);
}

/* A control's section, or Set_Default's. */
static void reader_control(dialog_reader* r, BYTE* entry)
{
    const char* name = (const char*) entry;
    BOOL every = r->is_default;
    BYTE* control = NULL;
    int margin[2];

    r->area.left = r->area.top = r->area.right = r->area.bottom = 0;
    if (!r->is_default) {
        if (reader_string(r, KEY(0x00571BAC), NO_TEXT, r->text)) {            /* Xy */
            game_FUN_0053a97b(r->text, XY_FORMAT, &r->area.left, &r->area.top);
            r->area.right = r->area.left;
            r->area.bottom = r->area.top;
        }
        if (reader_string(r, KEY(0x00571B98), NO_TEXT, r->text)) {            /* Xywh */
            game_FUN_0053a97b(r->text, XYXY_FORMAT, &r->area.left, &r->area.top, &r->area.right,
                              &r->area.bottom);
            r->area.right += r->area.left;
            r->area.bottom += r->area.top;
        }
        if (reader_string(r, KEY(0x00571B70), NO_TEXT, r->text))              /* Xyxy */
            game_FUN_0053a97b(r->text, XYXY_FORMAT, &r->area.left, &r->area.top, &r->area.right,
                              &r->area.bottom);
    }
    keys_int(r, KEY(0x00571B68), &r->defaults.group, &r->keys.group);       /* GroupId */
    keys_string(r, KEY(0x00571B60), r->defaults.type, r->keys.type);        /* Type */

    if (every || is_type(r, KEY(0x00571B58)) || is_type(r, KEY(0x00571B4C))
        || is_type(r, KEY(0x00571B40)))                                     /* BUTTON, SCROLLUP/DOWN */
        control = reader_button(r);
    if (every || is_type(r, KEY(0x00571AF8)))                               /* MULTISTATE */
        control = reader_multistate(r);
    if (every || is_type(r, KEY(0x00571AC8)))                               /* LISTBOX */
        control = reader_list(r);
    if (every || is_type(r, KEY(0x00571A54)))                               /* RADIOBUTTON */
        control = reader_radio(r);
    if (every || is_type(r, KEY(0x00571A3C)))                               /* STATIC */
        control = reader_picture(r, FALSE, 0x4F3);
    if (every || is_type(r, KEY(0x00571A30)) || is_type(r, KEY(0x00571A24)))  /* V/HSCROLLBAR */
        control = reader_scroll_bar(r);
    if (every || is_type(r, KEY(0x00571A00)))                               /* EDITBOX */
        control = reader_edit(r);
    if (every || is_type(r, KEY(0x005719D4)))                               /* STATICTEXT */
        control = reader_picture(r, TRUE, 0x545);
    if (every || !game_FUN_0054cd80(r->keys.type, KEY(0x005719C4), 12))     /* DROPDOWNLIST... */
        control = reader_combo(r, name);

    reader_common(r, margin);
    /* An unknown Type makes nothing, where the original faults. */
    if (r->is_default || !control)
        return;
    reader_apply(r, control, name, margin);
    FIELD(entry, ENTRY_VALUE, BYTE*) = control;
}

/*
    Read a dialog file (`data`, `size` bytes, or a file when `size` is 0): its entries, then the
    four passes, then fit the backdrop picture and place a popup on the screen.
*/
PORT(0x0046B850)
int __fastcall dialog_read(BYTE* self, void* edx_unused, void* data, DWORD size)
{
    /* 40 KB, too big for the stack without the CRT's probe; files are read one at a time. */
    static dialog_reader reader;
    dialog_reader* r = &reader;
    text_tokens sections;
    int kind = KIND_ANIM;
    int pass, i;
    char* section;

    (void) edx_unused;
    __stosb((unsigned char*) r, 0, sizeof *r);
    r->dialog = self;
    r->video_mode = video_mode();
    __stosd((unsigned long*) (self + DLG_ENTRIES), 0, DLG_ANIM_LIMIT * ENTRY_SIZE / 4);
    param_construct(&r->file);
    param_open(&r->file, NULL, data, size, TRUE);
    param_sections(&r->file, NULL, r->text, sizeof r->text);

    /* The entries: a name each, and what kind of section it is. */
    tokens_begin(&sections, NULL, r->text);
    for (section = tokens_next(&sections, NULL, SECTION_END); section;
         section = tokens_next(&sections, NULL, SECTION_END)) {
        BYTE* entry;

        if (!game_FUN_0054cd80(section, KEY(0x00571D18), 8)) {              /* Set_Anim */
            kind = KIND_ANIM;
            continue;
        }
        if (!game_FUN_0054cd80(section, KEY(0x00571D0C), 9)) {              /* Set_Sound */
            kind = KIND_SOUND;
            continue;
        }
        if (!game_FUN_0054cd80(section, KEY(0x00571D00), 11)) {             /* Set_Control */
            kind = KIND_CONTROL;
            continue;
        }
        entry = self + DLG_ENTRIES + ENTRY_SIZE * FIELD(self, DLG_ENTRY_COUNT, int);
        text_copy((char*) entry, section);
        FIELD(entry, ENTRY_VALUE, DWORD) = 0;
        if (!game_FUN_0054cd80(section, KEY(0x00571CF4), 8))                /* Set_Info */
            FIELD(entry, ENTRY_KIND, int) = KIND_INFO;
        else if (!game_FUN_0054cd80(section, KEY(0x00571CE8), 11))          /* Set_Default */
            FIELD(entry, ENTRY_KIND, int) = KIND_DEFAULT;
        else
            FIELD(entry, ENTRY_KIND, int) = kind;
        FIELD(self, DLG_ENTRY_COUNT, int)++;
    }

    r->defaults.colour = 0xFFFF;
    r->defaults.text_drop = 1;
    r->defaults.enable = 1;
    r->defaults.show = 1;
    r->more.max_items = 100;
    r->more.scale = 100;
    r->more.selectable = 1;
    r->more.num_states = 2;
    r->more.select_colour = 0xFF88FF;
    r->more.max_length = 1000;
    r->more.list_height = 20;
    r->more.select_box_colour = 0xFF000000;
    r->more.item_box_colour = 0xFF000000;

#ifdef OPENMCM2_DEV
    double pass_time[5];
#endif
    for (pass = 0; pass < 4; pass++) {
#ifdef OPENMCM2_DEV
        pass_time[pass] = dev_milliseconds();
#endif
        for (i = 0; i < FIELD(self, DLG_ENTRY_COUNT, int); i++) {
            BYTE* entry = self + DLG_ENTRIES + ENTRY_SIZE * i;

            param_select(&r->file, NULL, (const char*) entry);
            switch (FIELD(entry, ENTRY_KIND, int)) {
            case KIND_INFO:
                if (pass == 0)
                    reader_info(r);
                break;
            case KIND_ANIM:
                if (pass == 1)
                    reader_anim(r, entry);
                break;
            case KIND_SOUND:
                if (pass == 2)
                    reader_sound(r, entry);
                break;
            case KIND_DEFAULT:
            case KIND_CONTROL:
                if (pass == 3) {
                    r->is_default = FIELD(entry, ENTRY_KIND, int) == KIND_DEFAULT;
                    reader_control(r, entry);
                }
                break;
            }
        }
    }
#ifdef OPENMCM2_DEV
    /* What reading a layout costs: its settings, its pictures, its sounds, its controls. */
    pass_time[4] = dev_milliseconds();
    if (dev_settings.trace_display)
        dev_trace("load", "%s: settings %d ms, %d pictures %d ms, sounds %d ms, controls %d ms",
                  (char*) self + DLG_NAME, (int) (pass_time[1] - pass_time[0]),
                  FIELD(self, DLG_CONTROL_COUNT, int), (int) (pass_time[2] - pass_time[1]),
                  (int) (pass_time[3] - pass_time[2]), (int) (pass_time[4] - pass_time[3]));
#endif

    /* The backdrop picture: the dialog becomes its size. */
    if (r->background_image[0]) {
        BYTE* backdrop = dialog_find_control(self, NULL, BACKDROP_NAME, 5);
        ui_anim* anim = (ui_anim*) dialog_find(self, NULL, r->background_image);

        if (anim) {
            int width = FIELD(anim_picture(anim), 0x14, int);
            int height = FIELD(anim_picture(anim), 0x18, int);

            control_set_picture(backdrop, NULL, 0, (DWORD) anim);
            FIELD(backdrop, CTL_RECT, RECT).right = width;
            FIELD(backdrop, CTL_RECT, RECT).bottom = height;
            FIELD(backdrop, CTL_DESIGN, RECT) = FIELD(backdrop, CTL_RECT, RECT);
            FIELD(backdrop, 0x1CC, BYTE*) = VIRTUAL(backdrop, 0xC0, control_state_picture)(backdrop,
                                                                                         NULL, 0);
#ifdef OPENMCM2_DEV
            if (dev_settings.menus_widescreen) {
                width = (int) (width * FIELD(self, 0x9C4, float));
                height = (int) (height * FIELD(self, 0x9C8, float));
            }
#endif
            FIELD(self, DLG_AREA, RECT).right = FIELD(self, DLG_AREA, RECT).left + width;
            FIELD(self, DLG_AREA, RECT).bottom = FIELD(self, DLG_AREA, RECT).top + height;
            VIRTUAL(backdrop, 0x9C, method_0)(backdrop);
        }
    }

    /* A popup goes where DLG_ALIGN says on the screen. */
    if (FIELD(self, DLG_POPUP, int) && FIELD(self, DLG_DESIGN_W, int) && FIELD(self, DLG_DESIGN_H, int)) {
        RECT* area = &FIELD(self, DLG_AREA, RECT);
        DWORD align = FIELD(self, DLG_ALIGN, DWORD);
        int width = FIELD(object_screen(self), 0xC, int);
        int height = FIELD(object_screen(self), 0x10, int);
        int shift;

        if (align & 1) {
            area->right -= area->left;
            area->left = 0;
        } else {
            shift = align & 4 ? width - 2 * area->left - area->right
                              : (width - area->left - area->right) / 2 - area->left;
            area->left += shift;
            area->right += shift;
        }
        if (align & 8) {
            area->bottom -= area->top;
            area->top = 0;
        } else {
            shift = align & 0x20 ? height - 2 * area->top - area->bottom
                                 : (height - area->top - area->bottom) / 2 - area->top;
            area->top += shift;
            area->bottom += shift;
        }
    }
    param_destruct(&r->file);
    return 1;
}


#ifdef OPENMCM2_DEV
/* Controller focus does not require a legacy joystick or a mouse over a button.
   The mouse resumes ownership when it moves; otherwise it must not erase pad focus. */
BOOL pad_navigation_active = FALSE;

BOOL pad_close_dropdown(void* object)
{
    BYTE* control = object;

    if (control && *(void***) control == COMBO_BUTTON_VTABLE)
        control = FIELD(control, COMBO_BUTTON_OWNER, BYTE*);
    if (control && *(void***) control == COMBO_SLIDER_VTABLE)
        control = FIELD(control, COMBO_SLIDER_OWNER, BYTE*);
    if (control && *(void***) control == COMBO_LIST_VTABLE)
        control = FIELD(control, COMBO_LIST_OWNER, BYTE*);
    if (control && FIELD(control, CTL_TYPE, int) == 6 && FIELD(control, COMBO_OPEN, BOOL)) {
        combo_open(control, NULL, FALSE);
        return TRUE;
    }
    return FALSE;
}

static BOOL pad_focusable(BYTE* control)
{
    BYTE* dialog = FIELD(control, CTL_DIALOG, BYTE*);
    const RECT* area = &FIELD(control, CTL_RECT, RECT);
    const RECT* design = &FIELD(control, CTL_DESIGN, RECT);

    if (*(void***) control == COMBO_BUTTON_VTABLE || *(void***) control == COMBO_SLIDER_VTABLE)
        return FALSE;
    if (!takes_focus(control) || area->right <= area->left || area->bottom <= area->top
        || (design->right - design->left <= 4 && design->bottom - design->top <= 4)
        || !VIRTUAL(control, 0x78, method_0)(control))
        return FALSE;
    while (dialog) {
        if (!FIELD(dialog, DLG_SHOWN, int) || (FIELD(dialog, 0x25, BYTE) & 4) || FIELD(dialog, 0x7F50, int))
            return FALSE;
        if (FIELD(dialog, DLG_MODAL, int))
            break;                      /* the modal's parent is intentionally frozen */
        dialog = FIELD(dialog, DLG_PARENT, BYTE*);
    }
    return TRUE;
}

BOOL pad_back_from_dialog(void* object)
{
    static const char* names[] = {"ButCancelProfile", "CancelButton", "ButCancel", "ButBack", "Back", "ButResume", "ButExit"};
    BYTE* dialog = object;
    unsigned i;
    while (dialog) {
        for (i = 0; i < sizeof names / sizeof names[0]; i++) {
            BYTE* button = dialog_find_control(dialog, NULL, names[i], 0);
            if (button && pad_focusable(button)) {
                ui_event event;
                ZeroMemory(&event, sizeof event);
                control_tell(button, &event, EVENT_CLICK);
                return TRUE;
            }
        }
        {
            BYTE walk[0x94];
            BYTE* button;
            walk_begin(walk, NULL, dialog, 1, UICONTROL_CLASS);
            while ((button = walk_next(walk)) != NULL) {
                if (FIELD(button, CTL_DIALOG, BYTE*) == dialog && FIELD(button, CTL_TYPE, int) == 1
                    && FIELD(button, 0x1D8, DWORD) == 1 && pad_focusable(button)) {
                    ui_event event;
                    walk_end(walk);
                    ZeroMemory(&event, sizeof event);
                    control_tell(button, &event, EVENT_CLICK);
                    return TRUE;
                }
            }
            walk_end(walk);
        }
        if (FIELD(dialog, DLG_MODAL, int))
            break;
        dialog = FIELD(dialog, DLG_PARENT, BYTE*);
    }
    return FALSE;
}

/*
    The control to go to from `focus` in `direction` (1 up, 2 down, 3 left, 4 right): the nearest
    that way, preferring one in line. Up and down go round from the last row to the first. With no
    focus, the control at the top left.
*/
static BYTE* pad_focus_in(BYTE* root, BYTE* focus, int direction)
{
    BYTE walk[0x94];
    BYTE* control;
    BYTE* best = NULL;
    BYTE* round = NULL;
    float best_score = 1e30f, round_score = 1e30f;
    RECT from = {0, 0, 0, 0};

    if (focus)
        from = FIELD(focus, CTL_RECT, RECT);
    walk_begin(walk, NULL, root, 1, UICONTROL_CLASS);
    while ((control = walk_next(walk)) != NULL) {
        RECT to;
        float dx, dy, along, across, score;

        if (control == focus || !pad_focusable(control))
            continue;
        to = FIELD(control, CTL_RECT, RECT);
        if (!focus) {
            score = (float) to.top * 10000 + to.left;
            if (score < best_score) {
                best_score = score;
                best = control;
            }
            continue;
        }
        dx = (to.left + to.right - from.left - from.right) * 0.5f;
        dy = (to.top + to.bottom - from.top - from.bottom) * 0.5f;
        along = direction == 1 ? -dy : direction == 2 ? dy : direction == 3 ? -dx : dx;
        if (direction <= 2)
            across = to.left > from.right ? (float) (to.left - from.right)
                   : from.left > to.right ? (float) (from.left - to.right) : 0;
        else
            across = to.top > from.bottom ? (float) (to.top - from.bottom)
                   : from.top > to.bottom ? (float) (from.top - to.bottom) : 0;
        if (along > 1) {
            score = across * across * 16 + along * along + (dx * dx + dy * dy) * 0.05f;
            if (score < best_score) {
                best_score = score;
                best = control;
            }
        } else if (along < -1 && direction <= 2) {
            score = across * across * 16 - along * along;   /* the farthest back, in line */
            if (score < round_score) {
                round_score = score;
                round = control;
            }
        }
    }
    walk_end(walk);
    return best ? best : round;
}

/*
    A screen's tabs are its highest row of radio buttons in one group. The tab `step` along from
    the one that is on, or NULL at either end or on a screen without tabs.
*/
static BYTE* pad_tab_beside(BYTE* root, int step)
{
    BYTE walk[0x94];
    BYTE* control;
    BYTE* top = NULL;
    BYTE* row[16];
    int count = 0, on = -1, i;

    walk_begin(walk, NULL, root, 1, RADIO_CLASS);
    while ((control = walk_next(walk)) != NULL)
        if (pad_focusable(control) && (!top || FIELD(control, CTL_RECT, RECT).top < FIELD(top, CTL_RECT, RECT).top))
            top = control;
    walk_end(walk);
    if (!top)
        return NULL;

    walk_begin(walk, NULL, root, 1, RADIO_CLASS);
    while ((control = walk_next(walk)) != NULL && count < 16) {
        LONG below = FIELD(control, CTL_RECT, RECT).top - FIELD(top, CTL_RECT, RECT).top;

        if (FIELD(control, 0x7C, DWORD) != FIELD(top, 0x7C, DWORD) || below > 8 || !pad_focusable(control))
            continue;
        for (i = count++; i > 0 && FIELD(row[i - 1], CTL_RECT, RECT).left > FIELD(control, CTL_RECT, RECT).left; i--)
            row[i] = row[i - 1];
        row[i] = control;
    }
    walk_end(walk);
    for (i = 0; i < count; i++)
        if (option_current_index(row[i]) == 1)
            on = i;
    return on >= 0 && on + step >= 0 && on + step < count ? row[on + step] : NULL;
}

/* Press and release `control` as a click does, so it sends its usual events. */
static void pad_click(gui_user* user, BYTE* control)
{
    gui_user_set_capture(user, NULL, control, NULL);
    VIRTUAL(control, 0xDC, control_two)(control, NULL, 0, 0);
    gui_user_apply_focus(user, NULL, FALSE);
    VIRTUAL(control, 0xE0, control_two)(control, NULL, 0, 0);
}

/* Step the focused list, drop-down or slider along `direction`; FALSE when it does not go that
   way (or is at its end), so the focus moves on instead. A held slider speeds up after a second. */
static BOOL pad_adjust(BYTE* focus, int direction, BOOL held_long)
{
    int type = FIELD(focus, CTL_TYPE, int);
    int done = 0;

    if (type == 6 && (direction >= 3 || FIELD(focus, COMBO_OPEN, BOOL))) {
        BYTE* list = FIELD(focus, COMBO_LIST, BYTE*);
        int next = list_selected(list) + (direction == 1 || direction == 3 ? -1 : 1);

        if (next >= 0 && next < list_count(list)) {
            list_select_all(list, NULL, next);
            if (!FIELD(focus, COMBO_OPEN, BOOL))
                VIRTUAL(list, 0x108, list_stepped_slot)(list, NULL, &done);
        }
        return TRUE;
    }
    if (type == 3 && direction <= 2) {
        int next = list_selected(focus) + (direction == 1 ? -1 : 1);

        if (next < 0 || next >= list_count(focus))
            return FALSE;
        list_select_all(focus, NULL, next);
        if (*(void***) focus != COMBO_LIST_VTABLE)
            VIRTUAL(focus, 0x108, list_stepped_slot)(focus, NULL, &done);
        return TRUE;
    }
    if ((type == 7 || type == 8) && direction >= 3) {
        unsigned range = slider_steps(focus) ? slider_steps(focus) : FIELD(focus, SLIDER_RANGE, unsigned);
        int step = held_long && range >= 40 ? (int) range / 20 : 1;
        int value = slider_value(focus) + (direction == 3 ? -step : step);
        ui_event event;

        value = value < 0 ? 0 : (unsigned) value > range ? (int) range : value;
        if (value != slider_value(focus)) {
            slider_set(focus, value, range);
            ZeroMemory(&event, sizeof event);
            control_tell(focus, &event, 3);
        }
        return TRUE;
    }
    return FALSE;
}

void pad_menu_tick(void* object)
{
    gui_manager* manager = object;
    gui_user* user = manager->user_count ? manager->users[0] : NULL;
    static gui_manager* previous_manager;
    static POINT mouse;
    static int previous_direction, previous_tab;
    static DWORD repeat_at, held_since;
    static BOOL accept_was, back_was;
    BYTE* focus;
    BYTE* dialog;
    BYTE* active;
    BYTE* root;
    BOOL accept = pad_accept_down();
    BOOL pressed = accept && !accept_was;
    BOOL back = pad_back_down();
    BOOL back_pressed = back && !back_was;
    int direction = pad_menu_direction();
    int tab = pad_menu_tab();
    int tab_pressed = tab != previous_tab ? tab : 0;
    DWORD now = timeGetTime();

    back_was = back;
    accept_was = accept;                 /* held across a dialog transition is not another click */
    previous_tab = tab;
    if (manager != previous_manager) {
        previous_manager = manager;
        pad_navigation_active = FALSE;
        mouse.x = manager->mouse ? manager->mouse->x : 0;
        mouse.y = manager->mouse ? manager->mouse->y : 0;
    }
    if (manager->mouse && (mouse.x != manager->mouse->x || mouse.y != manager->mouse->y)) {
        mouse.x = manager->mouse->x;
        mouse.y = manager->mouse->y;
        pad_navigation_active = FALSE;
    }
    if (!user || !input_in_foreground() || pad_binding_active() || direction < 0) {
        previous_direction = 0;
        return;
    }
    if (back_pressed) {
        pad_navigation_active = TRUE;
        active = gui_manager_active_dialog(manager);
        dialog = user->focus ? FIELD(user->focus, CTL_DIALOG, BYTE*) : active;
        if (active && FIELD(active, DLG_MODAL, int))
            dialog = active;
        if (!pad_close_dropdown(user->focus))
            pad_back_from_dialog(dialog);
        previous_direction = 0;
        return;
    }
    if (!pad_navigation_active && !direction && !pressed && !tab_pressed) {
        previous_direction = 0;
        return;
    }
    focus = user->focus;
    active = gui_manager_active_dialog(manager);
    dialog = focus ? FIELD(focus, CTL_DIALOG, BYTE*) : active;
    if (active && FIELD(active, DLG_MODAL, int) && active != dialog) {
        dialog = active;
        focus = NULL;
    }
    if (!dialog || (FIELD(dialog, 0x25, BYTE) & 4) || FIELD(dialog, 0x7F50, int)) {
        previous_direction = direction;
        return;
    }
    root = dialog;
    while (!FIELD(root, DLG_MODAL, int) && FIELD(root, DLG_PARENT, BYTE*))
        root = FIELD(root, DLG_PARENT, BYTE*);
    pad_navigation_active = TRUE;

    /* A new screen starts on its first control, shown before anything is moved or pressed. */
    if (!focus || !pad_focusable(focus)) {
        focus = pad_focus_in(root, NULL, 0);
        if (focus)
            gui_user_set_capture(user, NULL, focus, NULL);
        previous_direction = direction;
        repeat_at = now + 350;
        return;
    }
    if (tab_pressed) {
        BYTE* next = pad_tab_beside(root, tab_pressed);

        if (next)
            pad_click(user, next);
        previous_direction = direction;
        return;
    }
    if (direction != previous_direction)
        held_since = now;
    if (direction && (direction != previous_direction || (LONG) (now - repeat_at) >= 0)) {
        repeat_at = now + (direction != previous_direction ? 350 : 110);
        if (!pad_adjust(focus, direction, now - held_since >= 1000)) {
            BYTE* next = pad_focus_in(root, focus, direction);

            if (next)
                gui_user_set_capture(user, NULL, next, NULL);
        }
    }
    previous_direction = direction;
    focus = user->focus;
    if (pressed && focus) {
        int done = 0;

        if (FIELD(focus, CTL_TYPE, int) == 3 && list_selected(focus) >= 0)
            VIRTUAL(focus, 0x108, list_stepped_slot)(focus, NULL, &done);
        else if (FIELD(focus, CTL_TYPE, int) == 6 && FIELD(focus, COMBO_OPEN, BOOL))
            VIRTUAL(FIELD(focus, COMBO_LIST, BYTE*), 0x108, list_stepped_slot)(FIELD(focus, COMBO_LIST, BYTE*), NULL, &done);
        else
            pad_click(user, focus);
    }
}
#endif
