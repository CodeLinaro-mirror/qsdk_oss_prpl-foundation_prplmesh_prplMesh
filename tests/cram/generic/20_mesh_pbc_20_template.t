Mesh-wide push-button onboarding, sessions on the template, no Enrollee:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"
  $ . "$TESTDIR/mesh_pbc_lib.sh"
  $ R logger -t cram "Starting $TESTFILE"
  $ R test -f "$READY" || exit 80
  $ T=$(M tpl cram)
  $ A=$(M agent)
  $ M ready 300

Session timeout:

  $ B=$(M bss)
  $ M stats_mark "$T"
  $ M within 10 call "${T}InitiatePBC()"
  Status=Error_Timeout
  FailureReason=0
  PBCSessionID=([0-9a-f]{2}:){5}[0-9a-f]{2}-[0-9a-f]{4} (re)
  $ S=$(M get "${T}Status.PBCSessionID")
  $ M reply "$OUT.now" | grep -Fx "PBCSessionID=$S"
  PBCSessionID=([0-9a-f]{2}:){5}[0-9a-f]{2}-[0-9a-f]{4} (re)
  $ M show "${T}Status." State Result TimeStamp
  State=Idle
  Result=Error_Timeout
  TimeStamp=[1-9]\d{3}-\d\d-\d\dT\d\d:\d\d:\d\d(\.\d+)?Z (re)
  $ M poll "${B}X_PRPLWARE-COM_MeshPBC.State" Idle 15
  $ M get "${B}X_PRPLWARE-COM_MeshPBC.PBCSessionID" | grep -Fx "$S"
  ([0-9a-f]{2}:){5}[0-9a-f]{2}-[0-9a-f]{4} (re)
  $ M poll "${A}WPS.Status" Configured 15
  $ M stats_delta "$T"
  Sessions+1
  Timeouts+1

Unknown BSSID, rejected at once:

  $ M within 2 call "${T}InitiatePBC(BSSIDList=\"00:11:22:33:44:55\")"
  Status=Error_Invalid_Input
  FailureReason=0
  $ M get "${T}Status.Result"
  Error_Invalid_Input
  $ M stats_delta "$T"
  InvalidInput+1

Disabled template:

  $ M put "${T}Enable" 0
  $ M poll "${A}WPS.Status" Unconfigured 60
  $ M within 2 call "${T}InitiatePBC()"
  Status=Error_Invalid_Input
  FailureReason=0
  $ M get "${T}Status.Result"
  Error_Invalid_Input
  $ M stats_delta "$T"
  InvalidInput+1
  $ M put "${T}ListenDuration" 20
  $ M put "${T}Enable" 1
  $ M ready 300

One session at a time:

  $ B=$(M bss)
  $ M bg "${T}InitiatePBC()"
  $ M poll "${T}Status.State" InProgress 10
  $ M get "${A}WPS.Status"
  InProgress
  $ M within 2 call "${T}InitiatePBC()"
  Status=Error_Not_Ready
  FailureReason=0
  $ M within 2 call "${A}X_PRPLWARE-COM_InitiatePBC()"
  Status=Error_Not_Ready
  FailureReason=0
  $ M call "${T}TerminatePBC()"
  $ M collect 10
  Status=Canceled
  FailureReason=0
  PBCSessionID=([0-9a-f]{2}:){5}[0-9a-f]{2}-[0-9a-f]{4} (re)
  $ M get "${T}Status.Result"
  Canceled
  $ M poll "${B}X_PRPLWARE-COM_MeshPBC.Result" Canceled 15
  $ M call "${T}TerminatePBC()"
  error 10
  $ M stats_delta "$T"
  Sessions+1
  Canceled+1
  $ M put "${T}ListenDuration" 5

Session history, one entry:

  $ M put "${T}Status.MaxHistoryEntries" 0
  $ M poll "${T}Status.HistoryNumberOfEntries" 0 5
  $ M put "${T}Status.MaxHistoryEntries" 1
  $ M poll "${B}X_PRPLWARE-COM_MeshPBC.State" Idle 15
  $ M within 10 call "${T}InitiatePBC()"
  Status=Error_Timeout
  FailureReason=0
  PBCSessionID=([0-9a-f]{2}:){5}[0-9a-f]{2}-[0-9a-f]{4} (re)
  $ S1=$(M get "${T}Status.PBCSessionID")
  $ M poll "${B}X_PRPLWARE-COM_MeshPBC.State" Idle 15
  $ M within 10 call "${T}InitiatePBC()"
  Status=Error_Timeout
  FailureReason=0
  PBCSessionID=([0-9a-f]{2}:){5}[0-9a-f]{2}-[0-9a-f]{4} (re)
  $ S2=$(M get "${T}Status.PBCSessionID")
  $ M get "${T}Status.HistoryNumberOfEntries"
  1
  $ M get "${T}Status.History.*.PBCSessionID" | grep -Fx "$S1"
  ([0-9a-f]{2}:){5}[0-9a-f]{2}-[0-9a-f]{4} (re)
  $ M get "${T}Status.History.*.Result"
  Error_Timeout
  $ H=$(M history_entry "$T")
  $ M put "${H}Result" Success
  error 15
  $ R ba-cli -l "${H}-" 2>&1 | errors
  error 11
  $ M poll "${B}X_PRPLWARE-COM_MeshPBC.State" Idle 15
  $ M within 10 call "${T}InitiatePBC()"
  Status=Error_Timeout
  FailureReason=0
  PBCSessionID=([0-9a-f]{2}:){5}[0-9a-f]{2}-[0-9a-f]{4} (re)
  $ M get "${T}Status.HistoryNumberOfEntries"
  1
  $ M get "${T}Status.History.*.PBCSessionID" | grep -Fx "$S2"
  ([0-9a-f]{2}:){5}[0-9a-f]{2}-[0-9a-f]{4} (re)
  $ M stats_delta "$T"
  Sessions+3
  Timeouts+3

No history:

  $ M put "${T}Status.MaxHistoryEntries" 0
  $ M poll "${T}Status.HistoryNumberOfEntries" 0 5
  $ M put "${T}Status.MaxHistoryEntries" 10
  $ M get "${T}Status.MaxHistoryEntries"
  10
