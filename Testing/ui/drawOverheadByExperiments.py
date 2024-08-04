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


def draw_overhead_by_experiments(frame: Frame, directory):
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

        percentage_overhead = 0
        percentage_packets_lost = 0
        theoretical_percentage_overhead = 0

        print("Experiment: ", name)

        # if the messages are sent reliable
        if int(config["Simulator"]["SEND_RELIABLE"]) == 1:
            # Get the number of data packets, by dividing the total messages by the payload size round up
            total_data_packets = total_messages * math.ceil(
                payload_size / max_packet_size
            )

            total_packets = total_data_packets * 2 + total_messages * 2
            print("Total Packets: ", total_packets)

            data = get_monitor_status(path)

            control_overhead = total_packets * 11

            ctrl_overhead_sync = data["totalSyncResend"] * 11

            ctrl_overhead_data = data["totalMessagesResend"] * 11

            total_control_overhead = (
                control_overhead + ctrl_overhead_sync + ctrl_overhead_data
            )

            total_data = total_control_overhead + (payload_size * total_messages)

            percentage_overhead = round((total_control_overhead / total_data) * 100, 2)

            total_packets_lost = data["totalSyncResend"] + data["totalMessagesResend"]

            percentage_packets_lost = round(
                (total_packets_lost / total_packets) * 100, 2
            )

            theoretical_percentage_overhead = round(
                (
                    control_overhead
                    / (control_overhead + (payload_size * total_messages))
                )
                * 100,
                2,
            )

            print("Total Packets Lost: ", total_packets_lost)
            print("Percentage Packets Lost: ", percentage_packets_lost)
            print("Control Overhead Sync: ", ctrl_overhead_sync)
            print("Control Overhead Data: ", ctrl_overhead_data)
            print("Total Control Overhead: ", total_control_overhead)
            print("Total Data: ", total_data)
            print("Percentage Overhead: ", percentage_overhead)
            print("Theoretical Percentage Overhead: ", theoretical_percentage_overhead)
            print()

        else:
            overhead_in_bytes = total_messages * 8
            total_data = overhead_in_bytes + (payload_size * total_messages)

            percentage_overhead = round((overhead_in_bytes / total_data) * 100, 2)
            theoretical_percentage_overhead = round(
                (overhead_in_bytes / (payload_size * total_messages)) * 100, 2
            )

            # Open the file messages.json and get the number of messages received
            messages_path = os.path.join(directory, dir, "messages.json")

            if not os.path.exists(messages_path):
                print("No messages.json file found in directory")
                continue

            with open(messages_path, "r") as f:
                messages = json.load(f)

            total_packets_lost = total_messages - len(messages)

            percentage_packets_lost = round(
                (total_packets_lost / total_messages) * 100, 2
            )

        # Add to the list of experiment directories
        experiment_directories.append(
            {
                # "Name": name,
                "Id": i,
                # "Control Overhead Sync": ctrl_overhead_sync,
                # "Control Overhead Data": ctrl_overhead_data,
                # "Total Control Overhead Data No overhead": total_control_overhead,
                # "Total Control Overhead Data overhead": total_control_overhead,
                # "Total Data": total_data,
                "Experimental Overhead": percentage_overhead,
                "Packets Lost": percentage_packets_lost,
                "Theoretical Overhead": theoretical_percentage_overhead,
            }
        )

        i += 1

    # Convert to pandas DataFrame
    df = pd.DataFrame(experiment_directories)

    # Create the figure and subplot
    fig = Figure(figsize=(10, 5), dpi=100)
    ax = fig.add_subplot(111)

    # Set the plot to font size 14
    plt.rc("font", size=14)

    # # Plot the bar chart
    # x_labels = df["address"]
    df[
        [
            "Theoretical Overhead",
            "Experimental Overhead",
            "Packets Lost",
        ]
    ].plot(kind="bar", ax=ax, width=0.7)

    # Add the x-axis labels
    ax.set_xticklabels(df["Id"], rotation=0)

    # Add labels and title
    ax.set_xlabel("Experiment [#]", fontsize=18)
    ax.set_ylabel("Percentage [%]", fontsize=18)
    ax.set_title("Experiment Control Overhead Analysis", fontsize=18)

    # Calculate the maximum height of the bars
    max_height = df[
        [
            "Experimental Overhead",
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
        ax.annotate(
            str(height) + "%",
            (p.get_x() + p.get_width() / 2.0, height + spacing),
            ha="center",
            va=va,
            size=10,
        )

    # Add the legend with size 14, upper left corner and a little lower
    ax.legend(
        fontsize=14,
        loc=(0.60, 0.65),
    )

    # Plot the figure
    canvas = FigureCanvasTkAgg(fig, master=frame)
    canvas.draw()
    canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

    # Add a button to download the plot
    download_button = tk.Button(
        frame,
        text="Download Plot",
        command=lambda: download_plot(canvas, directory, "OverheadByExperiments.png"),
    )
    download_button.pack(side=tk.BOTTOM)
