from pathlib import Path

import matplotlib as mpl
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch


mpl.rcParams.update(
    {
        "font.family": "sans-serif",
        "font.sans-serif": ["DejaVu Sans", "Arial", "Helvetica", "sans-serif"],
        "svg.fonttype": "none",
        "pdf.fonttype": 42,
        "font.size": 8,
    }
)

OUT = Path("docs/figures")
OUT.mkdir(parents=True, exist_ok=True)

COLORS = {
    "ink": "#1F2933",
    "muted": "#637381",
    "input": "#F7F8FA",
    "input_edge": "#B9C0C8",
    "search": "#D9E8F5",
    "search_edge": "#4C78A8",
    "eval": "#F4E1C6",
    "eval_edge": "#C47F2D",
    "route": "#E4E8D8",
    "route_edge": "#6B8E23",
    "memory": "#ECE7F2",
    "memory_edge": "#7A68A6",
    "final": "#F3D8D4",
    "final_edge": "#B8554B",
}


def box(ax, x, y, w, h, title, desc, face, edge, title_fs=7.6, desc_fs=6.3):
    patch = FancyBboxPatch(
        (x, y),
        w,
        h,
        boxstyle="round,pad=0.014,rounding_size=0.028",
        linewidth=1.22,
        edgecolor=edge,
        facecolor=face,
    )
    ax.add_patch(patch)
    ax.text(
        x + w / 2,
        y + h * 0.68,
        title,
        ha="center",
        va="center",
        fontsize=title_fs,
        fontweight="bold",
        color=COLORS["ink"],
        linespacing=0.95,
    )
    ax.text(
        x + w / 2,
        y + h * 0.27,
        desc,
        ha="center",
        va="center",
        fontsize=desc_fs,
        color=COLORS["ink"],
        linespacing=1.02,
    )


def group(ax, x, y, w, h, title, edge, label_x_offset=0.030, label_y_offset=0.50):
    patch = FancyBboxPatch(
        (x, y),
        w,
        h,
        boxstyle="round,pad=0.020,rounding_size=0.030",
        linewidth=1.0,
        linestyle=(0, (4, 3)),
        edgecolor=edge,
        facecolor="none",
        alpha=0.75,
    )
    ax.add_patch(patch)
    label_w = 0.165 if len(title) > 14 else 0.115
    label_h = 0.040
    label_x = x + label_x_offset
    label_y = y + h - label_h * label_y_offset
    label = FancyBboxPatch(
        (label_x, label_y),
        label_w,
        label_h,
        boxstyle="round,pad=0.004,rounding_size=0.010",
        linewidth=0,
        facecolor="white",
        zorder=4,
    )
    ax.add_patch(label)
    ax.text(
        label_x + label_w / 2,
        label_y + label_h / 2,
        title,
        ha="center",
        va="center",
        fontsize=7.2,
        fontweight="bold",
        color=edge,
        zorder=5,
    )


def arrow(ax, a, b, color=None, rad=0.0, lw=1.22, style="-|>"):
    ax.add_patch(
        FancyArrowPatch(
            a,
            b,
            arrowstyle=style,
            mutation_scale=12,
            linewidth=lw,
            color=color or COLORS["ink"],
            connectionstyle=f"arc3,rad={rad}",
            shrinkA=4,
            shrinkB=4,
        )
    )


