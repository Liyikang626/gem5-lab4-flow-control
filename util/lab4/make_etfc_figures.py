#!/usr/bin/env python3

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib import patches
import numpy as np
import pandas as pd


COLORS = {
    "wormhole": "#64748B",
    "bubble": "#2563EB",
    "escape": "#0F766E",
    "etfc": "#E8590C",
    "ink": "#172033",
    "muted": "#667085",
    "line": "#CBD5E1",
    "light": "#F8FAFC",
    "danger": "#C2413B",
    "safe": "#15803D",
    "token": "#F6C344",
}


def save(fig, output, name):
    fig.savefig(output / f"{name}.svg", bbox_inches="tight")
    fig.savefig(output / f"{name}.pdf", bbox_inches="tight")
    plt.close(fig)


def box(ax, xy, width, height, text, face="white", edge=None,
        fontsize=9, weight="normal", radius=0.08, text_color=None):
    edge = edge or COLORS["line"]
    text_color = text_color or COLORS["ink"]
    item = patches.FancyBboxPatch(
        xy, width, height,
        boxstyle=f"round,pad=0.02,rounding_size={radius}",
        linewidth=1.2, facecolor=face, edgecolor=edge,
    )
    ax.add_patch(item)
    ax.text(xy[0] + width / 2, xy[1] + height / 2, text,
            ha="center", va="center", fontsize=fontsize,
            color=text_color, weight=weight)
    return item


def arrow(ax, start, end, color=None, width=1.4, style="-"):
    ax.annotate(
        "", xy=end, xytext=start,
        arrowprops=dict(arrowstyle="-|>", color=color or COLORS["ink"],
                        linewidth=width, linestyle=style,
                        shrinkA=3, shrinkB=3),
    )


def panel_title(ax, title, subtitle, color):
    ax.text(.04, .94, title, transform=ax.transAxes, fontsize=13,
            weight="bold", color=color, va="top")
    ax.text(.04, .865, subtitle, transform=ax.transAxes, fontsize=8.5,
            color=COLORS["muted"], va="top")


def draw_queue(ax, x, y, filled, total=6, color=None, empty_color="#FFFFFF"):
    color = color or COLORS["bubble"]
    for i in range(total):
        face = color if i < filled else empty_color
        rect = patches.FancyBboxPatch(
            (x + i * .42, y), .34, .42,
            boxstyle="round,pad=0.01,rounding_size=.04",
            facecolor=face, edgecolor=COLORS["ink"], linewidth=.8)
        ax.add_patch(rect)


