from platformio import util
import os
import threading
import subprocess
import time
import colorama
from colorama import Fore
from error import set_error

# PlatfromIO configuration
envPort = {
    "COM7": "ttgo-t-beam",
    "COM9": "ttgo-t-beam",
    "COM11": "ttgo-t-beam",
}

TIMEOUT_BUILD = 60 * 10
TIMEOUT_MONITOR = 60 * 5


class PortsPlatformIo:
    def printPorts():
        ports = util.get_serial_ports()

        print("Ports: ", ports)

        for port in ports:
            print(port["port"], end=",", flush=True)

        print()


class UpdatePlatformIO:
    def __init__(self, directory, shared_state_change, shared_state):
        self.shared_state_change = shared_state_change

        self.shared_state = shared_state

        self.shared_state["uploadedPorts"] = []

        # Get all the serial ports
        ports = util.get_serial_ports()

        for port in ports:
            print(port["port"], end=",", flush=True)

        print()

        # Add a Timer to reset the building if it takes too long
        self.timer = threading.Timer(TIMEOUT_BUILD, self.buildAndUploadTimeout)

        # Generate an empty list of processes
        self.processes = []

        self.timers = {}

        self.file = os.path.join(directory, "Monitoring")

        os.makedirs(self.file, exist_ok=True)

        # Start the build
        if not self.build():
            set_error(
                self.shared_state,
                self.shared_state_change,
                "Error when building the project",
            )
            return

        shared_state["builded"] = True
        shared_state_change.set()

        # Create a new folder for the build
        self.buildFile = os.path.join(self.file, "build")

        os.makedirs(self.buildFile, exist_ok=True)

        # Spawn a thread for each port
        self.spawnThreads()

    def build(self):
        print(Fore.LIGHTGREEN_EX + "Building" + Fore.RESET, end="", flush=True)

        # Get the unique environments
        environments = set(envPort.values())

        buildThreads = []

        # Build the environments
        for env in environments:
            file = os.path.join(self.file, "build" + env + ".ans")

            buildThread = threading.Thread(
                target=self.buildEnv,
                args=(
                    env,
                    file,
                ),
            )

            buildThread.start()
            buildThreads.append(buildThread)

        # Wait for all the threads to finish
        for buildThread in buildThreads:
            buildThread.join()

        # Print
        print(Fore.GREEN + "Successfully builded" + Fore.RESET)

        return True

    def buildEnv(self, env, fileName):
        print("Start building env: " + env)

        process = subprocess.Popen(
            ["pio", "run", "-e", env],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.processes.append(process)

        i = 8

        # Check if one of the lines contains an error
        for line in process.stdout:
            decoded = line.decode("utf-8", "ignore")
            with open(fileName, "a") as f:
                f.write(decoded)

            if "[FAILED]" in decoded or "error occurred" in decoded:
                set_error(
                    self.shared_state,
                    self.shared_state_change,
                    "Build failed " + Fore.YELLOW + env + Fore.RESET,
                )
                self.killThreads()
                return False

            if i == 0:
                print(Fore.LIGHTGREEN_EX + "." + Fore.RESET, end="", flush=True)
                i = 8

            i -= 1

        # Print without newline character
        print()

        process.wait()

        # Remove the process from the list
        self.processes.remove(process)

    def spawnThreads(self):
        # Get all the serial ports
        ports = util.get_serial_ports()

        if len(ports) == 0:
            set_error(
                self.shared_state,
                self.shared_state_change,
                "No serial ports found",
            )
            return

        # Upload to all the serial ports
        for port in ports:
            if port["port"] not in envPort:
                print(
                    Fore.YELLOW
                    + "Port "
                    + port["port"]
                    + " not in the dictionary"
                    + Fore.RESET
                )
                continue

            # Spawn a thread for each port
            x = threading.Thread(
                target=self.uploadAndMonitor,
                args=(envPort[port["port"]], port["port"]),
            )

            x.start()

    def uploadToPort(self, env, portName):
        print("Start uploading to port: " + portName + ", env: " + env)
        process = subprocess.Popen(
            ["pio", "run", "-e", env, "--target", "upload", "--upload-port", portName],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.processes.append(process)

        i = 8

        buildFile = os.path.join(self.buildFile, portName + ".ans")

        # Check if one of the lines contains an error
        for line in process.stdout:
            decoded = line.decode("utf-8", "ignore")
            with open(buildFile, "a", encoding="utf-8") as f:
                f.write(decoded)

            if "[FAILED]" in decoded or "error occurred" in decoded:
                set_error(
                    self.shared_state,
                    self.shared_state_change,
                    "Failed to upload in port: " + portName,
                )
                process.kill()
                return False

            if i == 0:
                print(".", end="", flush=True)
                i = 8

            i -= 1

        # Print with newline character
        print()

        # Wait for the process to finish or timeout
        process.wait()

        # Remove the process from the list
        self.processes.remove(process)

        # Print
        print(Fore.GREEN + "Successfully uploaded to port: " + portName + Fore.RESET)

        return True

    def monitorPort(self, portName):
        print("Start monitoring port: " + portName)

        file = os.path.join(self.file, "monitor_" + portName + ".ans")

        process = subprocess.Popen(
            [
                "pio",
                "device",
                "monitor",
                "--port",
                portName,
                "-f",
                "esp32_exception_decoder",
                "-f",
                "time",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        # Add a Timer to reset the building if the monitor takes too long to start
        def resetTimer(self, portName):
            timer = threading.Timer(TIMEOUT_MONITOR, self.monitorTimeout)
            if self.timers.get(portName) and self.timers[portName].is_alive():
                self.timers[portName].cancel()

            self.timers[portName] = timer

        resetTimer(self, portName)

        self.processes.append(process)

        # Write the output to a file, but I want to read the file on the fly
        self.ErrorOccurred = False

        addrFound = False

        getNumberOfLinesError = 50

        initialLines = 50

        initialized = False

        for line in process.stdout:
            decoded_line = line.decode("utf-8", "ignore")
            with open(file, "a", encoding="utf-8") as f:
                f.write(decoded_line)

            resetTimer(self, portName)

            if (
                "Guru Meditation Error" in decoded_line
                or "assert failed" in decoded_line
            ):
                self.ErrorOccurred = True

            if "waiting for download" in decoded_line:
                set_error(
                    self.shared_state,
                    self.shared_state_change,
                    "Error in port waiting for upload: " + portName,
                )
                self.killThreads()
                return

            if not initialized:
                initialLines -= 1
                if "POWERON_RESET" in decoded_line:
                    initialized = True

                elif initialLines == 0:
                    set_error(
                        self.shared_state,
                        self.shared_state_change,
                        "Not upload correctly in port: " + portName,
                    )
                    self.killThreads()
                    return

            if self.ErrorOccurred:
                getNumberOfLinesError -= 1
                if getNumberOfLinesError == 0:
                    set_error(
                        self.shared_state,
                        self.shared_state_change,
                        "Error in port: " + portName,
                    )
                    self.killThreads()
                    return

            if not addrFound and "Local LoRa address" in decoded_line:
                addrFound = True
                # The last hex value of the line is the address
                hexAddress = decoded_line.split(" ")[-1]
                # Get only the first 4 characters
                hexAddress = hexAddress[:4]
                try:
                    self.shared_state["deviceAddressAndCOM"][portName] = int(
                        hexAddress, 16
                    )
                except ValueError:
                    set_error(
                        self.shared_state,
                        self.shared_state_change,
                        "Error in port: " + portName + " when getting the address",
                    )
                    self.killThreads()
                    return
                self.shared_state_change.set()

        process.wait()

        self.processes.remove(process)

    def uploadAndMonitor(self, env, portName):
        # Upload the code to the board
        if self.uploadToPort(env, portName):
            if self.shared_state["deviceMonitorStarted"] == False:
                self.shared_state["deviceMonitorStarted"] = True
                self.shared_state_change.set()

            # Remove the timer
            if self.checkIfAllPortsUploaded(portName):
                print(Fore.GREEN + "All ports uploaded" + Fore.RESET)
                self.timer.cancel()

            # Monitor the port
            self.monitorPort(portName)

    def checkIfAllPortsUploaded(self, port):
        self.shared_state["uploadedPorts"].append(port)

        if len(self.shared_state["uploadedPorts"]) == getNumberOfPorts():
            self.shared_state["allPortsUploaded"] = True
            self.shared_state_change.set()
            return True

        return False

    def killThreads(self):
        print("Killing update and monitor threads")
        for timer in self.timers.values():
            if timer and timer.is_alive():
                timer.cancel()
        for process in self.processes:
            process.kill()

    def buildAndUploadTimeout(self):
        set_error(
            self.shared_state, self.shared_state_change, "Timeout when build or Upload"
        )
        self.killThreads()

    def monitorTimeout(self):
        set_error(self.shared_state, self.shared_state_change, "Timeout when monitor")
        self.killThreads()


def getNumberOfPorts():
    return len(util.get_serial_ports())
