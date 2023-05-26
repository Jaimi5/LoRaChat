import os
import json


class SimConfiguration:
    def __init__(self, file):
        self.fileName = os.path.join(file, "simConfiguration.json")
        self.numberOfDevices = 0

        self.createConfiguration()

    def createAndOpenFile(self):
        if not os.path.exists(self.fileName):
            # If the file doesn't exist, create it
            with open(self.fileName, "w") as file:
                file.write("[]")

        # Read the file
        with open(self.fileName, "r") as file:
            data = file.read()

        # Parse the file
        json_data = json.loads(data)

        return json_data

    def saveFile(self, json_data):
        # Save the file
        with open(self.fileName, "w") as file:
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

        print("Enter the values for the configuration:")

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

        # Save the file
        with open(self.fileName, "w") as file:
            file.write(json.dumps(json_data, indent=4))


if __name__ == "__main__":
    simConfiguration = SimConfiguration(os.path.dirname(__file__))