def architecture_baselines(output):
    fig, axes = plt.subplots(2, 2, figsize=(11, 7.1))
    for ax in axes.flat:
        ax.set(xlim=(0, 6), ylim=(0, 4))
        ax.set_xticks([]); ax.set_yticks([])
        for spine in ax.spines.values():
            spine.set_edgecolor(COLORS["line"])

    ax = axes[0, 0]
    panel_title(ax, "Wormhole", "Local credit admission", COLORS["wormhole"])
    draw_queue(ax, .5, 1.85, 6, color=COLORS["danger"])
    arrow(ax, (.7, 1.55), (5.1, 1.55), COLORS["danger"], 2)
    ax.text(.5, 1.05, "admit when credits >= 1", fontsize=10,
            color=COLORS["ink"], weight="bold")
    ax.text(.5, .55, "All slots can be occupied; a cyclic dependency\n"
                     "has no guaranteed place to advance.", fontsize=9,
            color=COLORS["muted"])

    ax = axes[0, 1]
    panel_title(ax, "Bubble", "Per-VC progress reservation", COLORS["bubble"])
    draw_queue(ax, .5, 2.25, 5, color=COLORS["bubble"])
    draw_queue(ax, .5, 1.58, 2, color="#8AB4F8")
    ax.text(3.02, 2.46, "reserved bubble", fontsize=8, va="center",
            color=COLORS["safe"])
    ax.text(.5, 1.05, "entry/turn requires >= 2 credits", fontsize=10,
            color=COLORS["ink"], weight="bold")
    ax.text(.5, .48, "Safe and simple, but free entries in the idle VC\n"
                     "cannot serve the congested VC above.", fontsize=9,
            color=COLORS["muted"])

    ax = axes[1, 0]
    panel_title(ax, "Escape VC", "Break the channel-dependency cycle",
                COLORS["escape"])
    ax.text(.45, 2.62, "regular", fontsize=8.5, color=COLORS["muted"])
    draw_queue(ax, 1.3, 2.45, 5, color=COLORS["escape"])
    ax.text(.45, 1.72, "escape", fontsize=8.5, color=COLORS["muted"])
    draw_queue(ax, 1.3, 1.55, 3, color="#5FB3A7")
    ax.plot([3.34, 3.34], [1.35, 3.05], color=COLORS["danger"], lw=2)
    ax.text(3.43, 2.9, "dateline", fontsize=8, color=COLORS["danger"])
    arrow(ax, (2.85, 2.35), (3.65, 1.85), COLORS["escape"], 1.8)
    ax.text(.5, .62, "Deadlock is avoided by a restricted VC class,\n"
                     "at the cost of partitioned capacity and transitions.",
            fontsize=9, color=COLORS["muted"])

    ax = axes[1, 1]
    panel_title(ax, "ETFC", "Pooled storage + conserved progress token",
                COLORS["etfc"])
    ax.text(.48, 2.78, "shared payload slots", fontsize=8.5,
            color=COLORS["muted"])
    draw_queue(ax, .5, 2.25, 7, total=10, color=COLORS["etfc"])
    ax.text(.48, 1.72, "ordered descriptors", fontsize=8.5,
            color=COLORS["muted"])
    for y, label, count in ((1.34, "VC0", 4), (.91, "VC1", 3)):
        ax.text(.5, y + .14, label, fontsize=8, va="center")
        for i in range(count):
            box(ax, (1.15 + i * .5, y), .38, .28, str(i),
                face="#FFF3E8", edge=COLORS["etfc"], fontsize=7)
    box(ax, (4.65, .9), .8, .8, "1\nTOKEN", face="#FFF6CC",
        edge=COLORS["token"], fontsize=9, weight="bold")
    ax.text(.5, .34, "Idle-VC capacity is reclaimed while one global\n"
                     "cyclic progress opportunity remains protected.",
            fontsize=9, color=COLORS["muted"])

    fig.suptitle("Flow-control design space", fontsize=18, weight="bold",
                 color=COLORS["ink"], y=.995)
    fig.subplots_adjust(hspace=.16, wspace=.10)
    save(fig, output, "architecture_baselines")


