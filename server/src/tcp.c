/* tcp.c -- see tcp.h.
 *
 * Readiness is driven entirely by WaitSelect() -- a single blocking
 * call whose `signals` in/out parameter composes cleanly with Exec's
 * own signal-wait model (pass in the other signals this task also
 * wants to wake on; it reports back whichever of those actually fired,
 * or socket readiness via the fd_set arguments, whichever comes
 * first). bsdsocket's fd_set here is a plain 32-bit bitmask (bit N =
 * fd N), confirmed against Copperline's own implementation
 * (crates/hostsocket-plugin/src/lib.rs's read_fd_mask/scan_select) --
 * not the multi-word array shape some other Unix-alikes use.
 *
 * This is deliberately NOT the SBTC_SIGEVENTMASK + SO_EVENTMASK +
 * GetSocketEvents() async-notification mechanism the headers also
 * document: an earlier version of this file used that path, and while
 * SocketBaseTags() reported success and produced a plausible signal
 * bit, no signal was ever actually delivered when tested live against
 * a real Copperline hostsocket bridge (2026-08-06) -- TCP connections
 * completed and requests were sent, but the registered signal simply
 * never fired, so nothing ever drained them. WaitSelect() is the
 * mechanism bsdsocktest's own conformance suite exercises directly
 * (including a regression fix for its timeout handling), and it works
 * end to end: a full VERSION/TREE/TYPE/GETTEXT/CLICK/TREE/QUIT round
 * trip verified against TWO independent bsdsocket.library
 * implementations, both live, 2026-08-06 -- Copperline's own
 * from-scratch smoltcp-based stack (bridged to a host feth pair) and
 * Amiberry's (which forwards straight to the host OS's real socket
 * API, no virtual networking involved at all). Agreement between two
 * architecturally unrelated implementations is real evidence this
 * design is correct, not an artifact of one emulator's behaviour.
 */
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
/* arpa/inet.h (inet_aton()/inet_ntoa()) is deliberately never included
 * here at all -- not just to dodge a header landmine (an earlier
 * version of this file explicitly included it and hit one: the
 * ndk-include copy has no usable include guard against a second
 * inclusion once proto/bsdsocket.h below has already transitively
 * pulled it in, producing bizarre parse errors), but because
 * inet_aton() itself turned out to be unsafe to call at all -- see
 * ParseDottedQuad()'s own comment. This file parses dotted-quad
 * addresses and formats them (FormatIp()) entirely by hand instead,
 * needing neither of arpa/inet.h's functions nor the header itself. */

#include "tcp.h"

/* Sizing mirrors serial.c exactly -- see its own comment. */
#define AMIP_TCP_RXBUF 1024
#define AMIP_TCP_LINE  512

#define AMIP_TCP_MAX_ALLOW 16

typedef struct {
    ULONG network; /* host byte order */
    ULONG mask;
} AmipTcpAllowEntry;

struct AmipTcp {
    LONG listenSock;
    LONG clientSock; /* -1 when no client is connected */

    UBYTE rxBuf[AMIP_TCP_RXBUF];
    int rxHead, rxCount;

    char line[AMIP_TCP_LINE];
    int lineLen;
    BOOL lineOverflow;
    BOOL lastLineOverflowed;  /* see AmipTcpLastLineOverflowed() */
    char out[AMIP_TCP_LINE];

    AmipTcpAllowEntry allow[AMIP_TCP_MAX_ALLOW];
    int allowCount;
    BOOL authenticated;
};

static void SetErr(char *errOut, int errCap, const char *msg)
{
    if (errOut != NULL && errCap > 0) {
        strncpy(errOut, msg, errCap - 1);
        errOut[errCap - 1] = '\0';
    }
}

