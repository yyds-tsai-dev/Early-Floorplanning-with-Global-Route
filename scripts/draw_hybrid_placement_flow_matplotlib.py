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
    "neutral": "#F7F8FA",
    "neutral_edge": "#B9C0C8",
    "sp": "#D9E8F5",
    "sp_edge": "#4C78A8",
    "bt": "#E4E8D8",
    "bt_edge": "#6B8E23",
    "eval": "#F4E1C6",
    "eval_edge": "#C47F2D",
    "shared": "#ECE7F2",
    "shared_edge": "#7A68A6",
    "final": "#F3D8D4",
    "final_edge": "#B8554B",
}


def box(ax, x, y, w, h, title, desc, face, edge, title_fs=8.2, desc_fs=6.8, lw=1.25):
    patch = FancyBboxPatch(
        (x, y),
        w,
        h,
        boxstyle="round,pad=0.014,rounding_size=0.030",
        linewidth=lw,
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
        linespacing=0.96,
    )
    ax.text(
        x + w / 2,
        y + h * 0.28,
        desc,
        ha="center",
        va="center",
        fontsize=desc_fs,
        color=COLORS["ink"],
        linespacing=1.04,
    )


def dashed_group(ax, x, y, w, h, title):
    patch = FancyBboxPatch(
        (x, y),
        w,
        h,
        boxstyle="round,pad=0.018,rounding_size=0.028",
        linewidth=1.1,
        linestyle=(0, (4, 3)),
        edgecolor=COLORS["shared_edge"],
        facecolor="none",
        alpha=0.85,
    )
    ax.add_patch(patch)
    ax.text(
        x + w / 2,
        y + h + 0.022,
        title,
        ha="center",
        va="bottom",
        fontsize=7.2,
        color=COLORS["shared_edge"],
        fontweight="bold",
    )


def arrow(ax, a, b, color=None, rad=0.0, lw=1.25):
    ax.add_patch(
        FancyArrowPatch(
            a,
            b,
            arrowstyle="-|>",
            mutation_scale=12,
            linewidth=lw,
            color=color or COLORS["ink"],
            connectionstyle=f"arc3,rad={rad}",
            shrinkA=4,
            shrinkB=4,
        )
    )


