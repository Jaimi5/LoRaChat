import os
import json
import manageFile
import threading
import updatePlatformio
import mqttClient
import changeConfigurationSerial
import timeout
from datetime import datetime
from time import sleep
from colorama import Fore


class Simulation:
    def __init__(self, file, noBuild, name):
        self.Name = name
        self.fileName = file
        self.manageFile = manageFile.ManageFile(self.fileName)
        self.numberOfDevices = 0

        # Create an Event object to synchronize the main script
        self.shared_state_change = threading.Event()

        self.shared_state = {
            "startedBuilding": datetime.now().strftime(
                "%d/%m/%Y %H:%M:%S"
            ),  # Time when the build started
            "builded": False,  # True if the build is done
            "allPortsUploaded": False,  # True if all ports are uploaded
            "uploadedPorts": [],  # List of the ports that are uploaded
            "deviceMonitorStarted": False,  # True if the device monitor is started
            "deviceMonitorStartedTime": "",  # Time when the device monitor started
            "allDevicesStartedSim": False,  # True if all devices started the simulation
            "allDevicesStartedSimTime": "",  # Time when all devices started the simulation
            "allDevicesEndedSim": False,  # True if all devices ended the simulation
            "allDevicesEndedSimTime": "",  # Time when all devices ended the simulation
            "allDevicesEndedLogs": False,  # True if all devices ended the logs
            "allDevicesEndedLogsTime": "",  # Time when all devices ended the logs
            "deviceAddressAndCOM": {},  # Address and COM of the devices
            "error": False,  # True if there is an error
            "error_time": "",  # Time when the error occurred
            "error_message": "",  # Error message
        }

        def saveStatus():
            json_str = json.dumps(self.shared_state, indent=4)

            # Save the results in a file using a json format
            with open(os.path.join(self.fileName, "summary.json"), "w") as f:
                f.write(json_str)

        saveStatus()

        # If there is a configuration file for the simulation then use it
        self.configFile = os.path.join(self.fileName, "simConfiguration.json")
        if os.path.isfile(self.configFile):
            simConfig = changeConfigurationSerial.ChangeConfigurationSerial(
                self.configFile,
                ["ttgo-t-beam", "ttgo-lora32-v1", "esp-wrover-kitNAYAD_V1R2"],
            )
            simConfig.changeConfiguration()
            print("Configuration changed", self.Name)

        if not noBuild:
            # Update the PlatformIO
            self.updater = updatePlatformio.UpdatePlatformIO(
                self.fileName,
                self.shared_state_change,
                self.shared_state,
            )

            def waitUntilBuild():
                while not self.shared_state["builded"]:
                    self.shared_state_change.wait()

                    if self.shared_state_change.is_set():
                        self.shared_state_change.clear()

                        if self.shared_state["error"]:
                            self.shared_state["error_time"] = datetime.now().strftime(
                                "%d/%m/%Y %H:%M:%S"
                            )
                            print(
                                Fore.RED
                                + "Error: "
                                + self.shared_state["error_message"]
                                + Fore.RESET
                            )
                            return

            waitUntilBuild()

            def waitUntilADeviceBuilded():
                while not self.shared_state["deviceMonitorStarted"]:
                    self.shared_state_change.wait()

                    if self.shared_state_change.is_set():
                        self.shared_state_change.clear()

                        if self.shared_state["deviceMonitorStarted"]:
                            self.shared_state[
                                "deviceMonitorStartedTime"
                            ] = datetime.now().strftime("%d/%m/%Y %H:%M:%S")
                            print("Device monitor started")
                            break

                        if self.shared_state["error"]:
                            self.shared_state["error_time"] = datetime.now().strftime(
                                "%d/%m/%Y %H:%M:%S"
                            )
                            print(
                                Fore.RED
                                + "Error: "
                                + self.shared_state["error_message"]
                                + Fore.RESET
                            )
                            return

            waitUntilADeviceBuilded()

        self.mqttClient = mqttClient.MQTT(
            self.fileName,  # file of the results
            updatePlatformio.getNumberOfPorts(),
            self.shared_state_change,
            self.shared_state,
        )

        # Create a timeout using threads.
        # If the timeout occurs, set the shared_state_change event.
        self.timeout = timeout.Timeout(
            int(simConfig.getTimeout()), self.shared_state_change, self.shared_state
        )

        ErrorOccurred = False
        WaitErrorOccurred = 15

        try:
            while True:
                # Wait for the self.shared_state_change event to be set
                self.shared_state_change.wait(5)

                # If an error occurred wait some time to be sure that all messages are received
                if ErrorOccurred:
                    WaitErrorOccurred -= 1
                    if WaitErrorOccurred == 0:
                        break

                if self.shared_state_change.is_set():
                    self.shared_state_change.clear()

                    saveStatus()

                    if self.shared_state["error"]:
                        print(
                            Fore.RED
                            + "Error: "
                            + self.shared_state["error_message"]
                            + Fore.RESET
                        )
                        self.shared_state["error_time"] = datetime.now().strftime(
                            "%d/%m/%Y %H:%M:%S"
                        )
                        ErrorOccurred = True
                        self.timeout.cancel()

                    if self.shared_state["allDevicesEndedLogs"]:
                        print(Fore.GREEN + "All devices ended logs" + Fore.RESET)
                        self.timeout.cancel()
                        break

        except KeyboardInterrupt:
            print("KeyboardInterrupt")

        self.mqttClient.disconnect()

        try:
            if not noBuild:
                self.updater.killThreads()
        except NameError:
            pass

        saveStatus()

    def error(self):
        return self.shared_state["error"]
