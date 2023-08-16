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

# TODO: WTF is this? Python...
import sys

# Get the path to the root directory
sys.path.append(os.path.join(os.path.dirname(__file__), "..", ".."))
from Testing.monitoringAnalysis.timeoutsCounter import get_monitor_status


def draw_timeouts_by_device(frame: Frame, directory):
    path = os.path.join(directory, "Monitoring")

    if not os.path.exists(path):
        print("No Monitoring folder found in directory")
        return None

    # Clear the frame
    for widget in frame.winfo_children():
        widget.destroy()

    data = get_monitor_status(path)

    # Convert to pandas DataFrame
    df = pd.DataFrame(data)

    # Expand the 'payload' dictionary into separate columns
    payload_df = pd.json_normalize(df["monitorResults"])
    df = pd.concat([df.drop("monitorResults", axis=1), payload_df], axis=1)

    # Create the figure and subplot
    fig = Figure(figsize=(10, 5), dpi=100)
    ax = fig.add_subplot(111)

    # Convert hexadecimal addresses to integers
    df["address"] = df["address"].apply(lambda x: int(x, 16))

    # Sort the DataFrame by address in ascending order
    df = df.sort_values("address")

    # Plot the bar chart
    x_labels = df["address"]
    df[
        [
            "timeoutsReceived",
            "timeoutsSent",
            "maxTimeoutsReceived",
            "maxTimeoutsSent",
            "numOfResendsStartSequenceSend",
        ]
    ].plot(kind="bar", ax=ax, width=0.7)

    # Replace default x-axis labels with address values
    ax.set_xticklabels(x_labels, rotation=0)

    # Add labels and title
    ax.set_xlabel("Address")
    ax.set_ylabel("Count")
    ax.set_title("Timeouts and Max Timeouts")

    ax.rcParams.update({"font.size": 14})

    # Calculate the maximum height of the bars
    max_height = df[
        [
            "timeoutsReceived",
            "timeoutsSent",
            "maxTimeoutsReceived",
            "maxTimeoutsSent",
            "numOfResendsStartSequenceSend",
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
            str(height),
            (p.get_x() + p.get_width() / 2.0, height + spacing),
            ha="center",
            va=va,
        )

    # Plot the figure
    canvas = FigureCanvasTkAgg(fig, master=frame)
    canvas.draw()
    canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

    # Add a button to download the plot
    download_button = tk.Button(
        frame,
        text="Download Plot",
        command=lambda: download_plot(canvas, directory, "TimeoutsByDevice.png"),
    )
    download_button.pack(side=tk.BOTTOM)
