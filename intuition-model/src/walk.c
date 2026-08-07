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
 * intuition/classusr.h + classes.h rather than guessing.
 *
 * CONFIRMED LIMIT: this only sees gadgets actually linked onto
 * window->FirstGadget. A window.class window attaches exactly one
 * gadget there -- its top-level layout.gadget object -- not that
 * layout's individual button/string/checkbox children. There is no
 * documented, public API to enumerate a layout.gadget's children on
 * classic AmigaOS 3.x (LM_ADDCHILD/LM_REMOVECHILD/LM_MODIFYCHILD are
 * OS4-only in the NDK; on 3.x, LAYOUT_AddChild only ever adds, never
 * lists back out). Seeing those children would require reading
 * layout.gadget's private, undocumented instance data -- the kind of
 * hack this project rules out. See docs/implementation-plan.md's
 * "Honest limits" section. */
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

/* GFLG_RELWIDTH/RELHEIGHT/RELRIGHT/RELBOTTOM (intuition.h): a classic,
 * pre-BOOPSI Intuition convention where the corresponding Width/Height/
 * LeftEdge/TopEdge field is stored as a NEGATIVE offset from the window's
 * own dimension, not an absolute value -- e.g. GFLG_RELWIDTH set with
 * Width=-8 means "window width minus 8", not "-8 pixels wide". Confirmed
 * against fixtures/classact-app: its root layout.gadget (attached
 * directly to window->FirstGadget, the only child a window.class window
 * exposes there -- see CLAUDE.md's "Confirmed limit") answers
 * GA_Width/GA_Height successfully via GetAttr but with exactly this
 * negative-relative encoding (GFLG_RELWIDTH|GFLG_RELHEIGHT both set),
 * NOT a broken/garbage value as first assumed. Applies identically
 * whether the numbers came from GetAttr or the classic fields -- this is
 * an Intuition-level convention, not a BOOPSI one, so a classic GadTools
 * gadget using GFLG_RELRIGHT etc would need the same resolution. */
