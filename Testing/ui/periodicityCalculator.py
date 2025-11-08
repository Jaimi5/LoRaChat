"""
Message Periodicity Calculator
Computes periodicity metrics from message events
"""

import numpy as np
from dataclasses import dataclass
from typing import List, Dict, Tuple, Optional
from periodicityParser import MessageEvent


@dataclass
class PeriodicityMetrics:
    """Periodicity statistics for a set of messages"""
    message_count: int
    total_duration_sec: float  # Total time span
    inter_arrival_times: List[float]  # Time between consecutive messages (seconds)

    # Statistical metrics
    avg_period: float  # Average inter-arrival time (seconds)
    std_dev: float  # Standard deviation of inter-arrival times
    min_period: float  # Minimum inter-arrival time
    max_period: float  # Maximum inter-arrival time
    median_period: float  # Median inter-arrival time
    jitter: float  # Variance in inter-arrival times
    coefficient_of_variation: float  # Std dev / mean (measure of variability)

    # Frequency metrics
    avg_frequency: float  # Messages per second
    instantaneous_frequencies: List[float]  # 1/inter_arrival_time for each interval

    # Distribution metrics
    period_distribution: Dict[str, int]  # Histogram bins

    def __str__(self):
        return (
            f"Messages: {self.message_count}\n"
            f"Duration: {self.total_duration_sec:.2f}s\n"
            f"Avg Period: {self.avg_period:.3f}s\n"
            f"Std Dev: {self.std_dev:.3f}s\n"
            f"Min/Max: {self.min_period:.3f}s / {self.max_period:.3f}s\n"
            f"Jitter: {self.jitter:.3f}s\n"
            f"CV: {self.coefficient_of_variation:.3f}\n"
            f"Avg Frequency: {self.avg_frequency:.3f} msg/s"
        )


