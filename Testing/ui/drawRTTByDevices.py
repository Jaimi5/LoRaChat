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
from Testing.monitoringAnalysis.calculateRTTByDevice import (
    extract_rtt_values_final_pattern,
)


def draw_rtt_by_devices(frame: Frame, directory):
    # Clear the frame
    for widget in frame.winfo_children():
        widget.destroy()

    # Create the figure and subplot
    fig = Figure(figsize=(10, 5), dpi=100)
    ax = fig.add_subplot(111)

    # Get the path to the Monitoring folder
    path = os.path.join(directory, "Monitoring")

    # Get the last directory name
    name = os.path.basename(os.path.normpath(directory))

    if not os.path.exists(path):
        print("No Monitoring folder found in directory")
        return None

    monitoringFiles = os.listdir(path)

    if len(monitoringFiles) == 0:
        print("No Monitoring files found in directory")
        return None

    for file in monitoringFiles:
        if "monitor" in file:
            filePath = os.path.join(path, file)
            data = extract_rtt_values_final_pattern(filePath)

            ax.plot(
                data[0],
                linestyle="--",
                alpha=0.7,
            )

            # ax.plot(
            #     data[1],
            #     label="SRTT" + name,
            #     linestyle="--",
            #     alpha=0.7,
            # )

    # Adding the delay lines
    # ax.axhline(y=472, color="green", linestyle="--", label="472 ms (Doubled Delay Min)")
    # ax.axhline(
    #     y=2088, color="green", linestyle=":", label="2088 ms (Doubled Delay Max)"
    # )
    # Max time on air = 236 ms -> MToA
    # MToA*2 = 472 \\ Waiting Time min random, both sides
    # ([MToA*3+100(Per routing table size) -> Waiting Time max random]+MToA)*2 = 1616 * 2 = 2088 (Both ways)

    # Add labels and title
    ax.set_xlabel("Measurement Index")
    ax.set_ylabel("Time (ms)")
    ax.set_title("RTT and SRTT Values with Specified Delays")

    # Add a legend
    # ax.legend()

    # Plot the figure
    canvas = FigureCanvasTkAgg(fig, master=frame)
    canvas.draw()
    canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

    # Add a button to download the plot
    download_button = tk.Button(
        frame,
        text="Download Plot",
        command=lambda: download_plot(canvas, directory, "RTTByDevice.png"),
    )
    download_button.pack(side=tk.BOTTOM)
