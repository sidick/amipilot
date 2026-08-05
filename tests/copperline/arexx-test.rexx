/* arexx-test.rexx -- drives AmiPilotServer's ARexx port (phase 0.2) for
 * the on-target regression check (tests/copperline/run.sh). Run via `rx`
 * under a real resident RexxMast so RC/RESULT populate exactly as a
 * real user's script would see -- same technique and same OPTIONS
 * RESULTS requirement as ../amiauth's own arexx-probe.rexx (a hand-
 * rolled RexxMsg from a plain external C program never validates via
 * IsRexxMsg(), since rm_TaskBlock is only populated by a live ARexx
 * task's own context -- this has to go through the real interpreter).
 *
 * Exercises the phase 0.2 release gate end to end, entirely via ARexx,
 * no host involved: types into fixtures/gadtools-app's Host string
 * gadget and reads the value back (state changed, driven and observed
 * over the port), then clicks Connect and confirms the window is gone
 * afterward (the fixture quits on that click -- the literal "click a
 * button, assert something changed" the plan's release gate describes).
 */
OPTIONS RESULTS
ADDRESS 'AMIPILOT.1'

CALL PROBE 'TREE GadTools', 'TREE-BEFORE'
CALL PROBE 'CLICK GadTools 2', 'CLICK-HOST'
CALL PROBE 'TYPE GadTools 2 hello amipilot', 'TYPE-HOST'
CALL PROBE 'GETTEXT GadTools 2', 'GETTEXT-HOST'
CALL PROBE 'CLICK GadTools 1', 'CLICK-CONNECT'
CALL PROBE 'TREE GadTools', 'TREE-AFTER'
CALL PROBE 'QUIT', 'QUIT'
EXIT

PROBE: PROCEDURE
  cmd = ARG(1)
  tag = ARG(2)
  cmd
  if symbol('RESULT') = 'VAR' then r = RESULT
  else r = ''
  SAY tag' RC='RC' RESULT='r
  RETURN
