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


def draw_rtt_by_experiments(frame: Frame, directory):
    # Clear the frame
    for widget in frame.winfo_children():
        widget.destroy()

    # Create the figure and subplot
    fig = Figure(figsize=(10, 5), dpi=100)
    ax = fig.add_subplot(111)

    # Experiments
    experiments = os.listdir(directory)

    # Sort the directories by name
    experiments.sort()

    # Results by experiment
    results = []

    names = []

    for experiment in experiments:
        # Get the path to the Monitoring folder
        path = os.path.join(directory, experiment, "Monitoring")

        # Get the last directory name
        # name = os.path.basename(os.path.normpath(directory))

        if not os.path.exists(path):
            print("No Monitoring folder found in directory " + experiment)
            continue

        monitoringFiles = os.listdir(path)

        # Add the name of the experiment
        names.append(experiment)

        if len(monitoringFiles) == 0:
            print("No Monitoring files found in directory")
            return None

        experimentResults = []

        for file in monitoringFiles:
            if "monitor" in file:
                filePath = os.path.join(path, file)
                data = extract_rtt_values_final_pattern(filePath)

                # Concat the results
                experimentResults = experimentResults + data[0]

        results.append(experimentResults)

    # Print first results
    print(names)

    plt.rcParams["lines.linewidth"] = 1.5

    # Plot each experiment as a Box Plot
    ax.boxplot(results)

    # Set the title
    ax.set_title("RTT by Experiment", fontsize=18)

    # Set the x-axis label
    ax.set_xlabel("Experiment [#]", fontsize=18)

    # Set the y-axis label
    ax.set_ylabel("RTT [ms]", fontsize=18)

    ax.axhline(y=472, color="green", linestyle="--", label="472 ms (Doubled Delay Min)")
    ax.axhline(
        y=2088, color="green", linestyle="-", label="2088 ms (Doubled Delay Max)"
    )

    ax.axhline(
        y=472 * 9, color="red", linestyle="--", label="4248 ms (Doubled Delay Min) * 9"
    )
    ax.axhline(
        y=2088 * 9, color="red", linestyle="-", label="18792 ms (Doubled Delay Max) * 9"
    )

    # Set the x-axis ticks
    # ax.set_xticklabels(experiments)

    # Set the y-axis ticks
    # ax.set_yticks(range(0, 3000, 100))

    # Set the grid
    ax.grid(True, which="both", linestyle="--", linewidth=0.5)

    # Set the graph in logarithmic scale
    ax.set_yscale("log")

    plt.rcParams.update({"font.size": 13})

    # Set the legend
    ax.legend()

    # Plot the figure
    canvas = FigureCanvasTkAgg(fig, master=frame)
    canvas.draw()
    canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

    # Add a button to download the plot
    download_button = tk.Button(
        frame,
        text="Download Plot",
        command=lambda: download_plot(canvas, directory, "RTTByExperiments.png"),
    )
    download_button.pack(side=tk.BOTTOM)
