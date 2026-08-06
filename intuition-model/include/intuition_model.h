#ifndef AMIPILOT_INTUITION_MODEL_H
#define AMIPILOT_INTUITION_MODEL_H

/*
 * intuition-model: read-only walker for Intuition windows and gadgets.
 *
 * All structure walking happens under a brief LockIBase() hold and copies
 * data out into the model below before releasing the lock. Nothing here
 * hands out live Intuition pointers, and nothing here patches anything
 * (no SetFunction) -- see docs/implementation-plan.md.
 *
 * This library has no dependency on the AmiPilot server: it is usable
 * standalone by any tool that needs to read GUI structure (AmiInspect is
 * its first consumer).
 */

#include <exec/types.h>

/* AT-SPI-style role classification for a gadget, independent of which
 * toolkit (plain Intuition/GadTools, BOOPSI/ReAction, MUI) produced it. */
typedef enum {
    AMIP_ROLE_UNKNOWN = 0,
    AMIP_ROLE_BUTTON,
    AMIP_ROLE_STRING,
    AMIP_ROLE_INTEGER,
    AMIP_ROLE_CHECKBOX,
    AMIP_ROLE_RADIO_BUTTON,
    AMIP_ROLE_CYCLE,
    AMIP_ROLE_SLIDER,
    AMIP_ROLE_SCROLLER,
    AMIP_ROLE_LISTVIEW,
    AMIP_ROLE_LISTBROWSER,
    AMIP_ROLE_TEXT,
    AMIP_ROLE_MENU,
    AMIP_ROLE_MENU_ITEM,
    AMIP_ROLE_CUSTOM /* recognised structurally, but not classifiable */
} AmipRole;

typedef struct AmipGadgetModel {
    struct AmipGadgetModel *next;
    ULONG   gadgetId;      /* GA_ID, 0 if unset */
    AmipRole role;
    STRPTR  label;         /* copied out, caller-owned; NULL if none */
    STRPTR  className;     /* e.g. "button.gadget", NULL for plain GadTools */
    STRPTR  value;         /* current contents for string/integer gadgets
                            * (StringInfo->Buffer, copied out); NULL for
                            * roles with no textual value */
    WORD    left, top, width, height; /* resolved at walk time */
    ULONG   state;         /* opaque bitfield, meaning is role-specific */
} AmipGadgetModel;

typedef struct AmipWindowModel {
    struct AmipWindowModel *next;
    STRPTR  title;          /* copied out, caller-owned; NULL if untitled */
    WORD    left, top, width, height;
    AmipGadgetModel *gadgets; /* linked list, walk order */
} AmipWindowModel;

/* One menu item (a plain textual item, or -- via `subItems` -- a
 * pull-right submenu one level deep; classic Intuition menus don't
 * nest further, see intuition/intuition.h's own struct MenuItem).
 * `menuNum`/`itemNum`/`subNum` are this item's position in its own
 * chain (NextMenu/NextItem), 0-based -- identical to what Intuition
 * itself reports in IDCMP_MENUPICK's Code field via the MENUNUM()/
 * ITEMNUM()/SUBNUM() macros, and what a MENUPICK verb addresses a
 * pick by. `subNum` is -1 for a top-level item. */
typedef struct AmipMenuItemModel {
    struct AmipMenuItemModel *next;     /* next sibling at this level */
    struct AmipMenuItemModel *subItems; /* one level of submenu, NULL if none */
    STRPTR text;      /* copied out; NULL for a graphical (non-ITEMTEXT) item
                        * or a separator bar */
    BOOL   enabled;   /* ITEMENABLED */
    BOOL   checkit;   /* CHECKIT -- this item is a checkmark toggle */
    BOOL   checked;   /* CHECKED; only meaningful when checkit is TRUE */
    BOOL   hasShortcut; /* COMMSEQ, and a non-zero Command byte */
    UBYTE  shortcut;  /* the Command byte (a plain ASCII char); valid only
                        * when hasShortcut is TRUE */
    LONG   menuNum, itemNum, subNum;
} AmipMenuItemModel;

/* One top-level pulldown menu (a window's MenuStrip is a chain of
 * these via NextMenu). */
typedef struct AmipMenuModel {
    struct AmipMenuModel *next;
    STRPTR title;     /* copied out; NULL if unset */
    BOOL   enabled;   /* MENUENABLED */
    LONG   menuNum;
    AmipMenuItemModel *items;
} AmipMenuModel;

/* Walks window->MenuStrip, copying out a full model under a brief
 * LockIBase() hold -- same discipline as AmipWalkWindow. Returns NULL
 * if the window has no menu strip at all (not an error: most windows
 * don't) or on allocation failure; callers can't distinguish the two
 * from the return value alone, matching AmipWalkWindow's own
 * NULL-on-either convention. */
AmipMenuModel *AmipWalkMenuStrip(struct Window *window);

void AmipFreeMenuModel(AmipMenuModel *model);

/* Walks all windows on the given screen (NULL = default public screen),
 * copying out a full model. Returns NULL on allocation failure. The
 * lock on IntuitionBase is held only for the duration of the walk, never
 * across allocation. Caller must release with AmipFreeWindowModel(). */
AmipWindowModel *AmipWalkScreen(struct Screen *screen);

/* Walks a single known window. */
AmipWindowModel *AmipWalkWindow(struct Window *window);

void AmipFreeWindowModel(AmipWindowModel *model);

const char *AmipRoleName(AmipRole role);

#endif /* AMIPILOT_INTUITION_MODEL_H */
