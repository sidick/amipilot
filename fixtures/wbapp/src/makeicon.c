/*
 * makeicon -- a tiny, Shell-launched, build/test-time-only helper that
 * stamps a real, valid .info icon for the wbapp fixture (see main.c)
 * so the on-target WBLAUNCH check (tests/copperline/run.sh) has a
 * genuine icon to launch -- this project's icon-authoring tools don't
 * hand-author binary .info files (a real, documented, but fiddly
 * format -- image data, gadget geometry, etc.), so this instead reads
 * the SYSTEM'S OWN default WBTOOL icon (GetDefDiskObject(), V36) and
 * writes it back out under the fixture's own name with two baked-in
 * tooltypes, GREETING and PORT. AmiPilotServer's own WBLAUNCH
 * TOOLTYPE= merge (server/src/wblaunch.c) is exercised by overriding
 * PORT and leaving GREETING alone -- proving both the override and
 * the preserve-what-wasn't-named halves of that merge against a real
 * icon.library-round-tripped icon, not a hand-crafted one.
 *
 * Takes the icon's target path (WITHOUT ".info") as its one argument.
 */
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <workbench/workbench.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>

struct Library *IconBase;

int main(int argc, char **argv)
{
    struct DiskObject *dobj;
    STRPTR toolTypes[3];
    int rc = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: MakeIcon <path-without-.info>\n");
        return 20;
    }

    IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 0);
    if (IconBase == NULL) {
        fprintf(stderr, "MakeIcon: could not open icon.library\n");
        return 20;
    }

    dobj = GetDefDiskObject(WBTOOL);
    if (dobj == NULL) {
        fprintf(stderr, "MakeIcon: GetDefDiskObject(WBTOOL) failed\n");
        CloseLibrary(IconBase);
        return 20;
    }

    toolTypes[0] = (STRPTR)"GREETING=hello from the default icon";
    toolTypes[1] = (STRPTR)"PORT=1111";
    toolTypes[2] = NULL;
    dobj->do_ToolTypes = toolTypes;

    if (!PutDiskObject((CONST_STRPTR)argv[1], dobj)) {
        fprintf(stderr, "MakeIcon: PutDiskObject(%s) failed\n", argv[1]);
        rc = 20;
    }

    FreeDiskObject(dobj);
    CloseLibrary(IconBase);
    return rc;
}
