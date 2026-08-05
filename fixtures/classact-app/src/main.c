/*
 * classact-app -- KNOWN BLOCKED, minimal repro only (2026-08-05). This is
 * not yet the real fixture (see fixtures/gadtools-app for that shape);
 * it's deliberately reduced to the smallest reproduction of an unsolved
 * bug, left in place so the next session can pick up with full context
 * instead of re-discovering it.
 *
 * SYMPTOM: merely calling OpenLibrary("window.class", ...) anywhere in
 * this binary -- regardless of what else is or isn't around it --
 * results in the process exiting instantly with RETURN_FAIL (20), and
 * *none* of this program's own Write(Output(), ...) diagnostics ever
 * appear (not even the unconditional one before any library call at
 * all). In their place, exactly one line appears that this program never
 * wrote: "window.library failed to load" -- there is no such library;
 * that text's real origin is still unknown.
 *
 * Verified under Copperline (headless, --control), Kickstart 3.2 /
 * Workbench 3.2.3, against tests/copperline/copperline.local.toml's
 * hostfs-mounted Workbench install (the same one an existing Amiberry
 * setup uses). window.class and all four ReAction gadget classes exist
 * on that install at v47.38 (confirmed via `strings`).
 *
 * RULED OUT (each empirically, with fresh-binary verification -- diffing
 * `strings` output on the rebuilt binary before every test run):
 *   - Bare "window.class" vs the explicit absolute path
 *     "SYS:Classes/window.class" -- same failure either way, so this
 *     isn't a LIBS: search-path problem (confirmed
 *     "Assign LIBS: SYS:Classes ADD" is present and correct in
 *     S:Startup-Sequence).
 *   - NewObject(WINDOW_GetClass(), NULL, ...) vs
 *     NewObject(NULL, "window.class", ...) (the class-by-name form
 *     ../amiauth's working ReAction code uses) -- not reached; the
 *     failure is in OpenLibrary() itself, before any NewObject call.
 *   - Missing -lamiga (DoMethod/NewObject/GetAttr's amiga.lib varargs
 *     marshaling) -- added it (confirmed it actually links real code in,
 *     binary grew ~2.4 KB), no change.
 *   - Missing utility.library (../amiauth opens it before window.class)
 *     -- added it, no change.
 *   - Missing gadtools.library (both ../amiauth and ../amigui's
 *     generated ReAction code open it alongside window.class even
 *     though nothing else needs it) -- added it, no change.
 *   - The test methodology itself: a trivial isolation program
 *     (just Write(Output(), "hello\n", ...), no Intuition/BOOPSI at all)
 *     works perfectly under the exact same redirected/synchronous
 *     Copperline setup -- rules out a Run-redirection or hostfs quirk as
 *     the cause.
 *
 * NOT YET TRIED: running this exact minimal repro under Amiberry instead
 * of Copperline (would distinguish a Copperline-specific emulation/hostfs
 * gap around BOOPSI library loading from a real problem with this
 * particular window.class/Workbench install); using Copperline's own
 * debugger (break.add/regs.get/disasm via copperline-ctl) to see what's
 * actually executing at the machine level instead of trusting guest-side
 * output.
 */

#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

struct IntuitionBase *IntuitionBase;
struct Library *WindowBase;

static void Diag(const char *msg)
{
    Write(Output(), (APTR)msg, (LONG)strlen(msg));
}

int main(void)
{
    Diag("classact-app: start\n");

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 37);
    Diag(IntuitionBase != NULL ? "classact-app: intuition.library ok\n" : "classact-app: intuition.library FAILED\n");

    WindowBase = OpenLibrary((CONST_STRPTR)"window.class", 0);
    Diag(WindowBase != NULL ? "classact-app: window.class ok\n" : "classact-app: window.class FAILED\n");

    if (WindowBase != NULL) {
        CloseLibrary(WindowBase);
    }
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
    }

    Diag("classact-app: end\n");
    return 0;
}
