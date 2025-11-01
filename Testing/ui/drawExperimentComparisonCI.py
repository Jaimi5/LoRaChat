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


def calculate_overhead_and_loss(experiment_path, config):
    """
    Calculate Experimental Overhead % and Packets Lost % for a single experiment run.
    Returns: (experimental_overhead, packets_lost_percentage)
    """
    monitoring_path = os.path.join(experiment_path, "Monitoring")

    if not os.path.exists(monitoring_path):
        return None, None

    # Get configuration parameters
    total_messages = int(config["Simulator"]["PACKET_COUNT"])

    if int(config["Simulator"]["ONE_SENDER"]) == 0:
        total_messages = total_messages * 9

    max_packet_size = int(config["LoRaMesher"]["LM_MAX_PACKET_SIZE"]) - 11
    payload_size = int(config["Simulator"]["PACKET_SIZE"])

    percentage_overhead = 0
    percentage_packets_lost = 0

    # If the messages are sent reliable
    if int(config["Simulator"]["SEND_RELIABLE"]) == 1:
        # Get the number of data packets
        total_data_packets = total_messages * math.ceil(payload_size / max_packet_size)
        total_packets = total_data_packets * 2 + total_messages * 2

        data = get_monitor_status(monitoring_path)

        control_overhead = total_packets * 11
        ctrl_overhead_sync = data["totalSyncResend"] * 11
        ctrl_overhead_data = data["totalMessagesResend"] * 11

        total_control_overhead = control_overhead + ctrl_overhead_sync + ctrl_overhead_data
        total_data = total_control_overhead + (payload_size * total_messages)

        percentage_overhead = round((total_control_overhead / total_data) * 100, 2)

        total_packets_lost = data["totalSyncResend"] + data["totalMessagesResend"]
        percentage_packets_lost = round((total_packets_lost / total_packets) * 100, 2)
    else:
        # Unreliable mode
        overhead_in_bytes = total_messages * 8
        total_data = overhead_in_bytes + (payload_size * total_messages)

        percentage_overhead = round((overhead_in_bytes / total_data) * 100, 2)

        # Open messages.json to get the number of messages received
        messages_path = os.path.join(experiment_path, "messages.json")

        if not os.path.exists(messages_path):
            print(f"No messages.json file found in {experiment_path}")
            return percentage_overhead, None

        with open(messages_path, "r") as f:
            messages = json.load(f)

        total_packets_lost = total_messages - len(messages)
        percentage_packets_lost = round((total_packets_lost / total_messages) * 100, 2)

    return percentage_overhead, percentage_packets_lost


def calculate_metrics_with_ci(experiment_paths, config, confidence_level=0.95,
                              test_normality_flag=True):
    """
    Calculate metrics with confidence intervals for a group of experiment runs.
    Uses enhanced statistics with normality testing.

    Returns: { metric_name: { 'mean': X, 'ci_lower': Y, 'ci_upper': Z, 'std': W, 'n': N, 'values': [...] } }
    """
    overhead_values = []
    packets_lost_values = []

    for path in experiment_paths:
        overhead, packets_lost = calculate_overhead_and_loss(path, config)

        if overhead is not None:
            overhead_values.append(overhead)
        if packets_lost is not None:
            packets_lost_values.append(packets_lost)

    results = {}

    # Calculate statistics for each metric using enhanced CI module
    for metric_name, values in [("Experimental Overhead", overhead_values),
                                  ("Packets Lost", packets_lost_values)]:
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


