/*
 * Window/gadget walker. This is the 0.1 skeleton: it establishes the
 * LockIBase discipline and the copy-out model, with role classification
 * to be filled in incrementally (GadTools kinds first, then BOOPSI/
 * ReAction class readers). See docs/implementation-plan.md phase 0.1.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <string.h>

#include "intuition_model.h"

extern struct IntuitionBase *IntuitionBase;
/* proto/gadtools.h declares this extern; the calling program (AmiInspect,
 * later the server) owns opening it into gadtools.library and closing it
 * -- see main.c. Left unopened (NULL), BUTTON_KIND vs CHECKBOX_KIND
 * discrimination below just degrades to the old BUTTON-only guess rather
 * than failing, since a foreign target window may not need it and we
 * don't want the walker itself to depend on gadtools.library. */

static STRPTR CopyString(CONST_STRPTR src)
{
    ULONG len;
    STRPTR dst;

    if (src == NULL) {
        return NULL;
    }

    len = strlen((const char *)src) + 1;
    dst = AllocVec(len, MEMF_PUBLIC);
    if (dst != NULL) {
        CopyMem((APTR)src, dst, len);
    }
    return dst;
}

/* GadTools' BUTTON_KIND and CHECKBOX_KIND both create a plain
 * GTYP_BOOLGADGET -- nothing in the bare struct Gadget distinguishes
 * them post-creation (confirmed against fixtures/gadtools-app under
 * real Workbench 3.2.3). GT_GetGadgetAttrsA's documented contract
 * (gadtools.doc) is the discriminator instead: it only fills in
 * attributes that apply to the gadget's actual kind, and returns the
 * count it filled in. Asking a plain button for CHECKBOX_KIND's
 * GTCB_Checked is a safe, documented no-op (numProcessed stays 0) --
 * this is officially sanctioned kind probing, not a guessed heuristic. */
static AmipRole ClassifyBoolGadget(struct Gadget *gadget, struct Window *window)
{
    LONG checked = FALSE;
    LONG numProcessed;

    if (GadToolsBase == NULL || window == NULL) {
        return AMIP_ROLE_BUTTON;
    }

    numProcessed = GT_GetGadgetAttrs(gadget, window, NULL, GTCB_Checked, (ULONG)&checked, TAG_DONE);
    if (numProcessed >= 1) {
        return AMIP_ROLE_CHECKBOX;
    }

    return AMIP_ROLE_BUTTON;
}

static AmipRole ClassifyGadget(struct Gadget *gadget, struct Window *window)
{
    if (gadget == NULL) {
        return AMIP_ROLE_UNKNOWN;
    }

    switch (gadget->GadgetType & GTYP_GTYPEMASK) {
        case GTYP_BOOLGADGET:
            return ClassifyBoolGadget(gadget, window);
        case GTYP_STRGADGET:
            return AMIP_ROLE_STRING;
        case GTYP_PROPGADGET:
            return AMIP_ROLE_SLIDER;
        default:
            return AMIP_ROLE_UNKNOWN;
    }
}

/* BOOPSI/ReAction gadgets (GTYP_CUSTOMGADGET) are true class instances:
 * NewObject() allocates a private struct _Object header (a class pointer
 * plus a MinNode) immediately before the pointer it returns, precisely
 * so that "Gadget objects are Gadget pointers" (intuition/classes.h's
 * own comment). OCLASS() reads that header back -- Hyperion documents it
 * as "white box" access "for class implementors", but it is a stable,
 * shipped NDK mechanism, not an undocumented hack, and it is the only
 * way to identify a live BOOPSI object's class from outside. This is
 * unsafe to call on classic/GadTools gadgets (GTYP_BOOLGADGET etc.):
 * they carry no _Object header, so the caller must have already
 * confirmed GTYP_CUSTOMGADGET first. cl_ID is the class's registration
 * name ("button.gadget", "window.class", ...), confirmed by reading
 * intuition/classusr.h + classes.h rather than guessing. */
