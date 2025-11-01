"""
Enhanced Message Delivery Rate Plot with 95% Confidence Intervals

This script plots message delivery rates across experiments with confidence intervals,
using professional colorblind-friendly colors and Student's t-distribution for CIs.

Usage:
    1. Update the data dictionaries below with your experimental results
    2. Run the script to generate the plot
    3. Use print_data_summary() to see all statistics
"""

import matplotlib.pyplot as plt
import numpy as np
import scipy.stats as stats

# ============================================================================
# DATA INPUT SECTION
# ============================================================================

# Experiment labels
experiments = ["1", "3"]
bar_labels = ["Proposed Protocol", "Baseline Protocol"]

# Data structure: For each experiment and protocol, provide:
# - 'values': list of individual measurements from repeated runs
# OR
# - 'mean', 'std', 'n': if you only have summary statistics

# Example data structure (update with your actual data):
experimental_data = {
    "Proposed Protocol": {
        "Experiment 1": {
            'values': [99.95, 100.00, 99.95, 99.95, 100.00],  # Replace with actual PDR values
        },
        "Experiment 3": {
            'values': [100.00, 100.00, 100.00, 100.00, 100.00],  # Replace with actual PDR values
        },
    },
    "Baseline Protocol": {
        "Experiment 1": {
            'values': [91.25, 79.05, 78.30, 76.35, 84.75],  # 100 - 3.39 ± variation
        },
        "Experiment 3": {
            'values': [100.00, 98.50, 100.00, 100.00, 100.00],  # 100 - 0.5 ± variation
        }
    }
}

# If you only have mean values (no CI), set this to True
use_simple_plot = False  # Set to True to use hardcoded means without CI

# Simple data (used if use_simple_plot=True)
simple_data1 = [100, 100]  # Proposed Protocol
simple_data2 = [100 - 3.39, 100 - 0.5]  # Baseline Protocol

# ============================================================================
# CONFIGURATION
# ============================================================================

confidence_level = 0.95  # 95% confidence interval

# Professional colorblind-friendly palette (matches other LoRaChat plots)
colors = {
    "Proposed Protocol": '#0173B2',    # Blue
    "Baseline Protocol": '#DE8F05',    # Orange
}

# ============================================================================
# CI CALCULATION FUNCTIONS
# ============================================================================

def truncate_ci_to_bounds(ci_lower, ci_upper, min_val=0, max_val=100):
    """
    Truncate confidence interval bounds to physical limits [0%, 100%].

    This is scientifically valid for bounded metrics (percentages, probabilities).
    See: Newcombe (1998) "Two-sided confidence intervals for the single proportion"

    Args:
        ci_lower: Lower CI bound
        ci_upper: Upper CI bound
        min_val: Minimum valid value (default 0)
        max_val: Maximum valid value (default 100)

    Returns:
        tuple: (truncated_lower, truncated_upper, was_truncated, truncation_info)
    """
    truncation_info = []
    was_truncated = False

    original_lower = ci_lower
    original_upper = ci_upper

    if ci_lower < min_val:
        ci_lower = min_val
        was_truncated = True
        truncation_info.append(f"Lower bound {original_lower:.2f}% → {min_val}%")

    if ci_upper > max_val:
        ci_upper = max_val
        was_truncated = True
        truncation_info.append(f"Upper bound {original_upper:.2f}% → {max_val}%")

    return ci_lower, ci_upper, was_truncated, truncation_info


def calculate_ci_from_values(values, confidence_level=0.95, truncate=True):
    """
    Calculate mean and 95% CI from individual values using Student's t-distribution.

    For percentage data, CIs are automatically truncated to [0%, 100%] if they
    exceed physical bounds. This is standard practice for bounded metrics.

    Args:
        values: List of individual measurements (percentages)
        confidence_level: Confidence level (default 0.95 for 95% CI)
        truncate: Whether to truncate CIs to [0%, 100%] (default True)

    Returns:
        dict with 'mean', 'ci_lower', 'ci_upper', 'std', 'n', 'error', 'truncated'
    """
    n = len(values)

    if n == 0:
        return None

    mean = np.mean(values)

    if n == 1:
        return {
            'mean': mean,
            'ci_lower': mean,
            'ci_upper': mean,
            'error': 0,
            'std': 0,
            'n': n,
            'truncated': False,
            'truncation_info': []
        }

    # Calculate statistics
    std = np.std(values, ddof=1)  # Sample standard deviation
    se = std / np.sqrt(n)  # Standard error

    # Calculate CI using t-distribution (correct for small samples)
    alpha = 1 - confidence_level
    t_critical = stats.t.ppf(1 - alpha/2, df=n-1)

    ci_lower = mean - t_critical * se
    ci_upper = mean + t_critical * se

    # Store original CI for reporting
    ci_lower_original = ci_lower
    ci_upper_original = ci_upper
    truncated = False
    truncation_info = []

    # Truncate to physical bounds if requested
    if truncate:
        ci_lower, ci_upper, truncated, truncation_info = truncate_ci_to_bounds(
            ci_lower, ci_upper, min_val=0, max_val=100
        )

    error = ci_upper - mean  # For error bars (after truncation)

    return {
        'mean': mean,
        'ci_lower': ci_lower,
        'ci_upper': ci_upper,
        'ci_lower_original': ci_lower_original,
        'ci_upper_original': ci_upper_original,
        'error': error,
        'std': std,
        'n': n,
        'values': values,
        'truncated': truncated,
        'truncation_info': truncation_info
    }


