import json
import pandas as pd


def calculate_summary_by_device(fileName):
    # Load data from JSON
    with open(fileName, "r") as f:
        data = json.load(f)

    # Convert to pandas DataFrame
    df = pd.DataFrame(data)

    # Expand the 'payload' dictionary into separate columns
    payload_df = pd.json_normalize(df["payload"])

    df = pd.concat([df.drop("payload", axis=1), payload_df], axis=1)

    # Separate sent and received packets
    sent_packets = df[df["state.Type"] == 1]
    received_packets = df[df["state.Type"] == 0]

    sent_packets = sent_packets.copy()
    received_packets = received_packets.copy()

    # Create a unique identifier for each packet
    sent_packets.loc[:, "packet_id"] = (
        sent_packets["state.packetHeader.Type"].astype(str)
        + "-"
        + sent_packets["state.packetHeader.Id"].astype(str)
        + "-"
        + sent_packets["state.packetHeader.Src"].astype(str)
    )
    received_packets.loc[:, "packet_id"] = (
        received_packets["state.packetHeader.Type"].astype(str)
        + "-"
        + received_packets["state.packetHeader.Id"].astype(str)
        + "-"
        + received_packets["state.packetHeader.Src"].astype(str)
    )

    # Find the total number of devices
    total_devices = df["addrSrc"].nunique()

    # Initialize a dictionary to store the summary statistics for each device
    summary_by_device = {}

    # Calculate packet loss for each device
    for device in df["addrSrc"].unique():
        device_sent_packets = sent_packets[sent_packets["addrSrc"] == device]
        device_packet_loss = 0

        device_received_packets = received_packets[
            received_packets["addrSrc"] == device
        ]

        # Calculate the number of packets lost for each device
        for packet_id in device_sent_packets["packet_id"]:
            # Check how many devices received this packet
            received_count = received_packets[
                received_packets["packet_id"] == packet_id
            ]["addrSrc"].nunique()

            device_packet_loss += total_devices - 1 - received_count

        summary_by_device[device] = {}

        summary_by_device[device]["packet_loss"] = device_packet_loss

        summary_by_device[device]["total_sent"] = device_sent_packets.shape[0]

        summary_by_device[device]["total_received"] = device_received_packets.shape[0]

        summary_by_device[device]["packet_loss_rate"] = (
            device_packet_loss / device_sent_packets.shape[0]
        )

    return summary_by_device
