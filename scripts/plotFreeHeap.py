from datetime import datetime
import re
from matplotlib import pyplot as plt


def extract_time_and_heap_values(filename):
    with open(filename, "r") as file:
        content = file.readlines()

    # Regular expression to extract the time strings and heap values
    pattern = re.compile(r"(\d{2}:\d{2}:\d{2}\.\d{3}) .* FREE HEAP: (\d+)")

    times, heap_values = [], []

    for line in content:
        match = pattern.search(line)
        if match:
            # Convert time string to datetime object for plotting
            times.append(datetime.strptime(match.group(1), "%H:%M:%S.%f"))
            heap_values.append(int(match.group(2)))

    return times, heap_values


def extract_min_max_values(filename):
    with open(filename, "r") as file:
        content = file.readlines()

    # Regular expression to extract the Min and Max values
    pattern = re.compile(r"Min, Max: (\d+), (\d+)")

    min_values, max_values = [], []

    for line in content:
        match = pattern.search(line)
        if match:
            min_values.append(int(match.group(1)))
            max_values.append(int(match.group(2)))

    return min_values, max_values


# Extract Min and Max values from the provided files
min_values_file1, max_values_file1 = extract_min_max_values("monitor_112825_COM31.txt")
min_values_file2, max_values_file2 = extract_min_max_values("monitor_112826_COM10.txt")

min_values_file1[:5], max_values_file1[:5]


# Extract times and heap values from the provided files
times_file1, heap_values_file1 = extract_time_and_heap_values(
    "monitor_112825_COM31.txt"
)
times_file2, heap_values_file2 = extract_time_and_heap_values(
    "monitor_112826_COM10.txt"
)

times_file1[:5], heap_values_file1[:5]  # Displaying the first 5 entries as a sample

# Dropping the extra "Min, Max" value for the second file
min_values_file2 = min_values_file2[: len(times_file2)]
max_values_file2 = max_values_file2[: len(times_file2)]

# Plotting the heap, min, and max values against the extracted time values
plt.figure(figsize=(18, 10))

# Plotting for monitor_112825_COM31
plt.plot(
    times_file1,
    heap_values_file1,
    label="FREE HEAP (monitor_112825_COM31)",
    color="blue",
    alpha=0.7,
)
plt.plot(
    times_file1,
    min_values_file1,
    label="Min Value (monitor_112825_COM31)",
    linestyle="--",
    color="cyan",
    alpha=0.7,
)
plt.plot(
    times_file1,
    max_values_file1,
    label="Max Value (monitor_112825_COM31)",
    linestyle="--",
    color="darkblue",
    alpha=0.7,
)

# Plotting for monitor_112826_COM10
plt.plot(
    times_file2,
    heap_values_file2,
    label="FREE HEAP (monitor_112826_COM10)",
    color="red",
    alpha=0.7,
)
plt.plot(
    times_file2,
    min_values_file2,
    label="Min Value (monitor_112826_COM10)",
    linestyle="--",
    color="pink",
    alpha=0.7,
)
plt.plot(
    times_file2,
    max_values_file2,
    label="Max Value (monitor_112826_COM10)",
    linestyle="--",
    color="darkred",
    alpha=0.7,
)

plt.title("FREE HEAP, Min, and Max Values Over Time")
plt.xlabel("Time")
plt.ylabel("Value")
plt.legend()

plt.grid(True, which="both", linestyle="--", linewidth=0.5)
plt.xticks(rotation=45)
plt.tight_layout()

plt.show()