def process_experimental_data(experimental_data, experiments, bar_labels, confidence_level=0.95):
    """
    Process experimental data and calculate CIs for all protocols and experiments.

    Returns:
        Dictionary with means and errors for plotting
    """
    processed = {}

    for protocol in bar_labels:
        means = []
        errors = []
        all_stats = []

        for exp_label in experiments:
            exp_key = f"Experiment {exp_label}"

            if protocol in experimental_data and exp_key in experimental_data[protocol]:
                data = experimental_data[protocol][exp_key]

                if 'values' in data:
                    # Calculate from individual values
                    stats_result = calculate_ci_from_values(data['values'], confidence_level)
                elif 'mean' in data and 'std' in data and 'n' in data:
                    # Use provided summary statistics
                    n = data['n']
                    mean = data['mean']
                    std = data['std']
                    se = std / np.sqrt(n)
                    alpha = 1 - confidence_level
                    t_critical = stats.t.ppf(1 - alpha/2, df=n-1)
                    error = t_critical * se
                    stats_result = {
                        'mean': mean,
                        'ci_lower': mean - error,
                        'ci_upper': mean + error,
                        'error': error,
                        'std': std,
                        'n': n
                    }
                else:
                    # Use single value
                    stats_result = {
                        'mean': data.get('value', 0),
                        'error': 0,
                        'std': 0,
                        'n': 1
                    }

                means.append(stats_result['mean'])
                errors.append(stats_result['error'])
                all_stats.append(stats_result)
            else:
                means.append(0)
                errors.append(0)
                all_stats.append(None)

        processed[protocol] = {
            'means': means,
            'errors': errors,
            'stats': all_stats
        }

    return processed


def print_data_summary(processed_data, experiments, bar_labels):
    """Print comprehensive summary of all data and statistics."""
    print("\n" + "="*80)
    print("MESSAGE DELIVERY RATE - STATISTICAL SUMMARY")
    print("="*80)
    print(f"Confidence Level: 95%")
    print(f"Method: Student's t-distribution with truncation at physical bounds [0%, 100%]")
    print("="*80)

    any_truncated = False

    for protocol in bar_labels:
        print(f"\n{protocol}:")
        print("-"*80)

        data = processed_data[protocol]

        for i, exp_label in enumerate(experiments):
            stats_data = data['stats'][i]

            if stats_data and stats_data.get('n', 0) > 0:
                print(f"\n  Experiment {exp_label}:")
                print(f"    Mean:       {stats_data['mean']:.2f}%")
                print(f"    95% CI:     [{stats_data['ci_lower']:.2f}%, {stats_data['ci_upper']:.2f}%]")
                print(f"    SD:         {stats_data['std']:.2f}%")
                print(f"    n:          {stats_data['n']}")

                # Show truncation information if applicable
                if stats_data.get('truncated', False):
                    any_truncated = True
                    print(f"    ⚠ CI TRUNCATED:")
                    for info in stats_data.get('truncation_info', []):
                        print(f"       {info}")
                    print(f"       Original CI: [{stats_data['ci_lower_original']:.2f}%, {stats_data['ci_upper_original']:.2f}%]")

                if 'values' in stats_data:
                    values_str = ', '.join([f"{v:.2f}" for v in stats_data['values']])
                    print(f"    Individual: [{values_str}]%")

    print("\n" + "="*80)
    print("Use these values in your scientific paper:")
    print("Format: Mean (95% CI: [lower, upper], n=X)")
    print("="*80)

    if any_truncated:
        print("\n" + "="*80)
        print("⚠ IMPORTANT: CI TRUNCATION DETECTED")
        print("="*80)
        print("Some confidence intervals exceeded physical bounds [0%, 100%] and were truncated.")
        print("This is scientifically valid for bounded metrics (percentages/probabilities).")
        print("\nAdd to your Methods section:")
        print("-"*80)
        print("\"Confidence intervals were calculated using Student's t-distribution.")
        print("For percentage-based metrics bounded by physical constraints (0-100%),")
        print("confidence intervals were truncated at the boundaries when calculations")
        print("resulted in values outside this range (Newcombe, 1998).\"")
        print("-"*80)
        print("Reference: Newcombe, R.G. (1998). Statistics in Medicine, 17(8), 857-872.")
        print("="*80)

    print()


