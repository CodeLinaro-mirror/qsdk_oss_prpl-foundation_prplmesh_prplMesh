Mesh-wide push-button onboarding, a press on the Agent of the DUT:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"
  $ . "$TESTDIR/mesh_pbc_lib.sh"
  $ R logger -t cram "Starting $TESTFILE"
  $ R test -f "$READY" || exit 80
  $ T=$(M tpl cram)
  $ M ready 300

Button press:

  $ R "ba-cli -j -l 'X_PRPLWARE-COM_Agent.MeshWidePBCOnboarding.InitiatePBC()'" 2>&1 | errors
  $ M poll "${T}Status.State" InProgress 5
  $ M get "${T}Status.PBCSessionID" | grep -F "$(M get "$NW.ID")-"
  ([0-9a-f]{2}:){5}[0-9a-f]{2}-[0-9a-f]{4} (re)
  $ M poll "${T}Status.State" Idle 15
  $ M get "${T}Status.Result"
  Error_Timeout
