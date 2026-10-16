"""
Example
    run test case
        python3 orbit.py 5_1_4_connect_one_station_in_2g
    run test cases
        python3 orbit.py 5_1_4_connect_one_station_in_2g 5_1_23_wifi_multicast_in_2g
    clean up unfinished execution (for ci)
        python3 orbit.py --cleanup
"""
import sys
import os
import requests
import time
import argparse
import signal
import zipfile
from datetime import datetime
from urllib.parse import urljoin

config = {
    "server": "http://192.168.200.100/",
    "setup": "orbit-prpl-1",
    "dut": "Pi4_OSPv2"
}

WLOG = "wlog.txt"
CURRENTEXEC = ".orbit_current_exec"
PER_TEST_TIMEOUT = 60 * 60
CI_JOB_TIMEOUT_MARGIN = 120


class APIClient:
    def __init__(self, root: str, username: str = None, password: str = None, key: str = None):
        self.root = root
        self.auth = (username, password) if username and password else None
        self.headers = {"X-Orbit-API-Key": key} if key else None

    def _build_ep(self, path: str) -> str:
        return urljoin(self.root, path)

    def _get(self, path: str, params: dict = None) -> requests.Response:
        ep = self._build_ep(path)
        response = requests.get(ep, params=params, auth=self.auth,
                                headers=self.headers, timeout=(3, 5))
        response.raise_for_status()
        return response

    def _put(self, path: str, data: dict = None) -> requests.Response:
        ep = self._build_ep(path)
        response = requests.put(ep, json=data, auth=self.auth,
                                headers=self.headers, timeout=(3, 5))
        response.raise_for_status()
        return response

    def _post(self, path: str, data: dict = None) -> requests.Response:
        ep = self._build_ep(path)
        response = requests.post(ep, json=data, auth=self.auth,
                                 headers=self.headers, timeout=(3, 5))
        response.raise_for_status()
        return response

    def _del(self, path: str, data: dict = None) -> requests.Response:
        ep = self._build_ep(path)
        response = requests.delete(ep, json=data, auth=self.auth,
                                   headers=self.headers, timeout=(3, 5))
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

    def deleteExecution(self, execid):
        data = {
            "exec_id": execid
        }
        try:
            r = self._del(path="/api/executions", data=data)
            r.raise_for_status()
        except requests.exceptions.RequestException as e:
            print(f"Error fetching from runner: {e}")
            raise SystemExit(1)
        except ValueError:
            print("Invalid JSON response received while fetching from runner.")
            raise SystemExit(1)
        print(f"Deleted execution with ID: {execid}")

    def pollLoglines(self, execid, position: int = 0):
        data = {
            "exec_id": execid,
            "from_position": position,
            "type": "test-flow"
        }
        try:
            r = self._post(path="/api/executions", data=data)
            r.raise_for_status()
            rsp = r.json()
        except (requests.exceptions.RequestException, ValueError):
            return position
        if rsp.get("content"):
            print(rsp["content"])
        return rsp.get("total_length", position)

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

    def downloadExecution(self, execid, fileName) -> bool:
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
            return False
        with open(fileName, "wb") as f:
            f.write(r.content)
        return True


def saveCurrentExec(execid):
    try:
        with open(CURRENTEXEC, "w") as f:
            f.write(str(execid))
    except OSError as e:
        print(f"Warning: could not write {CURRENTEXEC}: {e}")


def clearCurrentExec():
    try:
        os.remove(CURRENTEXEC)
    except FileNotFoundError:
        pass
    except OSError as e:
        print(f"Warning: could not remove {CURRENTEXEC}: {e}")


def stopExecution(client: "APIClient", execid) -> bool:
    try:
        rsp = client.pollExecution(execid=execid)
        if rsp.get("status") == "ended":
            return True
    except (SystemExit, Exception):
        pass
    try:
        client.deleteExecution(execid=execid)
        return True
    except (SystemExit, Exception) as e:
        print(f"Failed to stop execution {execid}: {e}")
        return False


def cleanupFromStateFile(client: "APIClient") -> int:
    try:
        with open(CURRENTEXEC) as f:
            execid = f.read().strip()
    except FileNotFoundError:
        print("No running execution recorded, nothing to clean up.")
        return 0
    if not execid:
        clearCurrentExec()
        return 0
    print(f"Stopping leftover execution {execid}")
    ok = stopExecution(client, execid)
    if ok:
        clearCurrentExec()
    return 0 if ok else 1


def ciJobDeadline():
    timeout = os.environ.get("CI_JOB_TIMEOUT")
    if not timeout:
        return None
    try:
        timeout = int(timeout)
    except ValueError:
        return None
    jobStart = time.time()
    startedAt = os.environ.get("CI_JOB_STARTED_AT")
    if startedAt:
        try:
            jobStart = datetime.fromisoformat(startedAt.replace("Z", "+00:00")).timestamp()
        except ValueError:
            pass
    return jobStart + timeout - CI_JOB_TIMEOUT_MARGIN


