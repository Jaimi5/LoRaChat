import json
import pandas as pd
from tkinter import Frame
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from calculateSummaryByDevice import calculate_summary_by_device
import tkinter as tk


def draw_plot_and_summary(frame: Frame, fileName):
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
