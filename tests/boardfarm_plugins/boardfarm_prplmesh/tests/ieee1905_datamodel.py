import environment as env

from opts import message, debug, err
from .prplmesh_base_test import PrplMeshBaseTest
from boardfarm.exceptions import SkipTest
from dataclasses import dataclass
from typing import Any, Callable, Optional

# Sentinel to distinguish "argument not provided" from "expected value is None"
_MISSING = object()
IEEE1905_DM_ROOT = "IEEE1905"


@dataclass(frozen=True)
class Condition:
    """A callable boolean condition with a human-readable description for logs/errors."""
    desc: str
    fn: Callable[[Any], bool]

    def __call__(self, value: Any) -> bool:
        return self.fn(value)

    def __str__(self) -> str:
        return self.desc


def _to_int(value: Any) -> int:
    """Convert NBAPI values to int reliably (NBAPI may return strings)."""
    # bool is a subclass of int; treat explicitly to avoid surprises like True -> 1.
    if isinstance(value, bool):
        raise ValueError(f"Refusing to convert bool to int: {value}")
    return int(str(value).strip())


def check_default_value_of_param(
    device: env.ALEntity,
    param_path: str,
    expected_value: Any = _MISSING,
    condition: Optional[Callable[[Any], bool]] = None,
) -> None:
    """
    Check the default value of a parameter.

    Provide exactly one of:
      - expected_value: compare using string-normalized equality (can be None)
      - condition: predicate applied to the raw NBAPI value (caller responsible for coercion)

    :param device: The device to check.
    :param param_path: The path to the parameter, "Object.Path.Parameter".
    :param expected_value: The expected value (compared as str()); can be None.
    :param condition: A predicate that returns True for acceptable values.
    """
    message(f"Started checking of {param_path}")
    has_expected = expected_value is not _MISSING
    has_condition = condition is not None
    if has_expected == has_condition:
        raise ValueError("Provide exactly one of: expected_value or condition")
    # Validate path format early to fail with a clear error.
    if "." not in param_path:
        raise ValueError(f"param_path must contain '.', got: {param_path}")

    if param_path.endswith("."):
        raise ValueError(f"param_path looks like object path."
                         f" Should be full path to param. Got: {param_path}")

    obj_path, param_name = param_path.rsplit(".", 1)

    actual_value = device.nbapi_get_parameter(obj_path, param_name)
    message(f"{device.name}: {param_path} = {actual_value}")

    if has_expected:
        message(f"Checking: {device.name}: {param_path} == {expected_value}")
        if str(actual_value) != str(expected_value):
            raise AssertionError(f"{device.name}: {param_path} expected {expected_value}, "
                                 f"got {actual_value}")
        message(f"Successfully checked that {device.name}: {param_path} == {expected_value}")
        return

    # condition is not None here due to XOR validation above
    message(f"Checking: {device.name}: {param_path} satisfies: {condition}")
    try:
        ok = condition(actual_value)
    except Exception as e:
        raise AssertionError(f"{device.name}: {param_path} condition ({condition}) "
                             f"raised {type(e).__name__}: {e}. Got value {actual_value}") from e

    if not ok:
        raise AssertionError(f"{device.name}: {param_path} violates condition ({condition}). "
                             f"Got {actual_value}")
    message(f"Successfully checked that {device.name}: {param_path} satisfies: {condition}")


def nbapi_get_dm_instance_path_by_filter(
    device: env.ALEntity,
    object_path: str,
    parameter_name: str,
    filter_condition: Callable[[Any], bool],
    ignore_predicate_exceptions: bool = False,
) -> Optional[str]:
    """
    Get the path of a datamodel instance by filtering on a parameter value.

    :param device: The device to query.
    :param object_path: The datamodel object path (without instance number), e.g. "Ethernet.Link".
    :param parameter_name: The parameter name to check on each instance, e.g. "Name".
    :param filter_condition: Predicate applied to the parameter value.
    :param ignore_predicate_exceptions: If True, log predicate exceptions and continue searching.
                                        If False (default), re-raise to surface bugs in predicates.
    :return: The instance path that matches, or None if no match is found.
    """
    if not object_path:
        raise ValueError("object_path must be non-empty")
    if not parameter_name:
        raise ValueError("parameter_name must be non-empty")
    if filter_condition is None:
        raise ValueError("filter_condition must be provided")

    dm_paths: list[str] = device.nbapi_get_list_instances(object_path)

    for dm_path in dm_paths:
        param_value = device.nbapi_get_parameter(dm_path, parameter_name)
        debug(f"Checking instance {dm_path}.{parameter_name} "
              f"against condition ({filter_condition}). Current value: {param_value}")

        try:
            if filter_condition(param_value):
                debug(f"Found instance {dm_path} where {parameter_name}={param_value} "
                      f"satisfies condition ({filter_condition})")
                return dm_path
        except Exception as e:
            error_message = (f"Condition ({filter_condition}) raised {type(e).__name__}: {e} "
                             f"for instance {dm_path}.{parameter_name} with value {param_value}")
            err(error_message)
            if not ignore_predicate_exceptions:
                raise Exception(error_message)

        debug(f"Instance {dm_path}.{parameter_name} does not "
              f"satisfy condition ({filter_condition}). Moving to next instance...")

    err(f"No instances satisfy condition ({filter_condition}) under {object_path}.")
    return None