def readWlog(zipPath: str):
    try:
        with zipfile.ZipFile(zipPath) as zf:
            member = next((n for n in zf.namelist()
                           if os.path.basename(n) == WLOG), None)
            if member is None:
                print(f"{WLOG} not found in {zipPath}")
                return None
            raw = zf.read(member)
    except (zipfile.BadZipFile, OSError) as e:
        print(f"Could not open {zipPath}: {e}")
        return None
    return raw.decode("utf-8", errors="replace")


def printRemainingLog(zipPath: str, position: int):
    text = readWlog(zipPath)
    if text is None:
        return
    if position <= 0 or position > len(text):
        return
    rest = text[position:]
    if rest.strip():
        print(rest)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "list",
        nargs="*",
        help="list of test cases"
    )
    parser.add_argument(
        "--cleanup",
        action="store_true",
        help="stop the execution left running by a cancelled job, then exit"
    )
    args = parser.parse_args()

    server_user = os.environ.get("CI_ORBIT_SERVER_USER")
    server_password = os.environ.get("CI_ORBIT_SERVER_PASSWORD")
    if not (server_user and server_password):
        raise RuntimeError("CI_ORBIT_SERVER_USER and/or CI_ORBIT_SERVER_PASSWORD not visible")
    clientToServer = APIClient(
        root=config["server"],
        username=server_user,
        password=server_password
    )

    if args.cleanup:
        sys.exit(cleanupFromStateFile(clientToServer))

    lists = [x for x in args.list if x.strip()]
    if not lists:
        raise RuntimeError("usage: python3 orbit.py testcase")

    tests = ["edge_test_case_" + name for name in lists]

    # Stop the running execution when the CI job is cancelled or times out
    # (GitLab sends SIGTERM), or on Ctrl+C when run by hand.
    currentExec = {"id": None}
    stopping = {"flag": False}

    def onTerminate(signum, frame):
        if stopping["flag"]:
            return
        stopping["flag"] = True
        name = signal.Signals(signum).name
        print(f"\nReceived {name}, stopping current execution...")
        execid = currentExec["id"]
        if execid is not None and stopExecution(clientToServer, execid):
            clearCurrentExec()
        sys.exit(128 + signum)

    for sig in (signal.SIGTERM, signal.SIGINT, getattr(signal, "SIGHUP", None)):
        if sig is not None:
            signal.signal(sig, onTerminate)

    jobDeadline = ciJobDeadline()
    if jobDeadline is not None:
        left = int(jobDeadline - time.time())
        print(f"CI job timeout detected, will stop executions in {left}s at the latest")

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

    PASSLIST = []
    FAILLIST = []
    SKIPLIST = []
    for t in tests:
        if jobDeadline is not None and time.time() >= jobDeadline:
            SKIPLIST.append(t)
            continue
        print("")
        print(f"************************ Regist execution of {t} ******************************")
        execid = clientToServer.registExecution(setup=setupData, dut=deviceData, testName=t)
        currentExec["id"] = execid
        saveCurrentExec(execid)
        print(f"Registed execution of {t} with ID: {execid}")
        deadline = time.time() + PER_TEST_TIMEOUT
        if jobDeadline is not None:
            deadline = min(deadline, jobDeadline)
        position = 0
        started = False
        while True:
            time.sleep(5)
            rsp = clientToServer.pollExecution(execid=execid)
            status = rsp["status"]
            if status == "started":
                started = True
            if status == "ended":
                break
            if time.time() >= deadline:
                if jobDeadline is not None and deadline == jobDeadline:
                    print(f"Execid {execid} stopped: CI job is about to time out.")
                else:
                    print(f"Execid {execid} timed out after {PER_TEST_TIMEOUT // 60} minutes.")
                stopExecution(clientToServer, execid)
                break
            if started:
                position = clientToServer.pollLoglines(execid=execid, position=position)

        rsp = clientToServer.pollExecution(execid=execid)
        status = rsp["status"]
        result = "timeout"
        if status == "ended":
            result = rsp["result"]

        currentExec["id"] = None
        clearCurrentExec()

        fileName = f"{t}.zip"
        if clientToServer.downloadExecution(execid=execid, fileName=fileName):
            printRemainingLog(fileName, position)

        print(f"Execid {execid} ended")
        print(f"Execid {execid} {result}")
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
    if SKIPLIST:
        print("")
        print("NOT RUN (CI job timeout reached):")
        for s in SKIPLIST:
            print(f"    {s}")
    if not FAILLIST and not SKIPLIST:
        sys.exit(0)

    print("")
    print("FAILED:")
    for f in FAILLIST:
        print(f"    {f}")
    sys.exit(1)