def architecture_etfc(output):
    fig, ax = plt.subplots(figsize=(12.2, 6.7))
    ax.set(xlim=(0, 13), ylim=(0, 7)); ax.axis("off")
    ax.text(.25, 6.6, "ETFC router organization", fontsize=20,
            weight="bold", color=COLORS["ink"])
    ax.text(.25, 6.22, "Separate ordered identity from shared physical capacity",
            fontsize=10.5, color=COLORS["muted"])

    ax.text(.35, 5.62, "DATA PLANE", fontsize=9, weight="bold",
            color=COLORS["etfc"])
    box(ax, (.35, 3.25), 1.25, 1.45, "Input\nlink", face="#F1F5F9",
        edge=COLORS["wormhole"], fontsize=11, weight="bold")
    box(ax, (2.05, 2.75), 3.2, 2.45, "", face="#FFF8F1",
        edge=COLORS["etfc"])
    ax.text(3.65, 4.87, "ElasticBuffer", ha="center", fontsize=12,
            weight="bold", color=COLORS["etfc"])
    ax.text(2.3, 4.47, "payload pool", fontsize=8.5, color=COLORS["muted"])
    draw_queue(ax, 2.3, 3.92, 7, total=7, color=COLORS["etfc"])
    ax.text(2.3, 3.57, "VC descriptors", fontsize=8.5, color=COLORS["muted"])
    for y, values in ((3.22, "2  5  6"), (2.89, "0  1  4")):
        box(ax, (3.35, y), 1.25, .25, values, face="white",
            edge="#FDBA74", fontsize=8)
    ax.text(2.35, 3.34, "VC0", fontsize=8)
    ax.text(2.35, 3.01, "VC1", fontsize=8)
    box(ax, (5.85, 3.25), 1.6, 1.45, "Switch\nallocator",
        face="#EFF6FF", edge=COLORS["bubble"], fontsize=11, weight="bold")
    box(ax, (8.05, 3.25), 1.5, 1.45, "Crossbar", face="#F8FAFC",
        edge=COLORS["wormhole"], fontsize=11, weight="bold")
    box(ax, (10.15, 3.25), 1.45, 1.45, "Output\nlink", face="#F1F5F9",
        edge=COLORS["wormhole"], fontsize=11, weight="bold")
    for a, b in (((1.6, 3.98), (2.05, 3.98)), ((5.25, 3.98), (5.85, 3.98)),
                 ((7.45, 3.98), (8.05, 3.98)), ((9.55, 3.98), (10.15, 3.98))):
        arrow(ax, a, b, COLORS["ink"], 1.8)
    ax.text(11.95, 3.98, "to next\nrouter", fontsize=9, va="center",
            color=COLORS["muted"])
    arrow(ax, (11.6, 3.98), (12.75, 3.98), COLORS["ink"], 1.8)

    ax.text(.35, 2.05, "CONTROL PLANE", fontsize=9, weight="bold",
            color=COLORS["bubble"])
    box(ax, (.35, .45), 2.25, 1.2, "Aggregate credits\nper output and vnet",
        face="#EFF6FF", edge=COLORS["bubble"], fontsize=10, weight="bold")
    box(ax, (3.15, .45), 2.25, 1.2, "Ring token manager\n$F-R>1$ for entry",
        face="#FFF6CC", edge=COLORS["token"], fontsize=10, weight="bold")
    box(ax, (5.95, .45), 2.25, 1.2, "Directional pressure\nclose $C/8$, open $C/4$",
        face="#FEF2F2", edge=COLORS["danger"], fontsize=10, weight="bold")
    box(ax, (8.75, .45), 2.35, 1.2, "Transit-first selection\nonly on pressured ring",
        face="#ECFDF3", edge=COLORS["safe"], fontsize=10, weight="bold")
    arrow(ax, (2.6, 1.05), (3.15, 1.05), COLORS["bubble"])
    arrow(ax, (5.4, 1.05), (5.95, 1.05), COLORS["token"])
    arrow(ax, (8.2, 1.05), (8.75, 1.05), COLORS["danger"])
    arrow(ax, (9.9, 1.65), (6.7, 3.25), COLORS["safe"], 1.5, "--")
    arrow(ax, (10.85, 3.25), (1.5, 1.65), COLORS["bubble"], 1.2, "--")
    ax.text(11.05, 2.16, "credit return", fontsize=8.5,
            color=COLORS["bubble"])
    save(fig, output, "architecture_etfc")


def architecture_cycles(output):
    fig, axes = plt.subplots(1, 2, figsize=(12, 5.2))
    for ax in axes:
        ax.set_aspect("equal"); ax.axis("off")

    ax = axes[0]
    ax.set(xlim=(-1.4, 1.4), ylim=(-1.25, 1.45))
    ax.text(-1.34, 1.32, "Ring: one conserved progress token", fontsize=13,
            weight="bold", color=COLORS["ink"])
    count = 10
    for i in range(count):
        angle = 2 * np.pi * i / count
        nxt = 2 * np.pi * (i + 1) / count
        a = (.92 * np.cos(angle), .92 * np.sin(angle))
        b = (.92 * np.cos(nxt), .92 * np.sin(nxt))
        arrow(ax, a, b, COLORS["bubble"], 1.25)
        color = COLORS["token"] if i == 2 else COLORS["etfc"]
        circle = patches.Circle(a, .115, facecolor=color, edgecolor="white",
                                linewidth=1.4, zorder=3)
        ax.add_patch(circle)
    ax.annotate("protected opportunity", xy=(.3, .9), xytext=(-.45, .34),
                fontsize=9, color=COLORS["safe"],
                arrowprops=dict(arrowstyle="->", color=COLORS["safe"]))
    ax.text(-1.22, -1.18, "Transit moves the empty position backward.\n"
                         "New entry cannot consume the final token.",
            fontsize=9, color=COLORS["muted"])

    ax = axes[1]
    ax.set(xlim=(-.65, 3.65), ylim=(-.7, 3.7))
    ax.text(-.62, 3.52, "Torus: pressure is directional", fontsize=13,
            weight="bold", color=COLORS["ink"])
    for y in range(4):
        for x in range(4):
            ax.add_patch(patches.Circle((x, y), .13, facecolor="white",
                                        edgecolor=COLORS["ink"], linewidth=1.1,
                                        zorder=4))
            if x < 3:
                ax.plot([x+.13, x+1-.13], [y, y], color=COLORS["line"], lw=1)
            if y < 3:
                ax.plot([x, x], [y+.13, y+1-.13], color=COLORS["line"], lw=1)
    ax.plot([0, 3], [2, 2], color=COLORS["danger"], lw=8, alpha=.25)
    ax.plot([1, 1], [0, 3], color=COLORS["bubble"], lw=8, alpha=.22)
    for x in range(3):
        arrow(ax, (x+.18, 2), (x+.82, 2), COLORS["danger"], 2)
    for y in range(3):
        arrow(ax, (1, y+.18), (1, y+.82), COLORS["bubble"], 2)
    ax.text(2.95, 2.18, "pressured row", fontsize=8.5,
            color=COLORS["danger"], ha="right")
    ax.text(1.18, .15, "independent\ncolumn", fontsize=8.5,
            color=COLORS["bubble"])
    box(ax, (1.65, -.55), 1.85, .62, "transit before entry\nwhile pressure is closed",
        face="#FEF2F2", edge=COLORS["danger"], fontsize=8.5)
    save(fig, output, "architecture_cycles")


