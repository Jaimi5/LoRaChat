import os
import sys
import pandas as pd
import matplotlib.pyplot as plt
import tkinter as tk
from tkinter import Frame
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from downloadPlot import download_plot

sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..'))
from Testing.monitoringAnalysis.getV2OverheadData import get_v2_overhead_data

_STACK_COLS   = ['User Payload', 'DATA Header', 'ROUTE_TABLE', 'SYNC_BEACON', 'JOIN', 'Other']
_STACK_COLORS = ['#2196F3',      '#90CAF9',     '#FF9800',     '#4CAF50',     '#9C27B0', '#B0BEC5']


def draw_v2_overhead_by_experiments(frame: Frame, directory):
    for widget in frame.winfo_children():
        widget.destroy()

    directories = sorted(os.listdir(directory))
    rows = []
    i = 1

    for dir_name in directories:
        monitoring_path = os.path.join(directory, dir_name, 'Monitoring')

        if not os.path.exists(monitoring_path):
            continue

        data = get_v2_overhead_data(monitoring_path)

        if data['total_bytes'] == 0:
            print(f'Skipping {dir_name}: no data found')
            continue

        sf = max(data['total_superframes'], 1)

        print(f'Experiment {i} ({dir_name}):')
        print(f"  DATA transmissions  : {data['data_transmissions']}")
        print(f"  User payload bytes  : {data['data_payload_bytes']}")
        print(f"  DATA header bytes   : {data['data_header_bytes']}")
        print(f"  ROUTE_TABLE bytes   : {data['route_table_bytes']}")
        print(f"  SYNC_BEACON bytes   : {data['sync_beacon_bytes']}")
        print(f"  JOIN bytes          : {data['join_bytes']}")
        print(f"  Other bytes         : {data['other_msg_bytes']}")
        print(f"  Total bytes         : {data['total_bytes']}")
        print(f"  Total superframes   : {data['total_superframes']}")
        print(f"  Overhead %          : {data['overhead_pct']}")

        rows.append({
            'Id':           i,
            'User Payload': data['data_payload_bytes'] / sf,
            'DATA Header':  data['data_header_bytes']  / sf,
            'ROUTE_TABLE':  data['route_table_bytes']  / sf,
            'SYNC_BEACON':  data['sync_beacon_bytes']  / sf,
            'JOIN':         data['join_bytes']          / sf,
            'Other':        data['other_msg_bytes']     / sf,
            'Overhead %':   data['overhead_pct'],
        })
        i += 1

    if not rows:
        label = tk.Label(frame, text='No v2 experiment data found in directory', fg='red')
        label.pack()
        return

    df = pd.DataFrame(rows)

    fig = Figure(figsize=(12, 6), dpi=100)
    ax = fig.add_subplot(111)
    plt.rc('font', size=12)

    bottom = pd.Series([0.0] * len(df))
    x = range(len(df))

    for col, color in zip(_STACK_COLS, _STACK_COLORS):
        if col not in df.columns:
            continue
        ax.bar(x, df[col], bottom=bottom, label=col, color=color)
        bottom = bottom + df[col]

    # Annotate overhead % on top of each bar
    for idx, (kb, pct) in enumerate(zip(bottom, df['Overhead %'])):
        ax.annotate(
            f'{pct}%',
            (idx, kb),
            ha='center',
            va='bottom',
            fontsize=10,
            fontweight='bold',
        )

    ax.set_xticks(list(x))
    ax.set_xticklabels(df['Id'], rotation=0)
    ax.set_xlabel('Experiment [#]', fontsize=14)
    ax.set_ylabel('Bytes per Superframe [B/SF]', fontsize=14)
    ax.set_title('V2 Protocol Overhead by Experiment (normalized by superframes)', fontsize=16)
    ax.legend(fontsize=11, loc='upper left')

    canvas = FigureCanvasTkAgg(fig, master=frame)
    canvas.draw()
    canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

    tk.Button(
        frame,
        text='Download Plot',
        command=lambda: download_plot(canvas, directory, 'V2OverheadByExperiments.png'),
    ).pack(side=tk.BOTTOM)
