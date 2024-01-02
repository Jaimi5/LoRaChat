import os
import json
import graph
import numpy as np


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

        # Add the adjacency graph
        json_data["LoRaMesherAdjacencyGraph"] = self.getAdjacencyGraph().tolist()

        # Save the file
        with open(self.fileName, "w") as file:
            file.write(json.dumps(json_data, indent=4))

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
