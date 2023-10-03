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
from Testing.monitoringAnalysis.getFreeHeapByDevice import (
    get_free_heap_values,
)


def draw_free_heap_by_devices(frame: Frame, directory):
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
            data = get_free_heap_values(filePath)

            # ax.plot(
            #     data,
            #     linestyle="--",
            #     alpha=0.7,
            #     label=file,
            # )

            ax.scatter(
                range(len(data)),
                data,
                alpha=0.7,
                label=file,
            )

    # Add labels and title
    ax.set_xlabel("Time (ms)")
    ax.set_ylabel("Free Heap (bytes)")
    ax.set_title("Free Heap by Device")

    # Add a legend
    ax.legend()

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