AmipTcp *AmipTcpOpen(LONG port, char *errOut, int errCap)
{
    AmipTcp *tcp;
    struct sockaddr_in addr;

    tcp = AllocMem(sizeof(*tcp), MEMF_PUBLIC | MEMF_CLEAR);
    if (tcp == NULL) {
        SetErr(errOut, errCap, "out of memory");
        return NULL;
    }
    tcp->listenSock = -1;
    tcp->clientSock = -1;

    if (SocketBase == NULL) {
        SetErr(errOut, errCap, "bsdsocket.library not open");
        goto fail;
    }

    tcp->listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp->listenSock < 0) {
        SetErr(errOut, errCap, "socket() failed");
        goto fail;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((UWORD)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(tcp->listenSock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        SetErr(errOut, errCap, "bind() failed -- port already in use?");
        goto fail;
    }
    if (listen(tcp->listenSock, 1) < 0) {
        SetErr(errOut, errCap, "listen() failed");
        goto fail;
    }

    return tcp;

fail:
    AmipTcpClose(tcp);
    return NULL;
}

void AmipTcpClose(AmipTcp *tcp)
{
    if (tcp == NULL) {
        return;
    }
    if (tcp->clientSock >= 0) {
        CloseSocket(tcp->clientSock);
    }
    if (tcp->listenSock >= 0) {
        CloseSocket(tcp->listenSock);
    }
    FreeMem(tcp, sizeof(*tcp));
}

/* TRUE if peerAddr matches at least one granted TCPALLOW entry, or if
 * none have been granted at all (the default: accept every source,
 * unchanged from this transport's original behavior). */
static BOOL IsAllowedPeer(const AmipTcp *tcp, ULONG peerAddrHost)
{
    int i;

    if (tcp->allowCount == 0) {
        return TRUE;
    }
    for (i = 0; i < tcp->allowCount; i++) {
        if ((peerAddrHost & tcp->allow[i].mask) == tcp->allow[i].network) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Renders a host-byte-order IPv4 address as "a.b.c.d" into buf (must
 * be char[16] or larger) -- hand-rolled rather than inet_ntoa()
 * because arpa/inet.h isn't safe to include a second time in this
 * toolchain (see the header comment on this file's own includes). */
static void FormatIp(ULONG addrHost, char *buf)
{
    sprintf(buf, "%u.%u.%u.%u",
            (unsigned int)((addrHost >> 24) & 0xFF), (unsigned int)((addrHost >> 16) & 0xFF),
            (unsigned int)((addrHost >> 8) & 0xFF), (unsigned int)(addrHost & 0xFF));
}

/* Accepts a new connection (replacing any existing one -- see tcp.h's
 * own comment on the single-active-client policy), checks it against
 * any granted TCPALLOW entries, and resets the line buffer and
 * per-connection auth state for it. A rejected peer is closed
 * immediately, before ever becoming tcp->clientSock -- no reply is
 * ever sent, so a disallowed connection can't be used to fingerprint
 * this service (see tcp.h's own SECURITY note). */
static void AcceptNew(AmipTcp *tcp)
{
    struct sockaddr_in peerAddr;
    socklen_t peerLen = sizeof(peerAddr);
    LONG newSock;

    memset(&peerAddr, 0, sizeof(peerAddr));
    newSock = accept(tcp->listenSock, (struct sockaddr *)&peerAddr, &peerLen);
    if (newSock < 0) {
        return;
    }

    {
        ULONG peerAddrHost = (ULONG)ntohl(peerAddr.sin_addr.s_addr);

        if (!IsAllowedPeer(tcp, peerAddrHost)) {
            char ipStr[16];

            FormatIp(peerAddrHost, ipStr);
            printf("AmiPilotServer: TCP connection from %s rejected (not in TCPALLOW)\n",
                   ipStr);
            /* Explicit flush -- this print is the whole point of
             * rejecting rather than just silently dropping (operator
             * visibility into who's knocking), and stdio's own
             * buffering would otherwise hold it back until the server
             * eventually exits (confirmed live: redirected to a log
             * file, a rejection was genuinely rejected -- the client
             * saw its connection reset with no reply -- but the log
             * line itself didn't appear until the process ended,
             * 2026-08-07). */
            fflush(stdout);
            CloseSocket(newSock);
            return;
        }
    }

    if (tcp->clientSock >= 0) {
        CloseSocket(tcp->clientSock);
    }
    tcp->clientSock = newSock;
    tcp->rxHead = 0;
    tcp->rxCount = 0;
    tcp->lineLen = 0;
    tcp->lineOverflow = FALSE;
    tcp->authenticated = FALSE;
}

/* Reads whatever is available into rxBuf (resetting it first -- the
 * caller only reaches here once the buffer is fully consumed, so a
 * fresh append-at-0 is always correct, unlike appending at a stale
 * rxHead+rxCount offset that has drifted away from where NextLine()
 * reads from). A recv() of exactly 0 is a graceful close (the classic
 * Berkeley select() idiom): readable-but-empty means the peer is done,
 * same as the FD_CLOSE the async mechanism would have reported. */
static void ReadAvailable(AmipTcp *tcp)
{
    LONG n;

    tcp->rxHead = 0;
    tcp->rxCount = 0;

    n = recv(tcp->clientSock, tcp->rxBuf, AMIP_TCP_RXBUF, 0);
    if (n > 0) {
        tcp->rxCount = (int)n;
    } else {
        CloseSocket(tcp->clientSock);
        tcp->clientSock = -1;
    }
}

ULONG AmipTcpWait(AmipTcp *tcp, ULONG extraSigMask)
{
    LONG readMask = 0;
    LONG nfds = 0;
    ULONG sigs = extraSigMask;
    LONG ready;

    if (tcp->listenSock >= 0) {
        readMask |= 1L << tcp->listenSock;
        if (tcp->listenSock + 1 > nfds) {
            nfds = tcp->listenSock + 1;
        }
    }
    if (tcp->clientSock >= 0) {
        readMask |= 1L << tcp->clientSock;
        if (tcp->clientSock + 1 > nfds) {
            nfds = tcp->clientSock + 1;
        }
    }

    /* NULL timeout: block indefinitely, exactly like the exec Wait()
     * this replaces -- returns as soon as either a requested signal
     * or a watched fd is ready. */
    ready = WaitSelect(nfds, &readMask, NULL, NULL, NULL, &sigs);
    if (ready <= 0) {
        return sigs; /* woken by one of extraSigMask's own bits */
    }

    /* WaitSelect() only overwrites `sigs` on the signal-fired path
     * (confirmed against Copperline's own implementation); on this
     * fd-readiness path it leaves the pointer untouched, so `sigs`
     * still holds the INPUT extraSigMask here, not "nothing fired" --
     * returning it as-is would make the caller think every requested
     * signal had arrived. None of them did; only a socket did. */
    if (tcp->listenSock >= 0 && (readMask & (1L << tcp->listenSock))) {
        AcceptNew(tcp);
    }
    if (tcp->clientSock >= 0 && (readMask & (1L << tcp->clientSock))) {
        ReadAvailable(tcp);
    }
    return 0;
}

const char *AmipTcpNextLine(AmipTcp *tcp)
{
    while (tcp->rxHead < tcp->rxCount) {
        char c = (char)tcp->rxBuf[tcp->rxHead++];

        if (c == '\n') {
            int n = tcp->lineLen;

            /* Latch overflow state for this returned line before
             * resetting it for the next one -- see
             * AmipTcpLastLineOverflowed()'s doc comment for why the
             * caller must check this before parsing. */
            tcp->lastLineOverflowed = tcp->lineOverflow;
            tcp->lineLen = 0;
            tcp->lineOverflow = FALSE;
            if (n > 0 && tcp->line[n - 1] == '\r') {
                n--;
            }
            if (n == 0) {
                continue; /* empty lines are ignored (WIRE.md) */
            }
            memcpy(tcp->out, tcp->line, n);
            tcp->out[n] = '\0';
            return tcp->out;
        }
        if (tcp->lineOverflow) {
            continue;
        }
        if (tcp->lineLen >= AMIP_TCP_LINE - 1) {
            tcp->lineOverflow = TRUE;
            continue;
        }
        tcp->line[tcp->lineLen++] = c;
    }

    return NULL;
}

BOOL AmipTcpLastLineOverflowed(const AmipTcp *tcp)
{
    return tcp->lastLineOverflowed;
}

BOOL AmipTcpWrite(AmipTcp *tcp, const void *data, ULONG len)
{
    const UBYTE *p = (const UBYTE *)data;

    if (tcp->clientSock < 0) {
        return FALSE;
    }
    while (len > 0) {
        LONG n = send(tcp->clientSock, (APTR)p, (LONG)len, 0);
        if (n <= 0) {
            return FALSE;
        }
        p += n;
        len -= (ULONG)n;
    }
    return TRUE;
}

/* Splits "a.b.c.d/nn" at the '/', if present, into a dotted-quad part
 * (copied into ipBuf, cap ipCap) and *prefixOut (0-32, defaulting to
 * 32 -- an exact-address match -- when there's no '/'). Returns FALSE
 * on a malformed prefix (not a 1-2 digit number in 0-32, or trailing
 * garbage after it). */
static BOOL SplitCidr(const char *spec, char *ipBuf, size_t ipCap, int *prefixOut)
{
    const char *slash = strchr(spec, '/');
    size_t ipLen;

    if (slash == NULL) {
        strncpy(ipBuf, spec, ipCap - 1);
        ipBuf[ipCap - 1] = '\0';
        *prefixOut = 32;
        return TRUE;
    }

    ipLen = (size_t)(slash - spec);
    if (ipLen == 0 || ipLen >= ipCap) {
        return FALSE;
    }
    memcpy(ipBuf, spec, ipLen);
    ipBuf[ipLen] = '\0';

    {
        const char *p = slash + 1;
        int prefix = 0;
        int digits = 0;

        if (*p == '\0') {
            return FALSE;
        }
        while (*p != '\0') {
            if (*p < '0' || *p > '9') {
                return FALSE;
            }
            prefix = prefix * 10 + (*p - '0');
            digits++;
            if (digits > 2 || prefix > 32) {
                return FALSE;
            }
            p++;
        }
        *prefixOut = prefix;
    }
    return TRUE;
}

/* Hand-rolled dotted-quad parser -- NOT inet_aton(). inet_aton() is a
 * Roadshow-era extension at a high LVO offset (confirmed via this
 * toolchain's own clib/bsdsocket_protos.h: -594, far beyond classic
 * calls like socket()/bind() at -30/-36), not present in every real
 * bsdsocket.library implementation. Confirmed the hard way, 2026-08-07:
 * calling it under Amiberry's bsdsocket_emu (which forwards straight
 * to the host OS's real socket API, per this project's own TCP
 * verification notes in server/README.md) produced a genuine CPU trap
 * (illegal instruction), not a clean "function not found" failure --
 * reproduced in isolation with a 6-line standalone test program, so
 * this isn't specific to AmiPilotServer's own code. inet_addr() (the
 * classic, universally-available alternative) has its own problem:
 * its failure return (INADDR_NONE, 0xFFFFFFFF) is indistinguishable
 * from the valid address 255.255.255.255. Parsing four decimal octets
 * ourselves needs no library call at all, so it has neither
 * portability risk nor that ambiguity. */
static BOOL ParseDottedQuad(const char *s, ULONG *outHost)
{
    ULONG octets[4];
    const char *p = s;
    int i;

    for (i = 0; i < 4; i++) {
        int val = 0;
        int digits = 0;

        if (*p < '0' || *p > '9') {
            return FALSE;
        }
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            digits++;
            if (digits > 3 || val > 255) {
                return FALSE;
            }
            p++;
        }
        octets[i] = (ULONG)val;
        if (i < 3) {
            if (*p != '.') {
                return FALSE;
            }
            p++;
        }
    }
    if (*p != '\0') {
        return FALSE;
    }

    *outHost = (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
    return TRUE;
}

BOOL AmipTcpAllow(AmipTcp *tcp, const char *spec, char *errOut, int errCap)
{
    char ipBuf[32];
    int prefix;
    ULONG addrHost;
    ULONG mask;

    if (tcp->allowCount >= AMIP_TCP_MAX_ALLOW) {
        SetErr(errOut, errCap, "too many TCPALLOW entries");
        return FALSE;
    }
    if (!SplitCidr(spec, ipBuf, sizeof(ipBuf), &prefix)) {
        SetErr(errOut, errCap, "malformed TCPALLOW entry (want a.b.c.d or a.b.c.d/nn, nn 0-32)");
        return FALSE;
    }
    if (!ParseDottedQuad(ipBuf, &addrHost)) {
        SetErr(errOut, errCap, "malformed TCPALLOW address");
        return FALSE;
    }

    mask = (prefix == 0) ? 0UL : (0xFFFFFFFFUL << (32 - prefix));
    tcp->allow[tcp->allowCount].network = addrHost & mask;
    tcp->allow[tcp->allowCount].mask = mask;
    tcp->allowCount++;
    return TRUE;
}

BOOL AmipTcpIsAuthenticated(AmipTcp *tcp)
{
    return tcp->authenticated;
}

void AmipTcpSetAuthenticated(AmipTcp *tcp, BOOL authenticated)
{
    tcp->authenticated = authenticated;
}