def operation_timeline(output):
    fig, ax = plt.subplots(figsize=(11.5, 4.8))
    ax.set(xlim=(0, 12), ylim=(0, 6)); ax.axis("off")
    actors = [(1.2, "Input VC"), (4.0, "Switch allocator"),
              (7.0, "Ring manager"), (10.2, "Downstream pool")]
    for x, title in actors:
        box(ax, (x-.75, 5.12), 1.5, .55, title, face="#F8FAFC",
            edge=COLORS["line"], fontsize=9, weight="bold")
        ax.plot([x, x], [.5, 5.1], color=COLORS["line"], ls="--", lw=1)
    events = [
        (4.0, 7.0, 4.55, "reserve entry", COLORS["token"]),
        (7.0, 4.0, 3.85, "$F-R>1$: grant", COLORS["safe"]),
        (4.0, 10.2, 3.15, "send flit / consume token", COLORS["etfc"]),
        (4.0, 7.0, 2.45, "commit reservation", COLORS["token"]),
        (10.2, 1.2, 1.75, "credit after departure", COLORS["bubble"]),
        (1.2, 7.0, 1.05, "release ring slot", COLORS["safe"]),
    ]
    for x1, x2, y, label, color in events:
        arrow(ax, (x1, y), (x2, y), color, 1.7)
        ax.text((x1+x2)/2, y+.12, label, fontsize=8.5, ha="center",
                va="bottom", color=color, weight="bold")
    ax.text(.4, .25, "Reservation makes multiple same-cycle requests atomic; commit and credit return preserve token conservation.",
            fontsize=9.5, color=COLORS["muted"])
    save(fig, output, "operation_timeline")


def load_data(path):
    data = pd.read_csv(path)
    numeric = ["rate", "cycles", "inj_vnet", "vc_depth", "vcs", "seed",
               "packets_injected", "packets_received", "packet_latency",
               "network_latency", "queue_latency", "flit_latency", "hops"]
    for column in numeric:
        data[column] = pd.to_numeric(data[column], errors="coerce")
    data["throughput"] = data["packets_received"] / data["cycles"] / 16
    data["reception"] = data["packets_received"] / data["packets_injected"]
    return data


def method_lines(ax, frame, x, y, log=False):
    for method in ("bubble", "etfc"):
        part = frame[frame.controller == method].sort_values(x)
        ax.plot(part[x], part[y], marker="o", markersize=4.2, linewidth=2,
                label=method.upper() if method == "etfc" else method.title(),
                color=COLORS[method])
    if log:
        ax.set_yscale("log")
    ax.grid(True, alpha=.22, linewidth=.7)


