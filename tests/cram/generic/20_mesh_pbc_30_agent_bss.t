Mesh-wide push-button onboarding, sessions on one Agent and one BSS, no Enrollee:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"
  $ . "$TESTDIR/mesh_pbc_lib.sh"
  $ R logger -t cram "Starting $TESTFILE"
  $ R test -f "$READY" || exit 80
  $ T=$(M tpl cram)
  $ A=$(M agent)
  $ M put "${T}ListenDuration" 20

Unknown template:

  $ M within 2 call "${A}X_PRPLWARE-COM_InitiatePBC(PBCTemplateID=\"nosuch\")"
  Status=Error_Invalid_Input
  FailureReason=0
  $ M ready 300

Per-Agent call with the Primary template:

  $ B=$(M bss)
  $ M bg "${A}X_PRPLWARE-COM_InitiatePBC()"
  $ M poll "${T}Status.State" InProgress 10
  $ M get "${B}X_PRPLWARE-COM_MeshPBC.State"
  (Requested|InProgress) (re)
  $ M call "${A}X_PRPLWARE-COM_TerminatePBC(PBCSessionID=\"garbage\")"
  error 10
  $ M get "${T}Status.State"
  InProgress
  $ S=$(M get "${T}Status.PBCSessionID" | tr a-f A-F)
  $ M call "${A}X_PRPLWARE-COM_TerminatePBC(PBCSessionID=\"$S\")"
  $ M collect 10 | grep Status=
  Status=Error_Other

Named template:

  $ M ready 300
  $ B=$(M bss)
  $ M bg "${A}X_PRPLWARE-COM_InitiatePBC(PBCTemplateID=\"cram\")"
  $ M poll "${T}Status.State" InProgress 10
  $ M get "${B}X_PRPLWARE-COM_MeshPBC.State"
  (Requested|InProgress) (re)
  $ S=$(M get "${T}Status.PBCSessionID")
  $ M call "${A}X_PRPLWARE-COM_TerminatePBC(PBCSessionID=\"$S\")"
  $ M collect 10 | grep Status=
  Status=Error_Other

Per-Agent call with the BSSID of one BSS, timeout:

  $ M ready 300
  $ B=$(M bss)
  $ BSSID=$(M get "${B}BSSID")
  $ echo "$BSSID"
  ([0-9a-f]{2}:){5}[0-9a-f]{2} (re)
  $ M within 25 call "${A}X_PRPLWARE-COM_InitiatePBC(BSSIDList=\"$BSSID\")"
  Status=Error_Timeout
  FailureReason=0
  PBCSessionID=([0-9a-f]{2}:){5}[0-9a-f]{2}-[0-9a-f]{4} (re)
  $ M poll "${B}X_PRPLWARE-COM_MeshPBC.Result" Error_Timeout 15
  $ M put "${T}ListenDuration" 5
