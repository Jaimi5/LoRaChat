import os
import sys
import tkinter as tk
from tkinter import Frame
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from downloadPlot import download_plot

sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..'))
from Testing.monitoringAnalysis.getV2OverheadData import get_v2_per_device_data


def draw_v2_reception_rate(frame: Frame, directory):
    for widget in frame.winfo_children():
        widget.destroy()

    monitoring_path = os.path.join(directory, 'Monitoring')
    if not os.path.exists(monitoring_path):
        tk.Label(frame, text=f'No Monitoring folder in {directory}', fg='red').pack()
        return

    data = get_v2_per_device_data(monitoring_path)
    if not data:
        tk.Label(frame, text='No device data found', fg='red').pack()
        return

    devices = [d['device'] for d in data]
    sent     = [d['sent']           for d in data]
    received = [d['received']       for d in data]
    rates    = [d['reception_rate'] for d in data]

    x = list(range(len(devices)))
    bar_width = 0.35

    fig = Figure(figsize=(14, 6), dpi=100)
    ax1 = fig.add_subplot(111)
    ax2 = ax1.twinx()

    bars_sent = ax1.bar(
        [i - bar_width / 2 for i in x], sent,
        width=bar_width, label='Sent', color='#1565C0', alpha=0.8,
    )
    bars_recv = ax1.bar(
        [i + bar_width / 2 for i in x], received,
        width=bar_width, label='Received at dest', color='#43A047', alpha=0.8,
    )

    # Value labels on bars
    for bar in bars_sent:
        h = bar.get_height()
        if h > 0:
            ax1.text(bar.get_x() + bar.get_width() / 2, h + 1, str(int(h)),
                     ha='center', va='bottom', fontsize=9, color='#1565C0')
    for bar in bars_recv:
        h = bar.get_height()
        if h > 0:
            ax1.text(bar.get_x() + bar.get_width() / 2, h + 1, str(int(h)),
                     ha='center', va='bottom', fontsize=9, color='#2E7D32')

    # Reception rate line (only for sender devices, i.e. sent > 0)
    rate_x = [i for i, s in zip(x, sent) if s > 0]
    rate_y = [r for r, s in zip(rates, sent) if s > 0]
    ax2.plot(rate_x, rate_y, color='#E53935', marker='o', linewidth=2,
             label='Reception rate %')
    for xi, yi in zip(rate_x, rate_y):
        ax2.annotate(f'{yi:.0f}%', xy=(xi, yi), xytext=(0, 6),
                     textcoords='offset points', ha='center', fontsize=9,
                     color='#E53935')

    ax1.set_xticks(x)
    ax1.set_xticklabels([f'0x{d}' for d in devices], rotation=30, ha='right')
    ax1.set_xlabel('Device address', fontsize=13)
    ax1.set_ylabel('Message count', fontsize=13)
    ax2.set_ylabel('Reception rate [%]', fontsize=13)
    ax2.set_ylim(0, 130)
    ax1.set_title('V2 Per-Device Send / Receive & Reception Rate', fontsize=15)

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, fontsize=11, loc='upper left')
    ax1.grid(True, axis='y', alpha=0.3)

    fig.tight_layout()

    canvas = FigureCanvasTkAgg(fig, master=frame)
    canvas.draw()
    canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

    tk.Button(
        frame,
        text='Download Plot',
        command=lambda: download_plot(canvas, directory, 'V2ReceptionRate.png'),
    ).pack(side=tk.BOTTOM)
