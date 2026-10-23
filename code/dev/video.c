/*
    OpenMCM2 dev build - the game's movies, played from copies of our own.

    The three movies (ui\Intro.avi, ui\Credits.avi and ui\events.avi, the main menu's) are Indeo 5,
    which no Windows has played for years, and Wine's and Proton's DirectShow cannot either: Proton
    shows a test card in its place. So the originals are never opened. The launcher makes an MPEG-1
    copy of each beside it with ffmpeg when it finds one (ui\Intro.mpg), and this plays that copy,
    the same way everywhere: pl_mpeg (third_party/pl_mpeg.h, unchanged) decodes it, the frames go
    into a surface of the renderer's own that movie_render blits, and the sound goes out through
    waveOut. A movie with no copy is skipped, as the original was when it could not be played.

    Nothing here runs on a thread of its own: each movie decodes as its control ticks, and its
    sound is queued a little ahead, so when the control is gone its sound stops within that much.
*/

#define DIRECTDRAW_VERSION 0x0700
#define CINTERFACE

#include "dev.h"

#include <ddraw.h>
#include <intrin.h>
#include <mmsystem.h>

/* The back buffer of the screen a control draws to (its 0x18, the screen's 0x48: movie_render's). */
#define BACK_BUFFER(control) (*(IDirectDrawSurface7**) (*(BYTE**) ((BYTE*) (control) + 0x18) + 0x48))

/* pl_mpeg's needs: memory from the process heap, and copies of our own rather than the CRT's. */
static void* video_realloc(void* p, size_t size);
static void* video_copy(void* to, const void* from, size_t size);
static void* video_move(void* to, const void* from, size_t size);
static void* video_fill(void* to, int value, size_t size);

#define PLM_MALLOC(size)     mem_alloc(size)
#define PLM_FREE(p)          mem_free(p)
#define PLM_REALLOC(p, size) video_realloc(p, size)
#define PLM_NO_STDIO
#include <string.h>
#include <stdlib.h>
#define memcpy  video_copy
#define memmove video_move
#define memset  video_fill
#define PL_MPEG_IMPLEMENTATION
#pragma warning(push, 0)
#include "third_party/pl_mpeg.h"
#pragma warning(pop)
#undef memcpy
#undef memmove
#undef memset

#define VIDEOS_MOST    4            /* movies open at once: the game has one at a time */
#define AUDIO_BUFFERS  24           /* each one MP2 frame, 1152 samples: 0.6 s at 44.1 kHz */
#define AUDIO_LEAD     0.15         /* seconds of sound queued ahead of the picture */
#define TICK_MOST_MS   100          /* a longer gap between ticks (a load) is not caught up */

/* dlgprocs.c's callbacks for a movie that ends: the screen closes, or the movie starts again. */
#define MOVIE_ENDED    0x0044C1C0
#define MOVIE_LOOP     0x0044C190

typedef MMRESULT (WINAPI* wave_open_fn)(HWAVEOUT*, UINT, const WAVEFORMATEX*, DWORD_PTR, DWORD_PTR, DWORD);
typedef MMRESULT (WINAPI* wave_header_fn)(HWAVEOUT, WAVEHDR*, UINT);
typedef MMRESULT (WINAPI* wave_fn)(HWAVEOUT);
typedef DWORD    (WINAPI* seek_fn)(HANDLE, LONG, LONG*, DWORD);
typedef LPVOID   (WINAPI* heap_realloc_fn)(HANDLE, DWORD, LPVOID, SIZE_T);
typedef HANDLE   (WINAPI* heap_fn)(void);

void __fastcall dialog_end(BYTE* self, void* edx_unused, DWORD sound);

static struct {
    wave_open_fn   open;
    wave_header_fn prepare, unprepare, write;
    wave_fn        pause, restart, reset, close;
    seek_fn        seek;
    BOOL           found;
} g_api;

