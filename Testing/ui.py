import tkinter as tk
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
import json
from datetime import datetime
import numpy as np
import os
import tkinter.filedialog as filedialog
import random
import matplotlib.colors as mcolors


def calculate_summary_by_device(fileName):
    # Load data from JSON
    with open(fileName, "r") as f:
        data = json.load(f)

    # Convert to pandas DataFrame
    df = pd.DataFrame(data)

    # Expand the 'payload' dictionary into separate columns
    payload_df = pd.json_normalize(df["payload"])

    df = pd.concat([df.drop("payload", axis=1), payload_df], axis=1)

    # Separate sent and received packets
    sent_packets = df[df["state.Type"] == 1]
    received_packets = df[df["state.Type"] == 0]

    sent_packets = sent_packets.copy()
    received_packets = received_packets.copy()

    # Create a unique identifier for each packet
    sent_packets.loc[:, "packet_id"] = (
        sent_packets["state.packetHeader.Type"].astype(str)
        + "-"
        + sent_packets["state.packetHeader.Id"].astype(str)
        + "-"
        + sent_packets["state.packetHeader.Src"].astype(str)
    )
    received_packets.loc[:, "packet_id"] = (
        received_packets["state.packetHeader.Type"].astype(str)
        + "-"
        + received_packets["state.packetHeader.Id"].astype(str)
        + "-"
        + received_packets["state.packetHeader.Src"].astype(str)
    )

    # Find the total number of devices
    total_devices = df["addrSrc"].nunique()

    # Initialize a dictionary to store the summary statistics for each device
    summary_by_device = {}

    # Calculate packet loss for each device
    for device in df["addrSrc"].unique():
        device_sent_packets = sent_packets[sent_packets["addrSrc"] == device]
        device_packet_loss = 0

        device_received_packets = received_packets[
            received_packets["addrSrc"] == device
        ]

        # Calculate the number of packets lost for each device
        for packet_id in device_sent_packets["packet_id"]:
            # Check how many devices received this packet
            received_count = received_packets[
                received_packets["packet_id"] == packet_id
            ]["addrSrc"].nunique()

            device_packet_loss += total_devices - 1 - received_count

        summary_by_device[device] = {}

        summary_by_device[device]["packet_loss"] = device_packet_loss

        summary_by_device[device]["total_sent"] = device_sent_packets.shape[0]

        summary_by_device[device]["total_received"] = device_received_packets.shape[0]

        summary_by_device[device]["packet_loss_rate"] = (
            device_packet_loss / device_sent_packets.shape[0]
        )

    return summary_by_device


def draw_plot_and_summary(fileName):
    # Clear the frame
    for widget in frame.winfo_children():
        widget.destroy()

    # Load data from JSON
    with open(fileName, "r") as f:
        data = json.load(f)

    # Convert to pandas DataFrame
    df = pd.DataFrame(data)

    # Expand the 'payload' dictionary into separate columns
    payload_df = pd.json_normalize(df["payload"])
    df = pd.concat([df.drop("payload", axis=1), payload_df], axis=1)

    # Convert date to datetime object
    df["date"] = pd.to_datetime(df["date"], format="%d/%m/%Y %H:%M:%S")

    # Draw a scatter plot
    fig = Figure(figsize=(5, 4), dpi=100)
    ax = fig.add_subplot(111)

    colors = [
        "red",
        "green",
        "blue",
        "yellow",
    ]  # add more colors if you have more devices

    for i, device in enumerate(df["addrSrc"].unique()):
        device_df = df[df["addrSrc"] == device]
        ax.scatter(
            device_df["date"], device_df["simCommand"], color=colors[i % len(colors)]
        )

    ax.set_xlabel("Date")
    ax.set_ylabel("simCommand")
    ax.set_title("Scatter plot of simCommand over time")

    # Add the plot to the tkinter frame
    canvas = FigureCanvasTkAgg(fig, master=frame)  # A tk.DrawingArea.
    canvas.draw()
    canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")

    # Compute summary statistics
    message_size_stats = df.groupby("addrSrc")["messageSize"].agg(
        ["min", "max", "mean", "sum"]
    )
    unique_dst = df.groupby("addrSrc")["addrDst"].nunique()
    simcommand_freq = df.groupby(["addrSrc", "simCommand"]).size().unstack(fill_value=0)

    # Compute the summary for each device
    packet_loss = calculate_summary_by_device(fileName)

    # Make the summary dictionary into the same format as the other summary statistics
    packet_loss = pd.DataFrame.from_dict(packet_loss, orient="index")

    # Rename the columns
    packet_loss = packet_loss.rename(
        columns={
            "packet_loss": "packetLoss",
            "total_sent": "totalSent",
            "total_received": "totalReceived",
        }
    )

    # Create a text widget to display the summary
    summary = tk.Text(frame)
    summary.insert(tk.END, "\n\nSummary\n")
    summary.insert(tk.END, str(packet_loss))
    summary.insert(tk.END, "\n\nMessage Size Statistics per Device:\n")
    summary.insert(tk.END, str(message_size_stats))
    summary.insert(tk.END, "\n\nNumber of Unique Destination Addresses per Device:\n")
    summary.insert(tk.END, str(unique_dst))
    summary.insert(tk.END, "\n\nFrequency of Each simCommand per Device:\n")
    summary.insert(tk.END, str(simcommand_freq))
    summary.grid(row=0, column=2, sticky="nsew")

    # Plot a Bar chart of the packet loss by device
    fig2 = Figure(figsize=(5, 4), dpi=100)
    ax2 = fig2.add_subplot(111)
    ax2.bar(packet_loss.index, packet_loss["packetLoss"])
    ax2.set_xlabel("Device")
    ax2.set_ylabel("Packet Loss")
    ax2.set_title("Packet Loss by Device")

    # Add the plot to the tkinter frame
    canvas2 = FigureCanvasTkAgg(fig2, master=frame)  # A tk.DrawingArea.
    canvas2.draw()
    canvas2.get_tk_widget().grid(row=0, column=1, sticky="nsew")