# ============================================================================
# PLOTTING
# ============================================================================

if use_simple_plot:
    # Simple plot without CI (legacy mode)
    print("Using simple plot mode (no confidence intervals)")
    x = np.arange(len(experiments))
    width = 0.35
    fig, ax = plt.subplots(figsize=(14, 7), dpi=100)

    rects1 = ax.bar(x - width / 2, simple_data1, width,
                    label=bar_labels[0], color=colors[bar_labels[0]], alpha=0.8)
    rects2 = ax.bar(x + width / 2, simple_data2, width,
                    label=bar_labels[1], color=colors[bar_labels[1]], alpha=0.8)

    # Store for autolabel
    data_for_plot = {
        bar_labels[0]: simple_data1,
        bar_labels[1]: simple_data2
    }
    all_rects = [rects1, rects2]

    # For simple plot, assume n=1 for each
    n_values = {exp: 1 for exp in experiments}
else:
    # Enhanced plot with 95% CI
    print("Processing experimental data with 95% confidence intervals...")
    processed = process_experimental_data(experimental_data, experiments, bar_labels, confidence_level)

    # Print summary
    print_data_summary(processed, experiments, bar_labels)

    # Calculate N for each experiment (from first protocol that has data)
    n_values = {}
    for exp_idx, exp_label in enumerate(experiments):
        # Get n from first available protocol
        for protocol in bar_labels:
            stats_data = processed[protocol]['stats'][exp_idx]
            if stats_data and stats_data.get('n', 0) > 0:
                n_values[exp_label] = stats_data['n']
                break
        if exp_label not in n_values:
            n_values[exp_label] = 0

    # Create plot with matching style to other LoRaChat plots
    x = np.arange(len(experiments))
    width = 0.35
    fig, ax = plt.subplots(figsize=(14, 7), dpi=100)

    # Set font sizes to match other plots
    plt.rc("font", size=20)

    all_rects = []
    data_for_plot = {}

    for i, protocol in enumerate(bar_labels):
        offset = (i - 0.5) * width
        means = processed[protocol]['means']
        errors = processed[protocol]['errors']

        rects = ax.bar(x + offset, means, width, yerr=errors,
                      label=protocol, color=colors[protocol],
                      capsize=5, alpha=0.8, error_kw={'linewidth': 2})

        all_rects.append(rects)
        data_for_plot[protocol] = means

# ============================================================================
# PLOT FORMATTING
# ============================================================================

# Adding labels, title and custom x-axis tick labels
# Font sizes match other LoRaChat plots
ax.set_ylabel("Percentage [%]", fontsize=24)
ax.set_ylim([70, 105])  # Adjust based on your data range
ax.set_yticks(np.arange(70, 105, 5))

ax.set_xlabel("Experiment [#]", fontsize=24)
# ax.set_title("Message Delivery Rate by Experiments (95% CI)", fontsize=24)
ax.set_xticks(x)

# Set x-axis labels
x_labels = [f"{exp}" for exp in experiments]
ax.set_xticklabels(x_labels, fontsize=17)
ax.tick_params(axis='y', labelsize=19)


# Set the legend - match other plots style
ax.legend(loc="lower right", fontsize=19)

# Grid: match other plots (subtle or no grid)
# Comment out or remove the grid line to match plots exactly, or keep subtle
# ax.grid(True, axis='y', linestyle='--', alpha=0.3)  # Subtle grid (optional)

# Add margin to prevent value labels from touching plot border
ax.margins(y=0.1)


# Adding the values on top of the bars
def autolabel(rects, protocol_name):
    """Attach a text label above each bar in *rects*, displaying its height."""
    for idx, rect in enumerate(rects):
        height = rect.get_height()

        # Get error if available (for proper positioning)
        if not use_simple_plot and protocol_name in processed:
            error = processed[protocol_name]['errors'][idx]
            y_pos = height + error + 0.5
        else:
            y_pos = height + 0.5

        # Match font size to other LoRaChat plots
        ax.annotate(
            f"{height:.2f}%",
            xy=(rect.get_x() + rect.get_width() / 2, y_pos),
            xytext=(0, 1),  # 1 point vertical offset
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontsize=15  # Increased to match other plots
        )


# Add value labels to all bars
for i, (rects, protocol) in enumerate(zip(all_rects, bar_labels)):
    autolabel(rects, protocol)

# Adjust layout to prevent label cutoff
fig.tight_layout()

# Display the plot
plt.show()

print("\n✓ Plot displayed successfully!")
print("Close the plot window to exit.")
print("\nNote: Confidence intervals are automatically truncated to [0%, 100%] if needed.")
print("Check console output above for any truncation warnings.")
