import os
import json
import pandas as pd
import matplotlib.pyplot as plt
import tkinter as tk
from tkinter import Frame
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from downloadPlot import download_plot
from deviceColors import get_color_by_devices


def draw_eed_by_device(frame: Frame, directory):
    # Get the messages.json file
    messages = os.path.join(directory, "messages.json")
    if not os.path.exists(messages):
        print("No messages.json file found in directory")
        return None

    # Get the status.json file
    status = os.path.join(directory, "status.json")
    if not os.path.exists(status):
        print("No status.json file found in directory")
        return None

    # Clear the frame
    for widget in frame.winfo_children():
        widget.destroy()

    # Load data from JSON
    with open(messages, "r") as f:
        data = json.load(f)

    # Convert to pandas DataFrame
    df = pd.DataFrame(data)

    # Expand the 'payload' dictionary into separate columns
    payload_df = pd.json_normalize(df["payload"])
    df = pd.concat([df.drop("payload", axis=1), payload_df], axis=1)

    # Convert date to datetime object
    df["date"] = pd.to_datetime(df["date"], format="%d/%m/%Y %H:%M:%S")

    # Draw a scatter plot
    fig = Figure(figsize=(10, 5), dpi=100)
    ax = fig.add_subplot(111)

    # Find how many devices are in the data
    status_df = pd.read_json(status)
    status_df["startedDate"] = pd.to_datetime(
        status_df["startedDate"], format="%d/%m/%Y %H:%M:%S"
    )
    devices = status_df["device"].unique()

    # Sort the devices
    devices.sort()

    # Generate a list of colors for each device
    colors = get_color_by_devices(devices)

    # Read the configuration file
    configFile = os.path.join(directory, "simConfiguration.json")
    with open(configFile, "r") as f:
        config = json.load(f)

    # Get the time between messages
    time_between_messages = int(config["Simulator"]["PACKET_DELAY"])

    # Convert the time_between_messages from integer to timedelta
    time_between_messages_td = pd.to_timedelta(time_between_messages, unit="ms")

    # Values
    messages_eed = {}

    # List to hold each device's EED data
    eed_data = []

    # For each message, calculate the EED
    for device in devices:
        # Get the data for this device
        device_df = df[df["addrSrc"] == device].copy()

        # Get the time the device started
        device_start_time = status_df[status_df["device"] == device][
            "startedDate"
        ].iloc[0] + pd.to_timedelta(4, unit="m")

        # Calculate the EED
        device_df["EED"] = (device_df["date"] - device_start_time) - (
            time_between_messages_td * device_df["messageId"]
        )

        # Get the average EED
        avg_eed = device_df["EED"].mean()

        # Add the average EED to the dictionary
        messages_eed[device] = avg_eed.total_seconds()

        # Convert the EED data for this device to total seconds
        device_eed_seconds = device_df["EED"].dt.total_seconds()

        # Add this device's data to the list, resetting the index so they can be concatenated later
        eed_data.append(
            pd.DataFrame({device: device_eed_seconds}).reset_index(drop=True)
        )

    # Concatenate the data list into a DataFrame
    plot_df = pd.concat(eed_data, axis=1)

    # Set all the NaN values to 0
    # plot_df = plot_df.fillna(0)

    # Box plot the EED data and put it on the canvas
    plot_df.boxplot(ax=ax, grid=False)

    # Add a title
    ax.set_title("EED by Device")

    # Add a label to the x-axis
    ax.set_xlabel("Device ID")

    # Add a label to the y-axis
    ax.set_ylabel("EED (s)")

    # Add the plot to the tkinter frame
    canvas = FigureCanvasTkAgg(fig, master=frame)  # A tk.DrawingArea.
    canvas.draw()
    canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")

    # Add the toggle button
    addToggleLogScale(frame, canvas, ax)

    # Add a button to download the plot
    download_button = tk.Button(
        frame,
        text="Download Plot",
        command=lambda: download_plot(canvas, directory, "endToEndDelay.png"),
    )

    # Add the button to the frame
    download_button.grid(row=1, column=0, sticky="nsew")

    return canvas

    # For the bar plot, the keys (x values) should be strings, and the values (y values) should be floats
    messages_eed = {str(k): float(v) for k, v in messages_eed.items()}

    # Scatter plot with the x axis as the device ID. All the x axis should be separated the same distance and it should print the device ID
    ax.bar(messages_eed.keys(), messages_eed.values(), color=colors, width=0.5)

    # Add value on top of each bar
    for value in messages_eed.keys():
        minutes, seconds = divmod(
            messages_eed[value], 60
        )  # Converts seconds to minutes and seconds
        eed_str = "{:.0f}m {:.0f}s".format(minutes, seconds)
        ax.text(
            x=value,
            y=messages_eed[value] + 0.1,
            s=eed_str,
            ha="center",
            va="bottom",
        )

    # Add a title
    ax.set_title("EED by Device")

    # Add a label to the x-axis
    ax.set_xlabel("Device ID")

    # Add a label to the y-axis
    ax.set_ylabel("EED (s)")

    # Add the plot to the tkinter frame
    canvas = FigureCanvasTkAgg(fig, master=frame)  # A tk.DrawingArea.
    canvas.draw()
    canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")


def addToggleLogScale(frame, canvas, ax):
    """
    Adds a button to the frame that toggles the y-axis between a linear and log scale
    """

    # Create a variable to hold the state of the log scale
    log_scale = tk.BooleanVar()
    log_scale.set(False)

    # Create a button to toggle the log scale
    log_button = tk.Button(
        frame,
        text="Toggle Log Scale",
        command=lambda: toggleLogScale(log_scale, canvas, ax),
    )
    log_button.grid(row=2, column=0, sticky="nsew")

    return


def toggleLogScale(log_scale, canvas, ax):
    """
    Toggles the y-axis between a linear and log scale
    """

    # Get the current state of the log scale
    state = log_scale.get()

    # Toggle the state
    log_scale.set(not state)

    # Toggle the y-axis scale
    if state:
        ax.set_yscale("linear")
    else:
        ax.set_yscale("log")

    # Redraw the canvas
    canvas.draw()
    canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")

    return
