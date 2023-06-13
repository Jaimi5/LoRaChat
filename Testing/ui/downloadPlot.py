import os
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg


# Given the plot, download the plot as a PNG file
def download_plot(canvas: FigureCanvasTkAgg, directory, fileName):
    # Create a directory to store the plots
    plot_dir = os.path.join(directory, "plots")
    if not os.path.exists(plot_dir):
        os.makedirs(plot_dir)

    # Save the plot
    canvas.print_figure(os.path.join(plot_dir, fileName))
