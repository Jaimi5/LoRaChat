import os
import json
import graph
import numpy as np
from platformio import util


class SimConfiguration:
    def __init__(self, file, name):
        self.file = file
        self.fileName = os.path.join(file, "simConfiguration.json")
        self.Name = name

    def getFileName(self):
        return self.fileName

    def getName(self):
        return self.Name

    def copyConfiguration(self, directory):
        # Copy the file "simConfiguration.json" to the new directory
        with open(self.fileName, "r") as file:
            data = file.read()

        # Parse the file
        json_data = json.loads(data)

        # Save the file
        with open(os.path.join(directory, "simConfiguration.json"), "w") as file:
            file.write(json.dumps(json_data, indent=4))

    def createConfiguration(self):
        # Given the defaultConfigValues.json file, ask the user for the values of the configuration and save it in the simConfiguration.json file
        # Read the file
        with open(
            os.path.join(os.path.dirname(__file__), "defaultConfigValues.json"), "r"
        ) as file:
            data = file.read()

        # Parse the file
        json_data = json.loads(data)

        # Ask the user for the values of the configuration. If the user doesn't enter a value, the default value will be used
        # Check if the key is a dictionary or a simple value
        for key in json_data:
            if isinstance(json_data[key], dict):
                # If the key is a dictionary, ask for the values of the dictionary
                for subKey in json_data[key]:
                    value = input(subKey + " (" + str(json_data[key][subKey]) + "): ")
                    if value != "":
                        json_data[key][subKey] = value
            else:
                value = input(key + " (" + str(json_data[key]) + "): ")
                if value != "":
                    json_data[key] = value

        # Add the device mapping (port to environment)
        json_data["DeviceMapping"] = self.getDeviceMapping()

        # Add the adjacency graph
        json_data["LoRaMesherAdjacencyGraph"] = self.getAdjacencyGraph().tolist()

        # Save the file
        with open(self.fileName, "w") as file:
            file.write(json.dumps(json_data, indent=4))

    def getDeviceMapping(self):
        """Interactively detect devices one by one as they're connected"""
        print("\n=== Device Port Mapping Configuration ===")
        print("Connect devices one by one. The system will detect each new port.")

        # Available environments (from platformio.ini)
        available_envs = [
            "ttgo-t-beam",
            "ttgo-t-beam-v1-2",
            "ttgo-lora32-v1",
            "esp-wrover-kitNAYAD_V1R2",
            "MAKERFABS_SENSELORA_MOISTURE"
        ]

        print("\nAvailable PlatformIO environments:")
        for i, env in enumerate(available_envs):
            print(f"  {i+1}. {env}")

        device_mapping = {}

        # Get initial ports (baseline)
        current_ports = set([port['port'] for port in util.get_serial_ports()])
        print(f"\nCurrently connected ports: {list(current_ports) if current_ports else 'None'}")

        while True:
            response = input("\nConnect a device and press Enter (or type 'done' to finish): ").strip().lower()

            if response == 'done':
                break

            # Detect new ports
            new_ports_list = util.get_serial_ports()
            new_ports = set([port['port'] for port in new_ports_list])

            detected = new_ports - current_ports

            if len(detected) == 0:
                print("  No new device detected. Make sure the device is properly connected.")
                continue
            elif len(detected) > 1:
                print(f"  Warning: Multiple new ports detected: {list(detected)}")
                print(f"  Using the first one: {list(detected)[0]}")
                new_port = list(detected)[0]
            else:
                new_port = list(detected)[0]

            # Get device info
            port_info = next((p for p in new_ports_list if p['port'] == new_port), None)
            description = port_info.get('description', 'Unknown') if port_info else 'Unknown'

            print(f"\n✓ Detected: {new_port} ({description})")

            # Ask for environment
            while True:
                env_response = input(f"  Select environment (1-{len(available_envs)}): ").strip()

                try:
                    env_index = int(env_response) - 1
                    if 0 <= env_index < len(available_envs):
                        device_mapping[new_port] = available_envs[env_index]
                        print(f"  ✓ Mapped {new_port} -> {available_envs[env_index]}")
                        current_ports.add(new_port)
                        break
                    else:
                        print(f"  Invalid number. Please enter 1-{len(available_envs)}")
                except ValueError:
                    print("  Invalid input. Please enter a number")

        print(f"\n✓ Device mapping complete: {len(device_mapping)} device(s) configured")
        if device_mapping:
            print("Configured devices:")
            for port, env in device_mapping.items():
                print(f"  {port} -> {env}")

        return device_mapping

    def getAdjacencyGraph(self):
        # Ask the user if he wants to add a an adjacency graph
        addAdjacencyGraph = input("Do you want to add an adjacency graph? (y/n): ")
        while addAdjacencyGraph != "y" and addAdjacencyGraph != "n":
            addAdjacencyGraph = input("Do you want to add an adjacency graph? (y/n): ")

        if addAdjacencyGraph == "n":
            return np.array([])

        # # Ask the user if he wants to add manually the nodes or if he wants to add a matrix
        # addManually = input("Do you want to add the nodes manually? (y/n): ")
        # while addManually != "y" and addManually != "n":
        #     addManually = input("Do you want to add the nodes manually? (y/n): ")

        # if addManually == "y":
        return self.getAdjacencyGraphManually()
        # else:
        #     return self.getAdjacencyGraphMatrix()

    def getAdjacencyGraphManually(self):
        # Ask the user how many nodes he wants to add to the adjacency graph
        numberOfNodes = int(
            input("How many nodes do you want to add to the adjacency graph?: ")
        )

        print(
            "Enter the values for the adjacency graph, integer or hex values with (0x)"
        )

        # Ask the user the id of the nodes
        nodes = []
        for i in range(numberOfNodes):
            value = input("Node " + str(i) + " id: ")
            if value.startswith("0x"):
                nodes.append(int(value, 16))
            else:
                nodes.append(int(value))

        print(
            "Enter the distance between the nodes, 1 if they are neighbors, empty if they are not neighbors"
        )

        # Ask the user the adjacency matrix
        Graph = graph.Graph()

        for i in range(numberOfNodes):
            for j in range(numberOfNodes):
                if Graph.check_if_edge_exists(nodes[i], nodes[j]):
                    continue
                if i == j:
                    Graph.add_edge(nodes[i], nodes[j], 1)
                    continue
                distStr = input(
                    "Node "
                    + str(nodes[i])
                    + " to node "
                    + str(nodes[j])
                    + " distance: "
                )
                try:
                    if distStr.startswith("0x"):
                        distance = int(distStr, 16)
                    else:
                        distance = int(distStr)
                except ValueError:
                    distance = 0

                Graph.add_edge(nodes[i], nodes[j], distance)

        # Create the adjacency graph
        adjacencyGraph = Graph.get_adjacency_matrix()

        return adjacencyGraph

    def getAdjacencyGraphMatrix(self):
        print("Enter the matrix for the adjacency graph")

        # Ask the user the adjacency matrix string
        adjacencyGraph = input("Adjacency graph matrix: ")

        # Parse the adjacency matrix string
        numpy_array = json.loads(adjacencyGraph)

        return np.array(numpy_array)

    def printAdjacencyGraph(self):
        # Read the file
        with open(self.fileName, "r") as file:
            data = file.read()

        # Parse the file
        json_data = json.loads(data)

        # Print the adjacency graph
        print(json.dumps(json_data["LoRaMesherAdjacencyGraph"], indent=4))


if __name__ == "__main__":
    simConfiguration = SimConfiguration(os.path.dirname(__file__), "simConfiguration")
    simConfiguration.createConfiguration()
