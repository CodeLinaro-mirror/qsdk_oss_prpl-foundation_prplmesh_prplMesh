"""
Example
    run test case
        python3 orbit.py 5_1_4_connect_one_station_in_2g
    run test cases
        python3 orbit.py 5_1_4_connect_one_station_in_2g 5_1_23_wifi_multicast_in_2g
"""
import sys
import os
import requests
import time
import argparse
from urllib.parse import urljoin

config = {
    "server": "http://192.168.200.100/",
    "setup": "orbit-prpl-1",
    "dut": "Pi4_OSPv2"
}


class APIClient:
    def __init__(self, root: str, username: str = None, password: str = None, key: str = None):
        self.root = root
        self.auth = (username, password) if username and password else None
        self.headers = {"X-Orbit-API-Key": key} if key else None

    def _build_ep(self, path: str) -> str:
        return urljoin(self.root, path)

    def _get(self, path: str, params: dict = None) -> requests.Response:
        ep = self._build_ep(path)
        response = requests.get(ep, params=params, auth=self.auth, headers=self.headers)
        response.raise_for_status()
        return response

    def _put(self, path: str, data: dict = None) -> requests.Response:
        ep = self._build_ep(path)
        response = requests.put(ep, json=data, auth=self.auth, headers=self.headers)
        response.raise_for_status()
        return response

    def _post(self, path: str, data: dict = None) -> requests.Response:
        ep = self._build_ep(path)
        response = requests.post(ep, json=data, auth=self.auth, headers=self.headers)
        response.raise_for_status()
        return response

    def _del(self, path: str, data: dict = None) -> requests.Response:
        ep = self._build_ep(path)
        response = requests.delete(ep, json=data, auth=self.auth, headers=self.headers)
        response.raise_for_status()
        return response

    def getSetups(self, name: str = None):
        filters = {
            "type": "setup",
            "filters": {}
        }
        if name:
            filters["filters"]["setup_name"] = name
        try:
            r = self._post(path="/api/configs", data=filters)
            r.raise_for_status()
        except requests.exceptions.RequestException as e:
            print(f"Error fetching SETUP config: {e}")
            raise SystemExit(1)
        except ValueError:
            print("Invalid JSON response received while fetching SETUP config.")
            raise SystemExit(1)

        data = r.json()

        if not data or not data[0]:
            print("SETUP config not found or empty")
            raise SystemExit(1)

        return data

    def getDuts(self, name: str = None):
        filters = {
            "type": "dut",
            "filters": {}
        }
        if name:
            filters["filters"]["config_name"] = name
        try:
            r = self._post(path="/api/configs", data=filters)
            r.raise_for_status()
        except requests.exceptions.RequestException as e:
            print(f"Error fetching DUT config: {e}")
            raise SystemExit(1)
        except ValueError:
            print("Invalid JSON response received while fetching DUT config.")
            raise SystemExit(1)

        data = r.json()

        if not data or not data[0]:
            print("DUT config not found or empty")
            raise SystemExit(1)

        return data

    def cleanEnvelopes(self):
        r = self._del(path="/runner/envelopes/", data={"placeholder": "placeholder"})
        return r.json()

    def countDeviceEnvelopes(self, device):
        filters = {
            "operation": "list",
            "device_id": device
        }
        try:
            r = self._post(path="/runner/envelopes/", data=filters)
            r.raise_for_status()
        except requests.exceptions.RequestException as e:
            print(f"Error fetching from runner: {e}")
            raise SystemExit(1)
        except ValueError:
            print("Invalid JSON response received while fetching from runner.")
            raise SystemExit(1)

        data = r.json()
        return len(data)

    def remoteCmd(self, cmd):
        try:
            r = self._post(path="/os/cmd/", data={"cmd": cmd})
            r.raise_for_status()
        except requests.exceptions.RequestException as e:
            print(f"Error fetching from runner: {e}")
            raise SystemExit(1)
        except ValueError:
            print("Invalid JSON response received while fetching from runner.")
            raise SystemExit(1)
        data = r.json()
        return data["stdout"]

    def registExecution(self, setup, dut, testName):
        test = [
            {
                "test": testName,
                "subtest": None
            }
        ]
        data = {
            "setup_config": setup,
            "dut_config": dut,
            "tests": test,
            "execute": True
        }
        try:
            r = self._put(path="/api/executions", data=data)
            r.raise_for_status()
        except requests.exceptions.RequestException as e:
            print(f"Error fetching from runner: {e}")
            raise SystemExit(1)
        except ValueError:
            print("Invalid JSON response received while fetching from runner.")
            raise SystemExit(1)
        rsp = r.json()
        return rsp["registered_tests"][0]

    def pollExecution(self, execid):
        data = {
            "exec_id": execid,
            "type": "info"
        }
        try:
            r = self._post(path="/api/executions", data=data)
            r.raise_for_status()
        except requests.exceptions.RequestException as e:
            print(f"Error fetching from runner: {e}")
            raise SystemExit(1)
        except ValueError:
            print("Invalid JSON response received while fetching from runner.")
            raise SystemExit(1)
        rsp = r.json()
        return rsp

    def downloadExecution(self, execid, fileName):
        data = {
            "exec_id": execid,
            "type": "output",
            "wlog_only": False
        }
        try:
            r = self._post(path="/api/executions", data=data)
            r.raise_for_status()
        except requests.exceptions.RequestException as e:
            print(f"Error fetching from runner: {e}")
            raise SystemExit(1)
        except ValueError:
            print("Invalid JSON response received while fetching from runner.")
            raise SystemExit(1)
        with open(fileName, "wb") as f:
            f.write(r.content)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "list",
        nargs="*",
        help="list of test cases"
    )
    args = parser.parse_args()
    lists = [x for x in args.list if x.strip()]
    if not lists:
        raise RuntimeError("usage: python3 orbit.py testcase")

    tests = ["edge_test_case_" + name for name in lists]

    server_user = os.environ.get("CI_ORBIT_SERVER_USER")
    server_password = os.environ.get("CI_ORBIT_SERVER_PASSWORD")
    if not (server_user and server_password):
        raise RuntimeError("CI_ORBIT_SERVER_USER and/or CI_ORBIT_SERVER_PASSWORD not visible")
    clientToServer = APIClient(
        root=config["server"],
        username=server_user,
        password=server_password
    )

    data = clientToServer.getSetups(config["setup"])
    setupData = next((entry for entry in data if entry["setup_name"] == config["setup"]), None)
    if setupData is None:
        print(f'No SETUP found with name {config["setup"]}')
        sys.exit(1)

    data = clientToServer.getDuts(config["dut"])
    deviceData = next((entry for entry in data if entry["config_name"] == config["dut"]), None)
    if deviceData is None:
        print(f'No DUT found with name {config["dut"]}')
        sys.exit(1)

    runner = setupData["runner_pc"]["pc_ip"]
    device = deviceData["test_dut"]["device_id"]

    print(f"Runner Address: {runner}, Device ID: {device}")

    print("")
    runner_key = os.environ.get("CI_ORBIT_RUNNER_API_KEY")
    if not runner_key:
        raise RuntimeError("CI_ORBIT_RUNNER_API_KEY is not visible")
    clientToRunner = APIClient(
        root=f"http://{runner}:6001/",
        username=setupData["runner_pc"]["pc_user"],
        password=setupData["runner_pc"]["pc_pass"],
        key=runner_key
    )

    PASSLIST = []
    FAILLIST = []
    for t in tests:
        print("")
        print(f"************************ Regist execution of {t} ******************************")
        execid = clientToServer.registExecution(setup=setupData, dut=deviceData, testName=t)
        print(f"Registed execution of {t} with ID: {execid}")
        startTime = time.time()
        while True:
            time.sleep(60)
            rsp = clientToServer.pollExecution(execid=execid)
            status = rsp["status"]
            if status == "ended":
                print(f"Execid {execid} ended")
                break
            if time.time() - startTime >= 60*60:
                print(f"Execid {execid} timed out after 1 hour.")
                break
            print(f"Execid {execid} waiting...")

        rsp = clientToServer.pollExecution(execid=execid)
        status = rsp["status"]
        result = "timeout"
        if status == "ended":
            result = rsp["result"]
        print(f"Execid {execid} {result}")
        fileName = f"{t}.zip"
        clientToServer.downloadExecution(execid=execid, fileName=fileName)
        if result == "pass":
            PASSLIST.append(t)
        else:
            FAILLIST.append(t)

    print("")
    print("************************ All executions finished ******************************")

    print("")
    print("PASSED:")
    for p in PASSLIST:
        print(f"    {p}")
    if not FAILLIST:
        sys.exit(0)

    print("")
    print("FAILED:")
    for f in FAILLIST:
        print(f"    {f}")
    sys.exit(1)
