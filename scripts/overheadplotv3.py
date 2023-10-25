import matplotlib.pyplot as plt
import numpy as np

# Range of number of lost packets
num_lost_packets = np.arange(0, 11, 1)

# Size of each type of packet
size_sync = (
    size_ack
) = size_lost = 11  # SYNC, ACK, and LOST packets all have 11 bytes overhead
size_data = 11  # DATA packet has MAX_PACKET_SIZE = 222 bytes

# Additional overhead for each type of lost packet
additional_overhead_sync = size_sync
additional_overhead_data = size_data + size_lost
additional_overhead_ack = size_ack + size_lost

# Calculate total additional overhead for each type of lost packet
total_additional_overhead_sync = num_lost_packets * additional_overhead_sync
total_additional_overhead_data = num_lost_packets * additional_overhead_data
total_additional_overhead_ack = num_lost_packets * additional_overhead_ack

plt.rcParams.update({"font.size": 14})

# Plot results
plt.figure(figsize=(10, 6))
plt.plot(num_lost_packets, total_additional_overhead_sync, label="SYNC", linestyle=":")
plt.plot(num_lost_packets, total_additional_overhead_ack, label="ACK", linestyle="--")
plt.plot(
    num_lost_packets, total_additional_overhead_data + 2, label="DATA", linestyle="-."
)

plt.xlabel("Number of Lost Packets")
plt.ylabel("Total Additional Overhead (bytes)")
plt.title("Total Additional Overhead as a Function of the Number of Lost Packets")
plt.legend()
plt.grid(True)
plt.show()
