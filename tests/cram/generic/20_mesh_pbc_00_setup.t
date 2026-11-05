Mesh-wide push-button onboarding, save the DUT state and set up the DUT:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"
  $ . "$TESTDIR/mesh_pbc_lib.sh"
  $ R logger -t cram "Starting $TESTFILE"
  $ R rm -f "$READY"

Controller-and-Agent mode:

  $ M keep "$PM.ManagementMode"
  $ M keep "$PM.Enable"
  $ M put "$PM.ManagementMode" Multi-AP-Controller-and-Agent
  $ M put "$PM.Enable" 1
  $ M poll "$NW.ID" '..:..:..:..:..:..' 300

UseDataElementsVapConfigs:

  $ M save_controller
  $ M set_restart "$UD" 1
  $ M get "$UD"
  1

Template "cram":

  $ R "ba-cli -j -l '$PT.+{PBCTemplateID=\"cram\", ListenDuration=5}'" | sed '/^$/d'
  {"X_PRPLWARE-COM_Templates.X_PRPLWARE-COM_MeshPBCTemplate.\d+.":{"PBCTemplateID":"cram"}} (re)
  $ T=$(M tpl cram)
  $ M put "$TT.Network.X_PRPLWARE-COM_PrimaryMeshPBCTemplateReference" "${T%.}"
  $ M put "$(M bss_tpl_enable)" 1
  $ M put "$TT.Network.Enable" 1
  $ M link_template 660