def get_device_bridge_mac(device: env.ALEntity, bridge_name: str) -> Optional[str]:
    """
    Get the MAC address of a bridge interface on the device by its Name.

    IMPORTANT: If the bridge is found but MACAddress is missing, this returns None
    to fail at the real root cause.

    :param device: The device to query.
    :param bridge_name: The name of the bridge interface (e.g., "br-lan").
    :return: The MAC address of the bridge interface, or None if not found or missing.
    """
    bridge_path = nbapi_get_dm_instance_path_by_filter(
        device=device,
        object_path="Ethernet.Link",
        parameter_name="Name",
        filter_condition=Condition(
            f"equals {bridge_name}",
            lambda x: str(x) == str(bridge_name),
        ),
    )
    if bridge_path is None:
        err(f"No bridge interface found on {device.name} where "
            f"Ethernet.Link.*.Name equals {bridge_name}")
        return None

    bridge_mac = device.nbapi_get_parameter(bridge_path, "MACAddress")
    # nbapi_get_parameter() may return falsy values when absent; fail early here.
    if not bridge_mac:
        err(
            f"Bridge interface {bridge_path} on {device.name} has missing/empty MACAddress "
            f"(Name={bridge_name}, MACAddress={bridge_mac})"
        )
        return None

    mac_str = str(bridge_mac).strip()
    if not mac_str:
        err(
            f"Bridge interface {bridge_path} on {device.name} has blank MACAddress after stripping "
            f"(Name={bridge_name}, MACAddress={bridge_mac})"
        )
        return None

    message(f"Found MAC address of bridge interface on {device.name} "
            f"with Name {bridge_name}: {mac_str}")
    return mac_str


