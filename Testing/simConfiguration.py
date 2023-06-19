import os
import json


class SimConfiguration:
    def __init__(self, file, name):
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
        json_data["LoRaMesherAdjacencyGraph"] = self.getAdjacencyGraph()

        # Save the file
        with open(self.fileName, "w") as file:
            file.write(json.dumps(json_data, indent=4))

    def getAdjacencyGraph(self):
        # Ask the user if he wants to add a an adjacency graph
        addAdjacencyGraph = input("Do you want to add an adjacency graph? (y/n): ")
        while addAdjacencyGraph != "y" and addAdjacencyGraph != "n":
            addAdjacencyGraph = input("Do you want to add an adjacency graph? (y/n): ")

        if addAdjacencyGraph == "n":
            return []

        # Ask the user how many nodes he wants to add to the adjacency graph
        numberOfNodes = int(
            input("How many nodes do you want to add to the adjacency graph?: ")
        )

        # Ask the user the id of the nodes
        nodes = []
        for i in range(numberOfNodes):
            nodes.append(input("Node " + str(i) + " id: "))

        # Ask the user the adjacency matrix
        adjacencyMatrix = []

        for i in range(numberOfNodes):
            adjacencyMatrix.append([])
            for j in range(numberOfNodes):
                adjacencyMatrix[i].append(
                    input("Node " + str(i) + " to node " + str(j) + " distance: ")
                )

        # Create the adjacency graph
        adjacencyGraph = []

        for i in range(numberOfNodes):
            neighbors = []
            for j in range(numberOfNodes):
                if adjacencyMatrix[i][j] != "":
                    neighbors.append(
                        {"to": nodes[j], "distance": adjacencyMatrix[i][j]}
                    )
            adjacencyGraph.append({"id": nodes[i], "neighbors": neighbors})

        return adjacencyGraph

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
