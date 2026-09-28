import csv
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parents[1]
CAPTURES = ROOT / "outputs" / "captures"
OUT = ROOT / "outputs" / "channel_rssi_map.png"


def peak_hold(name):
    bins = defaultdict(list)
    with (CAPTURES / f"{name}.csv").open() as stream:
        for row in csv.DictReader(stream):
            if row["preset"] == "OOK270":
                bins[int(row["requested_hz"])].append(int(row["rssi_dbm_x10"]) / 10)
    return [(f / 1e6, max(levels)) for f, levels in sorted(bins.items())]


fig, axes = plt.subplots(1, 2, figsize=(14, 5), sharey=True, constrained_layout=True)
for ax, region, title in [
    (axes[0], (822.5, 832.5), "Нижняя группа"),
    (axes[1], (862.5, 865.5), "Верхняя группа"),
]:
    background = [(f, r) for f, r in peak_hold("ch1_off") if region[0] <= f <= region[1]]
    ax.plot(*zip(*background), color="#777777", lw=1.2, label="T130U выключен")
    ax.set_xlim(*region)
    ax.set_ylim(-105, -5)
    ax.set_xlabel("Настройка приёмника, МГц")
    ax.set_title(title)
    ax.grid(True, alpha=0.2)
axes[0].set_ylabel("RSSI CC1101, dBm")

for name, label, ax, color in [
    ("ch1_on", "CH1", axes[0], "#0072B2"),
    ("ch2_on", "CH2", axes[0], "#D55E00"),
    ("ch17_on", "CH17", axes[0], "#009E73"),
    ("ch18_on", "CH18", axes[1], "#CC79A7"),
    ("ch19_on", "CH19", axes[1], "#E69F00"),
]:
    data = [(f, r) for f, r in peak_hold(name) if ax.get_xlim()[0] <= f <= ax.get_xlim()[1]]
    ax.plot(*zip(*data), lw=1.4, label=label, color=color)

for ax in axes:
    ax.legend(loc="upper right")
fig.suptitle("T130U EU: RSSI при переборе частот, максимум из 3 проходов, фильтр 270 кГц")
fig.savefig(OUT, dpi=160)
print(OUT)
