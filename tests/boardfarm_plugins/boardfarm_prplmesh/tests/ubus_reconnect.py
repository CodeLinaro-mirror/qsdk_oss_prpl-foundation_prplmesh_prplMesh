# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

import time

from .prplmesh_base_test import PrplMeshBaseTest
from boardfarm.exceptions import SkipTest
from opts import debug


class UbusReconnect(PrplMeshBaseTest):
    """Restart ubusd on the DUT and check that the agent publishes its data model again."""

    def runTest(self):
        try:
            agent = self.dev.DUT.agent_entity
        except AttributeError as ae:
            raise SkipTest(ae)

        pid = agent.command("pidof", "beerocks_agent").strip()
        agent.nbapi_command("X_PRPLWARE-COM_Agent", "_get")

        debug("Restarting ubusd")
        agent.command("pkill", "-x", "ubusd")
        time.sleep(1)
        # setsid keeps ubusd alive after "docker exec" returns.
        agent.command("sh", "-c", "setsid ubusd </dev/null >/dev/null 2>&1 &")

        # amxrt retries 5-10 s after the disconnect.
        time.sleep(20)

        assert agent.command("pidof", "beerocks_agent").strip() == pid, \
            "beerocks_agent was restarted instead of reconnecting to ubusd"
        agent.nbapi_command("X_PRPLWARE-COM_Agent", "_get")
