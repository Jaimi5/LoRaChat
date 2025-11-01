import os
import json
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import tkinter as tk
from tkinter import Frame, messagebox
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from downloadPlot import download_plot
import scipy.stats as stats
import hashlib

# Import enhanced CI statistics module
import sys
sys.path.append(os.path.join(os.path.dirname(__file__), "..", ".."))
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
        messages_path = os.path.join(directory, subdir, "messages.json")

        # Check if both configuration and messages data exist
        if not os.path.exists(config_path):
            print(f"No simConfiguration.json found in {subdir}")
            continue

        if not os.path.exists(messages_path):
            print(f"No messages.json found in {subdir}")
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


def calculate_packet_loss_metrics(experiment_path, config):
    """
    Calculate packet delivery ratio and packet loss for a single experiment run.
    Returns: (pdr_percentage, loss_percentage)
    """
    messages_path = os.path.join(experiment_path, "messages.json")

    if not os.path.exists(messages_path):
        return None, None

    # Read messages.json to count received messages
    with open(messages_path, "r") as f:
        messages = json.load(f)

    actual_messages = len(messages)

    # Get configuration parameters
    num_devices = len(config["DeviceMapping"])
    packet_count = int(config["Simulator"]["PACKET_COUNT"])
    one_sender = int(config["Simulator"]["ONE_SENDER"])

    # Calculate expected messages based on ONE_SENDER configuration
    if one_sender == 0:
        # All devices send messages
        expected_messages = num_devices * packet_count
    else:
        # Only one device sends messages
        expected_messages = packet_count

    # Calculate metrics
    pdr_percentage = (actual_messages / expected_messages) * 100 if expected_messages > 0 else 0
    loss_percentage = ((expected_messages - actual_messages) / expected_messages) * 100 if expected_messages > 0 else 0

    return pdr_percentage, loss_percentage


