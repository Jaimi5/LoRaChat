import os
import json
import pandas as pd
import numpy as np
import math
import matplotlib.pyplot as plt
import tkinter as tk
from tkinter import Frame, messagebox
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from downloadPlot import download_plot
import scipy.stats as stats
import hashlib

# TODO: WTF is this? Python...
import sys

# Get the path to the root directory
sys.path.append(os.path.join(os.path.dirname(__file__), "..", ".."))
from Testing.monitoringAnalysis.timeoutsCounter import get_monitor_status
from Testing.ui.ciStatistics import calculate_ci_parametric, test_normality
from Testing.ui.exportResults import export_all_formats, print_export_summary


def get_config_hash(config_dict):
    """
    Generate a hash for a configuration dictionary.
    Excludes fields that might vary between runs of the same experiment.
    """
    # Create a copy and remove fields that might vary between runs
    config_copy = json.loads(json.dumps(config_dict))

    # Fields to exclude from hash (run-specific metadata)
    exclude_keys = ["timestamp", "seed", "output_path", "run_id", "date"]

    def remove_excluded_keys(d):
        if isinstance(d, dict):
            return {k: remove_excluded_keys(v) for k, v in d.items() if k not in exclude_keys}
        elif isinstance(d, list):
            return [remove_excluded_keys(item) for item in d]
        else:
            return d

    filtered_config = remove_excluded_keys(config_copy)

    # Create a stable JSON string and hash it
    config_str = json.dumps(filtered_config, sort_keys=True)
    return hashlib.md5(config_str.encode()).hexdigest()


def get_config_label(config_dict):
    """
    Generate a human-readable label for a configuration.
    If 'experiment_name' field exists in config, use it.
    Otherwise, auto-generate label from configuration parameters.
    """
    # Check if custom experiment name is provided
    if "experiment_name" in config_dict:
        return config_dict["experiment_name"]

    # Fall back to auto-generated label
    try:
        simulator = config_dict.get("Simulator", {})
        loramesh = config_dict.get("LoRaMesher", {})

        packet_count = simulator.get("PACKET_COUNT", "?")
        packet_size = simulator.get("PACKET_SIZE", "?")
        send_reliable = simulator.get("SEND_RELIABLE", "0")
        one_sender = simulator.get("ONE_SENDER", "0")

        reliability = "Reliable" if int(send_reliable) == 1 else "Unreliable"
        sender_mode = "SingleSender" if int(one_sender) == 1 else "MultiSender"

        return f"{reliability}_{sender_mode}_{packet_count}pkt_{packet_size}B"
    except Exception as e:
        return "UnknownConfig"


def group_experiments_by_config(directory):
    """
    Scan all subdirectories and group experiments by their configuration.
    Returns a dictionary: { config_hash: { 'paths': [...], 'config': {...}, 'label': '...' } }
    """
    if not os.path.exists(directory):
        print(f"Directory does not exist: {directory}")
        return {}

    subdirs = [d for d in os.listdir(directory) if os.path.isdir(os.path.join(directory, d))]

    config_groups = {}

    for subdir in subdirs:
        config_path = os.path.join(directory, subdir, "simConfiguration.json")
        monitoring_path = os.path.join(directory, subdir, "Monitoring")

        # Check if both configuration and monitoring data exist
        if not os.path.exists(config_path):
            print(f"No simConfiguration.json found in {subdir}")
            continue

        if not os.path.exists(monitoring_path):
            print(f"No Monitoring folder found in {subdir}")
            continue

        # Load configuration
        with open(config_path, "r") as f:
            config = json.load(f)

        # Generate hash and label
        config_hash = get_config_hash(config)
        config_label = get_config_label(config)

        # Group by hash
        if config_hash not in config_groups:
            config_groups[config_hash] = {
                'paths': [],
                'config': config,
                'label': config_label
            }

        config_groups[config_hash]['paths'].append(os.path.join(directory, subdir))

    return config_groups


