import os
import json
import pandas as pd
import tkinter as tk
from tkinter import Frame
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
from downloadPlot import download_plot
from deviceColors import get_color_by_devices


def draw_messages_by_device(frame: Frame, directory):
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
    colors = get_color_by_devices(devices)

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