def calculate_loss_metrics_with_ci(experiment_paths, config, confidence_level=0.95,
                                   test_normality_flag=True):
    """
    Calculate packet loss metrics with confidence intervals for a group of experiment runs.
    Uses enhanced statistics with normality testing.

    Returns: {
        'Packet Delivery Ratio': { 'mean': X, 'ci_lower': Y, 'ci_upper': Z, ... },
        'Packet Loss': { 'mean': X, 'ci_lower': Y, 'ci_upper': Z, ... }
    }
    """
    pdr_values = []
    loss_values = []

    for path in experiment_paths:
        pdr, loss = calculate_packet_loss_metrics(path, config)

        if pdr is not None:
            pdr_values.append(pdr)
        if loss is not None:
            loss_values.append(loss)

    results = {}

    # Calculate statistics for each metric using enhanced CI module
    for metric_name, values in [
        ("Packet Delivery Ratio", pdr_values),
        ("Packet Loss", loss_values)
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


def print_loss_metrics_values(all_results, confidence_level=0.95):
    """
    Print all metrics values in a format suitable for copying to standalone scripts.
    This function logs all data to console/terminal.
    """
    print("\n" + "="*80)
    print("PACKET LOSS METRICS - VALUES FOR plotLossByExperiments.py")
    print("="*80)
    print(f"Confidence Level: {int(confidence_level*100)}%")
    print(f"Method: Student's t-distribution")
    print("="*80)

    # Print experiment labels
    exp_labels = [result['label'] for result in all_results]
    print(f"\nExperiment Labels: {exp_labels}")
    print(f"Number of Configurations: {len(all_results)}")

    # Print data for each configuration
    for idx, result in enumerate(all_results, 1):
        print(f"\n{'─'*80}")
        print(f"Configuration {idx}: {result['label']}")
        print(f"Sample size: n={result['n']}")
        print(f"{'─'*80}")

        metrics = result.get('metrics', {})

        for metric_name in ["Packet Delivery Ratio", "Packet Loss"]:
            metric_data = metrics.get(metric_name)

            if metric_data:
                print(f"\n  {metric_name}:")
                print(f"    Mean:       {metric_data['mean']:.4f}%")
                print(f"    95% CI:     [{metric_data['ci_lower']:.4f}%, {metric_data['ci_upper']:.4f}%]")
                print(f"    SD:         {metric_data['std']:.4f}%")
                print(f"    SE:         {metric_data.get('se', 0):.4f}%")
                print(f"    n:          {metric_data['n']}")

                if 'values' in metric_data:
                    values_str = ', '.join([f"{v:.2f}" for v in metric_data['values']])
                    print(f"    Individual: [{values_str}]")

    # Print data in Python dict format for easy copying
    print("\n" + "="*80)
    print("PYTHON DICTIONARY FORMAT (copy to plotLossByExperiments.py):")
    print("="*80)
    print("\nexperimental_data = {")

    for result in all_results:
        config_label = result['label']
        metrics = result.get('metrics', {})

        pdr_data = metrics.get("Packet Delivery Ratio")
        if pdr_data and 'values' in pdr_data:
            values_str = ', '.join([f"{v:.2f}" for v in pdr_data['values']])
            print(f"    \"{config_label} - PDR\": {{")
            print(f"        'values': [{values_str}],")
            print(f"    }},")

        loss_data = metrics.get("Packet Loss")
        if loss_data and 'values' in loss_data:
            values_str = ', '.join([f"{v:.2f}" for v in loss_data['values']])
            print(f"    \"{config_label} - Loss\": {{")
            print(f"        'values': [{values_str}],")
            print(f"    }},")

    print("}")

    print("\n" + "="*80)
    print("✓ Values printed successfully!")
    print("Copy the data above to use in plotLossByExperiments.py")
    print("="*80 + "\n")


def draw_loss_messages_by_experiments_with_ci(frame: Frame, directory, confidence_level=0.95):
    """
    Draw a bar chart showing packet delivery ratio and packet loss for each configuration,
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

        metrics = calculate_loss_metrics_with_ci(
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
    metric_names = ["Packet Delivery Ratio", "Packet Loss"]
    x = np.arange(len(all_results))  # X-axis positions for each configuration
    width = 0.35  # Width of bars (2 bars per group)

    # Professional colorblind-friendly palette
    colors = ['#0173B2', '#DE8F05']  # Blue for PDR, Orange for Loss

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

        # Calculate offset to center the two bars around each x position
        offset = (i - 0.5) * width
        bars = ax.bar(x + offset, means, width, yerr=errors,
                      label=metric_name,
                      color=colors[i], capsize=5, alpha=0.8)

        # Add value labels on top of bars
        for j, (bar, mean, error) in enumerate(zip(bars, means, errors)):
            height = bar.get_height()
            if height > 0:
                ax.text(bar.get_x() + bar.get_width()/2., height + error + 1,
                       f'{mean:.2f}%',
                       ha='center', va='bottom', fontsize=15, rotation=0)

    # Add labels and title
    ax.set_xlabel("Experiment Configuration", fontsize=24, fontstyle='normal')
    ax.set_ylabel("Percentage [%]", fontsize=24)
    ax.set_title(f"Packet Loss Statistics by Experiment ({int(confidence_level*100)}% CI)", fontsize=24)
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
        command=lambda: download_plot(canvas, directory, "LossMessagesByExperimentsWithCI.png"),
        bg='#4CAF50',
        fg='white',
        padx=10,
        pady=5
    )
    download_button.pack(side=tk.LEFT, padx=5)

    # Add print values button (logs to console/terminal)
    def print_values():
        print_loss_metrics_values(all_results, confidence_level)
        messagebox.showinfo("Values Printed",
                          "All values have been printed to the console/terminal!\n\n"
                          "Check your terminal window to see the output.\n"
                          "Copy the data to use in plotLossByExperiments.py")

    print_button = tk.Button(
        button_frame,
        text="📋 Print Values to Console",
        command=print_values,
        bg='#9C27B0',
        fg='white',
        padx=10,
        pady=5
    )
    print_button.pack(side=tk.LEFT, padx=5)

    # Add export results button
    def export_results():
        try:
            output_files = export_all_formats(
                results=all_results,
                output_dir=directory,
                base_filename='loss_messages_by_experiments',
                confidence_level=confidence_level,
                table_caption='Packet Loss and Delivery Ratio Statistics'
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
