/*
 * reaction-classes-app -- exercises the BOOPSI/ReAction gadget classes
 * intuition-model's walker didn't role-classify before issue #69
 * (clicktab.gadget, colorwheel.gadget, datebrowser.gadget,
 * fuelgauge.gadget, getcolor.gadget, getfile.gadget, getfont.gadget,
 * getscreenmode.gadget, gradientslider.gadget, palette.gadget,
 * sketchboard.gadget, speedbar.gadget, texteditor.gadget).
 *
 * Deliberately does NOT use window.class/layout.gadget the way
 * fixtures/classact-app does -- a layout.gadget's own children are
 * permanently invisible to structural walking (issue #49's confirmed
 * limit), which would defeat this fixture's whole purpose. Instead,
 * each object is created directly via its own class's GetClass()
 * function with explicit GA_Left/GA_Top/GA_Width/GA_Height/GA_ID tags
 * (the same absolute-placement convention GadTools' CreateGadget()
 * uses internally), then linked into a PLAIN classic window's gadget
 * chain via AddGList()/RefreshGList() -- BOOPSI or not, any struct
 * Gadget* is Intuition-uniform (intuition/classes.h: "Gadget objects
 * are Gadget pointers"), the same property this project's own walker
 * already relies on.
 *
 * Every class here still needs OpenLibrary() to load its .gadget file
 * before ANY construction form works, including the two
 * (colorwheel.gadget, gradientslider.gadget) that register a public
 * class name (reaction/reaction_macros.h's own ColorWheelObject/
 * GradientObject macros construct via NewObject(NULL, "name", ...),
 * not a GetClass() call) -- confirmed against classact-app's own
 * precedent, which OpenLibrary()s button.gadget even though it then
 * constructs it by name too.
 *
 * texteditor.gadget is a real, documented NDK quirk: its own pragma
 * header (pragma/texteditor_lib.h) states outright "The library base
 * name is TextFieldBase and not TextEditorBase" even though the
 * exported function is TEXTEDITOR_GetClass() -- easy to get wrong
 * guessing from the class name alone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <intuition/classes.h>
#include <intuition/gadgetclass.h>
#include <gadgets/clicktab.h>
#include <gadgets/colorwheel.h>
#include <gadgets/datebrowser.h>
#include <gadgets/fuelgauge.h>
#include <gadgets/getcolor.h>
#include <gadgets/getfile.h>
#include <gadgets/getfont.h>
#include <gadgets/getscreenmode.h>
#include <gadgets/gradientslider.h>
#include <gadgets/palette.h>
#include <gadgets/sketchboard.h>
#include <gadgets/speedbar.h>
#include <gadgets/texteditor.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/clicktab.h>
#include <proto/datebrowser.h>
#include <proto/fuelgauge.h>
#include <proto/getcolor.h>
#include <proto/getfile.h>
#include <proto/getfont.h>
#include <proto/getscreenmode.h>
#include <proto/palette.h>
#include <proto/sketchboard.h>
#include <proto/speedbar.h>
#include <proto/texteditor.h>

struct IntuitionBase *IntuitionBase = NULL;
struct Library *ClickTabBase = NULL;
struct Library *ColorWheelBase = NULL;
struct Library *DateBrowserBase = NULL;
struct Library *FuelGaugeBase = NULL;
struct Library *GetColorBase = NULL;
struct Library *GetFileBase = NULL;
struct Library *GetFontBase = NULL;
struct Library *GetScreenModeBase = NULL;
struct Library *GradientSliderBase = NULL;
struct Library *PaletteBase = NULL;
struct Library *SketchBoardBase = NULL;
struct Library *SpeedBarBase = NULL;
struct Library *TextFieldBase = NULL; /* texteditor.gadget -- see header comment */

#define GID_CLICKTAB       101
#define GID_COLORWHEEL     102
#define GID_DATEBROWSER    103
#define GID_FUELGAUGE      104
#define GID_GETCOLOR       105
#define GID_GETFILE        106
#define GID_GETFONT        107
#define GID_GETSCREENMODE  108
#define GID_GRADIENTSLIDER 109
#define GID_PALETTE        110
#define GID_SKETCHBOARD    111
#define GID_SPEEDBAR       112
#define GID_TEXTEDITOR     113

