import re
import matplotlib.pyplot as plt


def extract_rtt_values_final_pattern(filename):
    with open(filename, "r") as file:
        content = file.readlines()

    # Final regular expression to extract RTT, SRTT, and RTTVAR values with parentheses
    pattern = re.compile(r"Updating RTT \((\d+) ms\), SRTT \((\d+)\), RTTVAR \((\d+)\)")

    rtt_values, srtt_values, rttvar_values = [], [], []

    for line in content:
        match = pattern.search(line)
        if match:
            rtt_values.append(int(match.group(1)))
            srtt_values.append(int(match.group(2)))
            rttvar_values.append(int(match.group(3)))

    return rtt_values, srtt_values, rttvar_values


# Extract RTT, SRTT, and RTTVAR values from the provided files using the final pattern
(
    rtt_values_file1,
    srtt_values_file1,
    rttvar_values_file1,
) = extract_rtt_values_final_pattern(
    "Logs\RTTTwoDevicesMonitorAndPlot\monitor_112825_COM31.txt"
)
(
    rtt_values_file2,
    srtt_values_file2,
    rttvar_values_file2,
) = extract_rtt_values_final_pattern(
    "Logs\RTTTwoDevicesMonitorAndPlot\monitor_112826_COM10.txt"
)

# Plotting the RTT, SRTT, and RTTVAR values
plt.figure(figsize=(18, 10))

# Plotting for monitor_112825_COM31
import numpy as np

# Create data for boxplots
data = [rtt_values_file1, rtt_values_file2]

# Create boxplot
# plt.boxplot(data, labels=["Device 1", "Device 2"], patch_artist=True, boxprops=dict(facecolor='blue', color='blue', alpha=0.7))

# Increase the line width
plt.rcParams["lines.linewidth"] = 2.5

# Create a line plot
plt.plot(
    rtt_values_file1,
    label="RTT (Device 1)",
    linestyle="-",
    color="red",
    alpha=0.7,
)

plt.plot(
    rtt_values_file2,
    label="RTT (Device 2)",
    linestyle="--",
    color="blue",
    alpha=0.7,
)

# plt.plot(
#     srtt_values_file2,
#     label="SRTT (Device 2)",
#     linestyle="--",
#     color="pink",
#     alpha=0.7,
# )
# plt.plot(
#     rttvar_values_file2,
#     label="RTTVAR (Device 2)",
#     linestyle=":",
#     color="darkred",
#     alpha=0.7,
# )

# Adding the delay lines
plt.axhline(
    y=472,
    color="green",
    linestyle="--",
    label="472 ms (Doubled Delay Min)",
)
plt.axhline(
    y=2088,
    color="green",
    linestyle="-",
    label="2088 ms (Doubled Delay Max)",
)
# Max time on air = 236 ms -> MToA
# Min time on air = 6 ms -> MinToA
# MToA*2 = 472 + MinToA \\ Waiting Time min random, both sides
# ([MToA*3+100(Per routing table size) -> Waiting Time max random]+MToA)*2 = 1616 * 2 = 2088 (Both ways)

# Change the size of the letters
plt.title("RTT of two devices", fontsize=40)
plt.xlabel("RTT [#]", fontsize=40)
plt.ylabel("RTT [ms]", fontsize=40)
plt.xticks(fontsize=20)
plt.yticks(fontsize=20)
plt.legend(fontsize=20)

plt.grid(True, which="both", linestyle="--", linewidth=0.5)
plt.tight_layout()

# Add log scale
plt.yscale("log")

plt.show()