static void ResolveGadgetGeometry(struct Window *window, UWORD flags,
                                   WORD *left, WORD *top, WORD *width, WORD *height)
{
    if (flags & GFLG_RELWIDTH) {
        *width = (WORD)(window->Width + *width);
    }
    if (flags & GFLG_RELHEIGHT) {
        *height = (WORD)(window->Height + *height);
    }
    if (flags & GFLG_RELRIGHT) {
        *left = (WORD)(window->Width + *left);
    }
    if (flags & GFLG_RELBOTTOM) {
        *top = (WORD)(window->Height + *top);
    }
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
            ULONG left = 0, top = 0, width = 0, height = 0;
            BOOL haveLeft, haveTop, haveWidth, haveHeight;

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

            /* Same story again for geometry: confirmed against
             * fixtures/classact-app that a BOOPSI gadgetclass descendant's
             * classic LeftEdge/TopEdge/Width/Height fields read back as
             * nonsensical (including negative Width/Height) -- GA_Left/
             * GA_Top/GA_Width/GA_Height (LONG, per gadgetclass.h) are the
             * real values, window-relative the same as the classic fields
             * (GA_Left's own doc: "relative to the left edge of the
             * window"), so no coordinate-convention change downstream.
             * Fall back to the classic fields only if a particular
             * attribute genuinely isn't answered -- degrade gracefully
             * rather than leaving the whole gadget unlocatable. */
            haveLeft   = GetAttr(GA_Left, gadget, &left);
            haveTop    = GetAttr(GA_Top, gadget, &top);
            haveWidth  = GetAttr(GA_Width, gadget, &width);
            haveHeight = GetAttr(GA_Height, gadget, &height);

            node->left   = haveLeft   ? (WORD)left   : gadget->LeftEdge;
            node->top    = haveTop    ? (WORD)top    : gadget->TopEdge;
            node->width  = haveWidth  ? (WORD)width  : gadget->Width;
            node->height = haveHeight ? (WORD)height : gadget->Height;
            node->state  = gadget->Flags;

            ResolveGadgetGeometry(window, gadget->Flags,
                                   &node->left, &node->top, &node->width, &node->height);
        } else {
            node->role = ClassifyGadget(gadget, window);
            node->className = NULL;
            /* GadTools populates gadget->GadgetText for external-label
             * placements (PLACETEXT_LEFT/RIGHT/ABOVE/BELOW) on kinds
             * like CHECKBOX_KIND/STRING_KIND (confirmed against
             * fixtures/gadtools-app's Enabled checkbox and Host string
             * gadget). **BUTTON_KIND is a documented exception**: a
             * PLACETEXT_IN button bakes its label into the rendered
             * imagery as expected, but a BUTTON_KIND with
             * PLACETEXT_RIGHT does too -- confirmed via AmiInspect
             * (ground truth, no server/wire involved) against a second
             * button added to fixtures/gadtools-app specifically to
             * test this (2026-08-07): gadget->GadgetText stayed NULL
             * regardless of the PLACETEXT_* flag chosen. So a button's
             * label is invisible to this tier under EVERY placement,
             * not just PLACETEXT_IN as originally assumed here -- a
             * real gap in what this tier can see (use a numeric
             * GA_ID-or-ROLE+INDEX locator for buttons, not LABEL=),
             * not a copy bug. PLACETEXT_LEFT/ABOVE/BELOW specifically
             * on BUTTON_KIND remain unconfirmed -- CHECKBOX_KIND/
             * STRING_KIND already prove this code path itself is
             * correct, so this wasn't chased further. */
            node->label = (gadget->GadgetText != NULL && gadget->GadgetText->IText != NULL)
                              ? CopyString(gadget->GadgetText->IText)
                              : NULL;

            /* A classic string gadget's live contents: SpecialInfo points
             * at its StringInfo, whose Buffer is the null-terminated
             * current text (intuition/sgadgets.h -- both STRING_KIND and
             * INTEGER_KIND go through the same structure). Copied out
             * under the same LockIBase() hold as everything else. */
            if ((gadget->GadgetType & GTYP_GTYPEMASK) == GTYP_STRGADGET
                && gadget->SpecialInfo != NULL) {
                struct StringInfo *si = (struct StringInfo *)gadget->SpecialInfo;
                if (si->Buffer != NULL) {
                    node->value = CopyString((CONST_STRPTR)si->Buffer);
                }
            }

            node->left = gadget->LeftEdge;
            node->top = gadget->TopEdge;
            node->width = gadget->Width;
            node->height = gadget->Height;
            node->state = gadget->Flags;

            ResolveGadgetGeometry(window, gadget->Flags,
                                   &node->left, &node->top, &node->width, &node->height);
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

static AmipWindowModel *WalkOneWindow(struct Window *window)
{
    AmipWindowModel *model = AllocVec(sizeof(AmipWindowModel), MEMF_PUBLIC | MEMF_CLEAR);
    if (model == NULL) {
        return NULL;
    }

    model->title = CopyString(window->Title);
    model->screenTitle = (window->WScreen != NULL)
                              ? CopyString(window->WScreen->DefaultTitle)
                              : NULL;
    model->left = window->LeftEdge;
    model->top = window->TopEdge;
    model->width = window->Width;
    model->height = window->Height;
    model->gadgets = WalkGadgetList(window->FirstGadget, window);

    return model;
}

/* TRUE if target is still genuinely linked into IntuitionBase's own
 * live screen/window lists -- confirmed via LockIBase(), not assumed
 * from the caller merely having a copy of the pointer. Duplicated
 * from server/src/action.c's own AmipIsWindowOpen() rather than
 * shared: this library has no dependency on the server (see this
 * file's header comment), so it can't call across that boundary, and
 * the walk needing this check for its own safety is a genuinely
 * separate concern from the action engine's locate-then-act one. */
static BOOL IsWindowStillOpen(struct Window *target)
{
    struct Screen *screen;
    struct Window *window;
    BOOL found = FALSE;

    LockIBase(0);
    for (screen = IntuitionBase->FirstScreen; screen != NULL && !found; screen = screen->NextScreen) {
        for (window = screen->FirstWindow; window != NULL; window = window->NextWindow) {
            if (window == target) {
                found = TRUE;
                break;
            }
        }
    }
    UnlockIBase(0);

    return found;
}

AmipWindowModel *AmipWalkWindow(struct Window *window)
{
    if (window == NULL) {
        return NULL;
    }

    /* Confirms `window` is still genuinely in Intuition's own live
     * list right before the walk starts -- replacing what used to be
     * here (a LockIBase()/UnlockIBase() pair wrapping nothing but a
     * pointer copy, which protected nothing at all: the caller
     * already had a valid pointer, so re-copying it under a lock
     * confirmed nothing new). This narrows, but does NOT eliminate,
     * the gap between this check and WalkOneWindow() finishing its
     * walk -- same honest, already-accepted limit AmipIsWindowOpen()
     * documents for the click path (server/include/action_engine.h):
     * the window could still close in the instant after this check
     * returns. The real fix ("action-scoped expectations", atomic
     * with respect to the target closing) is planned but not built.
     *
     * WalkOneWindow()'s own work (GetAttr()/OCLASS() dispatch via
     * WalkGadgetList(), CopyString()'s AllocVec()) genuinely cannot
     * happen while LockIBase() is held -- its own autodoc says so
     * explicitly: "Do not call any Intuition functions (nor any
     * graphics, layers, dos, or other high-level system function)
     * while holding this lock." So holding the lock across the whole
     * walk, which would look like a stronger guarantee, is not an
     * available option here, not just an unchosen one. */
    if (!IsWindowStillOpen(window)) {
        return NULL;
    }

    return WalkOneWindow(window);
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
            if (gadget->value != NULL) {
                FreeVec(gadget->value);
            }
            FreeVec(gadget);
            gadget = nextGadget;
        }

        if (model->title != NULL) {
            FreeVec(model->title);
        }
        if (model->screenTitle != NULL) {
            FreeVec(model->screenTitle);
        }
        FreeVec(model);
        model = nextWindow;
    }
}

