Mesh-wide push-button onboarding, restore the saved DUT state:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"
  $ . "$TESTDIR/mesh_pbc_lib.sh"
  $ R logger -t cram "Starting $TESTFILE"

The Controller saves its store at an instance delete, so the templates go last:

  $ M stop_session
  $ M restore_restart "$UD"
  $ M restore_templates
  $ M delete_templates
  $ M restore "$PM.ManagementMode"
  $ M restore "$PM.Enable"
  $ R rm -f "$STATE" "$OUT" "$OUT.now" "$OUT.tmp" "$READY"