def draw_messages_by_device(directory):
    # Get the messages.json file
    messages = os.path.join(directory, "messages.json")
    if not os.path.exists(messages):
        print("No messages.json file found in directory")
        return

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
    fig = Figure(figsize=(5, 4), dpi=100)
    ax = fig.add_subplot(111)

    # Find how many devices are in the data
    devices = df["addrSrc"].unique()

    # Generate a list of colors for each device
    colors = random.choices(list(mcolors.CSS4_COLORS.keys()), k=len(devices))

    # Plot each device in a different color
    for i, device in enumerate(devices):
        device_df = df[df["addrSrc"] == device]
        ax.scatter(
            device_df["date"], device_df["addrSrc"], color=colors[i % len(colors)]
        )

    ax.set_xlabel("Date")
    ax.set_ylabel("Device ID")
    ax.set_title("Scatter plot of Messages over time")

    # Add the plot to the tkinter frame
    canvas = FigureCanvasTkAgg(fig, master=frame)  # A tk.DrawingArea.
    canvas.draw()
    canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")

    # Add a button to download the plot
    download_button = tk.Button(
        frame,
        text="Download Plot",
        command=lambda: download_plot(canvas, directory, "plot.png"),
    )
    download_button.grid(row=1, column=0, sticky="nsew")


def draw_loss_messages_by_device(directory):
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
    colors = random.choices(list(mcolors.CSS4_COLORS.keys()), k=len(devices))

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
    for i, value in enumerate(packet_loss.keys()):
        ax.text(
            x=value,
            y=packet_loss[value] + 0.2,
            s=str(packet_loss[value]) + "%",
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


# Given the plot, download the plot as a PNG file
def download_plot(canvas, directory, fileName):
    # Create a directory to store the plots
    plot_dir = os.path.join(directory, "plots")
    if not os.path.exists(plot_dir):
        os.makedirs(plot_dir)

    # Save the plot
    canvas.print_figure(os.path.join(plot_dir, fileName))


# Given a directory, find the JSON file and draw the plot and summary
def find_file_json(directory):
    # Clear the frame
    for widget in frame.winfo_children():
        widget.destroy()

    # Find the JSON file
    monitors = os.path.join(directory, "stateMonitors.json")
    if os.path.exists(monitors):
        button = tk.Button(
            frame,
            text="stateMonitors",
            command=lambda: draw_plot_and_summary(monitors),
        )
        button.grid(row=0, column=0, sticky="nsew")

    button = tk.Button(
        frame,
        text="Messages By Device",
        command=lambda: draw_messages_by_device(directory),
    )
    button.grid(row=0, column=0, sticky="nsew")

    button = tk.Button(
        frame,
        text="Loss Messages By Device",
        command=lambda: draw_loss_messages_by_device(directory),
    )
    button.grid(row=1, column=0, sticky="nsew")


# Creating the main application window
root = tk.Tk()
root.title("Scatter Plot and Summary")

# Creating a frame widget
frame = tk.Frame(root)
frame.grid(row=0, column=0, sticky="nsew")

# Configure the grid to expand with window size
frame.columnconfigure(0, weight=1)
frame.columnconfigure(1, weight=1)
frame.rowconfigure(0, weight=1)

# Create button to draw plot and summary and pass the filename as an argument
draw_button = tk.Button(
    root,
    text="Draw Plot and Summary",
    command=lambda: draw_plot_and_summary("stateMonitors.json"),
)

draw_button.grid(row=1, column=0, sticky="ew")


def find_file(function):
    filename = filedialog.askdirectory(
        initialdir="./ZWeekendTesting06GOOD",
        title="Select a File",
    )
    draw_button["command"] = lambda: function(filename)


find_button = tk.Button(
    root, text="Find File", command=lambda: find_file(function=find_file_json)
)
find_button.grid(row=0, column=1, sticky="ew")


def find_file_and_execute_function(function):
    filename = filedialog.askdirectory(
        initialdir="./ZWeekendTesting06GOOD",
        title="Select a File",
    )
    function(filename)


def find_all_files_and_plot_loss_messages_by_device(directory):
    print(directory)
    # Create a new directory to store the plots
    plot_dir = os.path.join(directory, "plots")

    if not os.path.exists(plot_dir):
        os.makedirs(plot_dir)

    # Get all the directories in the directory
    directories = os.listdir(directory)

    # Execute the function for each directory
    for dir in directories:
        joined_dir = os.path.join(directory, dir)
        canvas = draw_loss_messages_by_device(joined_dir)
        if canvas:
            canvas.print_figure(os.path.join(plot_dir, dir + ".png"))


button_find_file_and_plot = tk.Button(
    root,
    text="Select file to plot multiple loss messages for all devices",
    command=lambda: find_file_and_execute_function(
        function=find_all_files_and_plot_loss_messages_by_device
    ),
)

button_find_file_and_plot.grid(row=1, column=1, sticky="ew")


root.mainloop()
