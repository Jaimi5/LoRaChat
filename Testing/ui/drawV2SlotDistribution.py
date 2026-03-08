import os
import sys
import tkinter as tk
from tkinter import Frame
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from downloadPlot import download_plot

sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..'))
from Testing.monitoringAnalysis.getV2OverheadData import get_v2_overhead_data

_ORDERED_GROUPS = [
    'SLEEP',
    'SYNC_BEACON_TX',
    'TX',
    'RX',
    'CONTROL_TX',
    'CONTROL_RX',
    'DISCOVERY_RX',
    'Other',
]
_GROUP_COLORS = {
    'SLEEP':          '#B0BEC5',
    'SYNC_BEACON_TX': '#4CAF50',
    'TX':             '#2196F3',
    'RX':             '#90CAF9',
    'CONTROL_TX':     '#FF9800',
    'CONTROL_RX':     '#FFE082',
    'DISCOVERY_RX':   '#9C27B0',
    'Other':          '#E0E0E0',
}
# Map log slot type names to canonical group names
_SLOT_GROUP_MAP = {g: g for g in _ORDERED_GROUPS}


def draw_v2_slot_distribution(frame: Frame, directory):
    for widget in frame.winfo_children():
        widget.destroy()

    directories = sorted(os.listdir(directory))
    rows = []
    labels = []

    for i, dir_name in enumerate(directories, 1):
        monitoring_path = os.path.join(directory, dir_name, 'Monitoring')
        if not os.path.exists(monitoring_path):
            continue

        data = get_v2_overhead_data(monitoring_path)
        slot_counts = data['slot_counts']
        if not slot_counts:
            continue

        total = sum(slot_counts.values())
        if total == 0:
            continue

        row = {g: 0.0 for g in _ORDERED_GROUPS}
        for slot_type, count in slot_counts.items():
            group = _SLOT_GROUP_MAP.get(slot_type, 'Other')
            row[group] += count / total * 100

        rows.append(row)
        labels.append(str(i))

    if not rows:
        tk.Label(frame, text='No v2 slot data found', fg='red').pack()
        return

    fig = Figure(figsize=(12, 6), dpi=100)
    ax = fig.add_subplot(111)

    x = range(len(rows))
    bottom = [0.0] * len(rows)

    for group in _ORDERED_GROUPS:
        values = [row[group] for row in rows]
        color = _GROUP_COLORS.get(group, '#E0E0E0')
        ax.bar(x, values, bottom=bottom, label=group, color=color)
        bottom = [b + v for b, v in zip(bottom, values)]

    ax.set_xticks(list(x))
    ax.set_xticklabels(labels, rotation=0)
    ax.set_xlabel('Experiment [#]', fontsize=14)
    ax.set_ylabel('Slot Share [%]', fontsize=14)
    ax.set_title('V2 Slot Type Distribution by Experiment', fontsize=16)
    ax.set_ylim(0, 105)
    ax.legend(fontsize=10, loc='upper right')

    canvas = FigureCanvasTkAgg(fig, master=frame)
    canvas.draw()
    canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

    tk.Button(
        frame,
        text='Download Plot',
        command=lambda: download_plot(canvas, directory, 'V2SlotDistribution.png'),
    ).pack(side=tk.BOTTOM)