def calculate_timeout_metrics(experiment_path, config):
    """
    Calculate timeout/resend metrics for a single experiment run.
    Returns: (sync_to_send, sync_resend, xl_data_to_send, xl_data_resend)
    """
    monitoring_path = os.path.join(experiment_path, "Monitoring")

    if not os.path.exists(monitoring_path):
        return None, None, None, None

    # Get configuration parameters
    total_messages = int(config["Simulator"]["PACKET_COUNT"])

    if int(config["Simulator"]["ONE_SENDER"]) == 0:
        total_messages = total_messages * 9

    max_packet_size = int(config["LoRaMesher"]["LM_MAX_PACKET_SIZE"]) - 11
    payload_size = int(config["Simulator"]["PACKET_SIZE"])

    # Get the number of data packets
    total_data_packets = total_messages * math.ceil(payload_size / max_packet_size)

    # Get monitoring data
    data = get_monitor_status(monitoring_path)

    sync_to_send = total_messages
    sync_resend = data["totalSyncResend"]
    xl_data_to_send = total_data_packets
    xl_data_resend = data["totalMessagesResend"]

    return sync_to_send, sync_resend, xl_data_to_send, xl_data_resend


def calculate_timeout_metrics_with_ci(experiment_paths, config, confidence_level=0.95,
                                      test_normality_flag=True):
    """
    Calculate timeout metrics with confidence intervals for a group of experiment runs.
    Uses enhanced statistics with normality testing.

    Returns: {
        'SYNC_P to Send': { 'mean': X, 'ci_lower': Y, 'ci_upper': Z, ... },
        'SYNC_P Resend': { 'mean': X, 'ci_lower': Y, 'ci_upper': Z, ... },
        'XL_DATA_P to Send': { 'mean': X, 'ci_lower': Y, 'ci_upper': Z, ... },
        'XL_DATA_P Resend': { 'mean': X, 'ci_lower': Y, 'ci_upper': Z, ... }
    }
    """
    sync_to_send_values = []
    sync_resend_values = []
    xl_data_to_send_values = []
    xl_data_resend_values = []

    for path in experiment_paths:
        sync_to_send, sync_resend, xl_data_to_send, xl_data_resend = calculate_timeout_metrics(path, config)

        if sync_to_send is not None:
            sync_to_send_values.append(sync_to_send)
        if sync_resend is not None:
            sync_resend_values.append(sync_resend)
        if xl_data_to_send is not None:
            xl_data_to_send_values.append(xl_data_to_send)
        if xl_data_resend is not None:
            xl_data_resend_values.append(xl_data_resend)

    results = {}

    # Calculate statistics for each metric using enhanced CI module
    for metric_name, values in [
        ("SYNC_P to Send", sync_to_send_values),
        ("SYNC_P Resend", sync_resend_values),
        ("XL_DATA_P to Send", xl_data_to_send_values),
        ("XL_DATA_P Resend", xl_data_resend_values)
    ]:
        if len(values) == 0:
            results[metric_name] = None
            continue

        # Use the enhanced CI calculation
        ci_result = calculate_ci_parametric(values, confidence_level)

        if ci_result:
            # Add normality test if requested and possible
            if test_normality_flag and len(values) >= 3:
                normality_result = test_normality(values)
                ci_result['normality'] = normality_result

            results[metric_name] = ci_result
        else:
            results[metric_name] = None

    return results


