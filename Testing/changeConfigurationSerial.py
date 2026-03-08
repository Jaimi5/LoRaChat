import os
import json
import simConfiguration
import numpy as np

# v1 param name (defaultConfigValues.json) -> v2 #define name in loraMeshService.cpp
V2_PARAM_MAP = {
    "LM_BAND":              ("LORA_FREQUENCY",         lambda v: v),
    "LM_BANDWIDTH":         ("LORA_BANDWIDTH",          lambda v: v),
    "LM_LORASF":            ("LORA_SPREADING_FACTOR",   lambda v: v),
    "LM_CODING_RATE":       ("LORA_CODING_RATE",        lambda v: v),
    "LM_PREAMBLE_LENGTH":   ("LORA_PREAMBLE_LENGTH",    lambda v: v),
    "LM_POWER":             ("LORA_POWER",              lambda v: v),
    "LM_DUTY_CYCLE":        ("LORA_DUTY_CYCLE",         lambda v: f"{float(v)/100.0}f"),
    "LM_MAX_PACKET_SIZE":   ("LORA_MAX_PACKET_SIZE",    lambda v: v),
    # Direct v2 params (no conversion needed):
    "LORA_MIN_SLEEP_FRACTION": ("LORA_MIN_SLEEP_FRACTION", lambda v: v),
}


def _is_v2_env(env):
    return env.endswith("-v2")


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

    def getDeviceMapping(self):
        """Get the DeviceMapping (port to environment mapping) from config"""
        # Read the file
        with open(self.fileName, "r") as file:
            data = file.read()

        # Parse the file
        json_data = json.loads(data)

        return json_data.get("DeviceMapping", {})

    def getEnvironments(self):
        """Extract unique environments from DeviceMapping"""
        device_mapping = self.getDeviceMapping()

        if not device_mapping:
            # Fallback to the environments passed in constructor
            return self.environments

        # Get unique environments from the mapping
        return list(set(device_mapping.values()))

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
        with open(self.fileName) as f:
            json_data = json.loads(f.read())

        envs = self.getEnvironments()
        v1_envs = [e for e in envs if not _is_v2_env(e)]
        v2_envs = [e for e in envs if _is_v2_env(e)]

        self._changeLoRaMesherV1(v1_envs, json_data)
        self._changeLoRaMesherV2(v2_envs, json_data)

    def _changeLoRaMesherV1(self, v1_envs, json_data):
        """Modify BuildOptions.h for each v1 environment."""
        for environment in v1_envs:
            pathName = os.path.dirname(__file__)
            if pathName.find("Testing") != -1:
                base = pathName.replace("Testing", "")
            else:
                base = pathName
            srcFile = os.path.join(base, ".pio", "libdeps", environment,
                                   "LoRaMesher", "src", "BuildOptions.h")
            if not os.path.isfile(srcFile):
                print("File not found: " + srcFile)
                continue
            with open(srcFile) as f:
                srcData = f.read()
            found_keys = []
            for line in srcData.splitlines():
                for key in json_data["LoRaMesher"]:
                    if key in found_keys or key.startswith("_comment"):
                        continue
                    if line.find("#define " + key) != -1:
                        srcData = srcData.replace(
                            line, "#define " + key + " " + str(json_data["LoRaMesher"][key]))
                        found_keys.append(key)
            with open(srcFile, "w") as f:
                f.write(srcData)

    def _changeLoRaMesherV2(self, v2_envs, json_data):
        """Modify loraMeshService.cpp once (all v2 envs share the same source file)."""
        if not v2_envs:
            return
        pathName = os.path.dirname(__file__)
        if pathName.find("Testing") != -1:
            base = pathName.replace("Testing", "")
        else:
            base = pathName
        srcFile = os.path.join(base, "src", "loramesh", "loraMeshService.cpp")
        if not os.path.isfile(srcFile):
            print("v2 config source not found: " + srcFile)
            return
        with open(srcFile) as f:
            srcData = f.read()
        for v1_key, (v2_key, converter) in V2_PARAM_MAP.items():
            if v1_key not in json_data["LoRaMesher"]:
                continue
            raw_value = json_data["LoRaMesher"][v1_key]
            converted_value = converter(raw_value)
            for line in srcData.splitlines():
                if line.find("#define " + v2_key + " ") != -1:
                    srcData = srcData.replace(
                        line, "#define " + v2_key + " " + str(converted_value))
                    break
        with open(srcFile, "w") as f:
            f.write(srcData)
        print("v2 config written to: " + srcFile)

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

        envs = self.getEnvironments()
        v1_envs = [e for e in envs if not _is_v2_env(e)]
        v2_envs = [e for e in envs if _is_v2_env(e)]

        if v2_envs:
            print("Adjacency graph not yet supported for v2 environments: " + str(v2_envs))

        for environment in v1_envs:
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
                if line.find("#define LM_TESTING") != -1:
                    srcData = srcData.replace(line, "#define LM_TESTING")
                    break

            # Save the file
            with open(srcFile, "w") as file:
                file.write(srcData)

        for environment in v1_envs:
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
