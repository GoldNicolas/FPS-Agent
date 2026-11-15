"""Grid geometry: walkability, heights, movement graph (with one-way drops), strongly connected components, 2.5D LOS."""
from __future__ import annotations

import math

from .spec import EYE_CM, FLOOR, HIGH, HIGH_COVER_CM, LOW, LOW_COVER_CM, RAMPS, VOID, WALL, MapSpec

DIRS = ((-1, 0), (1, 0), (0, 1), (0, -1))


def is_walkable(spec: MapSpec, r: int, c: int) -> bool:
    ch = spec.tile(r, c)
    return ch == FLOOR or ch in RAMPS


def walkable(spec: MapSpec) -> list[tuple[int, int]]:
    return [(r, c) for r, c in spec.cells() if is_walkable(spec, r, c)]


def floor_top_cm(spec: MapSpec, r: int, c: int) -> float:
    """Floor top height; a ramp counts as its midpoint (low level + half a level)."""
    base = spec.level(r, c) * spec.level_cm
    return float(base + spec.level_cm / 2 if spec.tile(r, c) in RAMPS else base)


def obstacle_top_cm(spec: MapSpec, r: int, c: int) -> float:
    ch = spec.tile(r, c)
    if ch == WALL:
        return math.inf
    if ch == VOID:
        return -math.inf
    top = floor_top_cm(spec, r, c)
    if ch == HIGH:
        return top + HIGH_COVER_CM
    if ch == LOW:
        return top + LOW_COVER_CM
    return top


def _edge_level(spec: MapSpec, r: int, c: int, dr: int, dc: int):
    """Floor level of tile (r, c) along its edge facing (dr, dc). None = a ramp side (not a walkable edge)."""
    ch = spec.tile(r, c)
    h = spec.level(r, c)
    if ch in RAMPS:
        rdr, rdc = RAMPS[ch]
        if (dr, dc) == (rdr, rdc):
            return h + 1
        if (dr, dc) == (-rdr, -rdc):
            return h
        return None
    return h


def can_move(spec: MapSpec, u: tuple[int, int], v: tuple[int, int]) -> bool:
    """One 4-neighbour step u -> v: same level or a drop; ramps only along their axis (leaving sideways = drop)."""
    if not (is_walkable(spec, *u) and is_walkable(spec, *v)):
        return False
    dr, dc = v[0] - u[0], v[1] - u[1]
    target = _edge_level(spec, *v, -dr, -dc)
    if target is None:
        return False  # cannot enter a ramp from its side
    source = _edge_level(spec, *u, dr, dc)
    if source is None:
        source = spec.level(*u)  # stepping off a ramp's side: you drop from its low level at best
    return target <= source


def neighbors_graph(spec: MapSpec) -> dict[tuple[int, int], set[tuple[int, int]]]:
    graph: dict[tuple[int, int], set[tuple[int, int]]] = {}
    for u in walkable(spec):
        graph[u] = set()
        for dr, dc in DIRS:
            v = (u[0] + dr, u[1] + dc)
            if spec.in_bounds(*v) and can_move(spec, u, v):
                graph[u].add(v)
    return graph


def strongly_connected_components(graph: dict) -> list[list]:
    """Iterative Kosaraju."""
    order, seen = [], set()
    for start in graph:
        if start in seen:
            continue
        stack = [(start, iter(graph[start]))]
        seen.add(start)
        while stack:
            node, it = stack[-1]
            nxt = next(it, None)
            if nxt is None:
                stack.pop()
                order.append(node)
            elif nxt not in seen:
                seen.add(nxt)
                stack.append((nxt, iter(graph[nxt])))
    reverse: dict = {u: set() for u in graph}
    for u, vs in graph.items():
        for v in vs:
            reverse[v].add(u)
    comps, assigned = [], set()
    for node in reversed(order):
        if node in assigned:
            continue
        comp, stack = [], [node]
        assigned.add(node)
        while stack:
            u = stack.pop()
            comp.append(u)
            for v in reverse[u]:
                if v not in assigned:
                    assigned.add(v)
                    stack.append(v)
        comps.append(comp)
    return comps


def scc_count(spec: MapSpec) -> int:
    return len(strongly_connected_components(neighbors_graph(spec)))


def has_los(spec: MapSpec, a: tuple[int, int], b: tuple[int, int], eye_cm: float = EYE_CM, step: float = 0.25) -> bool:
    """Straight segment between standing eye points at two tile centers; blocked if it passes at or below an obstacle top."""
    za = floor_top_cm(spec, *a) + eye_cm
    zb = floor_top_cm(spec, *b) + eye_cm
    pa = (a[0] + 0.5, a[1] + 0.5)
    pb = (b[0] + 0.5, b[1] + 0.5)
    dist = math.hypot(pb[0] - pa[0], pb[1] - pa[1])
    n = max(1, int(dist / step))
    for i in range(1, n):
        t = i / n
        r = int(math.floor(pa[0] + (pb[0] - pa[0]) * t))
        c = int(math.floor(pa[1] + (pb[1] - pa[1]) * t))
        if (r, c) == a or (r, c) == b:
            continue
        if za + (zb - za) * t <= obstacle_top_cm(spec, r, c):
            return False
    return True
