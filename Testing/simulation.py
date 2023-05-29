import os
import json
import manageFile
import threading
import updatePlatformio
import mqttClient
import changeConfigurationSerial
import timeout


class Simulation:
    def __init__(self, file, noBuild, name):
        self.Name = name
        self.fileName = file
        self.manageFile = manageFile.ManageFile(self.fileName)
        self.numberOfDevices = 0

        # Create an Event object to synchronize the main script
        self.shared_state_change = threading.Event()

        self.shared_state = {
            "builded": False,  # True if the build is done
            "deviceMonitorStarted": False,  # True if the device monitor is started
            "allDevicesStartedSim": False,
            "allDevicesEndedSim": False,
            "allDevicesEndedLogs": False,
            "error": False,
            "error_message": "",
        }

        # If there is a configuration file for the simulation then use it
        self.configFile = os.path.join(self.fileName, "simConfiguration.json")
        if os.path.isfile(self.configFile):
            simConfig = changeConfigurationSerial.ChangeConfigurationSerial(
                self.configFile, ["ttgo-t-beam", "ttgo-lora32-v1"]
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
                            print("Error: " + self.shared_state["error_message"])
                            return

            waitUntilBuild()

            def waitUntilADeviceBuilded():
                while not self.shared_state["deviceMonitorStarted"]:
                    self.shared_state_change.wait()

                    if self.shared_state_change.is_set():
                        self.shared_state_change.clear()

                        if self.shared_state["error"]:
                            print("Error: " + self.shared_state["error_message"])
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

        def saveStatus():
            json_str = json.dumps(self.shared_state, indent=4)

            # Save the results in a file using a json format
            with open(os.path.join(self.fileName, "results.json"), "w") as f:
                f.write(json_str)

        try:
            while True:
                # Wait for the self.shared_state_change event to be set
                self.shared_state_change.wait(5)

                if self.shared_state_change.is_set():
                    self.shared_state_change.clear()

                    saveStatus()

                    if self.shared_state["error"]:
                        print("Error: " + self.shared_state["error_message"])
                        self.timeout.cancel()
                        break

                    if self.shared_state["allDevicesEndedLogs"]:
                        print("All devices ended logs")
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
