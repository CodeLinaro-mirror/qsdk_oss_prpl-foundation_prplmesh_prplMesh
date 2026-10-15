Load the portable pair helpers and define prplOS-style remote aliases:

  $ . "$TESTDIR/utils.sh"
  $ . "$TESTDIR/ieee1905.sh"
  $ set -o pipefail
  $ shopt -s expand_aliases
  $ alias C='pair_remote controller'
  $ alias A='pair_remote agent'
  $ pair_init ieee1905-eth

Bring-up. Setup: the GW (controller board in Multi-AP-Controller-and-Agent mode) is connected over Ethernet to an EasyMesh device (agent board in Multi-AP-Agent mode). This is Setup B of the test plan; IEEE1905.t runs the data model and update checks next, with PAIR_IEEE1905_BACKHAUL=eth.

Verify that the expected controller and agent received this build:

  $ pair_preflight

Start prplMesh as controller and agent on the controller board:

  $ pair_restart_prplmesh controller Multi-AP-Controller-and-Agent

Start the agent with Ethernet backhaul:

  $ pair_restart_prplmesh agent Multi-AP-Agent
  $ A "ip link set dev '${TS_AGENT_WIRED}' up"
  $ A "ba-cli 'X_PRPLWARE-COM_Agent.Configuration.BackhaulWireDiscoveryMode=\"StaticList\"'" >/dev/null
  $ A "ba-cli 'X_PRPLWARE-COM_Agent.Configuration.BackhaulWireInterface=\"${TS_AGENT_WIRED}\"'" >/dev/null
  $ pair_restart_prplmesh agent Multi-AP-Agent
  $ pair_wait_agent_operational
