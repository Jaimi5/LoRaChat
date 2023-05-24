from platformio import util
import os
import threading
import subprocess

# PlatfromIO configuration
portsLora32 = {"COM12", "COM14", "COM9", "COM10"}


class UpdatePlatformIO:
    def __init__(self, directory, shared_state_change, shared_state):
        # os.system("pio run")

        self.shared_state_change = shared_state_change

        self.shared_state = shared_state

        self.file = os.path.join(directory, "monitors")

        os.makedirs(self.file, exist_ok=True)

        # Get all the serial ports
        ports = util.get_serial_ports()

        self.processes = []

        # Print the serial ports
        for port in ports:
            print(port["port"])

        # Upload to all the serial ports
        for port in ports:
            env = "ttgo-t-beam"
            if port["port"] in portsLora32:
                env = "ttgo-lora32-v1"

            x = threading.Thread(
                target=self.uploadAndMonitor,
                args=(env, port["port"]),
            )

            x.start()

    def uploadToPort(self, env, portName):
        print("Start uploading to port: " + portName)
        process = subprocess.Popen(
            ["pio", "run", "-e", env, "--target", "upload", "--upload-port", portName],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.processes.append(process)

        # Check if one of the lines contains an error
        for line in process.stdout:
            if "[FAILED]" in line.decode(
                "utf-8"
            ) or "A fatal error occurred" in line.decode("utf-8"):
                print("Error in port: " + portName)
                self.shared_state["error"] = True
                self.shared_state["error_message"] = portName + " failed to upload"
                self.shared_state_change.set()
                process.kill()
                return

        # Wait for the process to finish or timeout
        process.wait()

        # Remove the process from the list
        self.processes.remove(process)

        print("Successfully update port: " + portName)

    def monitorPort(self, portName):
        print("Start monitoring port: " + portName)

        file = os.path.join(self.file, "monitor_" + portName + ".txt")

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

        self.processes.append(process)

        # Write the output to a file, but I want to read the file on the fly

        for line in process.stdout:
            with open(file, "a") as f:
                f.write(line.decode("utf-8"))

            if "Guru Meditation Error" in line.decode("utf-8"):
                print("Error in port: " + portName)
                process.kill()
                return

        process.wait()

        self.processes.remove(process)

    def uploadAndMonitor(self, env, portName):
        # Upload the code to the board
        self.uploadToPort(env, portName)

        # Monitor the port
        self.monitorPort(portName)

    def killThreads(self):
        for process in self.processes:
            process.kill()


def getNumberOfPorts():
    return len(util.get_serial_ports())
