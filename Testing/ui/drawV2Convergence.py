import os
import sys
import tkinter as tk
from tkinter import Frame
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from downloadPlot import download_plot

sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..'))
from Testing.monitoringAnalysis.getV2OverheadData import get_v2_per_superframe_data

_AREAS = [
    ('data_payload_bytes', 'DATA Payload B', '#2196F3'),
    ('route_table_bytes', 'ROUTE_TABLE B',  '#FF9800'),
    ('sync_beacon_bytes', 'SYNC_BEACON B',  '#4CAF50'),
    ('join_bytes',        'JOIN B',          '#9C27B0'),
]


def draw_v2_convergence(frame: Frame, directory):
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

    fig = Figure(figsize=(14, 6), dpi=100)
    ax = fig.add_subplot(111)

    baseline = [0.0] * len(sf_data)
    for key, label, color in _AREAS:
        values = [d[key] for d in sf_data]
        top = [b + v for b, v in zip(baseline, values)]
        ax.fill_between(x, baseline, top, label=label, color=color, alpha=0.75)
        baseline = top

    ax.set_xlabel('Superframe [#]', fontsize=14)
    ax.set_ylabel('Count / Bytes', fontsize=14)
    ax.set_title('V2 Network Convergence Timeline', fontsize=16)
    ax.legend(fontsize=11, loc='upper right')
    ax.grid(True, alpha=0.3)

    canvas = FigureCanvasTkAgg(fig, master=frame)
    canvas.draw()
    canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

    tk.Button(
        frame,
        text='Download Plot',
        command=lambda: download_plot(canvas, directory, 'V2Convergence.png'),
    ).pack(side=tk.BOTTOM)
