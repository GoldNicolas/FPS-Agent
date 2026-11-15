"""Mutable drawing surface for authoring/generating specs: carve floors, place ramps/cover, paint zones."""
from __future__ import annotations

from .spec import FLOOR, RAMPS, WALL, MapSpec

DIR_TO_RAMP = {d: ch for ch, d in RAMPS.items()}  # (d_row, d_col) toward the high end -> ramp char


class Canvas:
    def __init__(self, rows: int, cols: int):
        self.rows, self.cols = rows, cols
        self.t = [[WALL] * cols for _ in range(rows)]
        self.h = [["0"] * cols for _ in range(rows)]
        self.z = [["."] * cols for _ in range(rows)]

    # --- primitives ------------------------------------------------------------------------------
    def inside(self, r: int, c: int) -> bool:
        return 0 <= r < self.rows and 0 <= c < self.cols

    def floor(self, r0, r1, c0, c1, level=0):
        for r in range(min(r0, r1), max(r0, r1) + 1):
            for c in range(min(c0, c1), max(c0, c1) + 1):
                if 0 < r < self.rows - 1 and 0 < c < self.cols - 1:  # never open the border
                    self.t[r][c], self.h[r][c] = FLOOR, str(level)

    def put(self, r, c, ch, level):
        self.t[r][c], self.h[r][c] = ch, str(level)

    def zone(self, r0, r1, c0, c1, z):
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                self.z[r][c] = z

    def level(self, r, c) -> int:
        return int(self.h[r][c])

    def is_floor(self, r, c) -> bool:
        return self.inside(r, c) and self.t[r][c] == FLOOR

    # --- ramps -------------------------------------------------------------------------------------
    def place_ramp(self, r, c, toward) -> bool:
        """Turn (r, c) into a ramp rising toward direction `toward` (d_row, d_col).
        Needs plain floor behind it (the low end) at the ramp's level and a high end exactly one level up."""
        dr, dc = toward
        low, high = (r - dr, c - dc), (r + dr, c + dc)
        if not (self.is_floor(*low) and self.inside(*high)):
            return False
        lvl = self.level(*low)
        if self.level(*high) != lvl + 1 or self.t[high[0]][high[1]] not in (FLOOR,):
            return False
        self.put(r, c, DIR_TO_RAMP[(dr, dc)], lvl)
        return True

    def collar_ramps(self, cells: set) -> int:
        """For a raised area (`cells`, all one level up from its surroundings): every floor tile touching it from
        outside becomes a ramp rising into it when possible, otherwise a wall (no one-way edges)."""
        placed = 0
        for r, c in sorted(cells):
            for dr, dc in ((-1, 0), (1, 0), (0, 1), (0, -1)):
                n = (r + dr, c + dc)
                if n in cells or not self.is_floor(*n):
                    continue
                if self.level(*n) == self.level(r, c):
                    continue
                if self.place_ramp(n[0], n[1], (-dr, -dc)):
                    placed += 1
                else:
                    self.put(n[0], n[1], WALL, 0)
        return placed

    def spec(self, name: str) -> MapSpec:
        join = lambda g: ["".join(row) for row in g]
        return MapSpec(name=name, tile_cm=400, level_cm=100, wall_cm=600,
                       tiles=join(self.t), heights=join(self.h), zones=join(self.z))
