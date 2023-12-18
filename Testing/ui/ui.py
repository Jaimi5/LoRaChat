import tkinter as tk
import os
import tkinter.filedialog as filedialog
from drawLossMessagesByDevice import draw_loss_messages_by_device
from drawMessageByDevice import draw_messages_by_device
from drawPlotAndSummary import draw_plot_and_summary
from drawEEDByDevice import draw_eed_by_device
from drawTimeoutsByDevice import draw_timeouts_by_device
from drawTimeoutsByExperiments import draw_timeouts_by_experiments
from drawRTTByDevices import draw_rtt_by_devices
from drawRTTByExperiments import draw_rtt_by_experiments
from drawOverheadByExperiments import draw_overhead_by_experiments
from Testing.ui.drawFreeHeapByDevices import draw_free_heap_by_devices


initialDirectory = "./"


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
            command=lambda: draw_plot_and_summary(frame, monitors),
        )
        button.grid(row=0, column=0, sticky="nsew")

    else:
        # Alert the user that no JSON file was found in top of the frame
        label = tk.Label(
            frame,
            text="No JSON file found in directory",
            fg="red",
        )
        label.grid(row=0, column=2, sticky="nsew")

    button = tk.Button(
        frame,
        text="Messages By Device",
        command=lambda: draw_messages_by_device(frame, directory),
    )
    button.grid(row=0, column=0, sticky="nsew")

    button = tk.Button(
        frame,
        text="Loss Messages By Device",
        command=lambda: draw_loss_messages_by_device(frame, directory),
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
    command=lambda: draw_plot_and_summary(frame, "stateMonitors.json"),
)

draw_button.grid(row=1, column=0, sticky="ew")


# def find_file(function):
#     filename = filedialog.askdirectory(
#         initialdir=initialDirectory,
#         title="Select a File",
#     )
#     draw_button["command"] = lambda: function(filename)


# find_button = tk.Button(
#     root, text="Find File", command=lambda: find_file(function=find_file_json)
# )
# find_button.grid(row=0, column=1, sticky="ew")


def find_file_and_execute_function(function):
    filename = filedialog.askdirectory(
        initialdir=initialDirectory,
        title="Select a File",
    )
    # Clear the frame
    for widget in frame.winfo_children():
        widget.destroy()

    function(frame, filename)


# def find_all_files_and_plot_loss_messages_by_device(frame, directory):
#     print(directory)
#     # Create a new directory to store the plots
#     plot_dir = os.path.join(directory, "plots")

#     if not os.path.exists(plot_dir):
#         os.makedirs(plot_dir)

#     # Get all the directories in the directory
#     directories = os.listdir(directory)

#     # Execute the function for each directory
#     for dir in directories:
#         joined_dir = os.path.join(directory, dir)
#         canvas = draw_loss_messages_by_device(frame, joined_dir)
#         if canvas:
#             canvas.print_figure(os.path.join(plot_dir, dir + ".png"))


# button_find_file_and_plot = tk.Button(
#     root,
#     text="Select file to plot multiple loss messages for all devices",
#     command=lambda: find_file_and_execute_function(
#         function=find_all_files_and_plot_loss_messages_by_device
#     ),
# )

# button_find_file_and_plot.grid(row=1, column=1, sticky="ew")

# Create a button to draw the eed by device
button_eed_by_device = tk.Button(
    root,
    text="EED By Device",
    command=lambda: find_file_and_execute_function(function=draw_eed_by_device),
)

button_eed_by_device.grid(row=2, column=0, sticky="ew")

# Create a button to draw the number of timeouts by device
button_timeouts_by_device = tk.Button(
    root,
    text="Timeouts By Device",
    command=lambda: find_file_and_execute_function(function=draw_timeouts_by_device),
)

button_timeouts_by_device.grid(row=2, column=1, sticky="ew")

# Create a button to draw the number of timeouts by experiments
button_timeouts_by_experiments = tk.Button(
    root,
    text="Timeouts By Experiments",
    command=lambda: find_file_and_execute_function(
        function=draw_timeouts_by_experiments
    ),
)

button_timeouts_by_experiments.grid(row=3, column=0, sticky="ew")

# Create a button to draw the RTT by devices
button_rtt_by_devices = tk.Button(
    root,
    text="RTT By Devices",
    command=lambda: find_file_and_execute_function(function=draw_rtt_by_devices),
)

button_rtt_by_devices.grid(row=3, column=1, sticky="ew")

# Create a button to draw the RTT by experiments
button_rtt_by_experiments = tk.Button(
    root,
    text="RTT By Experiments",
    command=lambda: find_file_and_execute_function(function=draw_rtt_by_experiments),
)

button_rtt_by_experiments.grid(row=4, column=0, sticky="ew")

# Create a button to draw the Control Overhead by experiments
button_overhead_by_experiments = tk.Button(
    root,
    text="Control Overhead By Experiments",
    command=lambda: find_file_and_execute_function(
        function=draw_overhead_by_experiments
    ),
)

button_overhead_by_experiments.grid(row=4, column=1, sticky="ew")

# Create a button to draw the Free Heap by devices
button_free_heap_by_device = tk.Button(
    root,
    text="Free Heap By Device",
    command=lambda: find_file_and_execute_function(function=draw_free_heap_by_devices),
)

button_free_heap_by_device.grid(row=5, column=0, sticky="ew")

# Create a button to draw the loss messages by device
button_loss_messages_by_device = tk.Button(
    root,
    text="Loss Messages By Device",
    command=lambda: find_file_and_execute_function(
        function=draw_loss_messages_by_device
    ),
)

button_loss_messages_by_device.grid(row=5, column=1, sticky="ew")

# Create a button to draw the messages by device
button_messages_by_device = tk.Button(
    root,
    text="Messages By Device",
    command=lambda: find_file_and_execute_function(function=draw_messages_by_device),
)

button_messages_by_device.grid(row=6, column=0, sticky="ew")

root.mainloop()
