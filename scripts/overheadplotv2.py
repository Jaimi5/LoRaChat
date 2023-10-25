import matplotlib.pyplot as plt
import numpy as np
import math

# Payload size
P = 10000

# Range of MAX_PACKET_SIZE values
max_packet_sizes = np.arange(12, 223, 1)

# Packet loss rates
packet_loss_rates = [0.0]

# Overhead for each packet
overhead_per_packet = 11

# For storing results
overhead_percentages = {}

# Calculate overhead for each packet loss rate
for p in packet_loss_rates:
    overhead_percentages[p] = []
    for max_packet_size in max_packet_sizes:
        # Number of packets
        n = math.ceil(P / (max_packet_size - overhead_per_packet))
        # Total number of packets
        total_packets = n * 2 + 2
        # Total overhead
        overhead = total_packets * overhead_per_packet
        # Total overhead
        # overhead = overhead + p * overhead
        # Total transmitted data
        total_data = P + overhead
        # Overhead percentage
        overhead_percentage = (overhead / total_data) * 100
        overhead_percentages[p].append(overhead_percentage)

plt.rcParams.update({"font.size": 14})

# Plot results
plt.figure(figsize=(10, 6))
for p in packet_loss_rates:
    plt.plot(max_packet_sizes, overhead_percentages[p], label=f"Packet loss rate = {p}")


# Add vertical line at MAX_PACKET_SIZE = 100
plt.axvline(x=100, color="r", linestyle="--")

# Annotate overhead percentages at MAX_PACKET_SIZE = 100
# Create a small offset for each annotation to avoid overlap
offsets = [-1.5, -0.5, 0.5, 1.5]
for it, (offset, p) in enumerate(zip(offsets, packet_loss_rates)):
    i = np.where(max_packet_sizes == 100)[0][0]
    plt.text(
        100,
        overhead_percentages[p][i] + 1,
        f"{overhead_percentages[p][i]:.2f}%",
        color="black",
        va="center",
        fontdict={"size": 14, "weight": "bold"},
    )

plt.xlabel("MAX_PACKET_SIZE (bytes)")
plt.ylabel("Control Overhead (%)")
plt.title("Control Overhead as a Function of MAX_PACKET_SIZE")
plt.legend()
plt.grid(True)
plt.show()
