import status
import os
import json
from datetime import datetime
from colorama import Fore


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
        if "data" in packet and "simCommand" in packet["data"]:
            simCommand = packet["data"]["simCommand"]

            if simCommand == 4:
                # If the command is "start simulation"
                self.SetStatusAndCheckAll(
                    packet,
                    self.status.startedDevice,
                    self.status.checkIfAllDevicesStarted,
                    "allDevicesStartedSim",
                    "Device already started simulation",
                    "All devices started simulation",
                )

            elif simCommand == 5:
                # If the command is "end simulation"
                self.SetStatusAndCheckAll(
                    packet,
                    self.status.endedSimulation,
                    self.status.checkIfAllDevicesEndedSimulation,
                    "allDevicesEndedSim",
                    "Device already ended simulation",
                    "All devices ended simulation",
                )

            elif simCommand == 6:
                # If the command is "end send logs simulation"
                self.SetStatusAndCheckAll(
                    packet,
                    self.status.endedLogsSimulation,
                    self.status.checkIfAllDevicesEndedLogs,
                    "allDevicesEndedLogs",
                    "Device already ended log Simulation",
                    "All devices ended log Simulation",
                )

            elif simCommand == 2:
                # Save the packet in the state monitor file
                self.saveMonitor(packet["data"])

            elif simCommand == 3:
                self.saveData(packet["data"])
        elif "data" in packet:
            self.saveData(packet["data"])

    def SetStatusAndCheckAll(
        self,
        packet,
        endDeviceFunction,
        endAllDevicesFunction,
        state,
        errorName,
        successName,
    ):
        if endDeviceFunction(packet["data"]["addrSrc"]):
            if endAllDevicesFunction():
                print(Fore.GREEN + successName + Fore.RESET)
                self.shared_state[state] = True
                self.shared_state[state + "Time"] = datetime.now().strftime(
                    "%d/%m/%Y %H:%M:%S"
                )
                self.shared_state_change.set()
        else:
            print(Fore.RED + errorName + Fore.RESET)
            self.shared_state["error"] = True
            self.shared_state["error_message"] = errorName
            self.shared_state_change.set()
