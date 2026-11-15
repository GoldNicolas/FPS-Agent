"""Validation rules for map format v1. `validate(spec)` returns error strings ("<rule>: detail"); [] = valid."""
from __future__ import annotations

import math
import random

from .grid import _edge_level, has_los, is_walkable, neighbors_graph, strongly_connected_components, walkable
from .spec import ATTACK, DEFEND, FLOOR, MID, RAMPS, SITES, TILE_CHARS, VOID, WALL, ZONE_CHARS, MapSpec

MIN_DIM = 8
MIN_SPAWN_TILES = 4
MIN_AREA_TILES = 9
MAX_REPORTED = 5

# Sightline distance bands (metres) and target fractions among visible pairs. Tunable: these encode
# "mostly close/mid fights, a few long angles", not measurements of any existing game.
# Calibration note (2026-11-11): pairs are sampled uniformly by tile, so geometry alone makes <10 m pairs
# rare (~4% of the area within 50 m). On map01 adding cover moved "close" only 0.23 -> 0.25, so the close
# floor is 0.20; the gameplay-relevant guards are the long cap (<=30%) and "some long angles" (>=3%).
SIGHTLINE_SAMPLES = 4000
MAX_SIGHT_M = 50.0
BAND_TARGETS = {"close": (0.20, 1.00), "mid": (0.25, 1.00), "long": (0.03, 0.30)}


def validate(spec: MapSpec, check_sightlines: bool = True) -> list[str]:
    errors = _check_dims(spec)
    if errors:
        return errors  # later rules assume rectangular grids
    errors = _check_chars(spec)
    if errors:
        return errors
    errors += _check_border(spec)
    errors += _check_ramps(spec)
    errors += _check_zones(spec)
    errors += _check_connectivity(spec)
    errors += _check_spawn_los(spec)
    if check_sightlines and not errors:
        errors += _check_sightlines(spec)
    return errors


def _check_dims(spec: MapSpec) -> list[str]:
    rows = len(spec.tiles)
    if rows < MIN_DIM or not spec.tiles or len(spec.tiles[0]) < MIN_DIM:
        return [f"dims: grid must be at least {MIN_DIM}x{MIN_DIM}"]
    cols = len(spec.tiles[0])
    errors = []
    for name, grid in (("tiles", spec.tiles), ("heights", spec.heights), ("zones", spec.zones)):
        if len(grid) != rows:
            errors.append(f"dims: {name} has {len(grid)} rows, expected {rows}")
        errors += [f"dims: {name} row {i} has length {len(row)}, expected {cols}" for i, row in enumerate(grid) if len(row) != cols]
    return errors[:MAX_REPORTED]


def _check_chars(spec: MapSpec) -> list[str]:
    errors = []
    for r, c in spec.cells():
        if spec.tile(r, c) not in TILE_CHARS:
            errors.append(f"chars: tile {spec.tile(r, c)!r} at ({r},{c})")
        if not spec.heights[r][c].isdigit():
            errors.append(f"chars: height {spec.heights[r][c]!r} at ({r},{c})")
        if spec.zone(r, c) not in ZONE_CHARS:
            errors.append(f"chars: zone {spec.zone(r, c)!r} at ({r},{c})")
    return errors[:MAX_REPORTED]


def _check_border(spec: MapSpec) -> list[str]:
    errors = []
    for r, c in spec.cells():
        on_border = r in (0, spec.rows - 1) or c in (0, spec.cols - 1)
        if on_border and spec.tile(r, c) not in (WALL, VOID):
            errors.append(f"border: open tile {spec.tile(r, c)!r} at ({r},{c})")
    return errors[:MAX_REPORTED]