def plot_load(data, output):
    frame = data[(data.suite == "load") & (data.cycles == 100000)]
    fig, axes = plt.subplots(2, 3, figsize=(12, 6.5), sharex="col")
    topologies = [("Ring", "Ring"), ("Torus2D", "4x4 Torus"),
                  ("Mesh2D", "4x4 Mesh")]
    for column, (topology, title) in enumerate(topologies):
        part = frame[frame.topology == topology]
        method_lines(axes[0, column], part, "rate", "throughput")
        method_lines(axes[1, column], part, "rate", "packet_latency", log=True)
        axes[0, column].set_title(title, weight="bold")
        axes[1, column].set_xlabel("Injection rate")
    axes[0, 0].set_ylabel("Received throughput\n(packets/node/cycle)")
    axes[1, 0].set_ylabel("Packet latency (ticks, log)")
    axes[0, 2].legend(frameon=False, ncol=2, loc="upper left")
    fig.suptitle("Saturation behavior at 100,000 cycles", fontsize=16,
                 weight="bold", color=COLORS["ink"])
    fig.subplots_adjust(hspace=.15, wspace=.28)
    save(fig, output, "result_load_100k")


def plot_duration(data, output):
    frame = data[data.suite == "load"]
    fig, axes = plt.subplots(2, 3, figsize=(12, 6.5), sharex="col")
    topologies = [("Ring", "Ring", .20), ("Torus2D", "4x4 Torus", .30),
                  ("Mesh2D", "4x4 Mesh", .30)]
    for column, (topology, title, rate) in enumerate(topologies):
        part = frame[(frame.topology == topology) & (frame.rate == rate)].copy()
        part["cycles_k"] = part.cycles / 1000
        method_lines(axes[0, column], part, "cycles_k", "throughput")
        method_lines(axes[1, column], part, "cycles_k", "packet_latency")
        axes[0, column].set_title(f"{title} (rate {rate:.2f})", weight="bold")
        axes[1, column].set_xlabel("Simulation length (thousand cycles)")
    axes[0, 0].set_ylabel("Received throughput\n(packets/node/cycle)")
    axes[1, 0].set_ylabel("Packet latency (ticks)")
    axes[0, 2].legend(frameon=False, ncol=2, loc="best")
    fig.suptitle("Duration scaling and steady-state behavior", fontsize=16,
                 weight="bold", color=COLORS["ink"])
    fig.subplots_adjust(hspace=.15, wspace=.28)
    save(fig, output, "result_duration")


def paired_improvement(data, suite, key):
    frame = data[data.suite == suite]
    index = ["topology", key]
    bubble = frame[frame.controller == "bubble"].set_index(index)
    etfc = frame[frame.controller == "etfc"].set_index(index)
    out = bubble[["packets_received", "packet_latency"]].join(
        etfc[["packets_received", "packet_latency"]],
        lsuffix="_b", rsuffix="_e")
    out["throughput_gain"] = 100 * (
        out.packets_received_e - out.packets_received_b) / out.packets_received_b
    out["latency_gain"] = 100 * (
        out.packet_latency_b - out.packet_latency_e) / out.packet_latency_b
    return out.reset_index()


def plot_patterns(data, output):
    frame = paired_improvement(data, "traffic", "traffic")
    patterns = ["uniform_random", "tornado", "bit_complement", "bit_reverse",
                "bit_rotation", "neighbor", "shuffle", "transpose"]
    topologies = ["Ring", "Torus2D", "Mesh2D"]
    fig, axes = plt.subplots(2, 1, figsize=(11, 5.8), sharex=True)
    for ax, metric, title in (
        (axes[0], "throughput_gain", "Received-throughput improvement"),
        (axes[1], "latency_gain", "Latency improvement")):
        matrix = np.array([[frame[(frame.topology == t) &
                                  (frame.traffic == p)][metric].iloc[0]
                            for p in patterns] for t in topologies])
        limit = max(1, np.nanmax(np.abs(matrix)))
        image = ax.imshow(matrix, cmap="RdBu", vmin=-limit, vmax=limit,
                          aspect="auto")
        for y in range(matrix.shape[0]):
            for x in range(matrix.shape[1]):
                ax.text(x, y, f"{matrix[y, x]:+.1f}%", ha="center", va="center",
                        fontsize=8, color="white" if abs(matrix[y, x]) > limit*.55
                        else COLORS["ink"])
        ax.set_yticks(range(3), ["Ring", "Torus", "Mesh"])
        ax.set_title(title + " (positive favors ETFC)", fontsize=11,
                     weight="bold")
        fig.colorbar(image, ax=ax, pad=.015, label="Improvement (%)")
    axes[1].set_xticks(range(len(patterns)),
                       [p.replace("_", "\n") for p in patterns])
    fig.suptitle("Traffic-pattern sensitivity at 50,000 cycles", fontsize=16,
                 weight="bold", color=COLORS["ink"])
    fig.subplots_adjust(hspace=.32)
    save(fig, output, "result_traffic_heatmap")