/* Writes directly to a fixed file via dos.library rather than stdout --
 * "Run >file <command>" does not reliably capture a background CLI's
 * own Output() stream under Copperline (confirmed repeatedly this
 * session, e.g. fixtures/classact-app's own DiagFile()); this
 * sidesteps that uncertainty entirely. */
static void Diag(const char *msg)
{
    BPTR fh = Open((CONST_STRPTR)"SRC:build/rc-diag.txt", MODE_READWRITE);
    if (fh == 0) {
        fh = Open((CONST_STRPTR)"SRC:build/rc-diag.txt", MODE_NEWFILE);
    }
    if (fh == 0) {
        return;
    }
    Seek(fh, 0, OFFSET_END);
    Write(fh, (APTR)msg, (LONG)strlen(msg));
    Close(fh);
}

static void CleanExit(struct Window *window, struct Gadget *glist, int rc)
{
    struct Gadget *g;

    if (window != NULL) {
        CloseWindow(window);
    }
    /* Every object in the chain is a real BOOPSI object -- DisposeObject,
     * not FreeGadget, same as classact-app's own DisposeObject use. */
    while (glist != NULL) {
        g = glist;
        glist = glist->NextGadget;
        DisposeObject((Object *)g);
    }

    if (TextFieldBase != NULL) CloseLibrary(TextFieldBase);
    if (SpeedBarBase != NULL) CloseLibrary(SpeedBarBase);
    if (SketchBoardBase != NULL) CloseLibrary(SketchBoardBase);
    if (PaletteBase != NULL) CloseLibrary(PaletteBase);
    if (GradientSliderBase != NULL) CloseLibrary(GradientSliderBase);
    if (GetScreenModeBase != NULL) CloseLibrary(GetScreenModeBase);
    if (GetFontBase != NULL) CloseLibrary(GetFontBase);
    if (GetFileBase != NULL) CloseLibrary(GetFileBase);
    if (GetColorBase != NULL) CloseLibrary(GetColorBase);
    if (FuelGaugeBase != NULL) CloseLibrary(FuelGaugeBase);
    if (DateBrowserBase != NULL) CloseLibrary(DateBrowserBase);
    if (ColorWheelBase != NULL) CloseLibrary(ColorWheelBase);
    if (ClickTabBase != NULL) CloseLibrary(ClickTabBase);
    if (IntuitionBase != NULL) CloseLibrary((struct Library *)IntuitionBase);

    exit(rc);
}

