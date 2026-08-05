/*
 * Window/gadget walker. This is the 0.1 skeleton: it establishes the
 * LockIBase discipline and the copy-out model, with role classification
 * to be filled in incrementally (GadTools kinds first, then BOOPSI/
 * ReAction class readers). See docs/implementation-plan.md phase 0.1.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <string.h>

#include "intuition_model.h"

extern struct IntuitionBase *IntuitionBase;

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

AmipRole AmipClassifyGadget(struct Gadget *gadget)
{
    if (gadget == NULL) {
        return AMIP_ROLE_UNKNOWN;
    }

    switch (gadget->GadgetType & GTYP_GTYPEMASK) {
        case GTYP_BOOLGADGET:
            /* TODO(confirmed against fixtures/gadtools-app under real
             * Workbench 3.2.3): GadTools' BUTTON_KIND and CHECKBOX_KIND
             * both create a plain GTYP_BOOLGADGET -- nothing in the bare
             * struct Gadget distinguishes them post-creation, so a real
             * checkbox currently misclassifies as AMIP_ROLE_BUTTON. Needs
             * research into recovering the GadTools "kind" after the
             * fact (there is no public field for it) before this can be
             * split into BUTTON vs CHECKBOX correctly. */
            return AMIP_ROLE_BUTTON;
        case GTYP_STRGADGET:
            return AMIP_ROLE_STRING;
        case GTYP_PROPGADGET:
            return AMIP_ROLE_SLIDER;
        default:
            break;
    }

    /* BOOPSI/custom-class gadgets need their class name read via
     * OM_CLASS / GA_ID GetAttr calls -- deferred to the class-reader
     * work in phase 0.1 once the plain GadTools path is proven. */
    if (gadget->GadgetType & GTYP_CUSTOMGADGET) {
        return AMIP_ROLE_CUSTOM;
    }

    return AMIP_ROLE_UNKNOWN;
}

static AmipGadgetModel *WalkGadgetList(struct Gadget *gadget)
{
    AmipGadgetModel *head = NULL;
    AmipGadgetModel *tail = NULL;

    for (; gadget != NULL; gadget = gadget->NextGadget) {
        AmipGadgetModel *node = AllocVec(sizeof(AmipGadgetModel), MEMF_PUBLIC | MEMF_CLEAR);
        if (node == NULL) {
            break;
        }

        node->gadgetId = gadget->GadgetID;
        node->role = AmipClassifyGadget(gadget);
        /* TODO(confirmed against fixtures/gadtools-app): this reads
         * gadget->GadgetText, which GadTools only populates for
         * PLACETEXT_LEFT/RIGHT/ABOVE/BELOW. A PLACETEXT_IN button (the
         * common case for BUTTON_KIND) bakes its label into the
         * rendered imagery instead, so GadgetText stays NULL and the
         * label reads as empty here -- not a copy bug, a real gap in
         * what this tier can see for that layout. */
        node->label = (gadget->GadgetText != NULL && gadget->GadgetText->IText != NULL)
                          ? CopyString(gadget->GadgetText->IText)
                          : NULL;
        node->className = NULL; /* filled in once class readers exist */
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
    model->gadgets = WalkGadgetList(window->FirstGadget);

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
