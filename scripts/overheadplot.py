import matplotlib.pyplot as plt
import numpy as np
import math

# Payload size
payload_size = [50, 500, 10000]

# Range of MAX_PACKET_SIZE values
max_packet_sizes = np.arange(12, 222, 1)

# Overhead for each packet
overhead_per_packet = 11

# For storing results
overhead_percentages = {}

p_min = 0
p_max = 0

# Minimum and maximum packet sizes
size_min = 25
size_max = 222

# Calculate packet loss rate for each packet size
packet_loss_rates = p_min + (p_max - p_min) * (max_packet_sizes - size_min) / (
    size_max - size_min
)

# Calculate overhead for each packet loss rate
for ps in payload_size:
    overhead_percentages[ps] = []

    for i, max_packet_size in enumerate(max_packet_sizes):
        p = packet_loss_rates[i]

        # Number of packets
        n = math.ceil(ps / (max_packet_size - overhead_per_packet))
        # Total overhead
        overhead = overhead_per_packet * n * 2 + overhead_per_packet * 2
        # Total overhead
        # overhead = (
        #     overhead
        #     + n * overhead_per_packet / (1 - p)
        #     + 2 * overhead_per_packet / (1 - p)
        # )
        # Total transmitted data
        total_data = ps + overhead
        # Overhead percentage
        overhead_percentage = (overhead / total_data) * 100
        overhead_percentages[ps].append(overhead_percentage)

plt.rcParams.update({"font.size": 14})

# Plot results
plt.figure(figsize=(10, 6))
for ps in payload_size:
    plt.plot(
        max_packet_sizes,
        overhead_percentages[ps],
        label=f"Message size = {ps} Bytes",
    )

# Add vertical line at MAX_PACKET_SIZE = 100
plt.axvline(x=100, color="r", linestyle="--")

# Annotate overhead percentages at MAX_PACKET_SIZE = 100
# Create a small offset for each annotation to avoid overlap
for it, (ps) in enumerate(payload_size):
    i = np.where(max_packet_sizes == 100)[0][0]
    plt.text(
        101,
        overhead_percentages[ps][i] + 1.5,
        f"{overhead_percentages[ps][i]:.2f}%",
        color="black",
        va="center",
        fontdict={"size": 14, "weight": "bold"},
    )

plt.xlabel("MAX_PACKET_SIZE [Bytes]", fontsize=18)
plt.ylabel("Control Overhead [%]", fontsize=18)
plt.title("Control Overhead as a Function of MAX_PACKET_SIZE", fontsize=18)
plt.legend()
plt.grid(True)
plt.show()
