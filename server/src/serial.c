/* serial.c -- see serial.h. */
#include <string.h>
#include <stdio.h>

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <devices/serial.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/alib.h>

#include "serial.h"

/* One drained chunk's worth of received bytes awaiting line assembly.
 * Sized above the wire's 512-byte request cap so a full request always
 * fits in one drain. */
#define AMIP_SER_RXBUF 1024
/* Request-line cap per WIRE.md (512 including the terminator). */
#define AMIP_SER_LINE 512

struct AmipSerial {
    struct MsgPort *readPort;
    struct MsgPort *writePort;
    struct IOExtSer *readIO;
    struct IOExtSer *writeIO;
    BOOL deviceOpen;
    BOOL readPending;

    UBYTE readByte;               /* the async 1-byte read's landing spot */
    UBYTE rxBuf[AMIP_SER_RXBUF];  /* drained but not yet line-assembled */
    int rxHead, rxCount;

    char line[AMIP_SER_LINE];     /* line being accumulated */
    int lineLen;
    BOOL lineOverflow;            /* drop-until-LF after hitting the cap */
    BOOL lastLineOverflowed;      /* true iff the line NextLine() most
                                    * recently returned was truncated --
                                    * see AmipSerialLastLineOverflowed() */
    char out[AMIP_SER_LINE];      /* the line handed to the caller */
};

static void SetErr(char *errOut, int errCap, const char *msg)
{
    if (errOut != NULL && errCap > 0) {
        strncpy(errOut, msg, errCap - 1);
        errOut[errCap - 1] = '\0';
    }
}

/* Posts the standing asynchronous 1-byte read. */
static void PostRead(AmipSerial *serial)
{
    serial->readIO->IOSer.io_Command = CMD_READ;
    serial->readIO->IOSer.io_Data = &serial->readByte;
    serial->readIO->IOSer.io_Length = 1;
    SendIO((struct IORequest *)serial->readIO);
    serial->readPending = TRUE;
}

AmipSerial *AmipSerialOpen(const char *device, LONG unit, LONG baud,
                           char *errOut, int errCap)
{
    AmipSerial *serial;

    serial = AllocMem(sizeof(*serial), MEMF_PUBLIC | MEMF_CLEAR);
    if (serial == NULL) {
        SetErr(errOut, errCap, "out of memory");
        return NULL;
    }

    serial->readPort = CreateMsgPort();
    serial->writePort = CreateMsgPort();
    if (serial->readPort == NULL || serial->writePort == NULL) {
        SetErr(errOut, errCap, "could not create message ports");
        goto fail;
    }

    serial->readIO = (struct IOExtSer *)CreateIORequest(serial->readPort,
                                                        sizeof(struct IOExtSer));
    serial->writeIO = (struct IOExtSer *)CreateIORequest(serial->writePort,
                                                         sizeof(struct IOExtSer));
    if (serial->readIO == NULL || serial->writeIO == NULL) {
        SetErr(errOut, errCap, "could not create IO requests");
        goto fail;
    }

    /* Exclusive open, xon/xoff disabled from the start: the wire's
     * length-prefixed payloads are binary-safe only if nothing in the
     * driver is eating 0x11/0x13 (WIRE.md "Transport"). */
    serial->readIO->io_SerFlags = SERF_XDISABLED;
    if (OpenDevice((CONST_STRPTR)device, unit,
                   (struct IORequest *)serial->readIO, 0) != 0) {
        SetErr(errOut, errCap, "could not open serial device/unit");
        goto fail;
    }
    serial->deviceOpen = TRUE;

    /* The write request shares the device open: clone the opened
     * request wholesale (io_Device/io_Unit and the serial extension),
     * then repoint its reply port -- the standard second-IORequest
     * pattern (RKRM: Devices, serial example). */
    *serial->writeIO = *serial->readIO;
    serial->writeIO->IOSer.io_Message.mn_ReplyPort = serial->writePort;

    /* 8N1 at the requested baud. SDCMD_SETPARAMS while no I/O is
     * pending (nothing has been posted yet), per the autodoc's rule. */
    serial->readIO->io_Baud = baud;
    serial->readIO->io_ReadLen = 8;
    serial->readIO->io_WriteLen = 8;
    serial->readIO->io_StopBits = 1;
    serial->readIO->io_SerFlags = SERF_XDISABLED;
    serial->readIO->IOSer.io_Command = SDCMD_SETPARAMS;
    if (DoIO((struct IORequest *)serial->readIO) != 0) {
        SetErr(errOut, errCap, "SDCMD_SETPARAMS rejected (baud?)");
        goto fail;
    }

    PostRead(serial);
    return serial;

fail:
    AmipSerialClose(serial);
    return NULL;
}

void AmipSerialClose(AmipSerial *serial)
{
    if (serial == NULL) {
        return;
    }
    if (serial->readPending) {
        AbortIO((struct IORequest *)serial->readIO);
        WaitIO((struct IORequest *)serial->readIO);
    }
    if (serial->deviceOpen) {
        CloseDevice((struct IORequest *)serial->readIO);
    }
    if (serial->writeIO != NULL) {
        DeleteIORequest((struct IORequest *)serial->writeIO);
    }
    if (serial->readIO != NULL) {
        DeleteIORequest((struct IORequest *)serial->readIO);
    }
    if (serial->writePort != NULL) {
        DeleteMsgPort(serial->writePort);
    }
    if (serial->readPort != NULL) {
        DeleteMsgPort(serial->readPort);
    }
    FreeMem(serial, sizeof(*serial));
}

