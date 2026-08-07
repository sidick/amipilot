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
 *
 * SECURITY: this transport is meant for a trusted LAN or a direct
 * machine-to-machine link -- NEVER expose it on an open port facing
 * the internet. AmipTcpAllow() (a source-IP/CIDR allowlist) and the
 * AUTH verb's password gate (server/src/amipilotserver/main.c) raise
 * the bar above "wide open to anyone," but neither makes this
 * internet-safe: there is no TLS (everything, including the AUTH
 * password, crosses the wire in cleartext), the AUTH default is a
 * fixed string checked into this public repo, and there is no
 * rate-limiting on repeated guesses. See server/README.md's TCP
 * section before deploying this anywhere but a trusted network.
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

/* Grants one TCPALLOW entry -- "a.b.c.d" (an exact address) or
 * "a.b.c.d/nn" (a CIDR range, nn 0-32) -- parsed by hand
 * (ParseDottedQuad(), tcp.c), NOT via inet_aton(): that call turned
 * out to trap under Amiberry's bsdsocket_emu (a Roadshow-era
 * extension not present in every real bsdsocket.library, confirmed
 * live 2026-08-07 -- see tcp.c's own comment on ParseDottedQuad()).
 * Once at least one entry has been granted, AcceptNew() (tcp.c) rejects any
 * connecting peer that doesn't match at least one of them -- an
 * immediate CloseSocket() with no reply ever sent, not an error a
 * port-scanner could use to fingerprint this service. With no entries
 * granted at all (the default), every source is accepted, unchanged
 * from this transport's original behavior. Returns FALSE on
 * malformed input (bad dotted-quad, prefix out of 0-32, or the
 * allowlist is already full); errOut (cap errCap, may be NULL) gets a
 * one-line reason -- same fatal-on-bad-config posture fs.h's
 * AmipFsGrantRoot() already uses for FSROOT. */
BOOL AmipTcpAllow(AmipTcp *tcp, const char *spec, char *errOut, int errCap);

/* TRUE once AmipTcpSetAuthenticated(tcp, TRUE) has been called for
 * the CURRENTLY active client connection -- reset to FALSE every time
 * a new connection is accepted (AcceptNew(), same "a new connection
 * replaces the old one" moment this transport already resets its line
 * buffer at). Meaningless/unused unless the caller is enforcing the
 * AUTH gate (server/src/amipilotserver/main.c's TCP dispatch loop) --
 * ARexx and serial.device never call this at all. */
BOOL AmipTcpIsAuthenticated(AmipTcp *tcp);
void AmipTcpSetAuthenticated(AmipTcp *tcp, BOOL authenticated);

#endif /* AMIPILOT_TCP_H */
