"""Procedural generator: an original 3-lane tactical template with jitter, raised sites/platforms, ramps and cover.

Every map is validated; a failed attempt retries with the next sub-seed, so `generate(seed)` is deterministic.
Template (row 0 = north = defenders):
    defender spawn (top centre) -> corridors west/east -> drop-ins into sites A (NW) / B (NE)
    mid (centre) -> links to A and B;  attacker spawn (bottom centre) -> back alley -> west / mid / east lanes
"""
from __future__ import annotations

import random

from .canvas import Canvas
from .spec import FLOOR, HIGH, LOW, MapSpec, WALL
from .validate import validate

ROWS = COLS = 30
MAX_ATTEMPTS = 200


def generate(seed: int, max_attempts: int = MAX_ATTEMPTS) -> MapSpec:
    for attempt in range(max_attempts):
        rng = random.Random(seed * 1000 + attempt)
        spec = _build(rng, f"gen_seed{seed}")
        if not validate(spec, check_sightlines=False) and not validate(spec):
            return spec
    raise RuntimeError(f"generate({seed}): no valid map in {max_attempts} attempts")


def _raise(m: Canvas, cells: set, level: int) -> None:
    for r, c in cells:
        m.put(r, c, FLOOR, level)


def _rect(r0, r1, c0, c1) -> set:
    return {(r, c) for r in range(r0, r1 + 1) for c in range(c0, c1 + 1)}