class IEEE1905Datamodel(PrplMeshBaseTest):
    """Check initial configuration on device."""

    @env.process_faults_check
    def runTest(self):
        try:
            controller = self.dev.lan.controller_entity
            agent = self.dev.DUT.agent_entity
        except AttributeError as ae:
            raise SkipTest(ae)

        controller_al_path = f"{IEEE1905_DM_ROOT}.Network.AL.1"

        check_default_value_of_param(controller, f"{IEEE1905_DM_ROOT}.Network.Enable",
                                     expected_value="True")
        check_default_value_of_param(controller, f"{IEEE1905_DM_ROOT}.Network.Status",
                                     expected_value="Available")
        check_default_value_of_param(
            controller,
            f"{IEEE1905_DM_ROOT}.Network.ALNumberOfEntries",
            condition=Condition(">= 2", lambda x: _to_int(x) >= 2))

        agent_brlan_mac = get_device_bridge_mac(agent, "br-lan")
        if agent_brlan_mac is None:
            raise AssertionError(f"Could not find Bridge LAN MAC address for agent {agent}")

        agent_ieee1905_id = agent_brlan_mac.lower()

        agent_al_path = nbapi_get_dm_instance_path_by_filter(
            device=controller,
            object_path=f"{IEEE1905_DM_ROOT}.Network.AL",
            parameter_name="IEEE1905Id",
            filter_condition=Condition(
                f"equals {agent_ieee1905_id}",
                lambda x: str(x).lower() == agent_ieee1905_id))

        if agent_al_path is None:
            raise AssertionError(f"Could not find AL instance for agent {agent} "
                                 f"with IEEE1905Id {agent_ieee1905_id}")

        message(f"Getting reference value of {agent.name} "
                f"FriendlyName, ManufacturerName, ManufacturerModel")
        agent_friendly_name = agent.nbapi_get_parameter("DeviceInfo", "FriendlyName")
        agent_model_name = agent.nbapi_get_parameter("DeviceInfo", "ModelName")
        agent_manufacturer = agent.nbapi_get_parameter("DeviceInfo", "Manufacturer")

        message(
            "Result:"
            f"\n\t * FriendlyName: {agent_friendly_name}"
            f"\n\t * ModelName: {agent_model_name}"
            f"\n\t * Manufacturer: {agent_manufacturer}"
        )

        check_default_value_of_param(controller, f"{agent_al_path}.FriendlyName",
                                     expected_value=agent_friendly_name)
        check_default_value_of_param(controller, f"{agent_al_path}.ManufacturerName",
                                     expected_value=agent_manufacturer)
        check_default_value_of_param(controller, f"{agent_al_path}.ManufacturerModel",
                                     expected_value=agent_model_name)

        check_default_value_of_param(
            controller,
            f"{agent_al_path}.RegistrarFreqBand",
            condition=Condition("is a string", lambda x: isinstance(x, str)),
        )
        check_default_value_of_param(
            controller,
            f"{agent_al_path}.ControlURL",
            condition=Condition("is a string", lambda x: isinstance(x, str)),
        )

        check_default_value_of_param(
            controller,
            f"{agent_al_path}.Version",
            expected_value="1905.1a")
        check_default_value_of_param(
            controller,
            f"{agent_al_path}.InterfaceNumberOfEntries",
            condition=Condition(">= 1", lambda x: _to_int(x) >= 1))
        check_default_value_of_param(
            controller,
            f"{agent_al_path}.IPv4AddressNumberOfEntries",
            condition=Condition(">= 1", lambda x: _to_int(x) >= 1))
        check_default_value_of_param(
            controller,
            f"{agent_al_path}.IPv6AddressNumberOfEntries",
            condition=Condition(">= 1", lambda x: _to_int(x) >= 1))
        check_default_value_of_param(
            controller,
            f"{agent_al_path}.AssocWiFiNetworkDeviceRef",
            condition=Condition(
                "starts with 'Device.WiFi.DataElements.Network.Device.'",
                lambda x: str(x).startswith("Device.WiFi.DataElements.Network.Device.")))

        message(f"Checking that {agent_al_path} has at least one "
                f"Interface with IEEE1905NeighborNumberOfEntries >= 1")
        agent_iface_to_controller_path = nbapi_get_dm_instance_path_by_filter(
            device=controller,
            object_path=f"{agent_al_path}.Interface",
            parameter_name="IEEE1905NeighborNumberOfEntries",
            filter_condition=Condition(">= 1", lambda x: _to_int(x) >= 1),
        )
        if agent_iface_to_controller_path is None:
            raise AssertionError(f"Could not find an Interface instance under "
                                 f"{agent_al_path}.Interface "
                                 f"with IEEE1905NeighborNumberOfEntries >= 1")
        message("Found Interface instance with IEEE1905NeighborNumberOfEntries >= 1: "
                f"{agent_iface_to_controller_path}")

        message(f"Checking that {controller_al_path}.Interface has at least one Interface "
                f"with IEEE1905NeighborNumberOfEntries >= 1")
        controller_iface_to_agent_path = nbapi_get_dm_instance_path_by_filter(
            device=controller,
            object_path=f"{controller_al_path}.Interface",
            parameter_name="IEEE1905NeighborNumberOfEntries",
            filter_condition=Condition(">= 1", lambda x: _to_int(x) >= 1))
        if controller_iface_to_agent_path is None:
            raise AssertionError(f"Could not find an Interface instance under "
                                 f"{controller_al_path}.Interface "
                                 f"with IEEE1905NeighborNumberOfEntries >= 1")
        message("Found Interface instance with IEEE1905NeighborNumberOfEntries >= 1: "
                f"{controller_iface_to_agent_path}")

        message(f"Checking that {agent_iface_to_controller_path}.IEEE1905Neighbor "
                f"contains instance with IEEE1905DeviceRef pointing to {controller_al_path}")
        controller_device_ref = f"Device.{controller_al_path}".removesuffix(".")
        agent_to_controller_neighbor_path = nbapi_get_dm_instance_path_by_filter(
            device=controller,
            object_path=f"{agent_iface_to_controller_path}.IEEE1905Neighbor",
            parameter_name="IEEE1905DeviceRef",
            filter_condition=Condition(
                f"equals {controller_device_ref}",
                lambda x: str(x) == controller_device_ref))
        if agent_to_controller_neighbor_path is None:
            raise AssertionError(
                f"Could not find IEEE1905Neighbor instance under "
                f"{agent_iface_to_controller_path}.IEEE1905Neighbor "
                f"with IEEE1905DeviceRef equal to {controller_device_ref}"
            )
        message(f"Found IEEE1905Neighbor instance with "
                f"IEEE1905DeviceRef equal to {controller_device_ref}")
        message(f"Checking that {controller_iface_to_agent_path}.IEEE1905Neighbor "
                f"contains instance with IEEE1905DeviceRef pointing to {agent_al_path}")
        agent_device_ref = f"Device.{agent_al_path}".removesuffix(".")
        controller_to_agent_neighbor_path = nbapi_get_dm_instance_path_by_filter(
            device=controller,
            object_path=f"{controller_iface_to_agent_path}.IEEE1905Neighbor",
            parameter_name="IEEE1905DeviceRef",
            filter_condition=Condition(f"equals {agent_device_ref}",
                                       lambda x: str(x) == agent_device_ref))
        if controller_to_agent_neighbor_path is None:
            raise AssertionError(
                f"Could not find IEEE1905Neighbor instance under "
                f"{controller_iface_to_agent_path}.IEEE1905Neighbor "
                f"with IEEE1905DeviceRef equal to {agent_device_ref}")
        message(f"Found IEEE1905Neighbor instance with "
                f"IEEE1905DeviceRef equal to {agent_device_ref}")