ULONG AmipSerialSigMask(const AmipSerial *serial)
{
    return 1UL << serial->readPort->mp_SigBit;
}

/* If the standing read completed, harvest its byte and drain whatever
 * else the driver has buffered (SDCMD_QUERY reports the count) in one
 * synchronous chunk, then re-post the standing read. Returns TRUE if any
 * bytes were added to rxBuf. */
static BOOL Refill(AmipSerial *serial)
{
    ULONG pending;

    if (!serial->readPending ||
        !CheckIO((struct IORequest *)serial->readIO)) {
        return FALSE;
    }
    WaitIO((struct IORequest *)serial->readIO);
    serial->readPending = FALSE;

    /* rxBuf is always fully consumed before Refill is called (see
     * AmipSerialNextLine), so it starts empty here. */
    serial->rxHead = 0;
    serial->rxCount = 0;
    if (serial->readIO->IOSer.io_Error == 0 &&
        serial->readIO->IOSer.io_Actual == 1) {
        serial->rxBuf[serial->rxCount++] = serial->readByte;
    }

    serial->readIO->IOSer.io_Command = SDCMD_QUERY;
    if (DoIO((struct IORequest *)serial->readIO) == 0) {
        pending = serial->readIO->IOSer.io_Actual;
        if (pending > (ULONG)(AMIP_SER_RXBUF - serial->rxCount)) {
            pending = AMIP_SER_RXBUF - serial->rxCount;
        }
        if (pending > 0) {
            serial->readIO->IOSer.io_Command = CMD_READ;
            serial->readIO->IOSer.io_Data = &serial->rxBuf[serial->rxCount];
            serial->readIO->IOSer.io_Length = pending;
            if (DoIO((struct IORequest *)serial->readIO) == 0) {
                serial->rxCount += serial->readIO->IOSer.io_Actual;
            }
        }
    }

    PostRead(serial);
    return serial->rxCount > 0;
}

const char *AmipSerialNextLine(AmipSerial *serial)
{
    for (;;) {
        while (serial->rxHead < serial->rxCount) {
            char c = (char)serial->rxBuf[serial->rxHead++];

            if (c == '\n') {
                int n = serial->lineLen;

                /* On overflow, still hand up the truncated prefix --
                 * but ALSO latch lastLineOverflowed so the caller can
                 * reject it with an explicit "line too long" RC 10
                 * before ever handing it to the parser, rather than
                 * relying on the parser happening to choke on the
                 * chopped text (a truncated-but-still-well-formed
                 * command would otherwise silently run as a
                 * different, unintended command -- see
                 * AmipSerialLastLineOverflowed()'s own doc comment). */
                serial->lastLineOverflowed = serial->lineOverflow;
                serial->lineLen = 0;
                serial->lineOverflow = FALSE;
                if (n > 0 && serial->line[n - 1] == '\r') {
                    n--;
                }
                if (n == 0) {
                    continue; /* empty lines are ignored (WIRE.md) */
                }
                memcpy(serial->out, serial->line, n);
                serial->out[n] = '\0';
                return serial->out;
            }
            if (serial->lineOverflow) {
                continue;
            }
            if (serial->lineLen >= AMIP_SER_LINE - 1) {
                serial->lineOverflow = TRUE;
                continue;
            }
            serial->line[serial->lineLen++] = c;
        }

        if (!Refill(serial)) {
            return NULL;
        }
    }
}

BOOL AmipSerialLastLineOverflowed(const AmipSerial *serial)
{
    return serial->lastLineOverflowed;
}

BOOL AmipSerialWrite(AmipSerial *serial, const void *data, ULONG len)
{
    if (len == 0) {
        return TRUE;
    }
    serial->writeIO->IOSer.io_Command = CMD_WRITE;
    serial->writeIO->IOSer.io_Data = (APTR)data;
    serial->writeIO->IOSer.io_Length = len;
    return DoIO((struct IORequest *)serial->writeIO) == 0;
}

#define AMIP_SER_READEXACT_DEFAULT_TIMEOUT 30 /* seconds */
#define AMIP_SER_READEXACT_POLL_TICKS 5        /* ~100ms, matching Refill()'s
                                                 * own drain granularity */

BOOL AmipSerialReadExact(AmipSerial *serial, UBYTE *buf, ULONG len,
                          long timeoutSeconds)
{
    ULONG got = 0;
    ULONG ticksTotal = (ULONG)(timeoutSeconds > 0 ? timeoutSeconds
                                                   : AMIP_SER_READEXACT_DEFAULT_TIMEOUT) * 50;
    ULONG ticksWaited = 0;

    for (;;) {
        while (serial->rxHead < serial->rxCount && got < len) {
            buf[got++] = serial->rxBuf[serial->rxHead++];
        }
        if (got >= len) {
            return TRUE;
        }
        if (ticksWaited >= ticksTotal) {
            return FALSE;
        }
        if (!Refill(serial)) {
            Delay(AMIP_SER_READEXACT_POLL_TICKS);
            ticksWaited += AMIP_SER_READEXACT_POLL_TICKS;
        }
    }
}
