/* arexx-manifest-test.rexx -- drives AmiPilotServer entirely through
 * manifest ("@name") locators, for the on-target regression check
 * (tests/copperline/run.sh). Same OPTIONS RESULTS / resident-RexxMast
 * requirements as arexx-test.rexx.
 *
 * The point of this script vs the ID-based one: every gadget reference
 * below is a logical name from fixtures/gadtools-app's GTApp.manifest
 * -- no GA_ID, window title, or position appears anywhere. If the
 * fixture relayouts or relabels, this script must keep passing
 * untouched (the implementation plan's own success criterion for the
 * manifest tier); only a GA_ID change would touch the manifest, and
 * even then only the manifest.
 */
OPTIONS RESULTS
ADDRESS 'AMIPILOT.1'

CALL PROBE 'MANIFEST SRC:fixtures/gadtools-app/GTApp.manifest', 'LOAD'
CALL PROBE 'CLICK @nosuchname', 'BADNAME'
CALL PROBE 'TYPE @host_field aminet.net', 'TYPE-HOST'
CALL PROBE 'GETTEXT @host_field', 'GETTEXT-HOST'
CALL PROBE 'CLICK @connect_button', 'CLICK-CONNECT'
CALL PROBE 'GETTEXT @host_field', 'GONE'
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
