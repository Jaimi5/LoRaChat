# MQTT configuration
import json
from datetime import datetime
import paho.mqtt.client as mqtt
import os


class MQTT:
    def __init__(self, file):
        self.file = file
        self.fileName = os.path.join(file, "data1.json")
        host = "localhost"
        port = 1883
        MQTT_TOPIC_IN = "to-server/#"

        try:
            self.client = mqtt.Client("Testing")
            self.client.connect(host, port)

            self.client.loop_start()

            self.client.subscribe(MQTT_TOPIC_IN, 2)

            self.client.on_message = self.on_message
        except ConnectionRefusedError:
            print("MQTT server is not running")
            print("Please run the MQTT server, ex. docker")
            return

        print("MQTT client started")

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

    def on_message(self, client, userdata, message):
        print("Message received, %s" % message.payload)
        date = datetime.now()

        # Read the file
        json_data = self.createAndOpenFile()

        # Parse the message.payload to json
        try:
            message_payload = json.loads(message.payload)

            # Add the new data
            json_data.append(
                {
                    "topic": message.topic,
                    "payload": message_payload,
                    "date": date.strftime("%Y-%m-%d %H:%M:%S"),
                }
            )

            # Write the file
            self.saveFile(json_data)

        except json.JSONDecodeError:
            print("JSONDecodeError")
            # There are data without json format and we don't want to save them

    def disconnect(self):
        print("disconnecting MQTT client")
        self.client.loop_stop(True)
        self.client.disconnect()


if __name__ == "__main__":
    file = os.path.dirname(os.path.realpath(__file__))

    mqtt = MQTT(file)

    while True:
        continue
