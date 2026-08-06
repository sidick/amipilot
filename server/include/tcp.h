/* tcp.h -- TCP transport for the AmiPilot server commodity (phase 0.4,
 * bsdsocket.library). Second carrier for the same line protocol
 * serial.c carries (server/WIRE.md) -- one framing/dispatch, two
 * transports.
 *
 * Unlike serial.h's Open/Close/SigMask/NextLine/Write shape, this
 * transport's readiness is driven by WaitSelect() (see tcp.c's own
 * header comment for why -- the more obvious SBTC_SIGEVENTMASK async-
 * signal mechanism looked correct and reported success, but never
 * actually delivered a signal when tested live), which is itself a
 * blocking call: AmipTcpWait() replaces the caller's own Exec Wait()
 * outright rather than contributing a signal bit to it.
 */
#ifndef AMIPILOT_TCP_H
#define AMIPILOT_TCP_H

#include <exec/types.h>

typedef struct AmipTcp AmipTcp;

/* Opens a listening socket on `port` (any local interface). Returns
 * NULL on any failure (no bsdsocket.library, bind/listen rejected);
 * errOut (cap errCap, may be NULL) gets a one-line reason. */
AmipTcp *AmipTcpOpen(LONG port, char *errOut, int errCap);

/* Closes any accepted client plus the listen socket, then frees
 * everything. NULL-safe. */
void AmipTcpClose(AmipTcp *tcp);

/* Blocks until either one of extraSigMask's Exec signals fires or the
 * transport has work to do (a new connection, data to read, or the
 * client closing) -- a drop-in replacement for the caller's own
 * Wait(extraSigMask | <this transport's signal>), except there is no
 * such signal: this function IS the blocking call, via WaitSelect().
 * Returns extraSigMask's own bits that actually fired (0 if it
 * returned because of transport activity instead, in which case any
 * newly available request lines are already sitting in the internal
 * buffer -- call AmipTcpNextLine() to drain them, same as after a
 * real signal). */
ULONG AmipTcpWait(AmipTcp *tcp, ULONG extraSigMask);

/* Returns the next complete request line (NUL-terminated, LF consumed,
 * trailing CR stripped -- WIRE.md's CRLF tolerance) already sitting in
 * the buffer AmipTcpWait() filled, or NULL when none remain. Call
 * repeatedly until NULL after each AmipTcpWait() call that returned 0
 * (transport activity): one drain can complete several lines.
 *
 * Only one client connection is treated as active at a time, matching
 * WIRE.md's "one test session at a time" model (the same shape
 * serial.c's single-consumer transport already has): a new connection
 * arriving while one is active replaces it, closing the old one first
 * -- unlike a real serial cable, plain TCP has no hardware reason to
 * refuse a second connection, so this is a deliberate policy choice,
 * not a transport limitation. Lines longer than the wire's 512-byte
 * request cap are truncated (the parser then rejects them, the spec'd
 * RC 10 outcome, not a transport error). */
const char *AmipTcpNextLine(AmipTcp *tcp);

/* Synchronously writes len bytes to the active client connection.
 * Returns FALSE if there is no active connection or the write failed
 * -- rare enough (a client that vanished between request and reply)
 * that the caller just logs it, same as AmipSerialWrite. */
BOOL AmipTcpWrite(AmipTcp *tcp, const void *data, ULONG len);

#endif /* AMIPILOT_TCP_H */
