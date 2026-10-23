/*
    OpenMCM2 - the game's own types, data and functions, as far as ported code uses them.

    Layouts are what the game's code does with them; the original field names are not known.
    Unknown stretches are padding named after their offset, and every known field's offset is
    checked at compile time.
*/

#pragma once

#include <intrin.h>
#include <stddef.h>
#include "runtime.h"

#define LAYOUT(type, field, offset) \
    _Static_assert(offsetof(type, field) == (offset), #type "." #field " is not at " #offset)

/*
    A field of an object whose layout has not been written down yet, by its offset. Objects that
    ported code owns get a struct instead.
*/
#define FIELD(object, offset, type) (*(type*) ((BYTE*) (object) + (offset)))

/*
    Slot `offset / 4` of an object's vtable, as a function pointer of `type`. C++ methods
    (__thiscall) are typed as __fastcall with an unused EDX parameter, as ported functions are.
*/
#define VIRTUAL(object, offset, type) ((type) (*(void***) (object))[(offset) / 4])

/* The game's tracked new and delete. The file and line are for its leak report. */
void* __cdecl game_FUN_004a4600(size_t size, const char* file, int line);
void  __cdecl game_FUN_004a46b0(void* p);
#define game_new    game_FUN_004a4600
#define game_delete game_FUN_004a46b0

/* The game's other tracked allocation pair, with the file and line passed to the free too. */
void* __cdecl game_FUN_004a4410(size_t size, const char* file, int line);
void  __cdecl game_FUN_004a4450(void* p, const char* file, int line);

/* The file names the game's tracked allocations report, at the game's own strings. */
#define GUIMANAGER_CPP ((const char*) 0x0057244C)   /* "...\GUIManager.cpp" */

/*
    The game's memory accounting: allocations are charged to the current zone. A function that
    allocates for a subsystem switches to its zone and back again.
*/
#define memory_zones (*(void* const*) 0x00573FB4)
#define ZONE_UI      ((void*) 0x00571778)            /* "UI" */
void* __fastcall game_FUN_004a42f0(void* zones, void* edx_unused, void* zone);
void  __fastcall game_FUN_004a4380(void* zones, void* edx_unused, void* previous);
#define zone_enter(zone)      game_FUN_004a42f0(memory_zones, NULL, zone)
#define zone_leave(previous)  game_FUN_004a4380(memory_zones, NULL, previous)

/* The game's main object, which 0x005743AC points at; its layout is mostly unknown. */
#define game_main (*(BYTE* const*) 0x005743AC)

/* String copies as the game inlines them (repne scasb, rep movs): the terminator is copied. */
static __inline void text_copy(char* to, const char* from)
{
    while ((*to++ = *from++) != 0)
        ;
}

static __inline size_t text_length(const char* text)
{
    const char* end = text;

    while (*end)
        end++;
    return end - text;
}

/* Append, as the game's inlined strcat. */
static __inline void text_append(char* to, const char* from)
{
    text_copy(to + text_length(to), from);
}

char* __cdecl game__strncpy(char* to, const char* from, size_t count);

/* At most `most` characters of `from`, ended, as the game inlines its bounded copies. */
static __inline void text_copy_most(char* to, const char* from, int most)
{
    int length = (int) text_length(from);

    if (length > most)
        length = most;
    game__strncpy(to, from, length);
    to[length] = 0;
}

typedef struct vector3 {
    float x, y, z;
} vector3;

static __inline void set_vector(vector3* v, float x, float y, float z)
{
    v->x = x;
    v->y = y;
    v->z = z;
}

/* `v` turned by `angle` about `axis`, scaled to length 1, into `out`; `axis` is scaled to length 1
   too. d3drm.dll's, ported (d3drm.c). */
void* __stdcall D3DRMVectorRotate(vector3* out, const vector3* v, vector3* axis, float angle);

/* Parameter.cpp's reader; its size is the room the original gives it. */
typedef struct param_file {
    BYTE opaque[0x6C4];
} param_file;

void __fastcall game_FUN_004bb880(param_file* self);
void __fastcall game_FUN_004bb8e0(param_file* self);
void __fastcall game_FUN_004bbef0(param_file* self, void* edx_unused, void* data, DWORD size,
                                  BOOL scan);
int  __fastcall game_FUN_004bc990(param_file* self, void* edx_unused, char* names, int size);
BOOL __fastcall game_FUN_004bc040(param_file* self, void* edx_unused, const char* section);
BOOL __fastcall game_FUN_004bc610(param_file* self, void* edx_unused, const char* key,
                                  const char* fallback, char* value, int size);
BOOL __fastcall game_FUN_004bc660(param_file* self, void* edx_unused, const char* key, int fallback,
                                  int* value);
BOOL __fastcall game_FUN_004bc690(param_file* self, void* edx_unused, const char* key,
                                  float fallback, float* value);
#define param_construct game_FUN_004bb880
#define param_destruct  game_FUN_004bb8e0
#define param_open      game_FUN_004bbef0
#define param_sections  game_FUN_004bc990   /* the section names, each ended by "?" */
#define param_select    game_FUN_004bc040
#define param_text      game_FUN_004bc610   /* `size` -1: the whole value */
#define param_int       game_FUN_004bc660

/* The device lists the game keeps: keyboard (0x34), mouse (0x30) and joysticks (at 0x10). */
#define game_inputs FIELD(game_main, 0x14, BYTE*)

/* The number of input devices the game found. */
static __inline int input_count(void)
{
    return *(int*) (*(BYTE**) (game_main + 0x14) + 4);
}

/* GameObject: the base of everything in the game's object tree, 44 bytes. */
typedef struct game_object {
    void** vtable;
    BYTE   unknown_04[0x28];
} game_object;

_Static_assert(sizeof(game_object) == 44, "GameObject is 44 bytes");

/* Slot 2 of a GameObject's vtable: unlink it from the tree and destroy it. */
static __inline void object_destroy(void* object)
{
    VIRTUAL(object, 8, void (__fastcall*)(void*))(object);
}

/* The rest of the slots ported code calls, by what they are known to do. */
typedef int (__fastcall* method_0)(void* self);
typedef int (__fastcall* method_1)(void* self, void* edx_unused, DWORD a);

/* The field every GameObject is created with: the screen it draws to. */
#define object_screen(object) FIELD(object, 0x18, BYTE*)

/*
    GameObject's constructor, destructor and set-up, and the base class's versions of the
    virtual functions the GUI classes override.
*/
void  __fastcall game_FUN_00469b80(void* self, void* edx_unused, DWORD flag);
void  __fastcall game_FUN_00469c40(void* self);
void  __fastcall game_vf_8_46A220(void* self, void* edx_unused, void* screen);
int   __fastcall game_vf_10_46A300(void* self, void* edx_unused, DWORD time);
int   __fastcall game_vf_13_46A260(void* self);
int   __fastcall game_vf_14_46A290(void* self);
int   __fastcall game_vf_15_46A2D0(void* self);
int   __fastcall game_vf_22_46A4B0(void* self, void* edx_unused, DWORD a, DWORD b);
int   __fastcall game_vf_23_46A500(void* self, void* edx_unused, DWORD a, DWORD b);
int   __fastcall game_vf_25_46A650(void* self, void* edx_unused, DWORD a);
int   __fastcall game_vf_4_469F80(void* self);                          /* hide */
int   __fastcall game_vf_5_469F90(void* self);                          /* show */
#define object_construct  game_FUN_00469b80
#define object_destruct   game_FUN_00469c40
#define object_setup      game_vf_8_46A220

/*
    The object tree. Each returns 0 without changing anything while the tree is locked (the game
    sets 0x00661690 for that). `order` is stored with the object; -1 is "last".
      object_add_child:   `child` becomes the last child of `parent`; returns `child`.
      object_add_sibling: `object` goes after the last sibling of `self`; returns `object`.
      object_move_before: `self` moves to just before `other`, under `other`'s parent.
*/
void* __fastcall game_FUN_0046a070(void* parent, void* edx_unused, void* child, int order);
void* __fastcall game_FUN_0046a010(void* self, void* edx_unused, void* object, int order);
int   __fastcall game_FUN_0046a190(void* self, void* edx_unused, void* other, int order);
#define object_add_child   game_FUN_0046a070
#define object_add_sibling game_FUN_0046a010
#define object_move_before game_FUN_0046a190

/* ContainerList (ContainerList.h): a growable array of pointers. */
typedef struct container_list {
    int    count;
    void** items;
    int    grow_by;
    int    capacity;
} container_list;

#define CONTAINER_LIST_H ((const char*) 0x0056C6E4)    /* the game's "...\ContainerList.h" */

/*
    ContainerList's add, as the game inlines it. When full, it grows to count + grow_by; if that
    is no bigger, or the allocation fails, the item is silently not added.
*/
static __inline void list_add(container_list* list, void* item)
{
    if (list->count >= list->capacity) {
        int capacity = list->count + list->grow_by;
        void** items;

        if (capacity <= list->capacity)
            return;
        items = game_new(capacity * sizeof *items, CONTAINER_LIST_H, 0x47);
        if (!items)
            return;
        __movsd((unsigned long*) items, (unsigned long*) list->items, list->capacity);
        game_delete(list->items);
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = item;
}