def main():
    fig, ax = plt.subplots(figsize=(12.0, 6.4))
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    w = 0.128
    h = 0.138
    top = 0.735
    mid = 0.455
    low = 0.200
    xs = [0.035, 0.185, 0.335, 0.485, 0.635, 0.785]

    group(ax, 0.020, 0.690, 0.945, 0.230, "Placement Search", COLORS["search_edge"])
    box(ax, xs[0], top, w, h, "Problem\nInput", "Blocks, Outline,\nConnections", COLORS["input"], COLORS["input_edge"], 7.8, 6.2)
    box(ax, xs[1], top, w, h, "Input\nPreparation", "Normalize Data\nAnd Constraints", COLORS["input"], COLORS["input_edge"], 7.6, 6.1)
    box(ax, xs[2], top, w, h, "Placement\nMode", "Sequence-Pair,\nB-Star, Or Hybrid", COLORS["search"], COLORS["search_edge"], 7.6, 6.1)
    box(ax, xs[3], top, w, h, "Placement\nStart", "Create Initial\nEncoded State", COLORS["search"], COLORS["search_edge"], 7.6, 6.1)
    box(ax, xs[4], top, w, h, "Placement\nPerturbation", "Change Order,\nShape, Or Anchor", COLORS["search"], COLORS["search_edge"], 7.2, 6.0)
    box(ax, xs[5], top, w, h, "Placement\nUnpacking", "Convert State To\nBlock Rectangles", COLORS["search"], COLORS["search_edge"], 7.4, 6.0)
    for i in range(5):
        arrow(ax, (xs[i] + w, top + h / 2), (xs[i + 1], top + h / 2))

    group(ax, 0.020, 0.405, 0.945, 0.225, "Routing-Aware Evaluation", COLORS["eval_edge"])
    mid_xs = list(reversed(xs))
    box(ax, mid_xs[0], mid, w, h, "Fast\nScreening", "Area, Overlap,\nAnd Wire Proxy", COLORS["eval"], COLORS["eval_edge"], 7.7, 6.0)
    box(ax, mid_xs[1], mid, w, h, "Evaluation\nGate", "Scheduled Or\nPromising Candidate", COLORS["memory"], COLORS["memory_edge"], 7.3, 5.9)
    box(ax, mid_xs[2], mid, w, h, "Channel\nExtraction", "Build Whitespace\nRouting Regions", COLORS["route"], COLORS["route_edge"], 7.6, 6.0)
    box(ax, mid_xs[3], mid, w, h, "Global\nRouting", "Route Bundles\nThrough Channels", COLORS["route"], COLORS["route_edge"], 7.7, 6.0)
    box(ax, mid_xs[4], mid, w, h, "Route-Aware\nScoring", "Wirelength,\nOverflow, Feedthrough", COLORS["eval"], COLORS["eval_edge"], 7.2, 5.9)
    box(ax, mid_xs[5], mid, w, h, "Best Candidate\nMemory", "Reuse Rounded\nGeometry Results", COLORS["memory"], COLORS["memory_edge"], 7.0, 5.9)
    for i in range(5):
        arrow(ax, (mid_xs[i], mid + h / 2), (mid_xs[i + 1] + w, mid + h / 2))

    arrow(ax, (xs[5] + w * 0.55, top), (mid_xs[0] + w * 0.55, mid + h), COLORS["eval_edge"], rad=0.0)
    ax.text(
        0.780,
        0.668,
        "Search Repeats Until Iteration Budget",
        ha="center",
        va="center",
        fontsize=7.0,
        color=COLORS["search_edge"],
        fontweight="bold",
    )

    group(ax, 0.020, 0.135, 0.545, 0.225, "Finalization", COLORS["final_edge"], label_x_offset=0.085, label_y_offset=-0.25)
    box(ax, 0.055, low, w, h, "Solution\nSelection", "Keep Lowest\nEvaluated Cost", COLORS["memory"], COLORS["memory_edge"], 7.5, 6.0)
    box(ax, 0.220, low, w, h, "Legality\nCheck", "Outline, Overlap,\nAnd Open Routes", COLORS["final"], COLORS["final_edge"], 7.5, 6.0)
    box(ax, 0.385, low, 0.140, h, "Solution\nOutput", "Write Final\nConfiguration File", COLORS["final"], COLORS["final_edge"], 7.6, 6.0)
    arrow(ax, (mid_xs[5] + w * 0.50, mid), (0.055 + w * 0.50, low + h), COLORS["memory_edge"], rad=0.0)
    arrow(ax, (0.055 + w, low + h / 2), (0.220, low + h / 2))
    arrow(ax, (0.220 + w, low + h / 2), (0.385, low + h / 2))

    ax.text(
        0.745,
        0.188,
        "Full Evaluation Expands The Placement Candidate Into\nChannels, Routed Paths, And Route-Aware Cost.",
        ha="center",
        va="center",
        fontsize=7.2,
        color=COLORS["muted"],
    )

    legend = [
        ("Input", COLORS["input"], COLORS["input_edge"]),
        ("Placement Search", COLORS["search"], COLORS["search_edge"]),
        ("Routing", COLORS["route"], COLORS["route_edge"]),
        ("Evaluation / Memory", COLORS["eval"], COLORS["eval_edge"]),
        ("Final Output", COLORS["final"], COLORS["final_edge"]),
    ]
    x = 0.050
    for name, face, edge in legend:
        ax.add_patch(
            FancyBboxPatch(
                (x, 0.060),
                0.020,
                0.023,
                boxstyle="round,pad=0.002",
                facecolor=face,
                edgecolor=edge,
                linewidth=0.9,
            )
        )
        ax.text(x + 0.027, 0.071, name, fontsize=6.8, color=COLORS["muted"], va="center")
        x += 0.170

    for ext in ("png", "svg", "pdf"):
        fig.savefig(OUT / f"algorithm_flow.{ext}", dpi=450, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    main()