def plot_resources(data, output):
    frame = data[data.suite == "depth"]
    fig, axes = plt.subplots(2, 3, figsize=(12, 6.5), sharex="col")
    topologies = [("Ring", "Ring"), ("Torus2D", "4x4 Torus"),
                  ("Mesh2D", "4x4 Mesh")]
    for column, (topology, title) in enumerate(topologies):
        part = frame[frame.topology == topology]
        method_lines(axes[0, column], part, "vc_depth", "throughput")
        method_lines(axes[1, column], part, "vc_depth", "packet_latency")
        axes[0, column].set_title(title, weight="bold")
        axes[1, column].set_xlabel("Entries per VC")
        axes[1, column].set_xticks([4, 8, 16, 32])
    axes[0, 0].set_ylabel("Received throughput\n(packets/node/cycle)")
    axes[1, 0].set_ylabel("Packet latency (ticks)")
    axes[0, 2].legend(frameon=False, ncol=2)
    fig.suptitle("Buffer-capacity sensitivity at 50,000 cycles", fontsize=16,
                 weight="bold", color=COLORS["ink"])
    fig.subplots_adjust(hspace=.15, wspace=.28)
    save(fig, output, "result_depth")


def plot_vcs_packets(data, output):
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.6))
    for column, topology in enumerate(("Ring", "Torus2D")):
        part = data[(data.suite == "vcs") & (data.topology == topology)]
        method_lines(axes[column], part, "vcs", "throughput")
        axes[column].set_title(("Ring" if topology == "Ring" else "Torus") +
                               " VC-count sensitivity", weight="bold")
        axes[column].set_xlabel("VCs per vnet"); axes[column].set_xticks([1,2,4,8])
        axes[column].set_ylabel("Received throughput\n(packets/node/cycle)")
    part = data[data.suite == "packet"]
    gains = paired_improvement(data, "packet", "inj_vnet")
    one = gains[gains.inj_vnet == 0].set_index("topology").throughput_gain
    five = gains[gains.inj_vnet == 2].set_index("topology").throughput_gain
    x = np.arange(3); width=.35
    axes[2].bar(x-width/2, [one[t] for t in ("Ring","Torus2D","Mesh2D")],
                width, label="1 flit", color="#94A3B8")
    axes[2].bar(x+width/2, [five[t] for t in ("Ring","Torus2D","Mesh2D")],
                width, label="5 flits", color=COLORS["etfc"])
    axes[2].axhline(0, color=COLORS["ink"], lw=.8)
    axes[2].set_xticks(x, ["Ring","Torus","Mesh"])
    axes[2].set_ylabel("ETFC throughput gain (%)")
    axes[2].set_title("Packet-size sensitivity", weight="bold")
    axes[2].legend(frameon=False)
    axes[0].legend(frameon=False, ncol=2)
    fig.suptitle("Virtual-channel and packet-size ablations", fontsize=16,
                 weight="bold", color=COLORS["ink"])
    fig.subplots_adjust(wspace=.34)
    save(fig, output, "result_vcs_packets")


def plot_seeds(data, output):
    frame = data[data.suite == "seed"]
    fig, axes = plt.subplots(1, 2, figsize=(10.5, 3.8))
    topologies = ["Ring", "Torus2D", "Mesh2D"]
    x = np.arange(3); shifts={"bubble": -.12, "etfc": .12}
    for metric, ax, label in (("throughput", axes[0], "Received throughput\n(packets/node/cycle)"),
                              ("packet_latency", axes[1], "Packet latency (ticks)")):
        for method in ("bubble", "etfc"):
            means=[]; errors=[]
            for topology in topologies:
                values=frame[(frame.topology==topology) &
                             (frame.controller==method)][metric]
                means.append(values.mean()); errors.append(values.std(ddof=1))
            ax.errorbar(x+shifts[method], means, yerr=errors, fmt="o", capsize=4,
                        markersize=7, linewidth=1.8, color=COLORS[method],
                        label=method.upper() if method == "etfc" else method.title())
        ax.set_xticks(x, ["Ring", "Torus", "Mesh"]); ax.set_ylabel(label)
        ax.grid(True, axis="y", alpha=.22)
    axes[0].legend(frameon=False, ncol=2)
    fig.suptitle("Four-seed robustness at 50,000 cycles", fontsize=16,
                 weight="bold", color=COLORS["ink"])
    fig.subplots_adjust(wspace=.3)
    save(fig, output, "result_seeds")