def draw_timeouts_by_experiments_with_ci(frame: Frame, directory, confidence_level=0.95):
    """
    Draw a bar chart showing timeout/resend metrics for each configuration,
    with confidence intervals across multiple runs.
    """
    # Clear the frame
    for widget in frame.winfo_children():
        widget.destroy()

    # Group experiments by configuration
    config_groups = group_experiments_by_config(directory)

    if len(config_groups) == 0:
        label = tk.Label(frame, text="No experiments found in directory", fg="red")
        label.pack()
        return

    # Calculate metrics for each configuration group
    all_results = []
    has_small_samples = False

    for config_hash, group_info in config_groups.items():
        n = len(group_info['paths'])
        if n < 5:
            has_small_samples = True

        metrics = calculate_timeout_metrics_with_ci(
            group_info['paths'],
            group_info['config'],
            confidence_level,
            test_normality_flag=True
        )

        all_results.append({
            'label': group_info['label'],
            'n': n,
            'metrics': metrics
        })

    # Show warning if any configuration has small sample size
    if has_small_samples:
        warning_msg = ("⚠️ Small sample sizes detected (n < 5).\n\n"
                      "Wide confidence intervals are expected and appropriate.\n"
                      "See Testing/ui/CI_GUIDE.md for reporting guidance.")
        messagebox.showwarning("Small Sample Size", warning_msg)

    # Create the figure and subplot
    fig = Figure(figsize=(14, 7), dpi=100)
    ax = fig.add_subplot(111)

    # Set font size
    plt.rc("font", size=20)

    # Prepare data for plotting
    metric_names = ["SYNC_P to Send", "SYNC_P Resend", "XL_DATA_P to Send", "XL_DATA_P Resend"]
    x = np.arange(len(all_results))  # X-axis positions for each configuration
    width = 0.2  # Width of bars (4 bars per group)

    # Professional colorblind-friendly palette suitable for scientific publications
    colors = ['#0173B2', '#DE8F05', '#CC78BC', '#029E73']  # Blue, Orange, Purple, Green

    # Plot bars for each metric
    for i, metric_name in enumerate(metric_names):
        means = []
        errors = []

        for result in all_results:
            metric_data = result['metrics'].get(metric_name)

            if metric_data is None:
                means.append(0)
                errors.append(0)
            else:
                means.append(metric_data['mean'])
                # Error bar is the difference between mean and CI bound
                error = metric_data['ci_upper'] - metric_data['mean']
                errors.append(error)

        # Calculate offset to center the four bars around each x position
        offset = (i - 1.5) * width
        bars = ax.bar(x + offset, means, width, yerr=errors,
                      label=metric_name,
                      color=colors[i], capsize=5, alpha=0.8)

        # Add value labels on top of bars
        for j, (bar, mean, error) in enumerate(zip(bars, means, errors)):
            height = bar.get_height()
            if height > 0:
                # Format as integer if it's a whole number, otherwise with decimals
                value_str = str(int(mean)) if mean == int(mean) else f'{mean:.1f}'
                ax.text(bar.get_x() + bar.get_width()/2., height + error + max(means) * 0.01,
                       value_str,
                       ha='center', va='bottom', fontsize=15, rotation=0)

    # Add labels and title
    ax.set_xlabel("Experiment Configuration", fontsize=24, fontstyle='normal')
    ax.set_ylabel("Count [#]", fontsize=24)
    ax.set_title(f"Timeout Statistics by Experiment ({int(confidence_level*100)}% CI)", fontsize=24)
    ax.set_xticks(x)

    # Set y-axis tick label font size
    ax.tick_params(axis='y', labelsize=19)

    # Create x-axis labels
    x_labels = [f"{result['label']}" for result in all_results]
    ax.set_xticklabels(x_labels, ha='right', fontsize=17, family='sans-serif')

    # Add legend
    ax.legend(fontsize=19, loc='best')

    # Add grid
    ax.grid(True, axis='y', linestyle='--', alpha=0.3)

    # Add margin to prevent value labels from touching the plot border
    ax.margins(y=0.15)

    # Adjust layout to prevent label cutoff
    fig.tight_layout()

    # Plot the figure
    canvas = FigureCanvasTkAgg(fig, master=frame)
    canvas.draw()
    canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

    # Create button frame
    button_frame = tk.Frame(frame)
    button_frame.pack(side=tk.BOTTOM, fill=tk.X, pady=5)

    # Add a button to download the plot
    download_button = tk.Button(
        button_frame,
        text="Download Plot",
        command=lambda: download_plot(canvas, directory, "TimeoutsByExperimentsWithCI.png"),
        bg='#4CAF50',
        fg='white',
        padx=10,
        pady=5
    )
    download_button.pack(side=tk.LEFT, padx=5)

    # Add export results button
    def export_results():
        try:
            output_files = export_all_formats(
                results=all_results,
                output_dir=directory,
                base_filename='timeouts_by_experiments',
                confidence_level=confidence_level,
                table_caption='Timeout and Retransmission Statistics'
            )
            print_export_summary(output_files)
            messagebox.showinfo("Export Complete",
                              f"Results exported successfully!\n\n"
                              f"Files saved to: {directory}")
        except Exception as e:
            messagebox.showerror("Export Error", f"Failed to export results:\n{str(e)}")

    export_button = tk.Button(
        button_frame,
        text="Export Results",
        command=export_results,
        bg='#2196F3',
        fg='white',
        padx=10,
        pady=5
    )
    export_button.pack(side=tk.LEFT, padx=5)
