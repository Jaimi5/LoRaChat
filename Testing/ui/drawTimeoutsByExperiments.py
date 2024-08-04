import os
import json
import pandas as pd
import math
import matplotlib.pyplot as plt
import tkinter as tk
from tkinter import Frame
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from downloadPlot import download_plot
from deviceColors import get_color_by_devices

# TODO: WTF is this? Python...
import sys

# Get the path to the root directory
sys.path.append(os.path.join(os.path.dirname(__file__), "..", ".."))
from Testing.monitoringAnalysis.timeoutsCounter import get_monitor_status


def draw_timeouts_by_experiments(frame: Frame, directory):
    # Clear the frame
    for widget in frame.winfo_children():
        widget.destroy()

    # Find all the directories in the directory
    directories = os.listdir(directory)

    # Sort the directories by name
    directories.sort()

    experiment_directories = []

    i = 1

    # Execute the function for each directory
    for dir in directories:
        # Get the path to the Monitoring folder
        path = os.path.join(directory, dir, "Monitoring")

        # Get the last directory name
        name = os.path.basename(os.path.normpath(dir))

        if not os.path.exists(path):
            print("No Monitoring folder found in directory")
            continue

        configPath = os.path.join(directory, dir, "simConfiguration.json")

        if not os.path.exists(configPath):
            print("No simConfiguration.json file found in directory")
            continue

        # Get the number of messages sent from the configuration file
        with open(configPath, "r") as f:
            config = json.load(f)

        # Get the number of messages sent
        total_messages = int(config["Simulator"]["PACKET_COUNT"])

        if int(config["Simulator"]["ONE_SENDER"]) == 0:
            total_messages = total_messages * 9

        # Get the LM_MAX_PACKET_SIZE minus the header size (overhead)
        max_packet_size = int(config["LoRaMesher"]["LM_MAX_PACKET_SIZE"]) - 11

        # Get the Payload size
        payload_size = int(config["Simulator"]["PACKET_SIZE"])

        # Get the number of data packets, by dividing the total messages by the payload size round up
        total_data_packets = total_messages * math.ceil(payload_size / max_packet_size)

        total_number_packets = total_data_packets * 2 + total_messages * 2

        data = get_monitor_status(path)

        # Add to the list of experiment directories
        experiment_directories.append(
            {
                "Name": name,
                "Id": i,
                "SYNC_P to Send": total_messages,
                "SYNC_P Resend": data["totalSyncResend"],
                "SYNC_P lost [%]": data["totalSyncResend"] / total_messages * 100,
                "XL_DATA_P to Send": total_data_packets,
                "XL_DATA_P Resend": data["totalMessagesResend"],
                "XL_DATA_P lost [%]": data["totalMessagesResend"]
                / total_data_packets
                * 100,
                "Data [KB]": int(total_messages * payload_size / 1000),
            }
        )

        i += 1

    # Convert to pandas DataFrame
    df = pd.DataFrame(experiment_directories)

    # Create the figure and subplot
    fig = Figure(figsize=(10, 5), dpi=100)
    ax = fig.add_subplot(111)

    plt.rc("font", size=14)

    # # Plot the bar chart
    # x_labels = df["address"]
    df[
        [
            "SYNC_P to Send",
            "SYNC_P Resend",
            "XL_DATA_P to Send",
            "XL_DATA_P Resend",
            # "Data [KB]",
        ]
    ].plot(kind="bar", ax=ax, width=0.7)

    # Add the x-axis labels
    ax.set_xticklabels(df["Id"], rotation=0)

    # Add labels and title
    ax.set_xlabel("Experiment [#]", fontsize=18)
    ax.set_ylabel("Count [#]", fontsize=18)
    ax.set_title("Experiment Packet Statistics Overview", fontsize=18)

    # Calculate the maximum height of the bars
    max_height = df[
        [
            "SYNC_P to Send",
            "SYNC_P Resend",
            "XL_DATA_P to Send",
            "XL_DATA_P Resend",
            # "Data [KB]",
        ]
    ].values.max()

    # Add the number labels on top of the bars
    for p in ax.patches:
        height = p.get_height()
        spacing = (
            max_height * 0.02
        )  # Adjust the spacing as a percentage of the maximum height
        if (
            height < max_height * 0.05
        ):  # If the height is less than 5% of the maximum height
            va = "bottom"  # Place the annotation below the bar
        else:
            va = "center"  # Place the annotation at the center of the bar

        # Add the annotation with a maximum of 2 decimal places, add decimal places if needed
        ax.annotate(
            str(height) if height.is_integer() else f"{height:.2f}",
            (p.get_x() + p.get_width() / 2.0, height + spacing),
            ha="center",
            va=va,
            size=10,
        )

    # Plot the figure
    canvas = FigureCanvasTkAgg(fig, master=frame)
    canvas.draw()
    canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

    # Add a button to download the plot
    download_button = tk.Button(
        frame,
        text="Download Plot",
        command=lambda: download_plot(canvas, directory, "TimeoutsByExperiments.png"),
    )
    download_button.pack(side=tk.BOTTOM)