def main():
    fig, ax = plt.subplots(figsize=(12.0, 6.2))
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    top_y = 0.690
    bot_y = 0.245
    h = 0.145
    w = 0.125
    gap = 0.025
    xs = [0.030, 0.180, 0.330, 0.480, 0.630, 0.795]

    ax.text(0.500, 0.915, "Phase One: Global Exploration", ha="center", fontsize=10.5, fontweight="bold", color=COLORS["ink"])
    box(ax, xs[0], top_y, w, h, "Problem\nInput", "CSV Constraints\nAnd Run Options", COLORS["neutral"], COLORS["neutral_edge"], 7.8, 6.4)
    box(ax, xs[1], top_y, w, h, "Sequence-Pair\nStart", "Initial Global\nSearch State", COLORS["sp"], COLORS["sp_edge"], 7.4, 6.2)
    box(ax, xs[2], top_y, w, h, "Sequence-Pair\nPerturbation", "Swap Orders\nTune Shapes", COLORS["sp"], COLORS["sp_edge"], 6.9, 6.0)
    box(ax, xs[3], top_y, w, h, "Sequence-Pair\nUnpacking", "Encoded State\nTo Rectangles", COLORS["sp"], COLORS["sp_edge"], 6.9, 6.0)
    box(ax, xs[4], top_y, w, h, "Candidate\nEvaluation", "Same Scoring\nAnd Routing Logic", COLORS["eval"], COLORS["eval_edge"], 7.3, 6.1)
    box(ax, xs[5], top_y, 0.145, h, "Best Sequence-Pair\nPlacement", "Selected For\nSeed Conversion", COLORS["sp"], COLORS["sp_edge"], 6.8, 6.0)

    for i in range(5):
        x0 = xs[i] + (0.145 if i == 5 else w)
        arrow(ax, (xs[i] + w, top_y + h / 2), (xs[i + 1], top_y + h / 2))

    ax.text(0.365, top_y - 0.055, "Search Accept Or Reject", fontsize=7.0, color=COLORS["sp_edge"], ha="center")
    arrow(ax, (xs[4] + 0.020, top_y), (xs[2] + w * 0.55, top_y), COLORS["sp_edge"], rad=-0.22, lw=1.1)

    # Sequential handoff.
    box(ax, 0.795, 0.470, 0.145, 0.135, "B-Star\nConversion", "Build Seed From\nBest Placement", COLORS["shared"], COLORS["shared_edge"], 7.4, 6.1)
    arrow(ax, (xs[5] + 0.072, top_y), (0.867, 0.605), COLORS["shared_edge"], lw=1.25)
    ax.plot(
        [0.795, 0.085, 0.085],
        [0.485, 0.485, bot_y + h + 0.010],
        color=COLORS["shared_edge"],
        linewidth=1.15,
    )
    arrow(ax, (0.085, bot_y + h + 0.010), (xs[0] + 0.063, bot_y + h), COLORS["shared_edge"], lw=1.15)
    ax.text(
        0.280,
        0.505,
        "Converted Seed Starts Phase Two",
        ha="center",
        va="bottom",
        fontsize=7.2,
        color=COLORS["shared_edge"],
        fontweight="bold",
    )

    ax.text(0.500, 0.415, "Phase Two: Local Compact Refinement", ha="center", fontsize=10.5, fontweight="bold", color=COLORS["ink"])
    box(ax, xs[0], bot_y, w, h, "B-Star\nStart", "Seed State For\nRefinement", COLORS["bt"], COLORS["bt_edge"], 7.6, 6.2)
    box(ax, xs[1], bot_y, w, h, "B-Star\nPerturbation", "Swap Labels Or\nMove A Leaf", COLORS["bt"], COLORS["bt_edge"], 7.1, 6.0)
    box(ax, xs[2], bot_y, w, h, "B-Star\nUnpacking", "Contour-Based\nRectangle Layout", COLORS["bt"], COLORS["bt_edge"], 7.2, 6.0)
    box(ax, xs[3], bot_y, w, h, "Candidate\nEvaluation", "Same Scoring\nAnd Routing Logic", COLORS["eval"], COLORS["eval_edge"], 7.3, 6.1)
    box(ax, xs[4], bot_y, w, h, "Placement\nMemory", "Reuse Rounded\nGeometry Results", COLORS["shared"], COLORS["shared_edge"], 7.2, 6.0)
    box(ax, xs[5], bot_y, 0.145, h, "Final Routed\nOutput", "Best Evaluated\nCFG Solution", COLORS["final"], COLORS["final_edge"], 7.3, 6.1)

    for i in range(5):
        arrow(ax, (xs[i] + w, bot_y + h / 2), (xs[i + 1], bot_y + h / 2))

    ax.text(0.220, bot_y - 0.055, "Search Accept Or Reject", fontsize=7.0, color=COLORS["bt_edge"], ha="center")
    arrow(ax, (xs[3] + 0.020, bot_y), (xs[1] + w * 0.55, bot_y), COLORS["bt_edge"], rad=-0.22, lw=1.1)

    dashed_group(ax, xs[4] - 0.018, top_y - 0.018, w + 0.036, h + 0.036, "Reused Module")
    dashed_group(ax, xs[3] - 0.018, bot_y - 0.018, w + 0.036, h + 0.036, "Reused Module")
    ax.text(
        0.625,
        0.535,
        "Same Evaluation Logic,\nCalled In Sequence",
        ha="center",
        va="center",
        fontsize=7.0,
        color=COLORS["shared_edge"],
        fontweight="bold",
    )
    ax.plot(
        [xs[4] + w / 2, 0.625, xs[3] + w / 2],
        [top_y - 0.020, 0.555, bot_y + h + 0.020],
        color=COLORS["shared_edge"],
        linewidth=0.95,
        linestyle=(0, (4, 3)),
        alpha=0.75,
    )

    legend = [
        ("Sequence-Pair", COLORS["sp"], COLORS["sp_edge"]),
        ("B-Star", COLORS["bt"], COLORS["bt_edge"]),
        ("Evaluation", COLORS["eval"], COLORS["eval_edge"]),
        ("Handoff / Memory", COLORS["shared"], COLORS["shared_edge"]),
        ("Final Output", COLORS["final"], COLORS["final_edge"]),
    ]
    x = 0.040
    for name, face, edge in legend:
        ax.add_patch(
            FancyBboxPatch(
                (x, 0.065),
                0.020,
                0.023,
                boxstyle="round,pad=0.002",
                facecolor=face,
                edgecolor=edge,
                linewidth=0.9,
            )
        )
        ax.text(x + 0.027, 0.076, name, fontsize=6.8, color=COLORS["muted"], va="center")
        x += 0.175

    for ext in ("png", "svg", "pdf"):
        fig.savefig(OUT / f"hybrid_placement_flow.{ext}", dpi=450, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    main()