def _check_ramps(spec: MapSpec) -> list[str]:
    errors = []
    for r, c in spec.cells():
        ch = spec.tile(r, c)
        if ch not in RAMPS:
            continue
        dr, dc = RAMPS[ch]
        h = spec.level(r, c)
        low, high = (r - dr, c - dc), (r + dr, c + dc)
        for end, want, facing, label in ((low, h, (dr, dc), "low"), (high, h + 1, (-dr, -dc), "high")):
            if not spec.in_bounds(*end) or not is_walkable(spec, *end):
                errors.append(f"ramps: ({r},{c}) {label} end {end} is not walkable")
            elif _edge_level(spec, *end, *facing) != want:
                errors.append(f"ramps: ({r},{c}) {label} end {end} must be level {want}")
    return errors[:MAX_REPORTED]


def _zone_tiles(spec: MapSpec, zone: str) -> list[tuple[int, int]]:
    return [(r, c) for r, c in spec.cells() if spec.zone(r, c) == zone]


def _check_zones(spec: MapSpec) -> list[str]:
    errors = []
    for zone, label in ((ATTACK, "attacker spawn"), (DEFEND, "defender spawn")):
        cells = _zone_tiles(spec, zone)
        if any(spec.tile(r, c) != FLOOR for r, c in cells):
            errors.append(f"zones: {label} '{zone}' must only cover floor tiles")
        if len(cells) < MIN_SPAWN_TILES:
            errors.append(f"zones: {label} '{zone}' has {len(cells)} tiles, needs {MIN_SPAWN_TILES}")
    for zone, required in (("A", True), ("B", True), ("C", False), (MID, True)):
        n = sum(1 for r, c in _zone_tiles(spec, zone) if is_walkable(spec, r, c))
        if (required or n > 0) and n < MIN_AREA_TILES:
            errors.append(f"zones: '{zone}' has {n} walkable tiles, needs {MIN_AREA_TILES}")
    return errors


def _check_connectivity(spec: MapSpec) -> list[str]:
    comps = strongly_connected_components(neighbors_graph(spec))
    if len(comps) <= 1:
        return []
    comps.sort(key=len)
    sample = sorted(comps[0])[0]
    return [f"connectivity: {len(comps)} strongly connected components (smallest has {len(comps[0])} tiles, e.g. {sample}); "
            f"every raised area needs a ramp back"]


def _check_spawn_los(spec: MapSpec) -> list[str]:
    attackers, defenders = _zone_tiles(spec, ATTACK), _zone_tiles(spec, DEFEND)
    for a in attackers:
        for d in defenders:
            if has_los(spec, a, d):
                return [f"spawn_los: attacker spawn {a} can see defender spawn {d}"]
    return []


def sightline_bands(spec: MapSpec, samples: int = SIGHTLINE_SAMPLES, seed: int = 0, max_m: float = MAX_SIGHT_M) -> dict:
    """Fractions of visible tile pairs (within max_m) per distance band: close <10 m, mid 10-25 m, long 25-50 m."""
    tiles = walkable(spec)
    rng = random.Random(seed)
    tile_m = spec.tile_cm / 100.0
    visible, tried, attempts = [], 0, 0
    while tried < samples and attempts < samples * 50 and len(tiles) >= 2:
        attempts += 1
        a, b = rng.sample(tiles, 2)
        dist = math.hypot(a[0] - b[0], a[1] - b[1]) * tile_m
        if dist > max_m:
            continue
        tried += 1
        if has_los(spec, a, b):
            visible.append(dist)
    n = max(1, len(visible))
    return {
        "close": sum(d < 10 for d in visible) / n,
        "mid": sum(10 <= d < 25 for d in visible) / n,
        "long": sum(25 <= d <= max_m for d in visible) / n,
        "visible_pairs": len(visible),
    }


def _check_sightlines(spec: MapSpec) -> list[str]:
    bands = sightline_bands(spec)
    errors = []
    for band, (lo, hi) in BAND_TARGETS.items():
        if not lo <= bands[band] <= hi:
            errors.append(f"sightlines: {band} fraction {bands[band]:.2f} outside [{lo:.2f}, {hi:.2f}]")
    return errors
