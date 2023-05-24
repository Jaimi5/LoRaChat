import mqttClient
import threading
import updatePlatformio
import os

# Create an Event object to synchronize the main script
# Create multiple Condition objects
shared_state_change = threading.Event()

shared_state = {
    "allDevicesStartedSim": False,
    "allDevicesEndedSim": False,
    "allDevicesStartedSendSim": False,
    "allDevicesEndedSendSim": False,
    "error": False,
    "error_message": "",
}

directory = "Testing1"

os.makedirs(directory, exist_ok=True)

message_thread = threading.Thread(
    target=mqttClient.MQTT,
    args=(
        directory,  # file of the results
        updatePlatformio.getNumberOfPorts(),
        shared_state_change,
        shared_state,
    ),
)

message_thread.start()

# Update the PlatformIO
updater = updatePlatformio.UpdatePlatformIO(
    directory,
    shared_state_change,
    shared_state,
)

try:
    while True:
        # Wait for the shared_state_change event to be set
        shared_state_change.wait(5)

        if shared_state_change.is_set():
            shared_state_change.clear()

            if shared_state["error"]:
                print("Error: " + shared_state["error_message"])
                break

except KeyboardInterrupt:
    print("KeyboardInterrupt")
    message_thread.join()
    updater.killThreads()

message_thread.join()
print("Successfully closed the program")
