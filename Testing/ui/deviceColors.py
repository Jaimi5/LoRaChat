import os
import json
import random
import matplotlib.colors as mcolors


def get_color_by_devices(devices):
    # Get the file containing the device colors or create it
    device_colors_file = os.path.join(os.path.dirname(__file__), "device_colors.json")
    if not os.path.exists(device_colors_file):
        with open(device_colors_file, "w") as f:
            json.dump({}, f)

    # Load the device colors
    with open(device_colors_file, "r") as f:
        device_colors = json.load(f)

    # Get the color for each device
    colors = []

    for device in devices:
        dev = str(device)
        # If the device is not in the file, add it
        if dev not in device_colors:
            # Generate a random color
            color = random.choice(list(mcolors.CSS4_COLORS.keys()))

            # Make sure the color is not already in use
            while color in device_colors.values():
                color = random.choice(list(mcolors.CSS4_COLORS.keys()))

            # Add the color to the dictionary
            device_colors[dev] = color

        # Add the color to the list
        colors.append(device_colors[dev])

    # Save the device colors
    with open(device_colors_file, "w") as f:
        json.dump(device_colors, f, indent=4)

    # Return the colors
    return colors