/* Fills one already-allocated node from `item`. Doesn't touch
 * next/subNum -- the two callers below (top-level vs. submenu) set
 * those according to which chain they're walking. */
static void FillMenuItemNode(AmipMenuItemModel *node, struct MenuItem *item,
                              LONG menuNum, LONG itemNum)
{
    /* ItemFill points to an Image, an IntuiText, or NULL depending on
     * the ITEMTEXT flag (intuition.h's own comment on the field) --
     * only read IText when ITEMTEXT says it's really an IntuiText. A
     * graphical (IM_ITEM/IM_SUB) item leaves text NULL, same
     * "captured what it structurally is, not what it can't be" stance
     * as walk.c's ClassifyByClassID for an unrecognised BOOPSI class. */
    if ((item->Flags & ITEMTEXT) && item->ItemFill != NULL) {
        struct IntuiText *itext = (struct IntuiText *)item->ItemFill;
        node->text = CopyString(itext->IText);
    } else {
        node->text = NULL;
    }

    node->enabled = (item->Flags & ITEMENABLED) ? TRUE : FALSE;
    node->checkit = (item->Flags & CHECKIT) ? TRUE : FALSE;
    node->checked = (item->Flags & CHECKED) ? TRUE : FALSE;

    /* Command is "only if appliprog sets the COMMSEQ flag" (intuition.h)
     * -- a zero Command byte with COMMSEQ set isn't a real shortcut
     * either, so require both. */
    if ((item->Flags & COMMSEQ) && item->Command != 0) {
        node->hasShortcut = TRUE;
        node->shortcut = (UBYTE)item->Command;
    } else {
        node->hasShortcut = FALSE;
        node->shortcut = 0;
    }

    node->menuNum = menuNum;
    node->itemNum = itemNum;
}

/* Walks a submenu's NextItem chain (item->SubItem, one level deep --
 * classic Intuition menus don't nest a submenu's own SubItem field
 * into a second pull-right level, so this never recurses). subNum is
 * this chain's own 0-based index, matching what Intuition's
 * IDCMP_MENUPICK SUBNUM() macro would report for the same entry. */
static AmipMenuItemModel *WalkSubItemChain(struct MenuItem *item, LONG menuNum, LONG itemNum)
{
    AmipMenuItemModel *head = NULL;
    AmipMenuItemModel *tail = NULL;
    LONG subNum;

    for (subNum = 0; item != NULL; item = item->NextItem, subNum++) {
        AmipMenuItemModel *node = AllocVec(sizeof(AmipMenuItemModel), MEMF_PUBLIC | MEMF_CLEAR);
        if (node == NULL) {
            break;
        }
        FillMenuItemNode(node, item, menuNum, itemNum);
        node->subNum = subNum;
        node->subItems = NULL;

        if (tail == NULL) {
            head = tail = node;
        } else {
            tail->next = node;
            tail = node;
        }
    }

    return head;
}

/* Walks a menu's top-level FirstItem chain, building one node per
 * item and -- for any item with a SubItem pointer -- its one level of
 * pull-right children via WalkSubItemChain(). */
