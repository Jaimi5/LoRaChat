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

    def changeAdjacencyGraph(self):
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

            # Define that we are in a test environment
            # Find the line where the LoRaMesher is defined and change it
            for line in srcData.splitlines():
                if line.find("#define TESTING") != -1:
                    srcData = srcData.replace(line, "#define TESTING")
                    break

            # Save the file
            with open(srcFile, "w") as file:
                file.write(srcData)

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
                "LoraMesher.cpp",
            )

            with open(srcFile, "r") as file:
                srcData = file.readlines()

            # Find the function where the LoRaMesher adjacency graph is defined and change it. It is called canReceivePacket
            # Clear the function contents
            start_line = -1
            end_line = -1
            for i, line in enumerate(srcData):
                if line.find("bool LoraMesher::canReceivePacket(") != -1:
                    start_line = i
                if start_line != -1 and line.strip() == "#endif":
                    end_line = i - 1
                    break

            if start_line >= 0 and end_line >= 0:
                # Clear function contents
                del srcData[start_line + 1 : end_line]

                adjacencyGraphInCpp = ""

                if json_data["LoRaMesherAdjacencyGraph"] == []:
                    adjacencyGraphInCpp = "\treturn true;"

                else:
                    adjacencyGraphInCpp = (
                        "\tuint16_t localAddress = getLocalAddress();\n"
                    )

                    # Iterate through the adjacency graph and add it to the cpp file
                    for i in range(0, len(json_data["LoRaMesherAdjacencyGraph"])):
                        adjacencyGraphInCpp += (
                            "\tif (localAddress == "
                            + str(json_data["LoRaMesherAdjacencyGraph"][i]["id"])
                            + ") {\n"
                        )
                        for neighbor in json_data["LoRaMesherAdjacencyGraph"][i][
                            "neighbors"
                        ]:
                            adjacencyGraphInCpp += (
                                "\t\tif (source == " + str(neighbor["to"]) + ") {\n"
                            )
                            adjacencyGraphInCpp += (
                                "\t\t\treturn "
                                + str(neighbor["distance"] == "1").lower()
                                + ";\n"
                            )
                            adjacencyGraphInCpp += "\t\t}\n"

                        adjacencyGraphInCpp += "\t\treturn false;\n"
                        adjacencyGraphInCpp += "\t}\n"

                    adjacencyGraphInCpp += "\treturn false;\n"

                # Add new contents
                srcData.insert(
                    start_line + 1,
                    adjacencyGraphInCpp,
                )

                # Write new content to the file
                with open(srcFile, "w") as file:
                    file.writelines(srcData)

    def startSim(self):
        os.system("python simulation.py")


if __name__ == "__main__":
    configFile = "simulationTest"
    os.makedirs("simulationTest", exist_ok=True)

    simConfig = simConfiguration.SimConfiguration(configFile, "simulationTest")

    changeConfiguration = ChangeConfigurationSerial(
        simConfig.getFileName(), ["ttgo-t-beam", "ttgo-lora32-v1"]
    )
    changeConfiguration.changeAdjacencyGraph()
    # changeConfiguration.changeSimulatorApp()
    # changeConfiguration.changeLoRaMesher()
