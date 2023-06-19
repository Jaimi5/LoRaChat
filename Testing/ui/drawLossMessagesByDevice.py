import os
import json
import pandas as pd
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
import tkinter as tk
from tkinter import Frame
from downloadPlot import download_plot
from deviceColors import get_color_by_devices


def draw_loss_messages_by_device(frame: Frame, directory):
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

    # Draw a scatter plot
    fig = Figure(figsize=(10, 5), dpi=100)
    ax = fig.add_subplot(111)

    # Find how many devices are in the data
    status_df = pd.read_json(status)
    devices = status_df["device"].unique()

    # Sort the devices
    devices.sort()

    # Generate a list of colors for each device
    colors = get_color_by_devices(devices)

    # Read the configuration file
    configFile = os.path.join(directory, "simConfiguration.json")
    with open(configFile, "r") as f:
        config = json.load(f)

    # Find the number of messages sent
    total_messages = int(config["Simulator"]["PACKET_COUNT"])

    packet_loss = {}

    # Calculate average packet loss for each device
    for device in devices:
        device_df = df[df["addrSrc"] == device]
        # Find the number of messages received by this device
        total_received = len(device_df)
        # Find the number of messages lost
        total_lost = total_messages - total_received
        # Calculate the percentage of messages lost
        total_lost = total_lost / total_messages * 100

        packet_loss[str(device)] = total_lost

    # Scatter plot with the x axis as the device ID. All the x axis should be separated the same distance and it should print the device ID
    ax.bar(packet_loss.keys(), packet_loss.values(), color=colors, width=0.5)

    # Add value on top of each bar
    for value in packet_loss.keys():
        ax.text(
            x=value,
            y=packet_loss[value] + 0.1,
            s="{:.2f}".format(packet_loss[value]) + "%",
            ha="center",
            va="bottom",
        )

    # ax.scatter(devices_indices, average_packet_loss_series.values, color=colors)

    # Replace the x-axis labels with the actual device IDs
    # plt.xticks(devices_indices, average_packet_loss_series.index)

    ax.set_xlabel("Device ID")
    ax.set_ylabel("Packet Loss (%)")
    ax.set_title("Packet Loss by Device")

    # Add the plot to the tkinter frame
    canvas = FigureCanvasTkAgg(fig, master=frame)  # A tk.DrawingArea.
    canvas.draw()
    canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")

    # Add a button to download the plot
    download_button = tk.Button(
        frame,
        text="Download Plot",
        command=lambda: download_plot(canvas, directory, "messagesLost.png"),
    )
    download_button.grid(row=1, column=0, sticky="nsew")

    return canvas
