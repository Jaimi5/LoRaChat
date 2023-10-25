import os
import json
from datetime import datetime
from colorama import Fore


class Status:
    def __init__(self, file, numberOfPorts):
        self.file = file
        self.fileName = os.path.join(file, "status.json")
        self.numberOfPorts = numberOfPorts

        self.createNewFile()

    def createNewFile(self):
        # Create the file
        with open(self.fileName, "w") as file:
            file.write("[]")

    def createAndOpenFile(self):
        if not os.path.exists(self.fileName):
            # If the file doesn't exist, create it
            with open(self.fileName, "w") as file:
                file.write("[]")

        # Read the file
        with open(self.fileName, "r") as file:
            data = file.read()

        # Parse the file
        json_data = json.loads(data)

        return json_data

    def startedDevice(self, device):
        json_data = self.createAndOpenFile()

        # Check if the device is already in the file
        for deviceInFile in json_data:
            if deviceInFile["device"] == device:
                return False

        # Add the device to the file
        json_data.append(
            {
                "device": device,
                "started": True,
                "startedDate": datetime.now().strftime("%d/%m/%Y %H:%M:%S"),
            }
        )

        # Write the file
        with open(self.fileName, "w") as file:
            file.write(json.dumps(json_data, indent=4))

        return True

    def checkIfAllDevicesStarted(self):
        json_data = self.createAndOpenFile()

        # Check if all the devices are started
        if len(json_data) == self.numberOfPorts:
            return True
        else:
            return False

    def endedSimulation(self, device):
        json_data = self.createAndOpenFile()

        # Check if the device is already in the file
        for deviceInFile in json_data:
            if deviceInFile["device"] == device:
                # Check if endedSim variable exists and is True
                if "endedSim" in deviceInFile and deviceInFile["endedSim"]:
                    return False

                deviceInFile["endedSim"] = True
                deviceInFile["endedDate"] = datetime.now().strftime("%d/%m/%Y %H:%M:%S")

        # Write the file
        with open(self.fileName, "w") as file:
            file.write(json.dumps(json_data, indent=4))

        return True

    def checkIfAllDevicesEndedSimulation(self):
        json_data = self.createAndOpenFile()

        # Check if all the devices are ended
        for deviceInFile in json_data:
            if not "endedSim" in deviceInFile or (
                "endedSim" in deviceInFile and not deviceInFile["endedSim"]
            ):
                return False

        return True

    def endedLogsSimulation(self, device):
        json_data = self.createAndOpenFile()

        # Check if the device is already in the file
        for deviceInFile in json_data:
            if deviceInFile["device"] == device:
                if "endedLogs" in deviceInFile and deviceInFile["endedLogs"]:
                    return False

                deviceInFile["endedLogs"] = True
                deviceInFile["endedLogsDate"] = datetime.now().strftime(
                    "%d/%m/%Y %H:%M:%S"
                )

        # Write the file
        with open(self.fileName, "w") as file:
            file.write(json.dumps(json_data, indent=4))

        return True

    def checkIfAllDevicesEndedLogs(self):
        json_data = self.createAndOpenFile()

        # Check if all the devices are ended
        for deviceInFile in json_data:
            if not "endedLogs" in deviceInFile or (
                "endedLogs" in deviceInFile and not deviceInFile["endedLogs"]
            ):
                return False

        return True
