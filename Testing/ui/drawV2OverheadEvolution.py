import os
import sys
import tkinter as tk
from tkinter import Frame
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from downloadPlot import download_plot

sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..'))
from Testing.monitoringAnalysis.getV2OverheadData import get_v2_per_superframe_data

_ROLLING_WINDOW = 5


def draw_v2_overhead_evolution(frame: Frame, directory):
    for widget in frame.winfo_children():
        widget.destroy()

    monitoring_path = os.path.join(directory, 'Monitoring')
    if not os.path.exists(monitoring_path):
        tk.Label(frame, text=f'No Monitoring folder in {directory}', fg='red').pack()
        return

    sf_data = get_v2_per_superframe_data(monitoring_path)
    if not sf_data:
        tk.Label(frame, text='No superframe data found', fg='red').pack()
        return

    x = [d['sf'] for d in sf_data]
    raw = [d['overhead_pct'] for d in sf_data]

    # Rolling average (simple Python, no pandas required)
    half = _ROLLING_WINDOW // 2
    smoothed = []
    for i in range(len(raw)):
        lo = max(0, i - half)
        hi = min(len(raw), i + half + 1)
        smoothed.append(sum(raw[lo:hi]) / (hi - lo))

    fig = Figure(figsize=(14, 6), dpi=100)
    ax = fig.add_subplot(111)

    ax.plot(x, raw,      color='#90CAF9', alpha=0.4, linewidth=1,
            label='Raw overhead %')
    ax.plot(x, smoothed, color='#1565C0', linewidth=2,
            label=f'Smoothed (window={_ROLLING_WINDOW})')

    ax.set_xlabel('Superframe [#]', fontsize=14)
    ax.set_ylabel('Overhead [%]', fontsize=14)
    ax.set_title('V2 Protocol Overhead Evolution', fontsize=16)
    ax.set_ylim(0, 105)
    ax.legend(fontsize=11, loc='upper right')
    ax.grid(True, alpha=0.3)

    canvas = FigureCanvasTkAgg(fig, master=frame)
    canvas.draw()
    canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

    tk.Button(
        frame,
        text='Download Plot',
        command=lambda: download_plot(canvas, directory, 'V2OverheadEvolution.png'),
    ).pack(side=tk.BOTTOM)
