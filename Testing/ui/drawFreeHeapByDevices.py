import os
import matplotlib.pyplot as plt
import tkinter as tk
from tkinter import Frame
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from downloadPlot import download_plot
import matplotlib.collections

# TODO: WTF is this? Python...
import sys

# Get the path to the root directory
sys.path.append(os.path.join(os.path.dirname(__file__), "..", ".."))
from Testing.monitoringAnalysis.getFreeHeapByDevice import (
    get_free_heap_values,
)

ax = None
canvas = None


def draw_free_heap_by_devices(frame: Frame, directory):
    # Clear the frame
    for widget in frame.winfo_children():
        widget.destroy()

    global ax
    global canvas

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

            ax.scatter(
                range(len(data)),
                data,
                alpha=0.7,
                label=file,
                picker=True,  # Enable pick events on the scatter plot
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

    # Connect the on_pick function to the 'pick_event'
    canvas.mpl_connect("pick_event", on_pick)

    # Add a button to download the plot
    download_button = tk.Button(
        frame,
        text="Download Plot",
        command=lambda: download_plot(canvas, directory, "RTTByDevice.png"),
    )
    download_button.pack(side=tk.BOTTOM)


annotations = []


def on_pick(event):
    # Check if the event is a pick event and if it's from a scatter plot
    if isinstance(event.artist, matplotlib.collections.PathCollection):
        ind = event.ind[0]  # Get the index of the point clicked
        point = event.artist.get_offsets()[ind]  # Get the x, y values of the point
        # print(f"Value: {point[1]}")  # Print the y-value to the console

        global ax
        global canvas
        global annotations

        # Annotate the point on the plot
        annotation = ax.annotate(
            f"({point[0]:.2f}, {point[1]:.2f})",
            (point[0], point[1]),
            textcoords="offset points",
            xytext=(0, 10),
            ha="center",
        )

        annotations.append(annotation)

        # If some annotation in the list overlaps with the new annotation then remove it
        for a in annotations:
            if a != annotation:
                if annotation.get_window_extent().overlaps(a.get_window_extent()):
                    a.remove()
                    annotations.remove(a)

        # Redraw the canvas to show the annotation
        canvas.draw()