def _build(rng: random.Random, name: str) -> MapSpec:
    m = Canvas(ROWS, COLS)
    j = lambda lo, hi: rng.randint(lo, hi)

    # --- spawns ------------------------------------------------------------------------------------
    dc0 = 11 + j(-1, 1)
    m.floor(1, 3, dc0, dc0 + 7)
    m.zone(1, 3, dc0 + 1, dc0 + 6, "d")
    ac0 = 11 + j(-1, 1)
    m.floor(25, 27, ac0, ac0 + 7)
    m.zone(25, 27, ac0 + 1, ac0 + 6, "a")

    # --- sites -------------------------------------------------------------------------------------
    a_r0, a_r1 = 5 + j(0, 1), 10 + j(-1, 0)
    a_c0, a_c1 = 2 + j(0, 1), 9 + j(-1, 0)
    b_r0, b_r1 = 5 + j(0, 1), 11 + j(-1, 0)
    b_c0, b_c1 = 20 + j(0, 1), 27 + j(-1, 0)
    m.floor(a_r0, a_r1, a_c0, a_c1)
    m.floor(b_r0, b_r1, b_c0, b_c1)
    m.zone(a_r0, a_r1, a_c0, a_c1, "A")
    m.zone(b_r0, b_r1, b_c0, b_c1, "B")

    # defender corridors + drop-ins
    m.floor(2, 3, a_c0 + 2, dc0 - 1)
    m.floor(2, 3, dc0 + 8, b_c1 - 2)
    m.floor(4, a_r0 - 1, j(a_c0 + 2, a_c1 - 1), j(a_c0 + 2, a_c1 - 1))
    dbc = j(b_c0, b_c1 - 3)
    m.floor(4, b_r0 - 1, dbc, dbc + 1)

    # --- mid + links ---------------------------------------------------------------------------------
    m_r0, m_r1 = 13 + j(-1, 1), 18 + j(-1, 0)
    m_c0, m_c1 = 11 + j(-1, 1), 18 + j(-1, 1)
    m.floor(m_r0, m_r1, m_c0, m_c1)
    m.zone(m_r0, m_r1, m_c0, m_c1, "m")
    la = j(a_r0 + 2, a_r1 - 1)                       # A link: up from mid, then west into A's east edge
    m.floor(la, m_r0 - 1, m_c0, m_c0 + 1)
    m.floor(la, la + 1, a_c1 + 1, m_c0 + 1)
    lb = j(b_r0 + 2, b_r1 - 1)                       # B link: mirrored
    m.floor(lb, m_r0 - 1, m_c1 - 1, m_c1)
    m.floor(lb, lb + 1, m_c1 - 1, b_c0 - 1)

    # --- lanes + back alley ----------------------------------------------------------------------------
    w_west, w_east = j(2, 3), j(2, 3)
    m.floor(a_r1 + 1, 24, 1, w_west)                 # west lane (touches A's south edge)
    m.floor(a_r1 + 1, a_r1 + 1, 1, a_c0 + 2)
    m.floor(b_r1 + 1, 24, 29 - w_east, 28)           # east lane (touches B's south edge)
    m.floor(b_r1 + 1, b_r1 + 1, b_c1 - 2, 28)
    mlc = j(m_c0 + 1, m_c1 - 2)
    m.floor(m_r1 + 1, 22, mlc, mlc + 1)              # mid lane
    m.floor(23, 24, 1, 28)                           # back alley
    if rng.random() < 0.7:                           # side window mid <-> west lane
        wr = j(m_r0 + 1, m_r1 - 1)
        m.floor(wr, wr, w_west, m_c0 - 1)

    # lane doglegs + staggered alley pillars (break long sightlines)
    for lane_c0, lane_c1 in ((1, w_west), (29 - w_east, 28)):
        r1 = j(15, 18)
        for c in range(lane_c0, lane_c1):
            m.put(r1, c, WALL, 0)
        for c in range(lane_c0 + 1, lane_c1 + 1):
            m.put(r1 + 3, c, WALL, 0)
    m.put(23, j(5, 9), HIGH, 0)
    m.put(24, j(19, 23), HIGH, 0)

    # --- elevation ---------------------------------------------------------------------------------------
    for (r0, r1, c0, c1) in ((a_r0, a_r1, a_c0, a_c1), (b_r0, b_r1, b_c0, b_c1)):
        if rng.random() < 0.6:                       # raised site
            cells = _rect(r0, r1, c0, c1)
            _raise(m, cells, 1)
            m.collar_ramps(cells)
            if rng.random() < 0.5:                   # level-2 perch in a corner, ramp from the site floor
                pr = r0 if rng.random() < 0.5 else r1 - 1
                pc = c0 if rng.random() < 0.5 else c1 - 1
                perch = _rect(pr, pr + 1, pc, pc + 1)
                _raise(m, perch, 2)
                m.collar_ramps(perch)
        elif rng.random() < 0.7:                     # balcony strip along the site's far wall
            cells = _rect(r0, r0 + 1, c0 + 2, c1)
            _raise(m, cells, 1)
            m.collar_ramps(cells)

    br, bc = j(m_r0 + 1, m_r1 - 2), j(m_c0 + 1, m_c1 - 2)  # raised box in mid (always: guarantees elevation)
    box = _rect(br, br + 1, bc, bc + 1)
    _raise(m, box, 1)
    m.collar_ramps(box)

    # --- cover ------------------------------------------------------------------------------------------
    def sprinkle(r0, r1, c0, c1, n):
        for _ in range(n):
            r, c = j(r0, r1), j(c0, c1)
            if m.t[r][c] == FLOOR and m.z[r][c] not in "ad" and not _next_to_ramp(m, r, c):
                m.put(r, c, HIGH if rng.random() < 0.4 else LOW, m.level(r, c))

    sprinkle(a_r0, a_r1, a_c0, a_c1, j(3, 5))
    sprinkle(b_r0, b_r1, b_c0, b_c1, j(3, 5))
    sprinkle(m_r0, m_r1, m_c0, m_c1, j(2, 4))
    sprinkle(2, 3, a_c0 + 2, b_c1 - 2, 2)            # defender corridors

    return m.spec(name)


def _next_to_ramp(m: Canvas, r: int, c: int) -> bool:
    return any(m.inside(r + dr, c + dc) and m.t[r + dr][c + dc] in "^v<>"
               for dr, dc in ((-1, 0), (1, 0), (0, 1), (0, -1)))
