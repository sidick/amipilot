/*
 * p96-app -- opens a real Picasso96/RTG CLUT screen with a known,
 * deterministic pen-ramp pattern painted on it, for the on-target
 * regression check of SCREENSHOT's P96 capture path (GitHub issue
 * #55). Unlike every other fixture in this directory, this one's
 * whole point is to exercise hardware/software that ISN'T guaranteed
 * to exist -- most machines running `make test-target` won't have
 * `[rtg]` configured in their own copperline.local.toml at all (it's
 * commented out in copperline.example.toml, opt-in), and even with a
 * real Picasso96/CGX board attached, the right monitor driver has to
 * be installed and bound before Picasso96API.library can offer any
 * display mode at all (a real gap this project hit and fixed live --
 * see the "P96/Picasso96 RTG on-target check" section of
 * tests/copperline/README.md).
 *
 * So this fixture never treats "no P96 mode available" as a crash or
 * an error: it writes an honest, greppable status line to
 * SRC:build/p96-status.txt (this project's established "read real
 * output back off the host filesystem via the SRC: hostfs mount,
 * don't parse stdout" pattern, since Run's own >file redirection was
 * confirmed unreliable for a launched-over-the-wire process's real
 * stdout during this feature's own development) and exits cleanly --
 * tests/copperline/screenshot-p96-test.py treats that as a genuine
 * SKIP, not a failure, exactly like the whole `make test-target` gate
 * itself skips cleanly (not falsely passes) when copperline.local.toml
 * is absent.
 *
 * When a P96 mode genuinely is available, opens a real screen with a
 * simple x%4 pen ramp (RectFill, not p96RectFill -- plain pen-based
 * rendering works identically on any CLUT screen, P96 or classic,
 * which is deliberately the point: SCREENSHOT's P96 path should
 * capture exactly what was actually drawn, verified pixel-for-pixel
 * by the host-side check) and a plain window, then waits for the
 * window to close.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/Picasso96.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>
#include <stdio.h>
#include <string.h>

struct IntuitionBase *IntuitionBase = NULL;
struct Library *P96Base = NULL;

#define P96_SCREEN_WIDTH  640
#define P96_SCREEN_HEIGHT 480
#define P96_SCREEN_DEPTH  8
#define RAMP_WIDTH        256
#define RAMP_TOP          40
#define RAMP_BOTTOM       200

static void WriteStatus(const char *msg)
{
    BPTR fh = Open((CONST_STRPTR)"SRC:build/p96-status.txt", MODE_NEWFILE);
    if (fh != 0) {
        Write(fh, (APTR)msg, (LONG)strlen(msg));
        Close(fh);
    }
}

int main(void)
{
    ULONG id;
    struct Screen *screen = NULL;
    struct Window *window = NULL;
    WORD pens[] = { ~0 };

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 37);
    if (IntuitionBase == NULL) {
        WriteStatus("SKIP intuition.library failed to open\n");
        return 0;
    }

    P96Base = OpenLibrary((CONST_STRPTR)"Picasso96API.library", 2);
    if (P96Base == NULL) {
        WriteStatus("SKIP Picasso96API.library not present\n");
        goto cleanup;
    }

    id = p96BestModeIDTags(
        P96BIDTAG_NominalWidth, P96_SCREEN_WIDTH,
        P96BIDTAG_NominalHeight, P96_SCREEN_HEIGHT,
        P96BIDTAG_Depth, P96_SCREEN_DEPTH,
        P96BIDTAG_FormatsAllowed, RGBFF_CLUT,
        TAG_DONE);
    if (id == (ULONG)INVALID_ID) {
        WriteStatus("SKIP no P96 CLUT mode available (no board, or no monitor driver bound)\n");
        goto cleanup;
    }

    screen = p96OpenScreenTags(
        P96SA_DisplayID, id,
        P96SA_Width, P96_SCREEN_WIDTH,
        P96SA_Height, P96_SCREEN_HEIGHT,
        P96SA_Depth, P96_SCREEN_DEPTH,
        P96SA_AutoScroll, TRUE,
        P96SA_Pens, (ULONG)pens,
        P96SA_Title, (ULONG)"AmiPilot P96 Fixture",
        TAG_DONE);
    if (screen == NULL) {
        WriteStatus("SKIP p96OpenScreenTags failed despite a valid mode ID\n");
        goto cleanup;
    }

    if ((ULONG)p96GetBitMapAttr(screen->RastPort.BitMap, P96BMA_ISP96) == 0) {
        /* Real screen opened, but it's NOT actually P96-backed -- the
         * honest thing is to say so and skip, not silently capture
         * the classic planar path and call that a P96 verification. */
        WriteStatus("SKIP screen opened but is not genuinely P96-backed\n");
        goto cleanup;
    }

    {
        WORD x;
        SetRGB32(&screen->ViewPort, 1, 0xFFFFFFFF, 0, 0);
        SetRGB32(&screen->ViewPort, 2, 0, 0xFFFFFFFF, 0);
        SetRGB32(&screen->ViewPort, 3, 0, 0, 0xFFFFFFFF);
        for (x = 0; x < RAMP_WIDTH; x++) {
            SetAPen(&screen->RastPort, (LONG)(x % 4));
            RectFill(&screen->RastPort, x, RAMP_TOP, x, RAMP_BOTTOM);
        }
    }

    window = OpenWindowTags(NULL,
                             WA_Left, 10, WA_Top, 10,
                             WA_Width, 300, WA_Height, 220,
                             WA_Title, (ULONG)"AmiPilot P96 Fixture Window",
                             WA_CloseGadget, TRUE,
                             WA_DragBar, TRUE,
                             WA_DepthGadget, TRUE,
                             WA_Activate, TRUE,
                             WA_IDCMP, IDCMP_CLOSEWINDOW,
                             WA_CustomScreen, (ULONG)screen,
                             TAG_DONE);
    if (window == NULL) {
        WriteStatus("SKIP OpenWindowTags failed\n");
        goto cleanup;
    }

    WriteStatus("READY\n");

    {
        BOOL done = FALSE;
        while (!done) {
            struct IntuiMessage *msg;
            WaitPort(window->UserPort);
            while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort)) != NULL) {
                if (msg->Class == IDCMP_CLOSEWINDOW) {
                    done = TRUE;
                }
                ReplyMsg((struct Message *)msg);
            }
        }
    }

cleanup:
    if (window != NULL) {
        CloseWindow(window);
    }
    if (screen != NULL) {
        p96CloseScreen(screen);
    }
    if (P96Base != NULL) {
        CloseLibrary(P96Base);
    }
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
    }
    return 0;
}