static AmipRole ClassifyByClassID(CONST_STRPTR classID)
{
    const char *id = (const char *)classID;

    if (id == NULL) {
        return AMIP_ROLE_CUSTOM;
    }
    if (strcmp(id, "button.gadget") == 0) {
        return AMIP_ROLE_BUTTON;
    }
    if (strcmp(id, "checkbox.gadget") == 0) {
        return AMIP_ROLE_CHECKBOX;
    }
    if (strcmp(id, "string.gadget") == 0 || strcmp(id, "getstring.gadget") == 0) {
        return AMIP_ROLE_STRING;
    }
    if (strcmp(id, "integer.gadget") == 0) {
        return AMIP_ROLE_INTEGER;
    }
    if (strcmp(id, "radiobutton.gadget") == 0) {
        return AMIP_ROLE_RADIO_BUTTON;
    }
    if (strcmp(id, "chooser.gadget") == 0) {
        return AMIP_ROLE_CYCLE;
    }
    if (strcmp(id, "scroller.gadget") == 0) {
        return AMIP_ROLE_SCROLLER;
    }
    if (strcmp(id, "slider.gadget") == 0) {
        return AMIP_ROLE_SLIDER;
    }
    if (strcmp(id, "listbrowser.gadget") == 0) {
        return AMIP_ROLE_LISTBROWSER;
    }

    /* Genuinely unrecognised class (a third-party subclass, or a
     * ReAction class this tier hasn't been taught yet) -- className is
     * still captured for the caller, so the tree says exactly what it
     * is even when the role can't be mapped. */
    return AMIP_ROLE_CUSTOM;
}

static AmipGadgetModel *WalkGadgetList(struct Gadget *gadget, struct Window *window)
{
    AmipGadgetModel *head = NULL;
    AmipGadgetModel *tail = NULL;

    for (; gadget != NULL; gadget = gadget->NextGadget) {
        AmipGadgetModel *node = AllocVec(sizeof(AmipGadgetModel), MEMF_PUBLIC | MEMF_CLEAR);
        if (node == NULL) {
            break;
        }

        node->gadgetId = gadget->GadgetID;

        /* GTYP_CUSTOMGADGET (0x0005) is a VALUE inside the 3-bit
         * GTYP_GTYPEMASK field (0x0007), not a standalone flag bit --
         * mask first, then compare equality. A bare `& GTYP_CUSTOMGADGET`
         * also matches GTYP_BOOLGADGET (0x0001, since 0x0001 & 0x0005 ==
         * 0x0001), which routed classic GadTools boolean gadgets into
         * OCLASS() below and crashed: they carry no _Object header, so
         * reading "before" them is a wild pointer dereference. Confirmed
         * via Copperline's debugger (disasm at the crash PC showed the
         * CPU chewing through zeroed memory -- a classic wild-jump
         * signature) against fixtures/gadtools-app. */
        if ((gadget->GadgetType & GTYP_GTYPEMASK) == GTYP_CUSTOMGADGET) {
            Class *cls = OCLASS(gadget);
            CONST_STRPTR classID = (cls != NULL) ? cls->cl_ID : NULL;
            ULONG text = 0;
            ULONG id = 0;

            node->role = ClassifyByClassID(classID);
            node->className = CopyString(classID);

            /* Same story as GA_Text: confirmed against fixtures/classact-
             * app that BOOPSI gadgetclass descendants don't mirror GA_ID
             * into the classic gadget->GadgetID field either -- every
             * child of a layout.gadget read back as id=0 until this was
             * read via GetAttr instead. */
            if (GetAttr(GA_ID, gadget, &id)) {
                node->gadgetId = (ULONG)id;
            }

            /* GA_Text (a plain STRPTR), not gadget->GadgetText (an
             * IntuiText*): confirmed against fixtures/classact-app that
             * BOOPSI gadgetclass descendants only answer their label
             * through GetAttr, leaving GadgetText NULL. GetAttr is only
             * safe here because we've already confirmed this gadget
             * carries a real _Object/class header (GTYP_CUSTOMGADGET);
             * calling it on a classic gadget below would read garbage. */
            if (GetAttr(GA_Text, gadget, &text) && text != 0) {
                node->label = CopyString((CONST_STRPTR)text);
            } else {
                node->label = NULL;
            }
        } else {
            node->role = ClassifyGadget(gadget, window);
            node->className = NULL;
            /* GadTools only populates gadget->GadgetText for
             * PLACETEXT_LEFT/RIGHT/ABOVE/BELOW; a PLACETEXT_IN button
             * (confirmed against fixtures/gadtools-app) bakes its label
             * into the rendered imagery instead, so this reads empty for
             * that layout -- a real gap in what this tier can see, not a
             * copy bug. */
            node->label = (gadget->GadgetText != NULL && gadget->GadgetText->IText != NULL)
                              ? CopyString(gadget->GadgetText->IText)
                              : NULL;
        }

        node->left = gadget->LeftEdge;
        node->top = gadget->TopEdge;
        node->width = gadget->Width;
        node->height = gadget->Height;
        node->state = gadget->Flags;

        if (tail == NULL) {
            head = tail = node;
        } else {
            tail->next = node;
            tail = node;
        }
    }

    return head;
}