int main(void)
{
    struct Screen *screen;
    struct Window *window;
    struct Gadget *glist = NULL, *tail = NULL, *g;
    BOOL done = FALSE;
    WORD col1x = 8, col2x = 172, y = 20, rowh = 26;

#define LINK(newg) \
    do { \
        g = (struct Gadget *)(newg); \
        if (g != NULL) { \
            if (tail == NULL) { glist = g; } else { tail->NextGadget = g; } \
            tail = g; \
        } \
    } while (0)

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 37);
    ClickTabBase = OpenLibrary((CONST_STRPTR)"gadgets/clicktab.gadget", 44);
    ColorWheelBase = OpenLibrary((CONST_STRPTR)"gadgets/colorwheel.gadget", 44);
    DateBrowserBase = OpenLibrary((CONST_STRPTR)"gadgets/datebrowser.gadget", 44);
    FuelGaugeBase = OpenLibrary((CONST_STRPTR)"gadgets/fuelgauge.gadget", 44);
    GetColorBase = OpenLibrary((CONST_STRPTR)"gadgets/getcolor.gadget", 44);
    GetFileBase = OpenLibrary((CONST_STRPTR)"gadgets/getfile.gadget", 44);
    GetFontBase = OpenLibrary((CONST_STRPTR)"gadgets/getfont.gadget", 44);
    GetScreenModeBase = OpenLibrary((CONST_STRPTR)"gadgets/getscreenmode.gadget", 44);
    GradientSliderBase = OpenLibrary((CONST_STRPTR)"gadgets/gradientslider.gadget", 44);
    PaletteBase = OpenLibrary((CONST_STRPTR)"gadgets/palette.gadget", 44);
    SketchBoardBase = OpenLibrary((CONST_STRPTR)"gadgets/sketchboard.gadget", 44);
    SpeedBarBase = OpenLibrary((CONST_STRPTR)"gadgets/speedbar.gadget", 44);
    TextFieldBase = OpenLibrary((CONST_STRPTR)"gadgets/texteditor.gadget", 44);

    if (IntuitionBase == NULL) {
        Diag("rcapp: intuition.library FAILED\n");
        CleanExit(NULL, NULL, RETURN_FAIL);
    }
    Diag(ClickTabBase != NULL ? "rcapp: clicktab.gadget ok\n" : "rcapp: clicktab.gadget FAILED\n");
    Diag(ColorWheelBase != NULL ? "rcapp: colorwheel.gadget ok\n" : "rcapp: colorwheel.gadget FAILED\n");
    Diag(DateBrowserBase != NULL ? "rcapp: datebrowser.gadget ok\n" : "rcapp: datebrowser.gadget FAILED\n");
    Diag(FuelGaugeBase != NULL ? "rcapp: fuelgauge.gadget ok\n" : "rcapp: fuelgauge.gadget FAILED\n");
    Diag(GetColorBase != NULL ? "rcapp: getcolor.gadget ok\n" : "rcapp: getcolor.gadget FAILED\n");
    Diag(GetFileBase != NULL ? "rcapp: getfile.gadget ok\n" : "rcapp: getfile.gadget FAILED\n");
    Diag(GetFontBase != NULL ? "rcapp: getfont.gadget ok\n" : "rcapp: getfont.gadget FAILED\n");
    Diag(GetScreenModeBase != NULL ? "rcapp: getscreenmode.gadget ok\n" : "rcapp: getscreenmode.gadget FAILED\n");
    Diag(GradientSliderBase != NULL ? "rcapp: gradientslider.gadget ok\n" : "rcapp: gradientslider.gadget FAILED\n");
    Diag(PaletteBase != NULL ? "rcapp: palette.gadget ok\n" : "rcapp: palette.gadget FAILED\n");
    Diag(SketchBoardBase != NULL ? "rcapp: sketchboard.gadget ok\n" : "rcapp: sketchboard.gadget FAILED\n");
    Diag(SpeedBarBase != NULL ? "rcapp: speedbar.gadget ok\n" : "rcapp: speedbar.gadget FAILED\n");
    Diag(TextFieldBase != NULL ? "rcapp: texteditor.gadget ok\n" : "rcapp: texteditor.gadget FAILED\n");

    screen = LockPubScreen(NULL);
    if (screen == NULL) {
        Diag("rcapp: LockPubScreen FAILED\n");
        CleanExit(NULL, NULL, RETURN_FAIL);
    }

    if (ClickTabBase != NULL) {
        LINK(NewObject(CLICKTAB_GetClass(), NULL,
                        GA_ID, GID_CLICKTAB, GA_Left, col1x, GA_Top, y,
                        GA_Width, 150, GA_Height, 22, TAG_DONE));
    }
    if (ColorWheelBase != NULL) {
        /* WHEEL_Screen is documented as a REQUIRED OM_NEW tag (the
         * autodoc: "must be provided when the wheel is created via
         * NewObject()") -- confirmed live: NewObject() returns NULL
         * without it, no other tag omission causes that here. */
        Object *cw = NewObject(NULL, (CONST_STRPTR)"colorwheel.gadget",
                                GA_ID, GID_COLORWHEEL, GA_Left, col2x, GA_Top, y,
                                GA_Width, 150, GA_Height, 60,
                                WHEEL_Screen, (ULONG)screen, TAG_DONE);
        Diag(cw != NULL ? "rcapp: colorwheel obj ok\n" : "rcapp: colorwheel obj FAILED\n");
        LINK(cw);
    }
    y += rowh + 38; /* colorwheel wants real vertical room to render as a circle */

    if (DateBrowserBase != NULL) {
        LINK(NewObject(DATEBROWSER_GetClass(), NULL,
                        GA_ID, GID_DATEBROWSER, GA_Left, col1x, GA_Top, y,
                        GA_Width, 150, GA_Height, 22, TAG_DONE));
    }
    if (FuelGaugeBase != NULL) {
        LINK(NewObject(FUELGAUGE_GetClass(), NULL,
                        GA_ID, GID_FUELGAUGE, GA_Left, col2x, GA_Top, y,
                        GA_Width, 150, GA_Height, 22,
                        FUELGAUGE_Min, 0, FUELGAUGE_Max, 100, FUELGAUGE_Level, 40,
                        TAG_DONE));
    }
    y += rowh;

    if (GetColorBase != NULL) {
        LINK(NewObject(GETCOLOR_GetClass(), NULL,
                        GA_ID, GID_GETCOLOR, GA_Left, col1x, GA_Top, y,
                        GA_Width, 150, GA_Height, 22,
                        GETCOLOR_Screen, (ULONG)screen, TAG_DONE));
    }
    if (GetFileBase != NULL) {
        LINK(NewObject(GETFILE_GetClass(), NULL,
                        GA_ID, GID_GETFILE, GA_Left, col2x, GA_Top, y,
                        GA_Width, 150, GA_Height, 22, TAG_DONE));
    }
    y += rowh;

    if (GetFontBase != NULL) {
        LINK(NewObject(GETFONT_GetClass(), NULL,
                        GA_ID, GID_GETFONT, GA_Left, col1x, GA_Top, y,
                        GA_Width, 150, GA_Height, 22, TAG_DONE));
    }
    if (GetScreenModeBase != NULL) {
        LINK(NewObject(GETSCREENMODE_GetClass(), NULL,
                        GA_ID, GID_GETSCREENMODE, GA_Left, col2x, GA_Top, y,
                        GA_Width, 150, GA_Height, 22, TAG_DONE));
    }
    y += rowh;

    if (GradientSliderBase != NULL) {
        LINK(NewObject(NULL, (CONST_STRPTR)"gradientslider.gadget",
                        GA_ID, GID_GRADIENTSLIDER, GA_Left, col1x, GA_Top, y,
                        GA_Width, 150, GA_Height, 22, TAG_DONE));
    }
    if (PaletteBase != NULL) {
        LINK(NewObject(PALETTE_GetClass(), NULL,
                        GA_ID, GID_PALETTE, GA_Left, col2x, GA_Top, y,
                        GA_Width, 150, GA_Height, 22,
                        PALETTE_NumColours, 4, TAG_DONE));
    }
    y += rowh;

    if (SketchBoardBase != NULL) {
        LINK(NewObject(SKETCHBOARD_GetClass(), NULL,
                        GA_ID, GID_SKETCHBOARD, GA_Left, col1x, GA_Top, y,
                        GA_Width, 150, GA_Height, 40, TAG_DONE));
    }
    if (SpeedBarBase != NULL) {
        LINK(NewObject(SPEEDBAR_GetClass(), NULL,
                        GA_ID, GID_SPEEDBAR, GA_Left, col2x, GA_Top, y,
                        GA_Width, 150, GA_Height, 22, TAG_DONE));
    }
    y += rowh + 18; /* sketchboard is taller */

    if (TextFieldBase != NULL) {
        LINK(NewObject(TEXTEDITOR_GetClass(), NULL,
                        GA_ID, GID_TEXTEDITOR, GA_Left, col1x, GA_Top, y,
                        GA_Width, 314, GA_Height, 30, TAG_DONE));
    }
    y += rowh + 8;

    window = OpenWindowTags(NULL,
                             WA_Left, 40, WA_Top, 20,
                             WA_Width, 340, WA_Height, (ULONG)(y + 20),
                             WA_Title, (ULONG)"AmiPilot ReAction Classes Fixture",
                             WA_Gadgets, (ULONG)glist,
                             WA_CloseGadget, TRUE,
                             WA_DragBar, TRUE,
                             WA_DepthGadget, TRUE,
                             WA_Activate, TRUE,
                             WA_SimpleRefresh, TRUE,
                             WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW,
                             WA_PubScreen, (ULONG)screen,
                             TAG_DONE);

    UnlockPubScreen(NULL, screen);

    if (window == NULL) {
        Diag("rcapp: OpenWindowTags FAILED\n");
        CleanExit(NULL, glist, RETURN_FAIL);
    }

    RefreshGadgets(glist, window, NULL);
    Diag("rcapp: ready\n");

    while (!done) {
        struct IntuiMessage *msg;

        Wait(1UL << window->UserPort->mp_SigBit);

        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort)) != NULL) {
            ULONG class = msg->Class;
            ReplyMsg((struct Message *)msg);

            if (class == IDCMP_CLOSEWINDOW) {
                done = TRUE;
            } else if (class == IDCMP_REFRESHWINDOW) {
                BeginRefresh(window);
                EndRefresh(window, TRUE);
            }
        }
    }

    CleanExit(window, glist, RETURN_OK);
    return RETURN_OK;
}
