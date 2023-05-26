import status
import os
import json
from datetime import datetime


class PacketService:
    def __init__(
        self,
        file,
        numberOfPorts,
        shared_state_change,
        shared_state,
    ):
        self.file = file
        self.monitorFileName = os.path.join(file, "stateMonitors.json")
        self.dataFileName = os.path.join(file, "messages.json")
        self.status = status.Status(file, numberOfPorts)
        self.shared_state_change = shared_state_change
        self.shared_state = shared_state

    def savePacket(self, fileName, packet):
        json_data = self.createAndOpenFile(fileName)

        json_data.append(
            {
                "payload": packet,
                "date": datetime.now().strftime("%d/%m/%Y %H:%M:%S"),
            }
        )

        # Write the file
        with open(fileName, "w") as file:
            file.write(json.dumps(json_data, indent=4))

    def saveMonitor(self, packet):
        self.savePacket(self.monitorFileName, packet)

    def saveData(self, packet):
        self.savePacket(self.dataFileName, packet)

    def createAndOpenFile(self, fileName):
        if not os.path.exists(fileName):
            # If the file doesn't exist, create it
            with open(fileName, "w") as file:
                file.write("[]")

        # Read the file
        with open(fileName, "r") as file:
            data = file.read()

        # Parse the file
        json_data = json.loads(data)

        return json_data

    def processPacket(self, packet):
        # Check if "appPortSrc" key exists in the JSON data
        if (
            "data" in packet
            and "appPortSrc" in packet["data"]
            and "simCommand" in packet["data"]
        ):
            simCommand = packet["data"]["simCommand"]

            if simCommand == 4:
                # If the command is "start simulation"
                if self.status.startedDevice(packet["data"]["addrSrc"]):
                    # If the device is not already in the file
                    if self.status.checkIfAllDevicesStarted():
                        self.shared_state["allDevicesStartedSim"] = True
                        self.shared_state_change.set()
                else:
                    self.shared_state["error"] = True
                    self.shared_state["error_message"] = "Device already started"
                    self.shared_state_change.set()

            elif simCommand == 5:
                # If the command is "end simulation"
                if self.status.endedSimulation(packet["data"]["addrSrc"]):
                    if self.status.checkIfAllDevicesEndedSimulation():
                        self.shared_state["allDevicesEndedSim"] = True
                        self.shared_state_change.set()
                else:
                    self.shared_state["error"] = True
                    self.shared_state["error_message"] = "Device already ended"
                    self.shared_state_change.set()

            elif simCommand == 6:
                # If the command is "end send simulation"
                if self.status.endedLogsSimulation(packet["data"]["addrSrc"]):
                    if self.status.checkIfAllDevicesEndedLogs():
                        self.shared_state["allDevicesEndedLogs"] = True
                        self.shared_state_change.set()
                else:
                    self.shared_state["error"] = True
                    self.shared_state[
                        "error_message"
                    ] = "Device already ended Simulation"
                    self.shared_state_change.set()
            elif simCommand == 2:
                # Save the packet in the state monitor file
                self.saveMonitor(packet["data"])

            elif simCommand == 3:
                self.saveData(packet["data"])
