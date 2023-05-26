import mqttClient
import threading
import updatePlatformio
import os
import sys
import json


def main():
    if len(sys.argv) < 2:
        print("Please provide the name of the directory.")
        return

    directory = sys.argv[1]
    # Use the directory_name in your code

    print("Directory name:", directory)

    # Create an Event object to synchronize the main script
    # Create multiple Condition objects
    shared_state_change = threading.Event()

    shared_state = {
        "builded": False,  # True if the build is done
        "deviceMonitorStarted": False,  # True if the device monitor is started
        "allDevicesStartedSim": False,
        "allDevicesEndedSim": False,
        "allDevicesStartedSendSim": False,
        "allDevicesEndedSendSim": False,
        "error": False,
        "error_message": "",
    }

    os.makedirs(directory, exist_ok=True)

    # Update the PlatformIO
    updater = updatePlatformio.UpdatePlatformIO(
        directory,
        shared_state_change,
        shared_state,
    )

    def waitUntilBuild():
        while not shared_state["builded"]:
            shared_state_change.wait()

            if shared_state_change.is_set():
                shared_state_change.clear()

                if shared_state["error"]:
                    print("Error: " + shared_state["error_message"])
                    return

    waitUntilBuild()

    def waitUntilADeviceBuilded():
        while not shared_state["deviceMonitorStarted"]:
            shared_state_change.wait()

            if shared_state_change.is_set():
                shared_state_change.clear()

                if shared_state["error"]:
                    print("Error: " + shared_state["error_message"])
                    return

    waitUntilADeviceBuilded()

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

    def saveStatus():
        json_str = json.dumps(shared_state, indent=4)

        # Save the results in a file using a json format
        with open(os.path.join(directory, "results.json"), "w") as f:
            f.write(json_str)

    try:
        while True:
            # Wait for the shared_state_change event to be set
            shared_state_change.wait(5)

            if shared_state_change.is_set():
                shared_state_change.clear()

                saveStatus()

                if shared_state["error"]:
                    print("Error: " + shared_state["error_message"])
                    break

    except KeyboardInterrupt:
        print("KeyboardInterrupt")
        message_thread.join()
        updater.killThreads()

    message_thread.join()
    saveStatus()

    print("Successfully closed the program")


if __name__ == "__main__":
    main()
