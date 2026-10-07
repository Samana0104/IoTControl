#!/usr/bin/env python3

import sys
import time

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

BLUEZ_SERVICE = "org.bluez"
AGENT_INTERFACE = "org.bluez.Agent1"
AGENT_MANAGER_INTERFACE = "org.bluez.AgentManager1"
ADAPTER_INTERFACE = "org.bluez.Adapter1"
DEVICE_INTERFACE = "org.bluez.Device1"
OBJECT_MANAGER_INTERFACE = "org.freedesktop.DBus.ObjectManager"
PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties"
AGENT_PATH = "/com/iotcontrol/BluetoothAgent"


class BluetoothAgent(dbus.service.Object):
    def __init__(self, bus, pin):
        super().__init__(bus, AGENT_PATH)
        self.pin = pin

    @dbus.service.method(AGENT_INTERFACE, in_signature="", out_signature="")
    def Release(self):
        return None

    @dbus.service.method(AGENT_INTERFACE, in_signature="o", out_signature="s")
    def RequestPinCode(self, device):
        return self.pin

    @dbus.service.method(AGENT_INTERFACE, in_signature="o", out_signature="u")
    def RequestPasskey(self, device):
        if not self.pin.isdigit():
            raise dbus.exceptions.DBusException("org.bluez.Error.Rejected", "PIN is not numeric")
        return dbus.UInt32(int(self.pin))

    @dbus.service.method(AGENT_INTERFACE, in_signature="ouq", out_signature="")
    def DisplayPasskey(self, device, passkey, entered):
        return None

    @dbus.service.method(AGENT_INTERFACE, in_signature="os", out_signature="")
    def DisplayPinCode(self, device, pin):
        return None

    @dbus.service.method(AGENT_INTERFACE, in_signature="ou", out_signature="")
    def RequestConfirmation(self, device, passkey):
        return None

    @dbus.service.method(AGENT_INTERFACE, in_signature="o", out_signature="")
    def RequestAuthorization(self, device):
        return None

    @dbus.service.method(AGENT_INTERFACE, in_signature="os", out_signature="")
    def AuthorizeService(self, device, uuid):
        return None

    @dbus.service.method(AGENT_INTERFACE, in_signature="", out_signature="")
    def Cancel(self):
        return None


def FindDevicePath(objectManager, bluetoothMac):
    managedObjects = objectManager.GetManagedObjects()
    adapterPath = None

    for objectPath, interfaces in managedObjects.items():
        if ADAPTER_INTERFACE in interfaces and adapterPath is None:
            adapterPath = objectPath
        deviceProperties = interfaces.get(DEVICE_INTERFACE)
        if deviceProperties is not None and str(deviceProperties.get("Address", "")).upper() == bluetoothMac:
            return objectPath, adapterPath
    return None, adapterPath


def DiscoverDevice(bus, objectManager, bluetoothMac, timeoutSeconds):
    devicePath, adapterPath = FindDevicePath(objectManager, bluetoothMac)
    if devicePath is not None:
        return devicePath
    if adapterPath is None:
        raise RuntimeError("Bluetooth adapter not found")

    adapter = dbus.Interface(bus.get_object(BLUEZ_SERVICE, adapterPath), ADAPTER_INTERFACE)
    discoveryStarted = False
    try:
        try:
            adapter.StartDiscovery()
            discoveryStarted = True
        except dbus.exceptions.DBusException as error:
            if error.get_dbus_name() != "org.bluez.Error.InProgress":
                raise

        deadline = time.monotonic() + timeoutSeconds
        while time.monotonic() < deadline:
            devicePath, unusedAdapterPath = FindDevicePath(objectManager, bluetoothMac)
            if devicePath is not None:
                return devicePath
            time.sleep(0.2)
    finally:
        if discoveryStarted:
            try:
                adapter.StopDiscovery()
            except dbus.exceptions.DBusException:
                pass
    raise RuntimeError("HC-05 was not found")


def PairDevice(bus, devicePath, timeoutSeconds):
    deviceObject = bus.get_object(BLUEZ_SERVICE, devicePath)
    properties = dbus.Interface(deviceObject, PROPERTIES_INTERFACE)
    if bool(properties.Get(DEVICE_INTERFACE, "Paired")):
        properties.Set(DEVICE_INTERFACE, "Trusted", dbus.Boolean(True))
        return

    device = dbus.Interface(deviceObject, DEVICE_INTERFACE)
    mainLoop = GLib.MainLoop()
    pairResult = {"error": None, "finished": False}

    def PairSucceeded():
        pairResult["finished"] = True
        mainLoop.quit()

    def PairFailed(error):
        pairResult["error"] = error
        pairResult["finished"] = True
        mainLoop.quit()

    def PairTimedOut():
        if not pairResult["finished"]:
            pairResult["error"] = RuntimeError("Bluetooth pairing timed out")
            mainLoop.quit()
        return False

    device.Pair(reply_handler=PairSucceeded, error_handler=PairFailed)
    GLib.timeout_add_seconds(timeoutSeconds, PairTimedOut)
    mainLoop.run()
    if pairResult["error"] is not None:
        raise pairResult["error"]
    properties.Set(DEVICE_INTERFACE, "Trusted", dbus.Boolean(True))


def Main():
    if len(sys.argv) != 3:
        return 2

    bluetoothMac = sys.argv[1].upper()
    timeoutSeconds = int(sys.argv[2])
    pin = sys.stdin.readline().rstrip("\n")
    if not pin or len(pin) > 16:
        return 2

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    objectManager = dbus.Interface(bus.get_object(BLUEZ_SERVICE, "/"), OBJECT_MANAGER_INTERFACE)
    agentManager = dbus.Interface(bus.get_object(BLUEZ_SERVICE, "/org/bluez"), AGENT_MANAGER_INTERFACE)
    agent = BluetoothAgent(bus, pin)
    agentRegistered = False

    try:
        agentManager.RegisterAgent(AGENT_PATH, "KeyboardOnly")
        agentRegistered = True
        devicePath = DiscoverDevice(bus, objectManager, bluetoothMac, timeoutSeconds)
        PairDevice(bus, devicePath, timeoutSeconds)
        return 0
    except Exception as error:
        print(f"Bluetooth pairing failed: {error}", file=sys.stderr)
        return 1
    finally:
        if agentRegistered:
            try:
                agentManager.UnregisterAgent(AGENT_PATH)
            except dbus.exceptions.DBusException:
                pass
        agent.pin = ""
        pin = ""


if __name__ == "__main__":
    sys.exit(Main())
