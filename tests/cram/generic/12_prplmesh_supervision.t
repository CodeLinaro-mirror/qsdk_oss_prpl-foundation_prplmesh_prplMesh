Create R alias:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"

  $ R logger -t cram "Starting $TESTFILE"

The supervisor and the daemons it starts (beerocks_fronthaul is a child of the agent):

  $ PATTERN='/opt/prplmesh/(scripts/prplmesh_utils.sh start -f|bin/(ieee1905_transport|beerocks_(controller|agent|vendor_message))$)'
  $ BEFORE=$(R "pgrep -af '$PATTERN'")
  $ echo "$BEFORE" | sed -E 's/^[0-9]+ //' | sort
  /bin/sh /opt/prplmesh/scripts/prplmesh_utils.sh start -f
  /opt/prplmesh/bin/beerocks_agent
  /opt/prplmesh/bin/beerocks_controller
  /opt/prplmesh/bin/beerocks_vendor_message
  /opt/prplmesh/bin/ieee1905_transport

Kill the transport like a crash would, without a core dump:

  $ R "kill -KILL \$(pgrep -f '^/opt/prplmesh/bin/ieee1905_transport$')"

The supervisor stops the other daemons and exits, procd respawns it after 5 s:

  $ sleep 30

  $ AFTER=$(R "pgrep -af '$PATTERN'")
  $ echo "$AFTER" | sed -E 's/^[0-9]+ //' | sort
  /bin/sh /opt/prplmesh/scripts/prplmesh_utils.sh start -f
  /opt/prplmesh/bin/beerocks_agent
  /opt/prplmesh/bin/beerocks_controller
  /opt/prplmesh/bin/beerocks_vendor_message
  /opt/prplmesh/bin/ieee1905_transport

No process from before the kill is still running:

  $ for pid in $(echo "$BEFORE" | cut -d' ' -f1); do echo "$AFTER" | cut -d' ' -f1 | grep -x "$pid"; done | sed 's/^/survived: /'

  $ R logger -t cram "$TESTFILE finished!"
