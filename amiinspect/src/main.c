/*
 * AmiInspect -- standalone Shell command that prints the frontmost (or
 * named) window's gadget tree: roles, labels, positions, states. No host
 * or server session required. See docs/implementation-plan.md phase 0.1.
 *
 * Template: WINDOW/K -- window title to inspect (substring match);
 * defaults to the active window when omitted. PICK/S,SCREEN/K -- issue
 * #65's interactive "pick mode": loops printing whichever window/gadget
 * is under the LIVE pointer, standing at the machine itself, no host or
 * server session at all (see PickLoop()'s own doc comment).
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/dos.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <stdio.h>
#include <string.h>

#include "intuition_model.h"

#define STR(s)  #s
#define XSTR(s) STR(s)

#ifndef VERSION
#define VERSION 0
#endif
#ifndef REVISION
#define REVISION 0
#endif

/* Standard AmigaDOS "$VER:" version cookie (RKRM: DOS, "The Version
 * Cookie") -- what the Shell's Version command scans a compiled
 * executable's segments for. `static volatile` (not `static const`) so
 * -O2 can't conclude the never-read array is dead and drop it: the
 * whole point is that ITS BYTES are read by an external tool, not by
 * this program. dd.mm.yyyy: the cookie's date field is three decimal
 * numbers, not a textual month -- see version.mk for VERSION/REVISION,
 * the single source of truth a release PR bumps. */
static volatile char version[] =
    "$VER: AmiInspect " XSTR(VERSION) "." XSTR(REVISION) " (05.08.2026)";

struct IntuitionBase *IntuitionBase = NULL;
/* Opened so intuition-model's walker can distinguish GadTools'
 * BUTTON_KIND from CHECKBOX_KIND (both are a plain GTYP_BOOLGADGET;
 * see walk.c's ClassifyBoolGadget). Optional: a missing gadtools.library
 * just means that discrimination degrades to "button", nothing fails. */
struct Library *GadToolsBase = NULL;

#define TEMPLATE "WINDOW/K,PICK/S,SCREEN/K"

struct InspectArgs {
    STRPTR windowTitle;
    LONG   pick;
    STRPTR screenSubstring;
};

static struct Window *FindWindow(CONST_STRPTR titleSubstring)
{
    struct Screen *screen = IntuitionBase->FirstScreen;
    struct Window *window;

    if (titleSubstring == NULL) {
        return IntuitionBase->ActiveWindow;
    }

    for (; screen != NULL; screen = screen->NextScreen) {
        for (window = screen->FirstWindow; window != NULL; window = window->NextWindow) {
            if (window->Title != NULL && strstr((const char *)window->Title, (const char *)titleSubstring) != NULL) {
                return window;
            }
        }
    }

    return NULL;
}

/* Mirrors AmipFindScreen() (server/src/action.c) exactly -- PICK mode's
 * own SCREEN= narrowing -- but re-implemented locally rather than
 * linking server/src/action.c into AmiInspect: that file pulls in
 * input.device/action-engine dependencies (AmipActionInit() etc.)
 * AmiInspect, a passive read-only inspector, has no other reason to
 * need. Default (NULL/empty substring): the frontmost screen. */
static struct Screen *FindScreen(CONST_STRPTR screenSubstring)
{
    struct Screen *screen;
    BOOL wantFilter = (screenSubstring != NULL && screenSubstring[0] != '\0');

    if (!wantFilter) {
        return IntuitionBase->FirstScreen;
    }
    for (screen = IntuitionBase->FirstScreen; screen != NULL; screen = screen->NextScreen) {
        if (screen->DefaultTitle != NULL
            && strstr((const char *)screen->DefaultTitle, (const char *)screenSubstring) != NULL) {
            return screen;
        }
    }
    return NULL;
}

/* Escapes '"' and '\' as '\"'/'\\', matching the wire's own escaping
 * (server/src/amipilotserver/main.c's EscapeQuotes()) exactly -- this
 * file's whole point is printing the same text shape the wire emits
 * (see its own header comment), so a gadget label/title/menu text
 * containing a literal '"' must render identically here too, not
 * break AmiInspect's own output while the wire handles it fine.
 * Returns a static scratch buffer; NOT reentrant, same as the wire's
 * own copy -- never call this twice for arguments to the same printf,
 * since argument evaluation order is unspecified and both calls
 * would race to fill the one buffer. Every call site below uses one
 * escape per printf for exactly this reason. */