static AmipMenuItemModel *WalkTopItemChain(struct MenuItem *item, LONG menuNum)
{
    AmipMenuItemModel *head = NULL;
    AmipMenuItemModel *tail = NULL;
    LONG itemNum;

    for (itemNum = 0; item != NULL; item = item->NextItem, itemNum++) {
        AmipMenuItemModel *node = AllocVec(sizeof(AmipMenuItemModel), MEMF_PUBLIC | MEMF_CLEAR);
        if (node == NULL) {
            break;
        }
        FillMenuItemNode(node, item, menuNum, itemNum);
        node->subNum = -1;
        node->subItems = (item->SubItem != NULL)
                              ? WalkSubItemChain(item->SubItem, menuNum, itemNum)
                              : NULL;

        if (tail == NULL) {
            head = tail = node;
        } else {
            tail->next = node;
            tail = node;
        }
    }

    return head;
}

static AmipMenuModel *WalkMenuList(struct Menu *menu)
{
    AmipMenuModel *head = NULL;
    AmipMenuModel *tail = NULL;
    LONG menuNum;

    for (menuNum = 0; menu != NULL; menu = menu->NextMenu, menuNum++) {
        AmipMenuModel *node = AllocVec(sizeof(AmipMenuModel), MEMF_PUBLIC | MEMF_CLEAR);
        if (node == NULL) {
            break;
        }

        node->title = CopyString(menu->MenuName);
        node->enabled = (menu->Flags & MENUENABLED) ? TRUE : FALSE;
        node->menuNum = menuNum;
        node->items = WalkTopItemChain(menu->FirstItem, menuNum);

        if (tail == NULL) {
            head = tail = node;
        } else {
            tail->next = node;
            tail = node;
        }
    }

    return head;
}

AmipMenuModel *AmipWalkMenuStrip(struct Window *window)
{
    struct Menu *strip;

    if (window == NULL) {
        return NULL;
    }

    /* Same IsWindowStillOpen() re-check AmipWalkWindow() uses (see its
     * own comment for the full rationale) -- window->MenuStrip is
     * only safe to dereference at all if `window` is still genuinely
     * live. Two separate LockIBase()/UnlockIBase() pairs, not one
     * held across both calls: IsWindowStillOpen() takes its own lock
     * internally, and LockIBase()'s own autodoc gives no indication
     * it's safe to nest. */
    if (!IsWindowStillOpen(window)) {
        return NULL;
    }
    LockIBase(0);
    strip = window->MenuStrip;
    UnlockIBase(0);

    if (strip == NULL) {
        return NULL;
    }

    return WalkMenuList(strip);
}

void AmipFreeMenuModel(AmipMenuModel *model)
{
    while (model != NULL) {
        AmipMenuModel *nextMenu = model->next;
        AmipMenuItemModel *item = model->items;

        while (item != NULL) {
            AmipMenuItemModel *nextItem = item->next;
            AmipMenuItemModel *sub = item->subItems;

            while (sub != NULL) {
                AmipMenuItemModel *nextSub = sub->next;
                if (sub->text != NULL) {
                    FreeVec(sub->text);
                }
                FreeVec(sub);
                sub = nextSub;
            }

            if (item->text != NULL) {
                FreeVec(item->text);
            }
            FreeVec(item);
            item = nextItem;
        }

        if (model->title != NULL) {
            FreeVec(model->title);
        }
        FreeVec(model);
        model = nextMenu;
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

/* ASCII case-insensitive full-string compare -- same small helper
 * arexx_cmd.c's own ci_streq keeps as a separate copy per-file rather
 * than sharing one (this library has no dependency on the server, see
 * this file's own header comment). */
static int RoleNameEq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

AmipRole AmipRoleFromName(const char *name)
{
    static const struct { const char *name; AmipRole role; } table[] = {
        { "button",       AMIP_ROLE_BUTTON },
        { "string",       AMIP_ROLE_STRING },
        { "integer",      AMIP_ROLE_INTEGER },
        { "checkbox",     AMIP_ROLE_CHECKBOX },
        { "radio_button", AMIP_ROLE_RADIO_BUTTON },
        { "cycle",        AMIP_ROLE_CYCLE },
        { "slider",       AMIP_ROLE_SLIDER },
        { "scroller",     AMIP_ROLE_SCROLLER },
        { "listview",     AMIP_ROLE_LISTVIEW },
        { "listbrowser",  AMIP_ROLE_LISTBROWSER },
        { "text",         AMIP_ROLE_TEXT },
        { "menu",         AMIP_ROLE_MENU },
        { "menu_item",    AMIP_ROLE_MENU_ITEM },
        { "custom",       AMIP_ROLE_CUSTOM },
    };
    size_t i;

    if (name == NULL) {
        return AMIP_ROLE_UNKNOWN;
    }
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (RoleNameEq(name, table[i].name)) {
            return table[i].role;
        }
    }
    return AMIP_ROLE_UNKNOWN;
}