def plot_baselines(data, output):
    load = data[(data.suite == "load") & (data.cycles == 50000)]
    refs = data[(data.suite == "baseline") & (data.cycles == 50000)]
    rows=[]
    for topology, rate in (("Ring", .20), ("Torus2D", .30), ("Mesh2D", .30)):
        rows.append(load[(load.topology==topology) & (load.rate==rate)])
        rows.append(refs[refs.topology==topology])
    frame=pd.concat(rows)
    fig, axes=plt.subplots(1, 3, figsize=(12, 3.8))
    order=["wormhole","bubble","escape","etfc"]
    for ax, topology, title in zip(axes, ("Ring","Torus2D","Mesh2D"),
                                   ("Ring","4x4 Torus","4x4 Mesh")):
        part=frame[frame.topology==topology].set_index("controller")
        vals=[part.loc[m,"throughput"] for m in order]
        ax.bar(range(4), vals, color=[COLORS[m] for m in order], width=.68)
        ax.set_xticks(range(4), ["Wormhole","Bubble","Escape\nVC","ETFC"],
                      rotation=15)
        ax.set_title(title, weight="bold"); ax.grid(True, axis="y", alpha=.22)
        ax.set_ylabel("Received throughput\n(packets/node/cycle)")
    fig.suptitle("Four-controller comparison at 50,000 cycles", fontsize=16,
                 weight="bold", color=COLORS["ink"])
    fig.subplots_adjust(wspace=.34)
    save(fig, output, "result_baselines")


def plot_latency_breakdown(data, output):
    load = data[(data.suite == "load") & (data.cycles == 50000)]
    fig, axes = plt.subplots(1, 3, figsize=(11.4, 3.7))
    settings = (("Ring", .20, "Ring"), ("Torus2D", .30, "4x4 Torus"),
                ("Mesh2D", .30, "4x4 Mesh"))
    for ax, (topology, rate, title) in zip(axes, settings):
        part = load[(load.topology == topology) &
                    (load.rate == rate)].set_index("controller")
        methods = ["bubble", "etfc"]
        network = [part.loc[m, "network_latency"] for m in methods]
        queue = [part.loc[m, "queue_latency"] for m in methods]
        x = np.arange(2)
        ax.bar(x, network, color="#7DD3FC", label="Network traversal")
        ax.bar(x, queue, bottom=network, color="#F97316", alpha=.85,
               label="Source/queue waiting")
        ax.set_xticks(x, ["Bubble", "ETFC"])
        ax.set_title(title, weight="bold")
        ax.set_ylabel("Packet latency (ticks)")
        ax.grid(True, axis="y", alpha=.22)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, frameon=False, ncol=2, loc="lower center",
               bbox_to_anchor=(.5, -.01))
    fig.suptitle("Where ETFC changes latency at 50,000 cycles", fontsize=16,
                 weight="bold", color=COLORS["ink"])
    fig.subplots_adjust(wspace=.36, bottom=.19)
    save(fig, output, "result_latency_breakdown")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("metrics", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    plt.rcParams.update({
        "font.family": "DejaVu Sans",
        "font.size": 9,
        "axes.edgecolor": COLORS["line"],
        "axes.labelcolor": COLORS["ink"],
        "xtick.color": COLORS["muted"],
        "ytick.color": COLORS["muted"],
        "figure.facecolor": "white",
        "savefig.facecolor": "white",
    })
    architecture_baselines(args.output)
    architecture_etfc(args.output)
    architecture_cycles(args.output)
    operation_timeline(args.output)
    data = load_data(args.metrics)
    plot_baselines(data, args.output)
    plot_load(data, args.output)
    plot_duration(data, args.output)
    plot_patterns(data, args.output)
    plot_resources(data, args.output)
    plot_vcs_packets(data, args.output)
    plot_seeds(data, args.output)
    plot_latency_breakdown(data, args.output)
    print(f"Wrote 12 figure pairs to {args.output}")


if __name__ == "__main__":
    main()
