import os
import json
import simConfiguration
import numpy as np


class ChangeConfigurationSerial:
    def __init__(self, configFile, environments):
        self.fileName = configFile
        self.environments = environments

    def changeConfiguration(self):
        try:
            self.changeSimulatorApp()
            self.changeLoRaMesher()
            self.changeAdjacencyGraph()
        except Exception as e:
            print("Error changing configuration: " + str(e))

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

        # Check if the file exists
        if not os.path.isfile(srcFile):
            print("File not found: " + srcFile)
            return

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
            srcFile = os.path.join(os.path.dirname(__file__))

            pathName = os.path.dirname(__file__)
            if pathName.find("Testing") != -1:
                # Find the LoRaMesher src file given the environment.
                srcFile = os.path.dirname(__file__).replace("Testing", "")

            srcFile = os.path.join(
                srcFile,
                ".pio",
                "libdeps",
                environment,
                "LoRaMesher",
                "src",
                "BuildOptions.h",
            )

            # Check if the file exists
            if not os.path.isfile(srcFile):
                print("File not found: " + srcFile)
                continue

            print(srcFile)

            with open(srcFile, "r") as file:
                srcData = file.read()

            found_keys = []

            # Find the line where the LoRaMesher is defined and change it
            for line in srcData.splitlines():
                for key in json_data["LoRaMesher"]:
                    if key in found_keys:
                        continue
                    if line.find("#define " + key) != -1:
                        srcData = srcData.replace(
                            line,
                            "#define " + key + " " + str(json_data["LoRaMesher"][key]),
                        )
                        found_keys.append(key)

            # Save the file
            with open(srcFile, "w") as file:
                file.write(srcData)

    def get_cpp_function(self, matrix):
        adjacencyGraphInCpp = "\tuint16_t localAddress = getLocalAddress();\n"

        # Extract the headers (node identifiers)
        headers = matrix[0][1:]  # Skip the first element

        # Add the headers size to the C++ code
        adjacencyGraphInCpp += (
            "\tuint16_t adjacencyGraphSize = " + str(len(headers)) + ";\n"
        )

        # Add the headers in the C++ code
        adjacencyGraphInCpp += "\tuint16_t headers[adjacencyGraphSize] = {"
        adjacencyGraphInCpp += ", ".join(str(header) for header in headers)
        adjacencyGraphInCpp += "};\n"

        # Find the index of the local node
        adjacencyGraphInCpp += "\tuint16_t localAddressIndex = 0;\n"
        adjacencyGraphInCpp += "\tfor (int i = 0; i < adjacencyGraphSize; i++) {\n"
        adjacencyGraphInCpp += "\t\tif (headers[i] == localAddress) {\n"
        adjacencyGraphInCpp += "\t\t\tlocalAddressIndex = i;\n"
        adjacencyGraphInCpp += "\t\t\tbreak;\n"
        adjacencyGraphInCpp += "\t\t}\n"
        adjacencyGraphInCpp += "\t}\n"

        # Find the index of the source node
        adjacencyGraphInCpp += "\tuint16_t sourceIndex = 0;\n"
        adjacencyGraphInCpp += "\tfor (int i = 0; i < adjacencyGraphSize; i++) {\n"
        adjacencyGraphInCpp += "\t\tif (headers[i] == source) {\n"
        adjacencyGraphInCpp += "\t\t\tsourceIndex = i;\n"
        adjacencyGraphInCpp += "\t\t\tbreak;\n"
        adjacencyGraphInCpp += "\t\t}\n"
        adjacencyGraphInCpp += "\t}\n"

        # Remove the first row and column
        matrix = np.delete(matrix, 0, 0)
        matrix = np.delete(matrix, 0, 1)

        # Create the C++ matrix excluding the headers
        cpp_matrix = ",\n    ".join(
            "{ " + ", ".join(str(cell) for cell in row) + " }" for row in matrix
        )

        adjacencyGraphInCpp += f"""
    uint16_t const matrix[adjacencyGraphSize][adjacencyGraphSize] = {{
    {cpp_matrix}
    }};

    return matrix[localAddressIndex][sourceIndex] != 0;
    """
        return adjacencyGraphInCpp

    def changeAdjacencyGraph(self):
        print("Changing adjacency graph")
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

            # Check if the file exists
            if not os.path.isfile(srcFile):
                print("File not found: " + srcFile)
                continue

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

            # Check if the file exists
            if not os.path.isfile(srcFile):
                print("File not found: " + srcFile)
                continue

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
                    adjacencyGraphInCpp = "\treturn true;\n"
                    print("No adjacency graph found")

                else:
                    string_matrix = json_data["LoRaMesherAdjacencyGraph"]

                    adjacencyGraphInCpp = self.get_cpp_function(string_matrix)

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