static AmipWindowModel *WalkOneWindow(struct Window *window)
{
    AmipWindowModel *model = AllocVec(sizeof(AmipWindowModel), MEMF_PUBLIC | MEMF_CLEAR);
    if (model == NULL) {
        return NULL;
    }

    model->title = CopyString(window->Title);
    model->left = window->LeftEdge;
    model->top = window->TopEdge;
    model->width = window->Width;
    model->height = window->Height;
    model->gadgets = WalkGadgetList(window->FirstGadget, window);

    return model;
}

AmipWindowModel *AmipWalkWindow(struct Window *window)
{
    AmipWindowModel *model;

    if (window == NULL) {
        return NULL;
    }

    /* Brief hold: no allocation happens while the lock is held. We take
     * a private snapshot of just the pointer we need before releasing. */
    LockIBase(0);
    struct Window *snapshot = window;
    UnlockIBase(0);

    model = WalkOneWindow(snapshot);
    return model;
}

AmipWindowModel *AmipWalkScreen(struct Screen *screen)
{
    AmipWindowModel *head = NULL;
    AmipWindowModel *tail = NULL;
    struct Window *window;

    LockIBase(0);
    window = (screen != NULL) ? screen->FirstWindow : NULL;
    UnlockIBase(0);

    /* TODO(0.1): default-public-screen enumeration when screen == NULL,
     * and re-take brief locks per window traversal step rather than
     * assuming FirstWindow/NextWindow are stable across the walk --
     * placeholder for the real brief-hold-per-step discipline described
     * in docs/implementation-plan.md. */
    for (; window != NULL; window = window->NextWindow) {
        AmipWindowModel *node = WalkOneWindow(window);
        if (node == NULL) {
            break;
        }
        if (tail == NULL) {
            head = tail = node;
        } else {
            tail->next = node;
            tail = node;
        }
    }

    return head;
}

void AmipFreeWindowModel(AmipWindowModel *model)
{
    while (model != NULL) {
        AmipWindowModel *nextWindow = model->next;
        AmipGadgetModel *gadget = model->gadgets;

        while (gadget != NULL) {
            AmipGadgetModel *nextGadget = gadget->next;
            if (gadget->label != NULL) {
                FreeVec(gadget->label);
            }
            if (gadget->className != NULL) {
                FreeVec(gadget->className);
            }
            FreeVec(gadget);
            gadget = nextGadget;
        }

        if (model->title != NULL) {
            FreeVec(model->title);
        }
        FreeVec(model);
        model = nextWindow;
    }
}

const char *AmipRoleName(AmipRole role)
{
    switch (role) {
        case AMIP_ROLE_BUTTON:        return "button";
        case AMIP_ROLE_STRING:        return "string";
        case AMIP_ROLE_INTEGER:       return "integer";
        case AMIP_ROLE_CHECKBOX:      return "checkbox";
        case AMIP_ROLE_RADIO_BUTTON:  return "radio_button";
        case AMIP_ROLE_CYCLE:         return "cycle";
        case AMIP_ROLE_SLIDER:        return "slider";
        case AMIP_ROLE_SCROLLER:      return "scroller";
        case AMIP_ROLE_LISTVIEW:      return "listview";
        case AMIP_ROLE_LISTBROWSER:   return "listbrowser";
        case AMIP_ROLE_TEXT:          return "text";
        case AMIP_ROLE_MENU:          return "menu";
        case AMIP_ROLE_MENU_ITEM:     return "menu_item";
        case AMIP_ROLE_CUSTOM:        return "custom";
        default:                      return "unknown";
    }
}
