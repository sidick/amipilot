/*
 * AmiInspect -- standalone Shell command that prints the frontmost (or
 * named) window's gadget tree: roles, labels, positions, states. No host
 * or server session required. See docs/implementation-plan.md phase 0.1.
 *
 * Template: WINDOW/K -- window title to inspect (substring match);
 * defaults to the active window when omitted.
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

#define TEMPLATE "WINDOW/K"

struct InspectArgs {
    STRPTR windowTitle;
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

static void PrintModel(const AmipWindowModel *model)
{
    const AmipGadgetModel *gadget;

    printf("window \"%s\" screen=\"%s\" [%d,%d %dx%d]\n",
           model->title != NULL ? (const char *)model->title : "(untitled)",
           model->screenTitle != NULL ? (const char *)model->screenTitle : "",
           model->left, model->top, model->width, model->height);

    for (gadget = model->gadgets; gadget != NULL; gadget = gadget->next) {
        printf("  gadget id=%lu role=%s class=\"%s\" label=\"%s\"",
               (unsigned long)gadget->gadgetId,
               AmipRoleName(gadget->role),
               gadget->className != NULL ? (const char *)gadget->className : "",
               gadget->label != NULL ? (const char *)gadget->label : "");
        if (gadget->value != NULL) {
            printf(" value=\"%s\"", (const char *)gadget->value);
        }
        printf(" [%d,%d %dx%d]\n",
               gadget->left, gadget->top, gadget->width, gadget->height);
    }
}

static void PrintMenuItemLine(const AmipMenuItemModel *item, const char *tag, int indent)
{
    printf("%*s%s num=%ld/%ld", indent, "", tag, (long)item->menuNum, (long)item->itemNum);
    if (item->subNum >= 0) {
        printf("/%ld", (long)item->subNum);
    }
    printf(" text=\"%s\"", item->text != NULL ? (const char *)item->text : "");
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

        printf("menu num=%ld title=\"%s\" enabled=%d\n",
               (long)menu->menuNum, menu->title != NULL ? (const char *)menu->title : "",
               menu->enabled);

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

    FreeArgs(rdargs);
    if (GadToolsBase != NULL) {
        CloseLibrary(GadToolsBase);
    }
    CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
