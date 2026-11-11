"""Top-down PNG previews of map specs (reviewable on GitHub without Unreal)."""
from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

from .spec import HIGH, LOW, RAMPS, VOID, WALL, MapSpec  # noqa: E402

WALL_RGB = (0.13, 0.14, 0.16)
VOID_RGB = (0.0, 0.0, 0.0)
LOW_RGB = (0.86, 0.72, 0.44)
HIGH_RGB = (0.55, 0.35, 0.20)
ZONES = {  # zone char -> (color, label)
    "a": ("#e4572e", "ATTACK"),
    "d": ("#2f7de1", "DEFEND"),
    "A": ("#f2c14e", "A"),
    "B": ("#f2c14e", "B"),
    "C": ("#f2c14e", "C"),
    "m": ("#43a047", "MID"),
}


def _floor_rgb(level: int) -> tuple[float, float, float]:
    v = min(0.95, 0.50 + 0.15 * level)
    return (v, v, v * 0.97)


def _draw(spec: MapSpec, ax, detail: bool = True) -> None:
    img = np.zeros((spec.rows, spec.cols, 3))
    for r, c in spec.cells():
        ch = spec.tile(r, c)
        img[r, c] = {WALL: WALL_RGB, VOID: VOID_RGB, LOW: LOW_RGB, HIGH: HIGH_RGB}.get(ch, _floor_rgb(spec.level(r, c)))
    ax.imshow(img, interpolation="nearest")

    # zone tint + labels
    for z, (color, label) in ZONES.items():
        cells = [(r, c) for r, c in spec.cells() if spec.zone(r, c) == z]
        if not cells:
            continue
        for r, c in cells:
            ax.add_patch(plt.Rectangle((c - 0.5, r - 0.5), 1, 1, color=color, alpha=0.28, lw=0))
        rr = sum(r for r, _ in cells) / len(cells)
        cc = sum(c for _, c in cells) / len(cells)
        if detail:
            ax.text(cc, rr, label, ha="center", va="center", fontsize=9, fontweight="bold", color=color,
                    bbox=dict(boxstyle="round,pad=0.15", fc="white", ec=color, alpha=0.85))

    # ramps (arrow points uphill) and raised-floor level digits
    for r, c in spec.cells():
        ch = spec.tile(r, c)
        if ch in RAMPS:
            dr, dc = RAMPS[ch]
            ax.arrow(c - 0.3 * dc, r - 0.3 * dr, 0.45 * dc, 0.45 * dr, head_width=0.35, head_length=0.25,
                     length_includes_head=True, color="#c62828", lw=1.2)
        elif detail and ch not in (WALL, VOID) and spec.level(r, c) > 0:
            ax.text(c, r, str(spec.level(r, c)), ha="center", va="center", fontsize=6, color="#333333")

    ax.set_xticks(np.arange(-0.5, spec.cols, 1), minor=True)
    ax.set_yticks(np.arange(-0.5, spec.rows, 1), minor=True)
    ax.grid(which="minor", color="#000000", alpha=0.08, lw=0.5)
    ax.tick_params(which="both", bottom=False, left=False, labelbottom=False, labelleft=False)


def render(spec: MapSpec, png_path, subtitle: str = "") -> None:
    fig, ax = plt.subplots(figsize=(spec.cols * 0.32, spec.rows * 0.32 + 0.6))
    _draw(spec, ax)
    meters = spec.cols * spec.tile_cm / 100
    ax.set_title(f"{spec.name}  ({meters:.0f} m across, N = top)  {subtitle}", fontsize=10)
    Path(png_path).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(png_path, dpi=110, bbox_inches="tight")
    plt.close(fig)


def contact_sheet(specs: list[MapSpec], png_path, ncols: int = 4) -> None:
    nrows = (len(specs) + ncols - 1) // ncols
    fig, axes = plt.subplots(nrows, ncols, figsize=(ncols * 4.2, nrows * 4.4))
    for ax in np.atleast_1d(axes).ravel():
        ax.axis("off")
    for spec, ax in zip(specs, np.atleast_1d(axes).ravel()):
        ax.axis("on")
        _draw(spec, ax, detail=False)
        ax.set_title(spec.name, fontsize=9)
    Path(png_path).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(png_path, dpi=90, bbox_inches="tight")
    plt.close(fig)
