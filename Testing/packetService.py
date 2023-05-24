import status


class PacketService:
    def __init__(
        self,
        file,
        numberOfPorts,
        shared_state_change,
        shared_state,
    ):
        self.file = file
        self.status = status.Status(file, numberOfPorts)
        self.shared_state_change = shared_state_change
        self.shared_state = shared_state

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
                        self.shared_state["error"] = False
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
                        self.shared_state["error"] = False
                        self.shared_state["allDevicesEndedSim"] = True
                        self.shared_state_change.set()
                else:
                    self.shared_state["error"] = True
                    self.shared_state["error_message"] = "Device already ended"
                    self.shared_state_change.set()

            elif simCommand == 6:
                # If the command is "end send simulation"
                if self.status.endedSendSimulation(packet["data"]["addrSrc"]):
                    if self.status.checkIfAllDevicesEndedSendSimulation():
                        self.shared_state["error"] = False
                        self.shared_state["allDevicesEndedSendSim"] = True
                        self.shared_state_change.set()
                else:
                    self.shared_state["error"] = True
                    self.shared_state[
                        "error_message"
                    ] = "Device already ended Simulation"
                    self.shared_state_change.set()
