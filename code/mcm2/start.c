/*
    OpenMCM2 - the image's entry point.

    Installs whatever replaces part of the game, then runs the game's own start-up, which never
    returns: the 1999 CRT ends the process itself.

    -ddraw on the command line keeps the original DirectDraw, traced, for comparison.
*/

#include "runtime.h"
#include "ddtrace.h"
#include "profile.h"
#include "render.h"

#ifdef OPENMCM2_DEV
#include "../dev/dev.h"
#endif

void game_entry(void);
void crash_install(void);

static BOOL has_switch(const char* name)
{
    const char* line = GetCommandLineA();
    int n = lstrlenA(name);

    for (; *line; ++line)
    {
        if ((line[0] == '-' || line[0] == '/') && CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, line + 1, n, name, n) == CSTR_EQUAL)
            return TRUE;
    }
    return FALSE;
}

/*
    The game sees the screen in its own pixels, not in those of Windows' display scaling. Not DPI
    aware, at 125% it was given a 1536x960 desktop and stretched to the 1920x1200 panel, and the
    exclusive DirectInput mouse mixed the two: each read pushed toward the bottom right, in the
    screen's proportions, by more than a slow move, so a slow move left went right. Wine has no
    such scaling. It must come before any window is made.
*/
static void dpi_aware(void)
{
    typedef BOOL (WINAPI* aware_fn)(void);
    aware_fn aware = (aware_fn) api("user32.dll", "SetProcessDPIAware");

    if (aware && aware())
        log_line("dpi: the screen in its own pixels, not scaled");
}

void open_start(void)
{
    log_open();
    log_line("OpenMCM2 starting");
    dpi_aware();
    crash_install();
    profile_install();
#ifdef OPENMCM2_DEV
    dev_install();
#endif

    if (has_switch("ddraw"))
        ddtrace_install();
    else
        render_install();

    game_entry();
}