typedef struct video {
    BYTE*                movie;     /* the UIMovie control it plays in */
    BYTE*                dialog;    /* told to close when the movie ends, unless it loops */
    BOOL                 started;   /* stream_play has been asked for */
    BOOL                 paused;
    BOOL                 ended;
    DWORD                last;      /* timeGetTime at the last tick */
    HANDLE               file;
    plm_t*               plm;
    plm_frame_t*         frame;     /* the newest frame decoded, not yet shown */
    IDirectDrawSurface7* surface;
    int                  width, height;
    HWAVEOUT             wave;      /* NULL for a movie with no sound, or no device */
    WAVEHDR              headers[AUDIO_BUFFERS];
    short                samples[AUDIO_BUFFERS][PLM_AUDIO_SAMPLES_PER_FRAME * 2];
} video;

static video* g_videos[VIDEOS_MOST];

static void* video_realloc(void* p, size_t size)
{
    static heap_realloc_fn heap_realloc;
    static HANDLE heap;

    if (!p)
        return mem_alloc(size);
    if (!heap_realloc) {
        heap_realloc = (heap_realloc_fn) api("kernel32.dll", "HeapReAlloc");
        heap = ((heap_fn) api("kernel32.dll", "GetProcessHeap"))();
    }
    return heap_realloc ? heap_realloc(heap, HEAP_ZERO_MEMORY, p, size) : NULL;
}

static void* video_copy(void* to, const void* from, size_t size)
{
    __movsb((unsigned char*) to, (const unsigned char*) from, size);
    return to;
}

static void* video_move(void* to, const void* from, size_t size)
{
    unsigned char* t = (unsigned char*) to;
    const unsigned char* f = (const unsigned char*) from;

    if (t <= f || t >= f + size)
        return video_copy(to, from, size);
    while (size--)
        t[size] = f[size];
    return to;
}

static void* video_fill(void* to, int value, size_t size)
{
    __stosb((unsigned char*) to, (unsigned char) value, size);
    return to;
}

static BOOL find_api(void)
{
    if (!g_api.found) {
        g_api.open = (wave_open_fn) api("winmm.dll", "waveOutOpen");
        g_api.prepare = (wave_header_fn) api("winmm.dll", "waveOutPrepareHeader");
        g_api.unprepare = (wave_header_fn) api("winmm.dll", "waveOutUnprepareHeader");
        g_api.write = (wave_header_fn) api("winmm.dll", "waveOutWrite");
        g_api.pause = (wave_fn) api("winmm.dll", "waveOutPause");
        g_api.restart = (wave_fn) api("winmm.dll", "waveOutRestart");
        g_api.reset = (wave_fn) api("winmm.dll", "waveOutReset");
        g_api.close = (wave_fn) api("winmm.dll", "waveOutClose");
        g_api.seek = (seek_fn) api("kernel32.dll", "SetFilePointer");
        g_api.found = TRUE;
    }
    return g_api.seek != NULL;
}

static video* video_of(void* movie)
{
    int i;

    for (i = 0; i < VIDEOS_MOST; ++i)
        if (g_videos[i] && g_videos[i]->movie == movie)
            return g_videos[i];
    return NULL;
}

/* ---- The file, read as pl_mpeg asks for it ---- */

static void file_load(plm_buffer_t* buffer, void* user)
{
    video* v = (video*) user;
    DWORD got = 0;

    if (buffer->discard_read_bytes)
        plm_buffer_discard_read_bytes(buffer);
    if (!ReadFile(v->file, buffer->bytes + buffer->length, (DWORD) (buffer->capacity - buffer->length), &got, NULL)
        || got == 0)
        buffer->has_ended = TRUE;
    buffer->length += got;
}

static void file_seek(plm_buffer_t* buffer, size_t offset, void* user)
{
    (void) buffer;
    g_api.seek(((video*) user)->file, (LONG) offset, NULL, FILE_BEGIN);
}

