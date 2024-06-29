# MQTT configuration
import json
from datetime import datetime
import paho.mqtt.client as mqtt
import os
import packetService
import manageFile


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
        self.manageFileData = manageFile.ManageFile(self.fileName)
        self.keepAliveFile = os.path.join(file, "keepAlive.json")
        self.numberOfPorts = numberOfPorts
        self.shared_state_change = shared_state_change
        self.shared_state = shared_state

        self.packetService = packetService.PacketService(
            file,
            numberOfPorts,
            shared_state_change,
            shared_state,
        )
        host = "localhost"
        port = 1883
        MQTT_TOPIC_IN = "to-server/#"

        try:
            self.client = mqtt.Client(protocol=mqtt.MQTTv311)
            self.client.on_message = self.on_message
            self.client.connect(host, port)

            self.client.loop_start()

            self.client.subscribe(MQTT_TOPIC_IN, 2)

        except ConnectionRefusedError:
            self.shared_state["error"] = True
            self.shared_state["error_message"] = "MQTT broker is not running"
            self.shared_state_change.set()

        print("MQTT client started")

    def on_message(self, client, userdata, message):
        date = datetime.now()

        # Read the file
        json_data = self.manageFileData.createAndOpenFile()

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
            self.manageFileData.saveFile(json_data)

        except json.JSONDecodeError:
            with open(self.keepAliveFile, "a") as file:
                file.write(
                    f"{message.topic}: {date.strftime('%Y-%m-%d %H:%M:%S')} - {message.payload}\n"
                )
            # There are data without json format and we don't want to save them

    def disconnect(self):
        print("disconnecting MQTT client")
        self.client.loop_stop()
        self.client.disconnect()