static const char *EscapeQuotes(const char *in)
{
    static char buf[512];
    size_t o = 0;

    if (in == NULL) {
        buf[0] = '\0';
        return buf;
    }
    for (; *in != '\0' && o + 2 < sizeof(buf); in++) {
        if (*in == '"' || *in == '\\') {
            buf[o++] = '\\';
        }
        buf[o++] = *in;
    }
    buf[o] = '\0';
    return buf;
}

static void PrintModel(const AmipWindowModel *model)
{
    const AmipGadgetModel *gadget;

    printf("window \"%s\" screen=\"", model->title != NULL
                                          ? EscapeQuotes((const char *)model->title)
                                          : "(untitled)");
    printf("%s\" [%d,%d %dx%d]\n", EscapeQuotes((const char *)model->screenTitle),
           model->left, model->top, model->width, model->height);

    for (gadget = model->gadgets; gadget != NULL; gadget = gadget->next) {
        printf("  gadget id=%lu role=%s class=\"",
               (unsigned long)gadget->gadgetId, AmipRoleName(gadget->role));
        printf("%s\" label=\"", EscapeQuotes((const char *)gadget->className));
        printf("%s\"", EscapeQuotes((const char *)gadget->label));
        if (gadget->value != NULL) {
            printf(" value=\"%s\"", EscapeQuotes((const char *)gadget->value));
        }
        printf(" [%d,%d %dx%d]\n",
               gadget->left, gadget->top, gadget->width, gadget->height);
    }
}

/* Formats one PICK-mode result into `buf` -- the same "window ...
 * [gadget ...]" text PrintModel() would print for a single-gadget
 * TREE, but into a buffer rather than straight to stdout, so PickLoop()
 * can compare successive polls and only print when the identified
 * window/gadget actually CHANGES (not spam a fresh line every ~200ms
 * poll tick, most of which land on the exact same gadget the pointer
 * hasn't moved off yet). `window` NULL means "no window under the
 * pointer on this screen at all". */
static void FormatPickState(const AmipWindowModel *window, const AmipGadgetModel *gadget,
                             char *buf, size_t cap)
{
    if (window == NULL) {
        snprintf(buf, cap, "(no window under the pointer)\n");
        return;
    }

    snprintf(buf, cap, "window \"%s\" screen=\"",
              window->title != NULL ? EscapeQuotes((const char *)window->title) : "(untitled)");
    snprintf(buf + strlen(buf), cap - strlen(buf), "%s\" [%d,%d %dx%d]\n",
             EscapeQuotes((const char *)window->screenTitle),
             window->left, window->top, window->width, window->height);

    if (gadget == NULL) {
        snprintf(buf + strlen(buf), cap - strlen(buf), "  (no gadget under the pointer)\n");
        return;
    }

    snprintf(buf + strlen(buf), cap - strlen(buf), "  gadget id=%lu role=%s class=\"",
             (unsigned long)gadget->gadgetId, AmipRoleName(gadget->role));
    snprintf(buf + strlen(buf), cap - strlen(buf), "%s\" label=\"",
             EscapeQuotes((const char *)gadget->className));
    snprintf(buf + strlen(buf), cap - strlen(buf), "%s\"", EscapeQuotes((const char *)gadget->label));
    if (gadget->value != NULL) {
        snprintf(buf + strlen(buf), cap - strlen(buf), " value=\"");
        snprintf(buf + strlen(buf), cap - strlen(buf), "%s\"", EscapeQuotes((const char *)gadget->value));
    }
    snprintf(buf + strlen(buf), cap - strlen(buf), " [%d,%d %dx%d]\n",
             gadget->left, gadget->top, gadget->width, gadget->height);
}

/* Issue #65's interactive "pick mode": poll the live pointer position
 * (AmipReadPointerPosition()) against `screen`'s windows
 * (AmipHitTest()) roughly 5 times a second, printing the identified
 * window/gadget locator only when it actually changes since the last
 * poll -- point at a gadget on the real screen, see its exact locator
 * appear in the Shell, standing at the machine itself. Stops on
 * Ctrl-C (CheckSignal(), the standard Shell idiom -- checked explicitly
 * each iteration rather than relying on libnix's own default abort
 * handling, so this exits cleanly and predictably even if the pointer
 * hasn't moved in a while and no stdio call has happened recently to
 * give an implicit check a chance to fire).
 *
 * See intuition-model's own AmipReadPointerPosition() doc comment for
 * the live-confirmed MouseY correction it applies before this loop
 * ever sees a coordinate. */
