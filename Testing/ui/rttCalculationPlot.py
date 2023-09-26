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
) = extract_rtt_values_final_pattern("monitor_112825_COM31.txt")
(
    rtt_values_file2,
    srtt_values_file2,
    rttvar_values_file2,
) = extract_rtt_values_final_pattern("monitor_112826_COM10.txt")

# Plotting the RTT, SRTT, and RTTVAR values
plt.figure(figsize=(18, 10))

# Plotting for monitor_112825_COM31
plt.plot(rtt_values_file1, label="RTT (monitor_112825_COM31)", color="blue", alpha=0.7)
plt.plot(
    srtt_values_file1,
    label="SRTT (monitor_112825_COM31)",
    linestyle="--",
    color="cyan",
    alpha=0.7,
)
plt.plot(
    rttvar_values_file1,
    label="RTTVAR (monitor_112825_COM31)",
    linestyle=":",
    color="darkblue",
    alpha=0.7,
)

# Plotting for monitor_112826_COM10
plt.plot(rtt_values_file2, label="RTT (monitor_112826_COM10)", color="red", alpha=0.7)
plt.plot(
    srtt_values_file2,
    label="SRTT (monitor_112826_COM10)",
    linestyle="--",
    color="pink",
    alpha=0.7,
)
plt.plot(
    rttvar_values_file2,
    label="RTTVAR (monitor_112826_COM10)",
    linestyle=":",
    color="darkred",
    alpha=0.7,
)

# Adding the delay lines
plt.axhline(y=472, color="green", linestyle="--", label="472 ms (Doubled Delay Min)")
plt.axhline(y=2088, color="green", linestyle=":", label="1616 ms (Doubled Delay Max)")

plt.title("RTT, SRTT, and RTTVAR Values")
plt.xlabel("Index")
plt.ylabel("Value")
plt.legend()

plt.grid(True, which="both", linestyle="--", linewidth=0.5)
plt.tight_layout()

plt.show()
