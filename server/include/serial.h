/* serial.h -- serial.device transport for the AmiPilot server commodity
 * (phase 0.3, the wire protocol -- server/WIRE.md).
 *
 * Thin line-discipline layer over serial.device: keeps one asynchronous
 * 1-byte CMD_READ outstanding (its reply-port signal is what the
 * commodity's main Wait() loop folds in), drains the device's own
 * receive buffer in a chunk whenever that byte lands (SDCMD_QUERY +
 * synchronous CMD_READ, so throughput isn't one-signal-per-byte), and
 * accumulates LF-terminated request lines. Writes are synchronous
 * CMD_WRITE -- responses are small and the dispatch loop is
 * single-threaded, so there is nothing to overlap with.
 *
 * Amiga-only by nature (exec device I/O); the portable request grammar
 * it carries is arexx_cmd.h's, and the response framing it serves is
 * WIRE.md's -- this module knows about neither, it only moves bytes and
 * lines.
 */
#ifndef AMIPILOT_SERIAL_H
#define AMIPILOT_SERIAL_H

#include <exec/types.h>

typedef struct AmipSerial AmipSerial;

/* Opens `device` unit `unit` at `baud` (8N1, xon/xoff disabled -- see
 * WIRE.md's transport note on why software flow control is off) and
 * posts the first asynchronous read. Returns NULL on any failure (device
 * missing, unit busy); errOut (cap errCap, may be NULL) gets a one-line
 * reason. */
AmipSerial *AmipSerialOpen(const char *device, LONG unit, LONG baud,
                           char *errOut, int errCap);

/* Aborts the outstanding read, closes the device, frees everything.
 * NULL-safe. */
void AmipSerialClose(AmipSerial *serial);

/* The signal mask the caller folds into its Wait() -- fires when the
 * outstanding read completes. */
ULONG AmipSerialSigMask(const AmipSerial *serial);

/* Returns the next complete request line (NUL-terminated, LF consumed,
 * trailing CR stripped -- WIRE.md's CRLF tolerance), or NULL when no
 * complete line is buffered. Call repeatedly until NULL after a signal:
 * one chunk drain can complete several lines. The returned pointer is
 * valid until the next AmipSerialNextLine() call. Lines longer than the
 * wire's 512-byte request cap are truncated (the parser then rejects
 * them, which is the spec'd RC 10 outcome, not a transport error). */
const char *AmipSerialNextLine(AmipSerial *serial);

/* Synchronously writes len bytes. Returns FALSE if the write failed
 * (io_Error non-zero) -- rare enough that the caller just logs it. */
BOOL AmipSerialWrite(AmipSerial *serial, const void *data, ULONG len);

#endif /* AMIPILOT_SERIAL_H */
