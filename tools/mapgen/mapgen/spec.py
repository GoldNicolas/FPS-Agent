"""Map spec format v1: three equal-size character grids (tiles, heights, zones). See README -> Maps."""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

# --- tile characters ---------------------------------------------------------------------------
WALL, FLOOR, LOW, HIGH, VOID = "#", ".", "l", "h", "x"
# Ramp char -> (d_row, d_col) pointing toward its HIGH end. Row 0 is north.
RAMPS = {"^": (-1, 0), "v": (1, 0), ">": (0, 1), "<": (0, -1)}
TILE_CHARS = set("#.lhx") | set(RAMPS)

# --- zone characters ---------------------------------------------------------------------------
ZONE_NONE, ATTACK, DEFEND, MID = ".", "a", "d", "m"
SITES = ("A", "B", "C")
ZONE_CHARS = {ZONE_NONE, ATTACK, DEFEND, MID, *SITES}

# --- vertical dimensions (cm) ------------------------------------------------------------------
EYE_CM = 158.0          # standing bean eye above floor top (C++: FBeanBody standing layout)
LOW_COVER_CM = 120.0    # shoot over standing, hide crouched
HIGH_COVER_CM = 250.0   # blocks standing sight


@dataclass
class MapSpec:
    name: str
    tile_cm: int
    level_cm: int
    wall_cm: int
    tiles: list[str]
    heights: list[str]
    zones: list[str]
    format: int = 1

    @property
    def rows(self) -> int:
        return len(self.tiles)

    @property
    def cols(self) -> int:
        return len(self.tiles[0]) if self.tiles else 0

    def in_bounds(self, r: int, c: int) -> bool:
        return 0 <= r < self.rows and 0 <= c < self.cols

    def tile(self, r: int, c: int) -> str:
        return self.tiles[r][c]

    def level(self, r: int, c: int) -> int:
        ch = self.heights[r][c]
        return int(ch) if ch.isdigit() else 0

    def zone(self, r: int, c: int) -> str:
        return self.zones[r][c]

    def cells(self):
        for r in range(self.rows):
            for c in range(self.cols):
                yield r, c


def to_dict(spec: MapSpec) -> dict:
    return {
        "format": spec.format,
        "name": spec.name,
        "tile_cm": spec.tile_cm,
        "level_cm": spec.level_cm,
        "wall_cm": spec.wall_cm,
        "tiles": list(spec.tiles),
        "heights": list(spec.heights),
        "zones": list(spec.zones),
    }


def from_dict(d: dict) -> MapSpec:
    return MapSpec(
        name=d["name"], tile_cm=int(d["tile_cm"]), level_cm=int(d["level_cm"]), wall_cm=int(d["wall_cm"]),
        tiles=list(d["tiles"]), heights=list(d["heights"]), zones=list(d["zones"]), format=int(d.get("format", 1)),
    )


def load(path) -> MapSpec:
    return from_dict(json.loads(Path(path).read_text(encoding="utf-8")))


def save(spec: MapSpec, path) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(to_dict(spec), indent=2) + "\n", encoding="utf-8", newline="\n")


def tile_center_world(spec: MapSpec, r: int, c: int) -> tuple[float, float, float]:
    """World-space (cm) center of the tile's floor top. Row 0 = north = +X max; column 0 = west (+Y east)."""
    from .grid import floor_top_cm  # local import: grid depends on spec

    x = (spec.rows - 1 - r + 0.5) * spec.tile_cm
    y = (c + 0.5) * spec.tile_cm
    return float(x), float(y), float(floor_top_cm(spec, r, c))
