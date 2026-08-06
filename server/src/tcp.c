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
#include <string.h>

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "tcp.h"

/* Sizing mirrors serial.c exactly -- see its own comment. */
#define AMIP_TCP_RXBUF 1024
#define AMIP_TCP_LINE  512

struct AmipTcp {
    LONG listenSock;
    LONG clientSock; /* -1 when no client is connected */

    UBYTE rxBuf[AMIP_TCP_RXBUF];
    int rxHead, rxCount;

    char line[AMIP_TCP_LINE];
    int lineLen;
    BOOL lineOverflow;
    char out[AMIP_TCP_LINE];
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

/* Accepts a new connection (replacing any existing one -- see tcp.h's
 * own comment on the single-active-client policy) and resets the line
 * buffer for it. */
static void AcceptNew(AmipTcp *tcp)
{
    LONG newSock = accept(tcp->listenSock, NULL, NULL);

    if (newSock < 0) {
        return;
    }
    if (tcp->clientSock >= 0) {
        CloseSocket(tcp->clientSock);
    }
    tcp->clientSock = newSock;
    tcp->rxHead = 0;
    tcp->rxCount = 0;
    tcp->lineLen = 0;
    tcp->lineOverflow = FALSE;
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
