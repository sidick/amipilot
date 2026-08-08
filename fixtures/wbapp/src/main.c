/*
 * wbapp -- a minimal Workbench-startable fixture used to verify
 * AmiPilotServer's WBLAUNCH verb (server/include/wblaunch.h, phase
 * 1.0) end to end against a REAL WBStartup handshake, not just that
 * the server-side code compiles.
 *
 * Deliberately does none of GTApp's/CAApp's GUI work -- WBLAUNCH's own
 * job is proven entirely by what arrives in _WBenchMsg (the standard
 * libnix startup-code global, see the libnix skill's startup.md), so
 * this fixture just reads that and reports it: its own icon's
 * tooltypes (via a self-lookup through WBenchMsg->sm_ArgList[0], the
 * same CurrentDir()+GetDiskObject() idiom every real Workbench-aware
 * program uses) and every argument it was launched with. Reported to
 * a plain text file (dos.library Open/FPuts/Close -- no stdio window,
 * since a genuine Workbench-style, non-CLI process has no console of
 * its own) at a fixed path the on-target test script reads back via
 * the existing FSGET verb, same "test-staging channel" convention
 * fs-test.py's own fixture use already established.
 *
 * If started from the Shell instead (no _WBenchMsg -- useful for
 * poking at this fixture by hand), just prints the same report to
 * stdout and exits; this is a debugging convenience, not this
 * fixture's actual job.
 */
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>

extern struct WBStartup *_WBenchMsg;

struct Library *IconBase;

#define RESULT_PATH "T:amipilot-wblaunch-result.txt"

static void WriteLine(BPTR fh, const char *line)
{
    Write(fh, (APTR)line, (LONG)strlen(line));
}

int main(void)
{
    BPTR out;
    char line[256];

    IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 0);

    out = Open((CONST_STRPTR)RESULT_PATH, MODE_NEWFILE);
    if (out == 0) {
        if (IconBase != NULL) {
            CloseLibrary(IconBase);
        }
        return 20;
    }

    if (_WBenchMsg == NULL) {
        WriteLine(out, "STARTED_FROM=SHELL\n");
        Close(out);
        if (IconBase != NULL) {
            CloseLibrary(IconBase);
        }
        return 0;
    }

    snprintf(line, sizeof(line), "STARTED_FROM=WORKBENCH\n");
    WriteLine(out, line);
    snprintf(line, sizeof(line), "NUMARGS=%ld\n", (long)_WBenchMsg->sm_NumArgs);
    WriteLine(out, line);

    if (_WBenchMsg->sm_NumArgs > 0) {
        LONG i;

        for (i = 0; i < _WBenchMsg->sm_NumArgs; i++) {
            snprintf(line, sizeof(line), "ARG%ld=%s\n", (long)i,
                     (const char *)_WBenchMsg->sm_ArgList[i].wa_Name);
            WriteLine(out, line);
        }

        if (IconBase != NULL) {
            struct DiskObject *dobj;

            /* The standard self-lookup idiom every Workbench-aware
             * program uses to find its own tooltypes: CurrentDir() to
             * the lock WBLAUNCH handed us for our own icon (arg 0),
             * then GetDiskObject() by name relative to it -- this is
             * exactly what makes AmiPilotServer's scratch-icon
             * TOOLTYPE= merge (wblaunch.c) actually reach a real,
             * unmodified target program. */
            CurrentDir(_WBenchMsg->sm_ArgList[0].wa_Lock);
            dobj = GetDiskObject(_WBenchMsg->sm_ArgList[0].wa_Name);
            if (dobj != NULL) {
                UBYTE *val;

                val = FindToolType((CONST_STRPTR *)dobj->do_ToolTypes, (CONST_STRPTR)"GREETING");
                snprintf(line, sizeof(line), "TOOLTYPE_GREETING=%s\n",
                         val != NULL ? (const char *)val : "(absent)");
                WriteLine(out, line);

                val = FindToolType((CONST_STRPTR *)dobj->do_ToolTypes, (CONST_STRPTR)"PORT");
                snprintf(line, sizeof(line), "TOOLTYPE_PORT=%s\n",
                         val != NULL ? (const char *)val : "(absent)");
                WriteLine(out, line);

                FreeDiskObject(dobj);
            } else {
                WriteLine(out, "TOOLTYPE_GREETING=(no icon found)\n");
                WriteLine(out, "TOOLTYPE_PORT=(no icon found)\n");
            }
        }
    }

    Close(out);
    if (IconBase != NULL) {
        CloseLibrary(IconBase);
    }
    return 0;
}
