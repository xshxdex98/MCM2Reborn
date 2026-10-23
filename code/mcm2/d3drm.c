/*
    OpenMCM2 - D3DRMVectorRotate, the one function the game takes from d3drm.dll (Direct3D
    Retained Mode), ported so the DLL is not needed. Windows has not shipped it since XP, and the
    CD keeps it in its root, outside the GAME folder an install is copied from.

    The game's thunk (0x005386BC, a jmp through the import slot) is ported, so every call goes to
    this one and the slot is never read. tools/link.py then leaves d3drm.dll out of the import
    directory the loader reads, in the room kept here; the retail import table in .rdata is left
    as it is.

    As d3drm's own: v turned about the axis by the right-hand rule (angle in radians), the result
    scaled to length 1, and a vector of no length taken as (1, 0, 0). The caller's axis is scaled
    to length 1 in place too, as d3drm's own does. Checked against the CD's D3DRM.DLL: within
    6e-7 of it over 200,000 rotations, the axis left the same.
*/

#include "game.h"

/* The import directory tools/link.py writes: one entry of 20 bytes for each DLL, and the empty
   one that ends it. Retail's has 15 DLLs. Not all zero, so it is data in the file the link can
   write, not room made at load time. */
const BYTE import_directory[20 * 16] = { 1 };

static float square_root(float x)
{
    __asm {
        fld x
        fsqrt
        fstp x
    }
    return x;
}

static void sine_cosine(float angle, float* sine, float* cosine)
{
    float s, c;
    __asm {
        fld angle
        fsincos
        fstp c
        fstp s
    }
    *sine = s;
    *cosine = c;
}

static vector3 unit(vector3 v)
{
    float length = square_root(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length == 0) {
        set_vector(&v, 1.0f, 0.0f, 0.0f);
        return v;
    }
    set_vector(&v, v.x / length, v.y / length, v.z / length);
    return v;
}

PORT(0x005386BC)
void* __stdcall D3DRMVectorRotate(vector3* out, const vector3* v, vector3* axis, float angle)
{
    vector3 n = unit(*axis), turned;
    float s, c, along;

    *axis = n;
    /* Rodrigues: v cos + (n x v) sin + n (n . v)(1 - cos), which is what d3drm's quaternions make. */
    sine_cosine(angle, &s, &c);
    along = (n.x * v->x + n.y * v->y + n.z * v->z) * (1 - c);
    turned.x = v->x * c + (n.y * v->z - n.z * v->y) * s + n.x * along;
    turned.y = v->y * c + (n.z * v->x - n.x * v->z) * s + n.y * along;
    turned.z = v->z * c + (n.x * v->y - n.y * v->x) * s + n.z * along;
    *out = unit(turned);
    return out;
}
