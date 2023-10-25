import matplotlib.pyplot as plt
import numpy as np

names = ["monitor_195149_COM32.txt", "monitor_195149_COM4.txt"]

for name in names:
    with open(name, encoding="utf-8") as f:
        lines = f.readlines()

    # Initialize lists to store the data
    free_heap = []
    max_alloc_heap = []
    min_free_heap = []

    i = 10

    # Parse the data
    for line in lines:
        # Only process lines that contain "FREE HEAP:"
        # if "FREE HEAP:" in line:

        #     # Split the line into parts
        #     parts = line.split(",")

        #     # Extract the data and remove any extra characters
        #     free_heap.append(int(parts[0].split("FREE HEAP: ")[1]))
        #     max_alloc_heap.append(int(parts[1]))
        #     min_free_heap.append(int(parts[2].replace(";\n", "")))

        if "Heap" in line:
            if i > 0:
                i -= 1
                continue

            parts = line.split(":")
            # get the last part of the line
            try:
                last_part = int(parts[-1])

                if last_part < 10:
                    continue

                free_heap.append(last_part)
            except:
                print("error")

    # Create the plot
    plt.figure(figsize=(10, 6))

    # Plot the data
    plt.plot(free_heap, label="Free Heap")

    # Add a grid
    plt.grid(True)

    # plt.plot(max_alloc_heap, label="Max Alloc Heap")
    # plt.plot(min_free_heap, label="Min Free Heap")

    # x = np.arange(len(free_heap))
    # plt.plot(x, np.poly1d(np.polyfit(x, free_heap, 1))(x), label="Free Heap Trend")
    # plt.plot(
    #     x, np.poly1d(np.polyfit(x, max_alloc_heap, 1))(x), label="Max Alloc Heap Trend"
    # )
    # plt.plot(x, np.poly1d(np.polyfit(x, min_free_heap, 1))(x), label="Min Free Heap Trend")

    # Add arrow annotations to indicate overall trend
    plt.annotate(
        "",
        xy=(len(free_heap) - 1, free_heap[-1]),
        xytext=(0, free_heap[0]),
        arrowprops=dict(arrowstyle="->", color="blue"),
    )

    # Add a legend
    plt.legend()

    # Add labels and title
    plt.xlabel("Time")
    plt.ylabel("Memory")
    plt.title("Memory Usage Over Time " + name)


# Show the plot
plt.show()