def draw_experiment_comparison_bar_ci(frame: Frame, directory, confidence_level=0.95,
                                     show_individual_points=False):
    """
    Draw a bar chart with error bars comparing experiments with confidence intervals.

    Args:
        frame: Tkinter frame to draw in
        directory: Directory containing experiment results
        confidence_level: Confidence level for CIs (default 0.95)
        show_individual_points: Whether to overlay individual data points (default True)
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

        metrics = calculate_metrics_with_ci(
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
                      "Individual data points will be shown for transparency.\n"
                      "See Testing/ui/CI_GUIDE.md for reporting guidance.")
        messagebox.showwarning("Small Sample Size", warning_msg)

    # Create the figure and subplot
    fig = Figure(figsize=(12, 6), dpi=100)
    ax = fig.add_subplot(111)

    # Set font size
    plt.rc("font", size=14)

    # Prepare data for plotting
    metric_names = ["Experimental Overhead", "Packets Lost"]
    x = np.arange(len(metric_names))
    width = 0.8 / len(all_results)  # Width of bars

    colors = plt.cm.Set2(np.linspace(0, 1, len(all_results)))

    # Plot bars for each configuration group
    for i, result in enumerate(all_results):
        means = []
        errors = []
        all_individual_values = []

        for metric_name in metric_names:
            metric_data = result['metrics'].get(metric_name)
            if metric_data is None:
                means.append(0)
                errors.append(0)
                all_individual_values.append([])
            else:
                means.append(metric_data['mean'])
                # Error bar is the difference between mean and CI bound
                error = metric_data['ci_upper'] - metric_data['mean']
                errors.append(error)
                all_individual_values.append(metric_data.get('values', []))

        offset = (i - len(all_results)/2 + 0.5) * width
        bars = ax.bar(x + offset, means, width, yerr=errors,
                      label=f"{result['label']} (n={result['n']})",
                      color=colors[i], capsize=5, alpha=0.8)

        # Overlay individual data points if requested (disabled by default)
        # if show_individual_points:
        #     for j, (bar, individual_values) in enumerate(zip(bars, all_individual_values)):
        #         if len(individual_values) > 0:
        #             # Add jitter for visibility
        #             x_positions = np.random.normal(bar.get_x() + bar.get_width()/2,
        #                                            bar.get_width()/8, len(individual_values))
        #             ax.scatter(x_positions, individual_values,
        #                       color='black', s=30, alpha=0.6, zorder=10,
        #                       edgecolors='white', linewidths=0.8)

        # Add value labels on top of bars
        for j, (bar, mean, error) in enumerate(zip(bars, means, errors)):
            height = bar.get_height()
            if height > 0:
                ax.text(bar.get_x() + bar.get_width()/2., height + error + 0.5,
                       f'{mean:.2f}%',
                       ha='center', va='bottom', fontsize=10)

    # Add labels and title
    ax.set_xlabel("Metric Type", fontsize=18)
    ax.set_ylabel("Percentage [%]", fontsize=18)
    ax.set_title(f"Experiment Comparison with {int(confidence_level*100)}% Confidence Intervals", fontsize=18)
    ax.set_xticks(x)
    ax.set_xticklabels(metric_names)

    # Add legend
    ax.legend(fontsize=12, loc='best')

    # Add grid
    ax.grid(True, axis='y', linestyle='--', alpha=0.3)

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
        command=lambda: download_plot(canvas, directory, "ExperimentComparisonCI.png"),
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
                base_filename='experiment_comparison_ci',
                confidence_level=confidence_level,
                table_caption='Experimental Overhead and Packet Loss Comparison'
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


def draw_experiment_comparison_box(frame: Frame, directory):
    """
    Draw box plots comparing experiments with individual data points overlaid.
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

    for config_hash, group_info in config_groups.items():
        metrics = calculate_metrics_with_ci(group_info['paths'], group_info['config'])

        all_results.append({
            'label': group_info['label'],
            'n': len(group_info['paths']),
            'metrics': metrics
        })

    # Create the figure and subplot
    fig = Figure(figsize=(12, 6), dpi=100)
    ax = fig.add_subplot(111)

    # Set font size
    plt.rc("font", size=14)

    # Prepare data for plotting
    metric_names = ["Experimental Overhead", "Packets Lost"]

    # Collect data for box plots
    box_data = []
    box_positions = []
    box_labels = []
    colors = plt.cm.Set2(np.linspace(0, 1, len(all_results)))

    position = 1
    for metric_idx, metric_name in enumerate(metric_names):
        for result_idx, result in enumerate(all_results):
            metric_data = result['metrics'].get(metric_name)
            if metric_data is not None and len(metric_data['values']) > 0:
                box_data.append(metric_data['values'])
                box_positions.append(position)
                box_labels.append(f"{result['label']}")

                # Overlay individual points
                y_values = metric_data['values']
                x_values = [position] * len(y_values)
                ax.scatter(x_values, y_values, color=colors[result_idx],
                          alpha=0.6, s=50, zorder=3, edgecolors='black', linewidths=0.5)

                position += 1

        position += 1  # Add space between metric groups

    # Create box plots
    bp = ax.boxplot(box_data, positions=box_positions, widths=0.6, patch_artist=True,
                    showmeans=True, meanline=True,
                    boxprops=dict(facecolor='lightblue', alpha=0.5),
                    medianprops=dict(color='red', linewidth=2),
                    meanprops=dict(color='green', linewidth=2, linestyle='--'))

    # Color the boxes
    box_idx = 0
    for metric_idx, metric_name in enumerate(metric_names):
        for result_idx, result in enumerate(all_results):
            metric_data = result['metrics'].get(metric_name)
            if metric_data is not None and len(metric_data['values']) > 0:
                bp['boxes'][box_idx].set_facecolor(colors[result_idx])
                bp['boxes'][box_idx].set_alpha(0.5)
                box_idx += 1

    # Add labels and title
    ax.set_xlabel("Experiment Configuration", fontsize=18, fontstyle='normal')
    ax.set_ylabel("Percentage [%]", fontsize=18)
    ax.set_title("Experiment Comparison - Distribution Analysis", fontsize=18)
    ax.set_xticks(box_positions)
    ax.set_xticklabels(box_labels, ha='right', fontsize=10, family='sans-serif')

    # Add vertical lines to separate metric groups
    separator_positions = []
    pos = 1
    for metric_idx, metric_name in enumerate(metric_names):
        if metric_idx > 0:
            separator_positions.append(pos - 0.5)
        pos += len([r for r in all_results if r['metrics'].get(metric_name) is not None])
        pos += 1

    for sep_pos in separator_positions:
        ax.axvline(x=sep_pos, color='gray', linestyle='--', alpha=0.5)

    # Add metric labels as text
    pos = 1
    for metric_idx, metric_name in enumerate(metric_names):
        count = len([r for r in all_results if r['metrics'].get(metric_name) is not None])
        if count > 0:
            mid_pos = pos + (count - 1) / 2
            ax.text(mid_pos, ax.get_ylim()[1] * 0.95, metric_name,
                   ha='center', va='top', fontsize=14, fontweight='bold',
                   bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
            pos += count
        pos += 1

    # Add grid
    ax.grid(True, axis='y', linestyle='--', alpha=0.3)

    # Create custom legend
    from matplotlib.patches import Patch
    legend_elements = [Patch(facecolor=colors[i], alpha=0.5, label=result['label'])
                      for i, result in enumerate(all_results)]
    legend_elements.append(plt.Line2D([0], [0], color='red', linewidth=2, label='Median'))
    legend_elements.append(plt.Line2D([0], [0], color='green', linewidth=2, linestyle='--', label='Mean'))
    ax.legend(handles=legend_elements, fontsize=11, loc='best')

    # Adjust layout
    fig.tight_layout()

    # Plot the figure
    canvas = FigureCanvasTkAgg(fig, master=frame)
    canvas.draw()
    canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

    # Add a button to download the plot
    download_button = tk.Button(
        frame,
        text="Download Plot",
        command=lambda: download_plot(canvas, directory, "ExperimentComparisonBox.png"),
    )
    download_button.pack(side=tk.BOTTOM)
