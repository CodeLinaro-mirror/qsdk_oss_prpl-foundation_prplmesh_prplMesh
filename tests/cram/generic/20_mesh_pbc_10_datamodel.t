Mesh-wide push-button onboarding, Controller data model:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"
  $ . "$TESTDIR/mesh_pbc_lib.sh"
  $ R logger -t cram "Starting $TESTFILE"
  $ R test -f "$READY" || exit 80
  $ T=$(M tpl cram)
  $ A=$(M agent)

Values of the template "cram":

  $ M show "$T" PBCTemplateID Enable PushButtonMethod ListenDuration WPS.DevicePasswordID WPS.DevicePassword
  PBCTemplateID=cram
  Enable=1
  PushButtonMethod=WPS
  ListenDuration=5
  WPS.DevicePasswordID=4
  WPS.DevicePassword=
  $ M get "${T}Status.State"
  Idle

Defaults of a new instance:

  $ R "ba-cli -j -l '$PT.+{PBCTemplateID=\"cram-default\"}'" | sed '/^$/d'
  {"X_PRPLWARE-COM_Templates.X_PRPLWARE-COM_MeshPBCTemplate.\d+.":{"PBCTemplateID":"cram-default"}} (re)
  $ D=$(M tpl cram-default)
  $ M show "$D" Enable PushButtonMethod ListenDuration WPS.DevicePasswordID WPS.DevicePassword
  Enable=1
  PushButtonMethod=WPS
  ListenDuration=120
  WPS.DevicePasswordID=4
  WPS.DevicePassword=
  $ M show "${D}Status." State Result FailureReason EnrolleeMACAddress BSSID TimeStamp PBCSessionID
  State=Idle
  Result=
  FailureReason=0
  EnrolleeMACAddress=
  BSSID=
  TimeStamp=0001-01-01T00:00:00Z
  PBCSessionID=
  $ R ba-cli -l "${D}-" 2>&1 | errors
  $ M tpl cram-default

UUID-E format:

  $ U=$(M get "$TT.Network.X_PRPLWARE-COM_MeshPBCUUID_E")
  $ echo "$U"
  [0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12} (re)

Agent capability and WPS status:

  $ M show "$A" X_PRPLWARE-COM_MeshWidePBCOnboardingSupport WPS.Status
  X_PRPLWARE-COM_MeshWidePBCOnboardingSupport=1
  WPS.Status=Configured
  $ M show "Device.WiFi.DataElements.${A#X_PRPLWARE-COM_WiFiController.}" X_PRPLWARE-COM_MeshWidePBCOnboardingSupport WPS.Status
  X_PRPLWARE-COM_MeshWidePBCOnboardingSupport=1
  WPS.Status=Configured

Template references:

  $ M get "$(M ssc_ref)" | grep -Fx "${T%.}"
  X_PRPLWARE-COM_Templates\.X_PRPLWARE-COM_MeshPBCTemplate\.\d+ (re)
  $ M get "$TT.Network.X_PRPLWARE-COM_PrimaryMeshPBCTemplateReference" | grep -Fx "${T%.}"
  X_PRPLWARE-COM_Templates\.X_PRPLWARE-COM_MeshPBCTemplate\.\d+ (re)

Rejected writes:

  $ M put "${T}ListenDuration" 0
  error 10
  $ M put "${T}PushButtonMethod" FOO
  error 10
  $ M put "${T}PushButtonMethod" ""
  error 10
  $ M put "${T}PBCTemplateID" ""
  error 10
  $ R "ba-cli -l '$PT.+{PBCTemplateID=\"cram\"}'" 2>&1 | errors
  error 13
  $ M put "$TT.Network.X_PRPLWARE-COM_MeshPBCUUID_E" asdf
  error 10
  $ M put "$TT.Network.X_PRPLWARE-COM_MeshPBCUUID_E" ""
  error 10
  $ M put "${T}Status.State" InProgress
  error 15
  $ M put "${A}X_PRPLWARE-COM_MeshWidePBCOnboardingSupport" 0
  error 15

Values after the rejected writes:

  $ M show "$T" PBCTemplateID PushButtonMethod ListenDuration Status.State
  PBCTemplateID=cram
  PushButtonMethod=WPS
  ListenDuration=5
  Status.State=Idle
  $ M get "$TT.Network.X_PRPLWARE-COM_MeshPBCUUID_E" | grep -Fx "$U"
  [0-9a-fA-F-]{36} (re)
  $ M get "${A}X_PRPLWARE-COM_MeshWidePBCOnboardingSupport"
  1
  $ R "ba-cli -l '$PT.*.PBCTemplateID?'" | grep -c '^cram$'
  1
