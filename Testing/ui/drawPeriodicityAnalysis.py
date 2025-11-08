"""
Message Periodicity Analysis - Real-Time Visualization UI

Main Tkinter application for analyzing and visualizing message periodicity patterns
"""

import tkinter as tk
from tkinter import filedialog, ttk
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
import glob
import json
import os
import sys
from typing import List, Dict, Optional

# Add current directory to path
sys.path.insert(0, os.path.dirname(__file__))

from periodicityParser import PeriodicityParser, MessageEvent, get_all_packet_type_names
from periodicityCalculator import PeriodicityCalculator, PeriodicityMetrics

# Try to load device colors
try:
    with open(os.path.join(os.path.dirname(__file__), 'device_colors.json'), 'r') as f:
        DEVICE_COLORS = json.load(f)
except:
    DEVICE_COLORS = {}


class MessagePeriodicityAnalyzer:
    """Main UI for message periodicity analysis"""

    def __init__(self):
        self.root = tk.Tk()
        self.root.title("LoRaMesher Message Periodicity Analyzer")
        self.root.geometry("1600x1000")

        # Data storage
        self.all_messages: List[MessageEvent] = []
        self.filtered_messages: List[MessageEvent] = []
        self.parser = PeriodicityParser()
        self.calculator = PeriodicityCalculator()

        # Filter state
        self.selected_types = []
        self.selected_devices = []
        self.selected_direction = tk.StringVar(value="all")
        self.view_mode = tk.StringVar(value="global")  # global, per_node, both

        # Build UI
        self.build_ui()

    def build_ui(self):
        """Build the user interface"""
        # Top control panel
        control_frame = tk.Frame(self.root, bg="#e0e0e0")
        control_frame.pack(side=tk.TOP, fill=tk.X, padx=10, pady=5)

        tk.Label(control_frame, text="Log Files:", bg="#e0e0e0").pack(side=tk.LEFT, padx=5)

        self.dir_label = tk.Label(control_frame, text="No files loaded", fg="gray", bg="#e0e0e0")
        self.dir_label.pack(side=tk.LEFT, padx=5)

        tk.Button(control_frame, text="Browse...", command=self.browse_files).pack(side=tk.LEFT, padx=5)
        tk.Button(control_frame, text="Refresh", command=self.refresh_analysis).pack(side=tk.LEFT, padx=5)

        # Filter panel
        filter_frame = tk.LabelFrame(self.root, text="Filters", bg="#f5f5f5")
        filter_frame.pack(side=tk.TOP, fill=tk.X, padx=10, pady=5)

        # Message type filter
        type_frame = tk.Frame(filter_frame, bg="#f5f5f5")
        type_frame.pack(side=tk.LEFT, fill=tk.Y, padx=10, pady=5)

        tk.Label(type_frame, text="Message Types:", bg="#f5f5f5", font=("Arial", 9, "bold")).pack(anchor=tk.W)

        self.type_checkboxes = {}
        self.type_vars = {}

        type_scroll_frame = tk.Frame(type_frame, bg="#f5f5f5")
        type_scroll_frame.pack(fill=tk.BOTH, expand=True)

        for type_name in get_all_packet_type_names():
            var = tk.BooleanVar(value=True)
            self.type_vars[type_name] = var
            cb = tk.Checkbutton(
                type_scroll_frame,
                text=type_name,
                variable=var,
                command=self.apply_filters,
                bg="#f5f5f5"
            )
            cb.pack(anchor=tk.W)
            self.type_checkboxes[type_name] = cb

        type_btn_frame = tk.Frame(type_frame, bg="#f5f5f5")
        type_btn_frame.pack(fill=tk.X)
        tk.Button(type_btn_frame, text="Select All", command=self.select_all_types, width=10).pack(side=tk.LEFT, padx=2)
        tk.Button(type_btn_frame, text="Clear All", command=self.clear_all_types, width=10).pack(side=tk.LEFT, padx=2)

        # Device filter
        device_frame = tk.Frame(filter_frame, bg="#f5f5f5")
        device_frame.pack(side=tk.LEFT, fill=tk.Y, padx=10, pady=5)

        tk.Label(device_frame, text="Devices:", bg="#f5f5f5", font=("Arial", 9, "bold")).pack(anchor=tk.W)

        self.device_listbox_frame = tk.Frame(device_frame, bg="#f5f5f5")
        self.device_listbox_frame.pack(fill=tk.BOTH, expand=True)

        self.device_listbox = tk.Listbox(self.device_listbox_frame, selectmode=tk.MULTIPLE, height=6, width=15)
        self.device_listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.device_listbox.bind('<<ListboxSelect>>', lambda e: self.apply_filters())

        device_scroll = tk.Scrollbar(self.device_listbox_frame)
        device_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.device_listbox.config(yscrollcommand=device_scroll.set)
        device_scroll.config(command=self.device_listbox.yview)

        device_btn_frame = tk.Frame(device_frame, bg="#f5f5f5")
        device_btn_frame.pack(fill=tk.X)
        tk.Button(device_btn_frame, text="Select All", command=self.select_all_devices, width=10).pack(side=tk.LEFT, padx=2)
        tk.Button(device_btn_frame, text="Clear All", command=self.clear_all_devices, width=10).pack(side=tk.LEFT, padx=2)

        # Direction filter
        direction_frame = tk.Frame(filter_frame, bg="#f5f5f5")
        direction_frame.pack(side=tk.LEFT, fill=tk.Y, padx=10, pady=5)

        tk.Label(direction_frame, text="Direction:", bg="#f5f5f5", font=("Arial", 9, "bold")).pack(anchor=tk.W)

        for direction in ["all", "send", "received"]:
            tk.Radiobutton(
                direction_frame,
                text=direction.capitalize(),
                variable=self.selected_direction,
                value=direction,
                command=self.apply_filters,
                bg="#f5f5f5"
            ).pack(anchor=tk.W)

        # View mode
        view_frame = tk.Frame(filter_frame, bg="#f5f5f5")
        view_frame.pack(side=tk.LEFT, fill=tk.Y, padx=10, pady=5)

        tk.Label(view_frame, text="View Mode:", bg="#f5f5f5", font=("Arial", 9, "bold")).pack(anchor=tk.W)

        for mode in [("Global", "global"), ("Per Device", "per_node"), ("Both", "both")]:
            tk.Radiobutton(
                view_frame,
                text=mode[0],
                variable=self.view_mode,
                value=mode[1],
                command=self.refresh_analysis,
                bg="#f5f5f5"
            ).pack(anchor=tk.W)

        # Main content area - Notebook with tabs
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=10, pady=5)

        # Tab 1: Overview with graphs
        self.overview_tab = tk.Frame(self.notebook)
        self.notebook.add(self.overview_tab, text="Overview & Graphs")

        # Create matplotlib figure for graphs
        self.fig_overview = Figure(figsize=(16, 8), dpi=100)
        self.canvas_overview = FigureCanvasTkAgg(self.fig_overview, master=self.overview_tab)
        self.canvas_overview.draw()
        self.canvas_overview.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        # Tab 2: Statistics table
        self.stats_tab = tk.Frame(self.notebook)
        self.notebook.add(self.stats_tab, text="Statistics Table")

        # Create treeview for statistics
        tree_frame = tk.Frame(self.stats_tab)
        tree_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        tree_scroll_y = tk.Scrollbar(tree_frame)
        tree_scroll_y.pack(side=tk.RIGHT, fill=tk.Y)

        tree_scroll_x = tk.Scrollbar(tree_frame, orient=tk.HORIZONTAL)
        tree_scroll_x.pack(side=tk.BOTTOM, fill=tk.X)

        columns = ("Device", "Type", "Count", "Avg Period (s)", "Std Dev (s)",
                   "Min (s)", "Max (s)", "Jitter", "CV", "Freq (msg/s)")

        self.stats_tree = ttk.Treeview(
            tree_frame,
            columns=columns,
            show="headings",
            yscrollcommand=tree_scroll_y.set,
            xscrollcommand=tree_scroll_x.set
        )

        for col in columns:
            self.stats_tree.heading(col, text=col, command=lambda c=col: self.sort_treeview(c))
            self.stats_tree.column(col, width=100)

        self.stats_tree.pack(fill=tk.BOTH, expand=True)

        tree_scroll_y.config(command=self.stats_tree.yview)
        tree_scroll_x.config(command=self.stats_tree.xview)

        # Export button
        export_frame = tk.Frame(self.stats_tab)
        export_frame.pack(fill=tk.X, padx=10, pady=5)
        tk.Button(export_frame, text="Export to CSV", command=self.export_to_csv).pack(side=tk.LEFT)

        # Tab 3: Timeline
        self.timeline_tab = tk.Frame(self.notebook)
        self.notebook.add(self.timeline_tab, text="Message Timeline")

        self.fig_timeline = Figure(figsize=(16, 8), dpi=100)
        self.canvas_timeline = FigureCanvasTkAgg(self.fig_timeline, master=self.timeline_tab)
        self.canvas_timeline.draw()
        self.canvas_timeline.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        # Status bar
        self.status_label = tk.Label(self.root, text="Ready - Please load log files", bd=1, relief=tk.SUNKEN, anchor=tk.W)
        self.status_label.pack(side=tk.BOTTOM, fill=tk.X)

    def browse_files(self):
        """Browse for log file directory"""
        directory = filedialog.askdirectory(title="Select Directory with .ans Log Files")
        if directory:
            self.load_directory(directory)

    def load_directory(self, dir_path: str):
        """Load all .ans files from directory"""
        log_files = glob.glob(f"{dir_path}/*.ans")

        if not log_files:
            self.status_label.config(text=f"No .ans files found in {dir_path}")
            return

        # Parse all log files
        self.all_messages = self.parser.parse_multiple_files(log_files)

        if not self.all_messages:
            self.status_label.config(text=f"No messages found in log files")
            return

        self.dir_label.config(text=f"{len(log_files)} files, {len(self.all_messages)} messages", fg="black")

        # Update device list
        devices = sorted(set(msg.device_id for msg in self.all_messages if msg.device_id))
        self.device_listbox.delete(0, tk.END)
        for device in devices:
            self.device_listbox.insert(tk.END, device)
            self.device_listbox.selection_set(tk.END)  # Select all by default

        self.status_label.config(
            text=f"Loaded {len(self.all_messages)} messages from {len(log_files)} files - "
            f"{len(devices)} devices found"
        )

        # Apply filters and refresh
        self.apply_filters()
        self.refresh_analysis()

    def select_all_types(self):
        """Select all message types"""
        for var in self.type_vars.values():
            var.set(True)
        self.apply_filters()

    def clear_all_types(self):
        """Clear all message type selections"""
        for var in self.type_vars.values():
            var.set(False)
        self.apply_filters()

    def select_all_devices(self):
        """Select all devices"""
        self.device_listbox.selection_set(0, tk.END)
        self.apply_filters()

    def clear_all_devices(self):
        """Clear all device selections"""
        self.device_listbox.selection_clear(0, tk.END)
        self.apply_filters()

    def apply_filters(self):
        """Apply current filters to messages"""
        if not self.all_messages:
            return

        # Get selected types
        selected_types = [type_name for type_name, var in self.type_vars.items() if var.get()]

        # Get selected devices
        selected_indices = self.device_listbox.curselection()
        selected_devices = [self.device_listbox.get(i) for i in selected_indices]

        # Get selected direction
        direction = self.selected_direction.get()

        # Filter messages
        filtered = self.all_messages

        if selected_types:
            filtered = [msg for msg in filtered if msg.packet_type_name in selected_types]

        if selected_devices:
            filtered = [msg for msg in filtered if msg.device_id in selected_devices]

        if direction != "all":
            filtered = [msg for msg in filtered if msg.direction == direction]

        self.filtered_messages = filtered

        # Build detailed status message showing which filters are active
        type_str = "All"
        if selected_types and len(selected_types) < len(self.type_vars):
            if len(selected_types) <= 3:
                type_str = ", ".join(selected_types)
            else:
                type_str = ", ".join(selected_types[:3]) + f" +{len(selected_types)-3} more"

        device_str = "All"
        if selected_devices:
            if len(selected_devices) <= 3:
                device_str = ", ".join(selected_devices)
            else:
                device_str = ", ".join(selected_devices[:3]) + f" +{len(selected_devices)-3} more"

        direction_str = direction.capitalize() if direction != "all" else "All"

        self.status_label.config(
            text=f"Showing {len(self.filtered_messages)} messages | "
                 f"Types: {type_str} | Devices: {device_str} | Direction: {direction_str}"
        )

        # Auto-refresh visualizations when filters change
        self.refresh_analysis()

    def refresh_analysis(self):
        """Refresh all visualizations and statistics"""
        if not self.filtered_messages:
            return

        # Update all tabs
        self.update_overview_graphs()
        self.update_statistics_table()
        self.update_timeline()

    def update_overview_graphs(self):
        """Update overview graphs"""
        self.fig_overview.clear()

        view_mode = self.view_mode.get()

        if view_mode == "global":
            # Global view - 2x2 grid
            axes = [
                self.fig_overview.add_subplot(2, 2, 1),
                self.fig_overview.add_subplot(2, 2, 2),
                self.fig_overview.add_subplot(2, 2, 3),
                self.fig_overview.add_subplot(2, 2, 4)
            ]
            self._plot_global_analysis(axes)

        elif view_mode == "per_node":
            # Per-device view
            devices = sorted(set(msg.device_id for msg in self.filtered_messages if msg.device_id))
            num_devices = len(devices)

            if num_devices == 0:
                return

            # Create subplots (2 columns)
            rows = (num_devices + 1) // 2
            for i, device in enumerate(devices):
                ax = self.fig_overview.add_subplot(rows, 2, i + 1)
                device_messages = [msg for msg in self.filtered_messages if msg.device_id == device]
                self._plot_device_analysis(ax, device, device_messages)

        else:  # both
            # Combined view - top global, bottom per-device comparison
            ax_hist = self.fig_overview.add_subplot(2, 2, 1)
            ax_freq = self.fig_overview.add_subplot(2, 2, 2)
            ax_comparison = self.fig_overview.add_subplot(2, 1, 2)

            self._plot_global_analysis([ax_hist, ax_freq, None, None])
            self._plot_device_comparison(ax_comparison)

        self.fig_overview.tight_layout()
        self.canvas_overview.draw()

    def _plot_global_analysis(self, axes):
        """Plot global analysis graphs"""
        metrics = self.calculator.calculate_metrics(self.filtered_messages)

        if not metrics:
            return

        # Period distribution histogram
        if axes[0]:
            bins = list(metrics.period_distribution.keys())
            counts = list(metrics.period_distribution.values())
            axes[0].bar(range(len(bins)), counts, tick_label=bins)
            axes[0].set_xlabel("Period Range")
            axes[0].set_ylabel("Count")
            axes[0].set_title("Period Distribution")
            axes[0].tick_params(axis='x', rotation=45)

        # Frequency over time
        if axes[1]:
            timestamps, frequencies = self.calculator.get_timeline_data(self.filtered_messages, window_size_sec=30.0)
            if timestamps:
                axes[1].plot(timestamps, frequencies, marker='o', markersize=3)
                axes[1].set_xlabel("Time (s)")
                axes[1].set_ylabel("Frequency (msg/s)")
                axes[1].set_title("Message Frequency Over Time (30s windows)")
                axes[1].grid(True, alpha=0.3)

        # Inter-arrival time scatter
        if axes[2]:
            iat = metrics.inter_arrival_times[:1000]  # Limit to first 1000
            axes[2].scatter(range(len(iat)), iat, alpha=0.5, s=10)
            axes[2].axhline(y=metrics.avg_period, color='r', linestyle='--', label=f'Mean: {metrics.avg_period:.3f}s')
            axes[2].set_xlabel("Message Index")
            axes[2].set_ylabel("Inter-Arrival Time (s)")
            axes[2].set_title("Inter-Arrival Times (first 1000)")
            axes[2].legend()
            axes[2].grid(True, alpha=0.3)

        # Summary statistics text
        if axes[3]:
            axes[3].axis('off')
            summary_text = (
                f"Global Statistics\n\n"
                f"Total Messages: {metrics.message_count}\n"
                f"Duration: {metrics.total_duration_sec:.2f}s\n\n"
                f"Avg Period: {metrics.avg_period:.3f}s\n"
                f"Std Dev: {metrics.std_dev:.3f}s\n"
                f"Min Period: {metrics.min_period:.3f}s\n"
                f"Max Period: {metrics.max_period:.3f}s\n"
                f"Median: {metrics.median_period:.3f}s\n\n"
                f"Jitter: {metrics.jitter:.3f}s²\n"
                f"CV: {metrics.coefficient_of_variation:.3f}\n\n"
                f"Avg Frequency: {metrics.avg_frequency:.3f} msg/s"
            )
            axes[3].text(0.1, 0.5, summary_text, fontsize=11, family='monospace',
                        verticalalignment='center')

    def _plot_device_analysis(self, ax, device, messages):
        """Plot analysis for a single device"""
        metrics = self.calculator.calculate_metrics(messages)

        if not metrics:
            return

        # Plot inter-arrival times
        iat = metrics.inter_arrival_times[:200]  # Limit display
        ax.plot(iat, marker='o', markersize=2, alpha=0.7)
        ax.axhline(y=metrics.avg_period, color='r', linestyle='--', linewidth=1)
        ax.set_title(f"{device}: Avg={metrics.avg_period:.3f}s, CV={metrics.coefficient_of_variation:.2f}")
        ax.set_xlabel("Message Index")
        ax.set_ylabel("Inter-Arrival Time (s)")
        ax.grid(True, alpha=0.3)

    def _plot_device_comparison(self, ax):
        """Plot comparison bar chart of devices"""
        per_device = self.calculator.calculate_per_device(self.filtered_messages)

        if not per_device:
            return

        devices = sorted(per_device.keys())
        avg_periods = [per_device[d].avg_period for d in devices]
        std_devs = [per_device[d].std_dev for d in devices]

        x = range(len(devices))
        colors = [DEVICE_COLORS.get(d, '#3498db') for d in devices]

        ax.bar(x, avg_periods, yerr=std_devs, tick_label=devices, color=colors, alpha=0.7, capsize=5)
        ax.set_xlabel("Device")
        ax.set_ylabel("Average Period (s)")
        ax.set_title("Average Message Period by Device")
        ax.grid(True, axis='y', alpha=0.3)

    def update_statistics_table(self):
        """Update statistics table"""
        # Clear existing data
        for item in self.stats_tree.get_children():
            self.stats_tree.delete(item)

        view_mode = self.view_mode.get()

        if view_mode == "global":
            # Global statistics
            metrics = self.calculator.calculate_metrics(self.filtered_messages)
            if metrics:
                self.stats_tree.insert("", tk.END, values=(
                    "ALL",
                    "ALL",
                    metrics.message_count,
                    f"{metrics.avg_period:.4f}",
                    f"{metrics.std_dev:.4f}",
                    f"{metrics.min_period:.4f}",
                    f"{metrics.max_period:.4f}",
                    f"{metrics.jitter:.4f}",
                    f"{metrics.coefficient_of_variation:.3f}",
                    f"{metrics.avg_frequency:.4f}"
                ))

        elif view_mode == "per_node":
            # Per-device statistics
            per_device = self.calculator.calculate_per_device(self.filtered_messages)
            for device, metrics in sorted(per_device.items()):
                self.stats_tree.insert("", tk.END, values=(
                    device,
                    "ALL",
                    metrics.message_count,
                    f"{metrics.avg_period:.4f}",
                    f"{metrics.std_dev:.4f}",
                    f"{metrics.min_period:.4f}",
                    f"{metrics.max_period:.4f}",
                    f"{metrics.jitter:.4f}",
                    f"{metrics.coefficient_of_variation:.3f}",
                    f"{metrics.avg_frequency:.4f}"
                ))

        else:  # both
            # Per-device and per-type
            per_device_type = self.calculator.calculate_per_device_and_type(self.filtered_messages)
            for (device, packet_type), metrics in sorted(per_device_type.items()):
                self.stats_tree.insert("", tk.END, values=(
                    device,
                    packet_type,
                    metrics.message_count,
                    f"{metrics.avg_period:.4f}",
                    f"{metrics.std_dev:.4f}",
                    f"{metrics.min_period:.4f}",
                    f"{metrics.max_period:.4f}",
                    f"{metrics.jitter:.4f}",
                    f"{metrics.coefficient_of_variation:.3f}",
                    f"{metrics.avg_frequency:.4f}"
                ))

    def update_timeline(self):
        """Update timeline visualization"""
        self.fig_timeline.clear()

        ax = self.fig_timeline.add_subplot(1, 1, 1)

        # Group messages by type
        by_type = {}
        for msg in self.filtered_messages:
            if msg.packet_type_name not in by_type:
                by_type[msg.packet_type_name] = []
            by_type[msg.packet_type_name].append(msg)

        # Plot timeline for each type
        colors = plt.cm.tab10(range(len(by_type)))

        for i, (type_name, messages) in enumerate(sorted(by_type.items())):
            times = [msg.uptime_ms / 1000.0 for msg in messages]
            y_values = [i] * len(times)
            ax.scatter(times, y_values, label=type_name, alpha=0.6, s=20, color=colors[i])

        ax.set_xlabel("Time (s)")
        ax.set_ylabel("Message Type")
        ax.set_yticks(range(len(by_type)))
        ax.set_yticklabels(sorted(by_type.keys()))
        ax.set_title("Message Timeline by Type")
        ax.legend(loc='upper right', bbox_to_anchor=(1.15, 1))
        ax.grid(True, axis='x', alpha=0.3)

        self.fig_timeline.tight_layout()
        self.canvas_timeline.draw()

    def sort_treeview(self, col):
        """Sort treeview by column"""
        # Get all items
        items = [(self.stats_tree.set(item, col), item) for item in self.stats_tree.get_children('')]

        # Sort items
        try:
            items.sort(key=lambda x: float(x[0]))
        except ValueError:
            items.sort(key=lambda x: x[0])

        # Rearrange items
        for index, (val, item) in enumerate(items):
            self.stats_tree.move(item, '', index)

    def export_to_csv(self):
        """Export statistics table to CSV"""
        filename = filedialog.asksaveasfilename(
            title="Save Statistics as CSV",
            defaultextension=".csv",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")]
        )

        if not filename:
            return

        try:
            with open(filename, 'w') as f:
                # Write header
                columns = self.stats_tree["columns"]
                f.write(",".join(columns) + "\n")

                # Write data
                for item in self.stats_tree.get_children():
                    values = [str(self.stats_tree.set(item, col)) for col in columns]
                    f.write(",".join(values) + "\n")

            self.status_label.config(text=f"Exported to {filename}")
        except Exception as e:
            self.status_label.config(text=f"Export failed: {e}")

    def run(self):
        """Start the application"""
        self.root.mainloop()


def main():
    """Main entry point"""
    app = MessagePeriodicityAnalyzer()
    app.run()


if __name__ == "__main__":
    main()