static size_t file_tell(plm_buffer_t* buffer, void* user)
{
    (void) buffer;
    return g_api.seek(((video*) user)->file, 0, NULL, FILE_CURRENT);
}

/* ---- Picture ---- */

/* A 4x4 ordered dither, so the 16-bit surface keeps the skies' gradients smooth. */
static const BYTE k_bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

static int clamp_byte(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

/* A frame as RGB565, by the BT.601 matrix pl_mpeg's own converters use. */
static void frame_to_565(const plm_frame_t* frame, BYTE* to, LONG pitch)
{
    unsigned int x, y;

    for (y = 0; y < frame->height; ++y) {
        const BYTE* luma = frame->y.data + y * frame->y.width;
        const BYTE* cb_row = frame->cb.data + (y >> 1) * frame->cb.width;
        const BYTE* cr_row = frame->cr.data + (y >> 1) * frame->cr.width;
        WORD* out = (WORD*) (to + y * pitch);

        for (x = 0; x < frame->width; ++x) {
            int cb = cb_row[x >> 1] - 128, cr = cr_row[x >> 1] - 128;
            int l = ((luma[x] - 16) * 76309) >> 16;
            int d = k_bayer[y & 3][x & 3];
            int r = clamp_byte(l + ((cr * 104597) >> 16) + (d >> 1));
            int g = clamp_byte(l - ((cb * 25674 + cr * 53278) >> 16) + (d >> 2));
            int b = clamp_byte(l + ((cb * 132201) >> 16) + (d >> 1));

            out[x] = (WORD) (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}

/* The surface filled from `frame`, or cleared to black without one. */
static void show(video* v, const plm_frame_t* frame)
{
    DDSURFACEDESC2 desc;

    __stosb((unsigned char*) &desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    if (v->surface->lpVtbl->Lock(v->surface, NULL, &desc, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL) != DD_OK)
        return;
    if (frame)
        frame_to_565(frame, (BYTE*) desc.lpSurface, desc.lPitch);
    else
        __stosb((unsigned char*) desc.lpSurface, 0, (size_t) desc.lPitch * v->height);
    v->surface->lpVtbl->Unlock(v->surface, NULL);
}

static void on_frame(plm_t* plm, plm_frame_t* frame, void* user)
{
    (void) plm;
    ((video*) user)->frame = frame;
}

/* An offscreen surface of the renderer's, the movie's size, made through the screen's DirectDraw. */
static IDirectDrawSurface7* make_surface(BYTE* movie, int width, int height)
{
    IDirectDrawSurface7* back = BACK_BUFFER(movie);
    IDirectDrawSurface7* made = NULL;
    IDirectDraw7* dd = NULL;
    DDSURFACEDESC2 desc;

    if (!back || back->lpVtbl->GetDDInterface(back, (void**) &dd) != DD_OK || !dd)
        return NULL;
    __stosb((unsigned char*) &desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = (DWORD) width;
    desc.dwHeight = (DWORD) height;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    desc.ddpfPixelFormat.dwSize = sizeof desc.ddpfPixelFormat;      /* RGB565, the renderer's first */
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 16;
    desc.ddpfPixelFormat.dwRBitMask = 0xF800;
    desc.ddpfPixelFormat.dwGBitMask = 0x07E0;
    desc.ddpfPixelFormat.dwBBitMask = 0x001F;
    if (dd->lpVtbl->CreateSurface(dd, &desc, &made, NULL) != DD_OK)
        made = NULL;
    dd->lpVtbl->Release(dd);
    return made;
}

/* ---- Sound ---- */

static void on_samples(plm_t* plm, plm_samples_t* samples, void* user)
{
    video* v = (video*) user;
    int i, n;

    (void) plm;
    for (i = 0; i < AUDIO_BUFFERS; ++i)
        if (!(v->headers[i].dwFlags & WHDR_INQUEUE))
            break;
    if (i == AUDIO_BUFFERS)
        return;                     /* every buffer queued: the device is behind, this frame is dropped */
    for (n = 0; n < (int) samples->count * 2; ++n) {
        float s = samples->interleaved[n] * 32767.0f;

        v->samples[i][n] = (short) (s > 32767.0f ? 32767 : s < -32768.0f ? -32768 : s);
    }
    v->headers[i].dwBufferLength = samples->count * 2 * sizeof(short);
    g_api.write(v->wave, &v->headers[i], sizeof v->headers[i]);
}

static BOOL sound_queued(const video* v)
{
    int i;

    for (i = 0; v->wave && i < AUDIO_BUFFERS; ++i)
        if (v->headers[i].dwFlags & WHDR_INQUEUE)
            return TRUE;
    return FALSE;
}

static void open_sound(video* v)
{
    WAVEFORMATEX format;
    int i;

    if (!g_api.open || !g_api.prepare || !g_api.write || plm_get_num_audio_streams(v->plm) < 1)
        return;
    __stosb((unsigned char*) &format, 0, sizeof format);
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = (DWORD) plm_get_samplerate(v->plm);
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = format.nSamplesPerSec * 4;
    if (g_api.open(&v->wave, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        v->wave = NULL;
        log_line("video: no sound device, the movie plays silent");
        return;
    }
    g_api.pause(v->wave);           /* until it starts */
    for (i = 0; i < AUDIO_BUFFERS; ++i) {
        v->headers[i].lpData = (LPSTR) v->samples[i];
        v->headers[i].dwBufferLength = sizeof v->samples[i];
        g_api.prepare(v->wave, &v->headers[i], sizeof v->headers[i]);
    }
    plm_set_audio_decode_callback(v->plm, on_samples, v);
    plm_set_audio_lead_time(v->plm, AUDIO_LEAD);
}

static void close_sound(video* v)
{
    int i;

    if (!v->wave)
        return;
    g_api.reset(v->wave);
    for (i = 0; i < AUDIO_BUFFERS; ++i)
        g_api.unprepare(v->wave, &v->headers[i], sizeof v->headers[i]);
    g_api.close(v->wave);
    v->wave = NULL;
}

/* ---- The movie control's ---- */

void video_close(void* movie)
{
    video* v = video_of(movie);
    int i;

    if (!v)
        return;
    for (i = 0; i < VIDEOS_MOST; ++i)
        if (g_videos[i] == v)
            g_videos[i] = NULL;
    close_sound(v);
    if (v->plm)
        plm_destroy(v->plm);        /* and its buffer */
    if (v->file != INVALID_HANDLE_VALUE)
        CloseHandle(v->file);
    if (v->surface)
        v->surface->lpVtbl->Release(v->surface);
    mem_free(v);
}

/*
    `file` (ui\Intro.avi) as its copy, ui\Intro.mpg, in place of what `movie` played; `ended` and
    `user` are what the game gives its movie for when it ends (MOVIE_ENDED and the screen, or
    MOVIE_LOOP). FALSE when there is no copy, or it cannot be played.
*/
BOOL video_open(void* movie, const char* file, BOOL play, DWORD ended, DWORD user)
{
    char name[MAX_PATH];
    char* dot = NULL;
    char* at = name;
    plm_buffer_t* buffer;
    video* v;
    int slot;
    DWORD size;

    video_close(movie);
    if (!find_api())
        return FALSE;
    while (*file && at < name + sizeof name - 5) {
        if (*file == '.')
            dot = at;
        else if (*file == '\\' || *file == '/')
            dot = NULL;
        *at++ = *file++;
    }
    if (*file)
        return FALSE;               /* too long a name */
    video_copy(dot ? dot : at, ".mpg", 5);

    for (slot = 0; slot < VIDEOS_MOST && g_videos[slot]; ++slot)
        ;
    if (slot == VIDEOS_MOST) {
        log_line("video: %d movies open, the oldest closed", VIDEOS_MOST);
        video_close(g_videos[0]->movie);
        slot = 0;
    }

    v = (video*) mem_alloc(sizeof *v);
    if (!v)
        return FALSE;
    v->movie = (BYTE*) movie;
    v->file = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (v->file == INVALID_HANDLE_VALUE) {
        log_line("video: no %s (the launcher makes it with ffmpeg), %s is skipped", name, file);
        mem_free(v);
        return FALSE;
    }
    g_videos[slot] = v;

    size = GetFileSize(v->file, NULL);
    buffer = plm_buffer_create_with_callbacks(file_load, file_seek, file_tell, size, v);
    buffer->discard_read_bytes = TRUE;
    v->plm = plm_create_with_buffer(buffer, TRUE);
    if (!v->plm || !plm_has_headers(v->plm) || plm_get_num_video_streams(v->plm) < 1) {
        log_line("video: %s is not an MPEG-1 movie, skipped", name);
        video_close(movie);
        return FALSE;
    }
    v->width = plm_get_width(v->plm);
    v->height = plm_get_height(v->plm);
    v->surface = make_surface(v->movie, v->width, v->height);
    if (!v->surface) {
        log_line("video: no surface for %s", name);
        video_close(movie);
        return FALSE;
    }
    show(v, NULL);

    plm_set_loop(v->plm, ended == MOVIE_LOOP);
    if (ended == MOVIE_ENDED)
        v->dialog = (BYTE*) user;
    plm_set_video_decode_callback(v->plm, on_frame, v);
    open_sound(v);
    log_line("video: %s, %dx%d%s", name, v->width, v->height, v->wave ? " with sound" : "");
    if (play)
        video_play(movie);
    return TRUE;
}

/* The movie's size, when video.c plays it. */
BOOL video_size(void* movie, LONG* width, LONG* height)
{
    video* v = video_of(movie);

    if (!v)
        return FALSE;
    *width = v->width;
    *height = v->height;
    return TRUE;
}

/* The surface its frames are in (an IDirectDrawSurface7), or NULL. */
void* video_picture(void* movie)
{
    video* v = video_of(movie);

    return v ? v->surface : NULL;
}

void video_play(void* movie)
{
    video* v = video_of(movie);

    if (!v)
        return;
    v->started = TRUE;
    v->paused = FALSE;
    v->last = timeGetTime();
    if (v->wave)
        g_api.restart(v->wave);
}

void video_pause(void* movie, BOOL paused)
{
    video* v = video_of(movie);

    if (!v || v->paused == paused)
        return;
    v->paused = paused;
    v->last = timeGetTime();
    if (v->wave)
        (paused ? g_api.pause : g_api.restart)(v->wave);
}

/* Stopped, and back at its start. */
void video_stop(void* movie)
{
    video* v = video_of(movie);

    if (!v)
        return;
    v->started = FALSE;
    v->ended = FALSE;
    if (v->wave) {
        g_api.reset(v->wave);
        g_api.pause(v->wave);
    }
    plm_rewind(v->plm);
    show(v, NULL);
}

/* As the control ticks: decode up to now, show the newest frame, and close the screen at the end. */
void video_tick(void* movie)
{
    video* v = video_of(movie);
    DWORD now, gap;

    if (!v)
        return;
    now = timeGetTime();
    gap = now - v->last;
    v->last = now;
    if (!v->started || v->paused)
        return;
    if (!v->ended) {
        v->frame = NULL;
        plm_decode(v->plm, (gap > TICK_MOST_MS ? TICK_MOST_MS : gap) / 1000.0);
        if (v->frame)
            show(v, v->frame);
        v->ended = plm_has_ended(v->plm);
    }
    /* Its last sound heard first; then the screen closes at its next change (dialog_end). */
    if (v->ended && v->dialog && !sound_queued(v)) {
        dialog_end(v->dialog, NULL, 0);
        v->dialog = NULL;
    }
}