class PeriodicityCalculator:
    """Calculator for message periodicity metrics"""

    # Period distribution bins (in seconds)
    PERIOD_BINS = [
        ("0-100ms", 0, 0.1),
        ("100-500ms", 0.1, 0.5),
        ("500ms-1s", 0.5, 1.0),
        ("1-5s", 1.0, 5.0),
        ("5-30s", 5.0, 30.0),
        ("30-60s", 30.0, 60.0),
        ("60s+", 60.0, float('inf'))
    ]

    def __init__(self):
        pass

    def calculate_metrics(self, messages: List[MessageEvent]) -> Optional[PeriodicityMetrics]:
        """
        Calculate periodicity metrics for a list of messages

        Args:
            messages: List of MessageEvent objects (should be sorted by time)

        Returns:
            PeriodicityMetrics object or None if insufficient data
        """
        if len(messages) < 2:
            return None

        # Sort messages by uptime to ensure chronological order
        sorted_messages = sorted(messages, key=lambda x: x.uptime_ms)

        # Calculate inter-arrival times
        inter_arrival_times = []
        for i in range(1, len(sorted_messages)):
            time_diff = (sorted_messages[i].uptime_ms - sorted_messages[i-1].uptime_ms) / 1000.0
            inter_arrival_times.append(time_diff)

        # Calculate total duration
        total_duration = (sorted_messages[-1].uptime_ms - sorted_messages[0].uptime_ms) / 1000.0

        # Statistical calculations
        inter_arrival_array = np.array(inter_arrival_times)
        avg_period = np.mean(inter_arrival_array)
        std_dev = np.std(inter_arrival_array)
        min_period = np.min(inter_arrival_array)
        max_period = np.max(inter_arrival_array)
        median_period = np.median(inter_arrival_array)
        jitter = np.var(inter_arrival_array)

        # Coefficient of variation (std/mean)
        coefficient_of_variation = std_dev / avg_period if avg_period > 0 else 0

        # Frequency calculations
        avg_frequency = len(messages) / total_duration if total_duration > 0 else 0

        # Instantaneous frequency (1 / inter-arrival time)
        instantaneous_frequencies = [1.0 / iat if iat > 0 else 0 for iat in inter_arrival_times]

        # Period distribution
        period_distribution = self._calculate_distribution(inter_arrival_times)

        return PeriodicityMetrics(
            message_count=len(messages),
            total_duration_sec=total_duration,
            inter_arrival_times=inter_arrival_times,
            avg_period=avg_period,
            std_dev=std_dev,
            min_period=min_period,
            max_period=max_period,
            median_period=median_period,
            jitter=jitter,
            coefficient_of_variation=coefficient_of_variation,
            avg_frequency=avg_frequency,
            instantaneous_frequencies=instantaneous_frequencies,
            period_distribution=period_distribution
        )

    def calculate_per_device(self, messages: List[MessageEvent]) -> Dict[str, PeriodicityMetrics]:
        """
        Calculate metrics for each device separately

        Args:
            messages: List of all MessageEvent objects

        Returns:
            Dictionary mapping device_id -> PeriodicityMetrics
        """
        # Group by device
        by_device = {}
        for msg in messages:
            if msg.device_id not in by_device:
                by_device[msg.device_id] = []
            by_device[msg.device_id].append(msg)

        # Calculate metrics for each device
        results = {}
        for device_id, device_messages in by_device.items():
            metrics = self.calculate_metrics(device_messages)
            if metrics:
                results[device_id] = metrics

        return results

    def calculate_per_type(self, messages: List[MessageEvent]) -> Dict[str, PeriodicityMetrics]:
        """
        Calculate metrics for each packet type separately

        Args:
            messages: List of all MessageEvent objects

        Returns:
            Dictionary mapping packet_type_name -> PeriodicityMetrics
        """
        # Group by type
        by_type = {}
        for msg in messages:
            type_name = msg.packet_type_name
            if type_name not in by_type:
                by_type[type_name] = []
            by_type[type_name].append(msg)

        # Calculate metrics for each type
        results = {}
        for type_name, type_messages in by_type.items():
            metrics = self.calculate_metrics(type_messages)
            if metrics:
                results[type_name] = metrics

        return results

    def calculate_per_device_and_type(
        self,
        messages: List[MessageEvent]
    ) -> Dict[Tuple[str, str], PeriodicityMetrics]:
        """
        Calculate metrics for each (device, packet_type) combination

        Args:
            messages: List of all MessageEvent objects

        Returns:
            Dictionary mapping (device_id, packet_type_name) -> PeriodicityMetrics
        """
        # Group by device and type
        by_device_type = {}
        for msg in messages:
            key = (msg.device_id, msg.packet_type_name)
            if key not in by_device_type:
                by_device_type[key] = []
            by_device_type[key].append(msg)

        # Calculate metrics for each combination
        results = {}
        for key, msgs in by_device_type.items():
            metrics = self.calculate_metrics(msgs)
            if metrics:
                results[key] = metrics

        return results

    def calculate_per_direction(self, messages: List[MessageEvent]) -> Dict[str, PeriodicityMetrics]:
        """
        Calculate metrics separately for send and received messages

        Args:
            messages: List of all MessageEvent objects

        Returns:
            Dictionary mapping direction ("send"/"received") -> PeriodicityMetrics
        """
        # Group by direction
        by_direction = {}
        for msg in messages:
            if msg.direction not in by_direction:
                by_direction[msg.direction] = []
            by_direction[msg.direction].append(msg)

        # Calculate metrics for each direction
        results = {}
        for direction, dir_messages in by_direction.items():
            metrics = self.calculate_metrics(dir_messages)
            if metrics:
                results[direction] = metrics

        return results

    def _calculate_distribution(self, inter_arrival_times: List[float]) -> Dict[str, int]:
        """Calculate histogram distribution of inter-arrival times"""
        distribution = {bin_name: 0 for bin_name, _, _ in self.PERIOD_BINS}

        for iat in inter_arrival_times:
            for bin_name, bin_min, bin_max in self.PERIOD_BINS:
                if bin_min <= iat < bin_max:
                    distribution[bin_name] += 1
                    break

        return distribution

    def get_timeline_data(
        self,
        messages: List[MessageEvent],
        window_size_sec: float = 60.0
    ) -> Tuple[List[float], List[float]]:
        """
        Get message frequency over time in sliding windows

        Args:
            messages: List of MessageEvent objects
            window_size_sec: Window size in seconds

        Returns:
            Tuple of (timestamps, frequencies) where frequencies is messages per second
        """
        if not messages:
            return [], []

        sorted_messages = sorted(messages, key=lambda x: x.uptime_ms)

        # Create time windows
        start_time = sorted_messages[0].uptime_ms / 1000.0
        end_time = sorted_messages[-1].uptime_ms / 1000.0

        timestamps = []
        frequencies = []

        current_time = start_time
        while current_time <= end_time:
            window_start = current_time
            window_end = current_time + window_size_sec

            # Count messages in this window
            count = sum(
                1 for msg in sorted_messages
                if window_start <= msg.uptime_ms / 1000.0 < window_end
            )

            frequency = count / window_size_sec
            timestamps.append(current_time)
            frequencies.append(frequency)

            current_time += window_size_sec / 2  # 50% overlap

        return timestamps, frequencies

    def export_to_dict(self, metrics: PeriodicityMetrics) -> dict:
        """Export metrics to dictionary for JSON serialization"""
        return {
            "message_count": metrics.message_count,
            "total_duration_sec": metrics.total_duration_sec,
            "avg_period": metrics.avg_period,
            "std_dev": metrics.std_dev,
            "min_period": metrics.min_period,
            "max_period": metrics.max_period,
            "median_period": metrics.median_period,
            "jitter": metrics.jitter,
            "coefficient_of_variation": metrics.coefficient_of_variation,
            "avg_frequency": metrics.avg_frequency,
            "period_distribution": metrics.period_distribution
        }


if __name__ == "__main__":
    # Test the calculator
    import sys
    import glob
    from periodicityParser import PeriodicityParser

    if len(sys.argv) > 1:
        pattern = sys.argv[1]
    else:
        pattern = "etx_validation_tests5/first/Monitoring/monitor_*.ans"

    files = glob.glob(pattern)
    print(f"Found {len(files)} files to parse")

    parser = PeriodicityParser()
    messages = parser.parse_multiple_files(files)

    print(f"\nParsed {len(messages)} message events")

    calculator = PeriodicityCalculator()

    # Global metrics
    print("\n" + "="*60)
    print("GLOBAL METRICS")
    print("="*60)
    global_metrics = calculator.calculate_metrics(messages)
    if global_metrics:
        print(global_metrics)
        print("\nPeriod Distribution:")
        for bin_name, count in global_metrics.period_distribution.items():
            percentage = (count / len(global_metrics.inter_arrival_times) * 100) if global_metrics.inter_arrival_times else 0
            print(f"  {bin_name:15s}: {count:5d} ({percentage:5.1f}%)")

    # Per-device metrics
    print("\n" + "="*60)
    print("PER-DEVICE METRICS")
    print("="*60)
    per_device = calculator.calculate_per_device(messages)
    for device_id, metrics in sorted(per_device.items()):
        print(f"\nDevice {device_id}:")
        print(metrics)

    # Per-type metrics
    print("\n" + "="*60)
    print("PER-TYPE METRICS")
    print("="*60)
    per_type = calculator.calculate_per_type(messages)
    for type_name, metrics in sorted(per_type.items()):
        print(f"\n{type_name}:")
        print(metrics)
