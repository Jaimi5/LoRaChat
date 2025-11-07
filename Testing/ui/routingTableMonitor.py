"""
Routing Table Monitor - Real-Time Visualization UI

Main Tkinter application for monitoring and visualizing routing table changes in real-time.
"""

import tkinter as tk
from tkinter import filedialog, ttk
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
import glob
from pathlib import Path
from typing import List, Dict, Tuple
import sys
import os

# Add current directory to path
sys.path.insert(0, os.path.dirname(__file__))

from logParser import parse_log_file, parse_log_lines, RoutingTableSnapshot, RouteTimeoutEvent, RouteEntry
from fileWatcher import FileWatcher
from networkRenderer import render_network_graph, render_color_legend


class RoutingTableMonitor:
    """Main UI for routing table monitoring"""

    def __init__(self):
        self.root = tk.Tk()
        self.root.title("LoRaMesher Routing Table Monitor")
        self.root.geometry("1400x900")

        # Data storage
        self.snapshots: List[RoutingTableSnapshot] = []
        self.timeout_events: List[RouteTimeoutEvent] = []
        self.monitoring_dir = None
        self.file_watcher = None

        # Auto-refresh state
        self.auto_refresh_enabled = tk.BooleanVar(value=False)
        self.update_interval = 500  # milliseconds

        # Timeline navigation
        self.current_snapshot_index = 0
        self.snapshots_by_time = []  # Sorted list of snapshot groups by time
        self.timeouts_by_time = []   # Sorted list of timeout event groups by time

        # Build UI
        self.build_ui()

    def build_ui(self):
        """Build the user interface"""
        # Top control panel
        control_frame = tk.Frame(self.root)
        control_frame.pack(side=tk.TOP, fill=tk.X, padx=10, pady=5)

        tk.Label(control_frame, text="Monitoring Directory:").pack(side=tk.LEFT, padx=5)

        self.dir_label = tk.Label(control_frame, text="No directory selected", fg="gray")
        self.dir_label.pack(side=tk.LEFT, padx=5)

        tk.Button(control_frame, text="Browse...", command=self.browse_directory).pack(side=tk.LEFT, padx=5)

        tk.Checkbutton(
            control_frame,
            text="Auto-Refresh",
            variable=self.auto_refresh_enabled,
            command=self.toggle_auto_refresh
        ).pack(side=tk.LEFT, padx=15)

        tk.Button(control_frame, text="Refresh Now", command=self.manual_refresh).pack(side=tk.LEFT, padx=5)

        # Timeline navigation controls
        timeline_frame = tk.Frame(self.root, bg="#f0f0f0")
        timeline_frame.pack(side=tk.TOP, fill=tk.X, padx=10, pady=5)

        tk.Label(timeline_frame, text="Timeline:", bg="#f0f0f0").pack(side=tk.LEFT, padx=5)

        tk.Button(timeline_frame, text="◀◀ Start", command=self.goto_start).pack(side=tk.LEFT, padx=2)
        tk.Button(timeline_frame, text="◀ Previous", command=self.goto_previous).pack(side=tk.LEFT, padx=2)

        self.timeline_label = tk.Label(timeline_frame, text="No data", bg="#f0f0f0", width=30)
        self.timeline_label.pack(side=tk.LEFT, padx=10)

        tk.Button(timeline_frame, text="Next ▶", command=self.goto_next).pack(side=tk.LEFT, padx=2)
        tk.Button(timeline_frame, text="End ▶▶", command=self.goto_end).pack(side=tk.LEFT, padx=2)

        # Slider for timeline
        self.timeline_slider = tk.Scale(
            timeline_frame,
            from_=0,
            to=100,
            orient=tk.HORIZONTAL,
            command=self.on_slider_change,
            showvalue=False,
            length=200
        )
        self.timeline_slider.pack(side=tk.LEFT, padx=10)

        # Main content area - split horizontally
        content_frame = tk.Frame(self.root)
        content_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=10, pady=5)

        # Left side - Network graph
        left_frame = tk.Frame(content_frame)
        left_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        tk.Label(left_frame, text="Network Topology", font=("Arial", 12, "bold")).pack()

        # Create matplotlib figure for network graph
        self.fig = Figure(figsize=(8, 6), dpi=100)
        self.ax_graph = self.fig.add_subplot(121)
        self.ax_legend = self.fig.add_subplot(122)

        self.canvas = FigureCanvasTkAgg(self.fig, master=left_frame)
        self.canvas.draw()
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        # Right side - Routing table text view
        right_frame = tk.Frame(content_frame, width=400)
        right_frame.pack(side=tk.RIGHT, fill=tk.BOTH)

        tk.Label(right_frame, text="Routing Tables", font=("Arial", 12, "bold")).pack()

        # Text widget with scrollbar
        text_frame = tk.Frame(right_frame)
        text_frame.pack(fill=tk.BOTH, expand=True)

        scrollbar = tk.Scrollbar(text_frame)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        self.text_widget = tk.Text(text_frame, wrap=tk.WORD, yscrollcommand=scrollbar.set, font=("Courier", 9))
        self.text_widget.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        scrollbar.config(command=self.text_widget.yview)

        # Status bar
        self.status_label = tk.Label(self.root, text="Ready", bd=1, relief=tk.SUNKEN, anchor=tk.W)
        self.status_label.pack(side=tk.BOTTOM, fill=tk.X)

        # Initial render
        self.update_visualization()

    def browse_directory(self):
        """Browse for monitoring directory"""
        directory = filedialog.askdirectory(title="Select Monitoring Directory")
        if directory:
            self.load_directory(directory)

    def load_directory(self, dir_path: str):
        """Load all .ans files from directory"""
        self.monitoring_dir = dir_path
        self.dir_label.config(text=dir_path, fg="black")

        # Find all .ans files
        log_files = glob.glob(f"{dir_path}/*.ans")

        if not log_files:
            self.status_label.config(text=f"No .ans files found in {dir_path}")
            return

        # Parse all log files
        self.snapshots.clear()
        self.timeout_events.clear()
        for log_file in log_files:
            try:
                snapshots, timeouts = parse_log_file(log_file)
                self.snapshots.extend(snapshots)
                self.timeout_events.extend(timeouts)
            except Exception as e:
                print(f"Error parsing {log_file}: {e}")

        self.status_label.config(text=f"Loaded {len(self.snapshots)} snapshots and {len(self.timeout_events)} timeouts from {len(log_files)} files")
        self.update_visualization()

        # Start file watcher if auto-refresh is enabled
        if self.auto_refresh_enabled.get():
            self.start_file_watcher(log_files)

    def toggle_auto_refresh(self):
        """Toggle auto-refresh on/off"""
        if self.auto_refresh_enabled.get():
            if self.monitoring_dir:
                log_files = glob.glob(f"{self.monitoring_dir}/*.ans")
                self.start_file_watcher(log_files)
        else:
            self.stop_file_watcher()

    def start_file_watcher(self, log_files: List[str]):
        """Start watching log files for changes"""
        self.stop_file_watcher()  # Stop existing watcher if any

        def on_new_content(new_lines: List[str], file_path: str):
            """Callback when new content is detected"""
            try:
                new_snapshots, new_timeouts = parse_log_lines(new_lines, file_path)
                if new_snapshots or new_timeouts:
                    self.snapshots.extend(new_snapshots)
                    self.timeout_events.extend(new_timeouts)
                    # Schedule UI update on main thread
                    self.root.after(0, self.update_visualization)
            except Exception as e:
                print(f"Error processing new lines: {e}")

        self.file_watcher = FileWatcher(log_files, on_new_content, interval=self.update_interval / 1000)
        self.file_watcher.start()

        self.status_label.config(text=f"Monitoring {len(log_files)} files (auto-refresh every {self.update_interval}ms)")

    def stop_file_watcher(self):
        """Stop file watcher"""
        if self.file_watcher:
            self.file_watcher.stop()
            self.file_watcher = None

    def manual_refresh(self):
        """Manually refresh data"""
        if self.monitoring_dir:
            self.load_directory(self.monitoring_dir)

    def update_visualization(self):
        """Update network graph and routing table text"""
        # Group snapshots by time for timeline navigation
        self._update_timeline()

        # Get cumulative snapshots for current timeline position
        current_snapshots = self._get_current_snapshots()

        # Get timeout events at current time
        current_timeouts = self._get_current_timeouts()

        # Update graph
        render_network_graph(current_snapshots, self.ax_graph, current_timeouts)
        render_color_legend(self.ax_legend)
        self.canvas.draw()

        # Update text view
        self.update_text_view(current_snapshots)

    def _update_timeline(self):
        """Group snapshots and timeouts by unique timestamps for timeline navigation"""
        if not self.snapshots and not self.timeout_events:
            self.snapshots_by_time = []
            self.timeouts_by_time = []
            self.timeline_slider.config(to=0)
            self.timeline_label.config(text="No data")
            return

        # Collect all unique time keys
        all_time_keys = set()

        # Group snapshots by uptime (rounded to nearest second)
        snapshot_time_groups = {}
        for snapshot in self.snapshots:
            time_key = snapshot.uptime_ms // 1000
            all_time_keys.add(time_key)
            if time_key not in snapshot_time_groups:
                snapshot_time_groups[time_key] = []
            snapshot_time_groups[time_key].append(snapshot)

        # Group timeout events by uptime
        timeout_time_groups = {}
        for timeout in self.timeout_events:
            time_key = timeout.uptime_ms // 1000
            all_time_keys.add(time_key)
            if time_key not in timeout_time_groups:
                timeout_time_groups[time_key] = []
            timeout_time_groups[time_key].append(timeout)

        # Create sorted list of all time points
        sorted_times = sorted(all_time_keys)

        # Build time-indexed lists
        self.snapshots_by_time = [snapshot_time_groups.get(t, []) for t in sorted_times]
        self.timeouts_by_time = [timeout_time_groups.get(t, []) for t in sorted_times]

        # Update slider range
        max_index = len(sorted_times) - 1
        self.timeline_slider.config(to=max(1, max_index))

        # Clamp current index
        if self.current_snapshot_index >= len(sorted_times):
            self.current_snapshot_index = max(0, len(sorted_times) - 1)

        # Update timeline label
        self._update_timeline_label()

    def _get_current_snapshots(self) -> List[RoutingTableSnapshot]:
        """Get cumulative routing state at current timeline position"""
        if not self.snapshots_by_time:
            return []

        if self.current_snapshot_index >= len(self.snapshots_by_time):
            self.current_snapshot_index = len(self.snapshots_by_time) - 1

        # Build cumulative state up to current index
        return self._build_cumulative_state(self.current_snapshot_index)

    def _get_current_timeouts(self) -> List[RouteTimeoutEvent]:
        """Get timeout events at exact current timeline position"""
        if not self.timeouts_by_time:
            return []

        if self.current_snapshot_index >= len(self.timeouts_by_time):
            return []

        return self.timeouts_by_time[self.current_snapshot_index]

    def _build_cumulative_state(self, up_to_index: int) -> List[RoutingTableSnapshot]:
        """
        Build cumulative routing state by merging all snapshots and applying timeouts
        up to the given timeline index.

        Routes are added from routing table snapshots and removed by timeout events.
        """
        # State: {(device_id, target_address): RouteEntry}
        route_state: Dict[Tuple[str, str], RouteEntry] = {}

        # Device metadata: {device_id: (com_port, last_timestamp, last_uptime)}
        device_meta: Dict[str, Tuple[str, str, int]] = {}

        # Process all events up to and including current index
        for i in range(up_to_index + 1):
            # Process routing table snapshots
            for snapshot in self.snapshots_by_time[i]:
                device_id = snapshot.device_id

                # Update device metadata
                device_meta[device_id] = (snapshot.com_port, snapshot.timestamp, snapshot.uptime_ms)

                # Add/update routes from this snapshot
                for route in snapshot.routes:
                    key = (device_id, route.address)
                    route_state[key] = route

            # Process timeout events - mark routes as timed out instead of removing
            for timeout in self.timeouts_by_time[i]:
                key = (timeout.device_id, timeout.timed_out_address)
                if key in route_state:
                    # Create a new RouteEntry with is_timed_out flag set
                    old_route = route_state[key]
                    route_state[key] = RouteEntry(
                        index=old_route.index,
                        address=old_route.address,
                        via=old_route.via,
                        total_etx=old_route.total_etx,
                        reverse_etx=old_route.reverse_etx,
                        forward_etx=old_route.forward_etx,
                        asymmetry=old_route.asymmetry,
                        hops=old_route.hops,
                        role=old_route.role,
                        link_quality=old_route.link_quality,
                        is_timed_out=True
                    )

        # Convert state back to RoutingTableSnapshot objects
        # Group routes by device
        routes_by_device: Dict[str, List[RouteEntry]] = {}
        for (device_id, target_addr), route in route_state.items():
            if device_id not in routes_by_device:
                routes_by_device[device_id] = []
            routes_by_device[device_id].append(route)

        # Create synthetic snapshots
        cumulative_snapshots = []
        for device_id, routes in routes_by_device.items():
            com_port, timestamp, uptime_ms = device_meta.get(device_id, ("UNKNOWN", "00:00:00.000", 0))

            snapshot = RoutingTableSnapshot(
                device_id=device_id,
                com_port=com_port,
                timestamp=timestamp,
                uptime_ms=uptime_ms,
                routes=routes
            )
            cumulative_snapshots.append(snapshot)

        return cumulative_snapshots

    def _update_timeline_label(self):
        """Update the timeline label with current position info"""
        if not self.snapshots_by_time:
            self.timeline_label.config(text="No data")
            return

        current_snapshots = self._get_current_snapshots()
        if current_snapshots:
            timestamp = current_snapshots[0].timestamp
            uptime = current_snapshots[0].uptime_ms / 1000
            total = len(self.snapshots_by_time)
            self.timeline_label.config(text=f"Time: {timestamp} ({uptime:.1f}s) - {self.current_snapshot_index + 1}/{total}")
        else:
            self.timeline_label.config(text="No data")

    def goto_start(self):
        """Go to first snapshot"""
        self.current_snapshot_index = 0
        self.timeline_slider.set(0)
        self.update_visualization()

    def goto_previous(self):
        """Go to previous snapshot"""
        if self.current_snapshot_index > 0:
            self.current_snapshot_index -= 1
            self.timeline_slider.set(self.current_snapshot_index)
            self.update_visualization()

    def goto_next(self):
        """Go to next snapshot"""
        if self.snapshots_by_time and self.current_snapshot_index < len(self.snapshots_by_time) - 1:
            self.current_snapshot_index += 1
            self.timeline_slider.set(self.current_snapshot_index)
            self.update_visualization()

    def goto_end(self):
        """Go to last snapshot"""
        if self.snapshots_by_time:
            self.current_snapshot_index = len(self.snapshots_by_time) - 1
            self.timeline_slider.set(self.current_snapshot_index)
            self.update_visualization()

    def on_slider_change(self, value):
        """Handle slider movement"""
        new_index = int(float(value))
        if new_index != self.current_snapshot_index:
            self.current_snapshot_index = new_index
            self.update_visualization()

    def update_text_view(self, snapshots: List[RoutingTableSnapshot]):
        """Update routing table text display"""
        self.text_widget.delete('1.0', tk.END)

        if not snapshots:
            self.text_widget.insert(tk.END, "No routing table data available.\n")
            return

        # Group snapshots by COM port
        by_com_port: Dict[str, RoutingTableSnapshot] = {}
        for snapshot in snapshots:
            com_port = snapshot.com_port
            by_com_port[com_port] = snapshot

        # Display routing tables
        for com_port, snapshot in sorted(by_com_port.items()):
            device_id = snapshot.device_id if snapshot.device_id != "UNKNOWN" else "?"

            self.text_widget.insert(tk.END, f"\n{'='*60}\n", 'header')
            self.text_widget.insert(tk.END, f"{com_port} (Device: {device_id})\n", 'header')
            self.text_widget.insert(tk.END, f"Timestamp: {snapshot.timestamp}  Uptime: {snapshot.uptime_ms/1000:.1f}s\n", 'header')
            self.text_widget.insert(tk.END, f"{'='*60}\n\n", 'header')

            if not snapshot.routes:
                self.text_widget.insert(tk.END, "  (No routes)\n\n")
                continue

            for route in snapshot.routes:
                role_str = " [Gateway]" if route.role == 1 else ""
                self.text_widget.insert(tk.END, f"  {route.index}. ", 'normal')
                self.text_widget.insert(tk.END, f"{route.address}", 'bold')
                self.text_widget.insert(tk.END, f" via {route.via}{role_str}\n", 'normal')

                # ETX info
                etx_str = f"      ETX={route.total_etx:.1f} (R:{route.reverse_etx:.1f} + F:{route.forward_etx:.1f})"
                etx_str += f"  asym:{route.asymmetry:.2f}  hops:{route.hops}\n"
                self.text_widget.insert(tk.END, etx_str, 'etx')

                # Link quality if available
                if route.link_quality:
                    lq = route.link_quality
                    lq_str = f"      └─ Direct: hellos={lq.hellos_received}/{lq.hellos_expected} ({lq.hello_rate:.1f}%)  SNR={lq.snr}\n"
                    self.text_widget.insert(tk.END, lq_str, 'link_quality')

                self.text_widget.insert(tk.END, "\n")

        # Configure text tags for coloring
        self.text_widget.tag_config('header', foreground='blue', font=("Courier", 9, "bold"))
        self.text_widget.tag_config('bold', foreground='black', font=("Courier", 9, "bold"))
        self.text_widget.tag_config('etx', foreground='darkgreen')
        self.text_widget.tag_config('link_quality', foreground='gray')

    def run(self):
        """Start the UI main loop"""
        self.root.mainloop()

        # Cleanup
        self.stop_file_watcher()


def main():
    """Main entry point"""
    monitor = RoutingTableMonitor()

    # If directory provided as argument, load it
    if len(sys.argv) > 1:
        monitor.load_directory(sys.argv[1])

    monitor.run()


if __name__ == "__main__":
    main()
