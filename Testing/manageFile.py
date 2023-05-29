import os
import json


class ManageFile:
    def __init__(self, file):
        self.fileName = file

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