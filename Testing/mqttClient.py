# MQTT configuration
import json
from datetime import datetime
import paho.mqtt.client as mqtt
import os
import packetService


class MQTT:
    def __init__(
        self,
        file,
        numberOfPorts,
        shared_state_change,
        shared_state,
    ):
        self.file = file
        self.fileName = os.path.join(file, "data.json")
        self.keepAliveFile = os.path.join(file, "keepAlive.json")
        self.numberOfPorts = numberOfPorts

        self.packetService = packetService.PacketService(
            file,
            numberOfPorts,
            shared_state_change,
            shared_state,
        )

        host = "localhost"
        port = 1883
        MQTT_TOPIC_IN = "to-server/#"
        client = mqtt.Client("Testing")
        client.connect(host, port)

        client.loop_start()

        client.subscribe(MQTT_TOPIC_IN)

        client.on_message = self.on_message

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

    def on_message(self, client, userdata, message):
        date = datetime.now()

        # Read the file
        json_data = self.createAndOpenFile()

        # Parse the message.payload to json
        try:
            message_payload = json.loads(message.payload)
            self.packetService.processPacket(message_payload)

            # Add the new data
            json_data.append(
                {
                    "topic": message.topic,
                    "payload": message_payload,
                    "date": date.strftime("%Y-%m-%d %H:%M:%S"),
                }
            )

            # Write the file
            with open(self.fileName, "w") as file:
                file.write(json.dumps(json_data, indent=4))
        except json.JSONDecodeError:
            with open(self.keepAliveFile, "a") as file:
                file.write(
                    f"{message.topic}: {date.strftime('%Y-%m-%d %H:%M:%S')} - {message.payload}\n"
                )
            # There are data without json format and we don't want to save them
