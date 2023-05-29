import os
import json
import simConfiguration


class ChangeConfigurationSerial:
    def __init__(self, configFile, environments):
        self.fileName = configFile
        self.environments = environments

    def changeConfiguration(self):
        self.changeSimulatorApp()
        self.changeLoRaMesher()

    def getTimeout(self):
        # Read the file
        with open(self.fileName, "r") as file:
            data = file.read()

        # Parse the file
        json_data = json.loads(data)

        return json_data["SimulationTimeoutMinutes"]

    def changeSimulatorApp(self):
        # Read the file
        with open(self.fileName, "r") as file:
            data = file.read()

        # Parse the file
        json_data = json.loads(data)

        # Find the src file. We are in /Testing, and we want to go to ../src
        srcFile = os.path.join(os.path.dirname(__file__), "..", "src", "config.h")

        with open(srcFile, "r") as file:
            srcData = file.read()

        # Find the line where the simulator is defined and change it
        for line in srcData.splitlines():
            for key in json_data["Simulator"]:
                if line.find("#define " + key) != -1:
                    srcData = srcData.replace(
                        line, "#define " + key + " " + str(json_data["Simulator"][key])
                    )

        # Save the file
        with open(srcFile, "w") as file:
            file.write(srcData)

    def changeLoRaMesher(self):
        # Read the file
        with open(self.fileName, "r") as file:
            data = file.read()

        # Parse the file
        json_data = json.loads(data)

        for environment in self.environments:
            # Find the LoRaMesher src file given the environment.
            srcFile = os.path.join(
                os.path.dirname(__file__),
                "..",
                ".pio",
                "libdeps",
                environment,
                "LoRaMesher",
                "src",
                "BuildOptions.h",
            )

            with open(srcFile, "r") as file:
                srcData = file.read()

            # Find the line where the LoRaMesher is defined and change it
            for line in srcData.splitlines():
                for key in json_data["LoRaMesher"]:
                    if line.find("#define " + key) != -1:
                        srcData = srcData.replace(
                            line,
                            "#define " + key + " " + str(json_data["LoRaMesher"][key]),
                        )

            # Save the file
            with open(srcFile, "w") as file:
                file.write(srcData)

    def startSim(self):
        os.system("python simulation.py")


if __name__ == "__main__":
    configFile = "simulationTest"
    os.makedirs("simulationTest", exist_ok=True)

    simConfig = simConfiguration.SimConfiguration(configFile)

    changeConfiguration = ChangeConfigurationSerial(
        simConfig.getFileName(), ["ttgo-t-beam", "ttgo-lora32-v1"]
    )
    changeConfiguration.changeSimulatorApp()
    changeConfiguration.changeLoRaMesher()