static void PickLoop(struct Screen *screen)
{
    char lastBuf[512] = "";
    char curBuf[512];

    printf("AmiInspect: pick mode -- move the pointer, Ctrl-C to stop\n");

    for (;;) {
        AmipWindowModel *hitWindow = NULL;
        AmipGadgetModel *hitGadget = NULL;
        AmipWindowModel *models;
        WORD x, y;

        if (CheckSignal(SIGBREAKF_CTRL_C)) {
            printf("AmiInspect: pick mode stopped\n");
            return;
        }

        AmipReadPointerPosition(&x, &y);
        models = AmipHitTest(screen, x, y, &hitWindow, &hitGadget);
        FormatPickState(hitWindow, hitGadget, curBuf, sizeof(curBuf));
        if (strcmp(curBuf, lastBuf) != 0) {
            printf("%s", curBuf);
            strcpy(lastBuf, curBuf);
        }
        AmipFreeWindowModel(models);

        Delay(10); /* ~200ms at NTSC/PAL's 50-60Hz tick -- frequent
                    * enough to feel live, not a busy-poll */
    }
}

static void PrintMenuItemLine(const AmipMenuItemModel *item, const char *tag, int indent)
{
    printf("%*s%s num=%ld/%ld", indent, "", tag, (long)item->menuNum, (long)item->itemNum);
    if (item->subNum >= 0) {
        printf("/%ld", (long)item->subNum);
    }
    printf(" text=\"%s\"", EscapeQuotes((const char *)item->text));
    if (item->hasShortcut) {
        printf(" shortcut=%c", (char)item->shortcut);
    }
    printf(" checkit=%d checked=%d enabled=%d\n",
           item->checkit, item->checked, item->enabled);
}

static void PrintMenus(const AmipMenuModel *menus)
{
    const AmipMenuModel *menu;

    for (menu = menus; menu != NULL; menu = menu->next) {
        const AmipMenuItemModel *item;

        printf("menu num=%ld title=\"", (long)menu->menuNum);
        printf("%s\" enabled=%d\n", EscapeQuotes((const char *)menu->title), menu->enabled);

        for (item = menu->items; item != NULL; item = item->next) {
            const AmipMenuItemModel *sub;

            PrintMenuItemLine(item, "item", 2);
            for (sub = item->subItems; sub != NULL; sub = sub->next) {
                PrintMenuItemLine(sub, "subitem", 4);
            }
        }
    }
}

int main(void)
{
    struct RDArgs *rdargs;
    struct InspectArgs args;
    struct Window *target;
    AmipWindowModel *model;
    int rc = RETURN_OK;

    memset(&args, 0, sizeof(args));

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 37);
    if (IntuitionBase == NULL) {
        fprintf(stderr, "AmiInspect: requires intuition.library V37 (AmigaOS 2.04) or newer\n");
        return RETURN_FAIL;
    }

    /* Best-effort: a target window not using GadTools, or a system
     * without it, still gets inspected -- just without BUTTON_KIND vs
     * CHECKBOX_KIND discrimination. */
    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 37);

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, (LONG *)&args, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"AmiInspect");
        if (GadToolsBase != NULL) {
            CloseLibrary(GadToolsBase);
        }
        CloseLibrary((struct Library *)IntuitionBase);
        return RETURN_FAIL;
    }

    if (args.pick) {
        struct Screen *screen = FindScreen((CONST_STRPTR)args.screenSubstring);

        if (args.windowTitle != NULL) {
            fprintf(stderr, "AmiInspect: WINDOW is ignored with PICK -- pick mode "
                            "hit-tests the live pointer, it doesn't target one window\n");
        }
        if (screen == NULL) {
            fprintf(stderr, "AmiInspect: no matching screen found\n");
            rc = RETURN_WARN;
        } else {
            PickLoop(screen);
        }
    } else {
        target = FindWindow(args.windowTitle);
        if (target == NULL) {
            fprintf(stderr, "AmiInspect: no matching window found\n");
            rc = RETURN_WARN;
        } else {
            model = AmipWalkWindow(target);
            if (model == NULL) {
                fprintf(stderr, "AmiInspect: out of memory walking window\n");
                rc = RETURN_FAIL;
            } else {
                AmipMenuModel *menus = AmipWalkMenuStrip(target);

                PrintModel(model);
                AmipFreeWindowModel(model);
                if (menus != NULL) {
                    PrintMenus(menus);
                    AmipFreeMenuModel(menus);
                }
            }
        }
    }

    FreeArgs(rdargs);
    if (GadToolsBase != NULL) {
        CloseLibrary(GadToolsBase);
    }
    CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
