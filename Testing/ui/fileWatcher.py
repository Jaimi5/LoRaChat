"""
File Watcher for Real-Time Log Monitoring

Monitors .ans log files for changes and triggers callbacks when new content is added.
Uses incremental reading to only process new lines, making it efficient for real-time monitoring.
"""

import os
import time
import threading
from typing import List, Callable, Dict
from pathlib import Path


class FileWatcher:
    """
    Monitor log files for changes and trigger callback with new content

    Features:
    - Monitors multiple files simultaneously
    - Incremental reading (only new lines)
    - Background thread monitoring
    - Configurable polling interval
    - Automatic file position tracking
    """

    def __init__(self, file_paths: List[str], callback: Callable[[List[str], str], None], interval: float = 0.5):
        """
        Initialize file watcher

        Args:
            file_paths: List of file paths to monitor
            callback: Function to call with (new_lines, file_path) when new content is detected
            interval: Polling interval in seconds (default: 0.5s = 500ms)
        """
        self.file_paths = [str(Path(p).resolve()) for p in file_paths]
        self.callback = callback
        self.interval = interval

        # Track last read position for each file
        self.file_positions: Dict[str, int] = {}

        # Track last modification time for each file
        self.last_modified: Dict[str, float] = {}

        # Initialize file positions
        for file_path in self.file_paths:
            if os.path.exists(file_path):
                self.file_positions[file_path] = os.path.getsize(file_path)
                self.last_modified[file_path] = os.path.getmtime(file_path)
            else:
                self.file_positions[file_path] = 0
                self.last_modified[file_path] = 0

        # Threading control
        self.running = False
        self.thread = None

    def start(self):
        """Start monitoring files in background thread"""
        if self.running:
            return

        self.running = True
        self.thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self.thread.start()

    def stop(self):
        """Stop monitoring"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=2.0)

    def add_file(self, file_path: str):
        """
        Add a new file to monitor

        Args:
            file_path: Path to file to add
        """
        file_path = str(Path(file_path).resolve())
        if file_path not in self.file_paths:
            self.file_paths.append(file_path)

            if os.path.exists(file_path):
                self.file_positions[file_path] = os.path.getsize(file_path)
                self.last_modified[file_path] = os.path.getmtime(file_path)
            else:
                self.file_positions[file_path] = 0
                self.last_modified[file_path] = 0

    def remove_file(self, file_path: str):
        """
        Remove a file from monitoring

        Args:
            file_path: Path to file to remove
        """
        file_path = str(Path(file_path).resolve())
        if file_path in self.file_paths:
            self.file_paths.remove(file_path)
            self.file_positions.pop(file_path, None)
            self.last_modified.pop(file_path, None)

    def _monitor_loop(self):
        """
        Background monitoring loop
        Runs in separate thread
        """
        while self.running:
            try:
                self._check_files()
            except Exception as e:
                # Silently continue on errors (file might not exist yet, etc.)
                pass

            time.sleep(self.interval)

    def _check_files(self):
        """Check all monitored files for changes"""
        for file_path in self.file_paths[:]:  # Copy list to avoid modification during iteration
            try:
                if not os.path.exists(file_path):
                    continue

                # Check if file has been modified
                current_mtime = os.path.getmtime(file_path)
                if current_mtime <= self.last_modified.get(file_path, 0):
                    # File hasn't been modified
                    continue

                # File has been modified - check if it has grown
                current_size = os.path.getsize(file_path)
                last_position = self.file_positions.get(file_path, 0)

                if current_size > last_position:
                    # File has grown - read new content
                    new_lines = self._read_new_content(file_path, last_position)

                    if new_lines:
                        # Update tracking
                        self.file_positions[file_path] = current_size
                        self.last_modified[file_path] = current_mtime

                        # Trigger callback
                        self.callback(new_lines, file_path)

                elif current_size < last_position:
                    # File was truncated or recreated - reset position
                    self.file_positions[file_path] = 0
                    self.last_modified[file_path] = current_mtime

            except (IOError, OSError) as e:
                # File might be temporarily locked or inaccessible
                continue

    def _read_new_content(self, file_path: str, start_position: int) -> List[str]:
        """
        Read new content from file starting at given position

        Args:
            file_path: Path to file
            start_position: Byte position to start reading from

        Returns:
            List of new lines
        """
        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                f.seek(start_position)
                new_lines = f.readlines()
                return new_lines
        except (IOError, OSError):
            return []


def watch_files(file_paths: List[str], callback: Callable[[List[str], str], None], interval: float = 0.5) -> FileWatcher:
    """
    Convenience function to create and start a file watcher

    Args:
        file_paths: List of files to monitor
        callback: Function to call with (new_lines, file_path) when changes detected
        interval: Polling interval in seconds

    Returns:
        FileWatcher instance (already started)
    """
    watcher = FileWatcher(file_paths, callback, interval)
    watcher.start()
    return watcher


if __name__ == "__main__":
    # Test the file watcher
    import sys

    if len(sys.argv) < 2:
        print("Usage: python fileWatcher.py <log_file.ans> [log_file2.ans ...]")
        sys.exit(1)

    log_files = sys.argv[1:]

    print(f"Monitoring {len(log_files)} files:")
    for f in log_files:
        print(f"  - {f}")
    print("\nWaiting for changes... (Press Ctrl+C to stop)\n")

    def on_new_content(new_lines: List[str], file_path: str):
        """Callback when new content is detected"""
        filename = Path(file_path).name
        print(f"[{filename}] {len(new_lines)} new lines:")
        for line in new_lines[:5]:  # Show first 5 lines
            print(f"  {line.rstrip()}")
        if len(new_lines) > 5:
            print(f"  ... and {len(new_lines) - 5} more lines")
        print()

    watcher = watch_files(log_files, on_new_content, interval=1.0)

    try:
        # Keep main thread alive
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping file watcher...")
        watcher.stop()
        print("Stopped.")
