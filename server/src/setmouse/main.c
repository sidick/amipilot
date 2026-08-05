/*
 * AmiSetMouse -- dev-only diagnostic, as close to the RKM's documented
 * Devices/Dev_examples/Set_Mouse.c as practical (ADCD 2.1, "This example
 * sets the mouse at x=100 and y=200"): position the pointer at absolute
 * pixel coordinates on the default public screen via
 * IECLASS_NEWPOINTERPOS/IESUBCLASS_PIXEL, nothing else. No clicking, no
 * action-engine involvement -- this exists to verify the documented
 * pointer-move mechanism in isolation (where does the sprite land? what
 * do IntuitionBase->MouseX/MouseY read back as?) before layering button
 * events on top.
 *
 * Template: X/N/A,Y/N/A
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <intuition/screens.h>
#include <intuition/intuitionbase.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <string.h>

struct IntuitionBase *IntuitionBase = NULL;

#define TEMPLATE "X/N/A,Y/N/A"

struct SetMouseArgs {
    LONG *x;
    LONG *y;
};

int main(void)
{
    struct RDArgs *rdargs;
    struct SetMouseArgs args;
    struct MsgPort *inputMP = NULL;
    struct IOStdReq *inputIO = NULL;
    struct InputEvent *fakeEvent = NULL;
    struct IEPointerPixel *neoPix = NULL;
    struct Screen *pubScreen = NULL;
    int rc = RETURN_FAIL;

    memset(&args, 0, sizeof(args));

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 37);
    if (IntuitionBase == NULL) {
        fprintf(stderr, "AmiSetMouse: requires intuition.library V37+\n");
        return RETURN_FAIL;
    }

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, (LONG *)&args, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"AmiSetMouse");
        CloseLibrary((struct Library *)IntuitionBase);
        return RETURN_FAIL;
    }

    /* Same structure as the RKM example: message port, InputEvent and
     * IEPointerPixel in MEMF_PUBLIC allocations (the example is explicit
     * about that), IORequest, OpenDevice, one IND_WRITEEVENT. */
    if ((inputMP = CreateMsgPort()) != NULL) {
        if ((fakeEvent = AllocMem(sizeof(struct InputEvent), MEMF_PUBLIC | MEMF_CLEAR)) != NULL) {
            if ((neoPix = AllocMem(sizeof(struct IEPointerPixel), MEMF_PUBLIC | MEMF_CLEAR)) != NULL) {
                if ((inputIO = (struct IOStdReq *)CreateIORequest(inputMP, sizeof(struct IOStdReq))) != NULL) {
                    if (OpenDevice((CONST_STRPTR)"input.device", 0, (struct IORequest *)inputIO, 0) == 0) {
                        if ((pubScreen = LockPubScreen(NULL)) != NULL) {
                            LONG preX, preY, postX, postY;

                            LockIBase(0);
                            preX = IntuitionBase->MouseX;
                            preY = IntuitionBase->MouseY;
                            UnlockIBase(0);

                            neoPix->iepp_Screen = pubScreen;
                            neoPix->iepp_Position.X = (WORD)*args.x;
                            neoPix->iepp_Position.Y = (WORD)*args.y;

                            fakeEvent->ie_EventAddress = (APTR)neoPix;
                            fakeEvent->ie_NextEvent = NULL;
                            fakeEvent->ie_Class = IECLASS_NEWPOINTERPOS;
                            fakeEvent->ie_SubClass = IESUBCLASS_PIXEL;
                            fakeEvent->ie_Code = IECODE_NOBUTTON;
                            fakeEvent->ie_Qualifier = 0;

                            inputIO->io_Data = (APTR)fakeEvent;
                            inputIO->io_Length = sizeof(struct InputEvent);
                            inputIO->io_Command = IND_WRITEEVENT;
                            DoIO((struct IORequest *)inputIO);

                            Delay(2); /* let Intuition's handler consume it */

                            LockIBase(0);
                            postX = IntuitionBase->MouseX;
                            postY = IntuitionBase->MouseY;
                            UnlockIBase(0);

                            printf("AmiSetMouse: requested [%ld,%ld] io_Error=%ld\n",
                                   (long)*args.x, (long)*args.y, (long)inputIO->io_Error);
                            printf("AmiSetMouse: MouseX/Y before = [%ld,%ld], after = [%ld,%ld]\n",
                                   (long)preX, (long)preY, (long)postX, (long)postY);
                            printf("AmiSetMouse: screen Width=%d Height=%d ViewPort Modes=0x%04x\n",
                                   pubScreen->Width, pubScreen->Height,
                                   pubScreen->ViewPort.Modes);

                            UnlockPubScreen(NULL, pubScreen);
                            rc = RETURN_OK;
                        } else {
                            fprintf(stderr, "AmiSetMouse: LockPubScreen failed\n");
                        }
                        CloseDevice((struct IORequest *)inputIO);
                    } else {
                        fprintf(stderr, "AmiSetMouse: OpenDevice(input.device) failed\n");
                    }
                    DeleteIORequest((struct IORequest *)inputIO);
                } else {
                    fprintf(stderr, "AmiSetMouse: CreateIORequest failed\n");
                }
                FreeMem(neoPix, sizeof(struct IEPointerPixel));
            } else {
                fprintf(stderr, "AmiSetMouse: AllocMem(IEPointerPixel) failed\n");
            }
            FreeMem(fakeEvent, sizeof(struct InputEvent));
        } else {
            fprintf(stderr, "AmiSetMouse: AllocMem(InputEvent) failed\n");
        }
        DeleteMsgPort(inputMP);
    } else {
        fprintf(stderr, "AmiSetMouse: CreateMsgPort failed\n");
    }

    FreeArgs(rdargs);
    CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
