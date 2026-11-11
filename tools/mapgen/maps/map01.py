"""Authoring script for map01, an ORIGINAL tactical blockout (not modeled on any existing game's map).

Layout (row 0 = north = defenders, row 29 = south = attackers; 4 m tiles, 120 m across):
  - Defender spawn top centre, attacker spawn bottom centre; a solid block between them (rows 4-6).
  - Site A (west): raised level-1 plaza, three ramp entrances (south lane, east link, north from defender
    spawn) and a level-2 corner perch.
  - Site B (east): level-0 courtyard with high-cover boxes and a level-1 balcony reached by two ramps.
  - Mid: courtyard with a raised box and cover; links to A (west link) and B (east link) via 2-wide chokes,
    and a side window into the west lane.
  - Three lanes (west / mid / east) plus a back alley behind attacker spawn for rotations.

Run:  python -m mapgen.maps.map01   (writes Game/FPSAgent/Content/MapSpecs/map01.json)
"""
from __future__ import annotations

from pathlib import Path

from mapgen.canvas import Canvas
from mapgen.spec import MapSpec, save

ROWS = COLS = 30
OUT = Path(__file__).resolve().parents[3] / "Game/FPSAgent/Content/MapSpecs/map01.json"


def build() -> MapSpec:
    m = Canvas(ROWS, COLS)

    # --- defender side ---------------------------------------------------------------------
    m.floor(1, 3, 11, 18)                  # defender spawn
    m.zone(1, 3, 12, 17, "d")
    m.floor(2, 3, 4, 10)                   # west corridor -> A north ramp
    m.floor(2, 3, 19, 24)                  # east corridor -> B
    m.floor(4, 4, 20, 21)                  # drop-in to B courtyard

    # --- site A: raised plaza (level 1) + perch (level 2) ----------------------------------------
    m.floor(5, 10, 2, 9, level=1)
    m.floor(5, 6, 2, 3, level=2)           # corner perch
    m.put(7, 2, "^", 1)                    # perch ramp: (8,2) L1 -> (6,2) L2
    m.put(4, 6, "v", 0)                    # north ramp: (3,6) L0 -> (5,6) L1
    m.put(11, 4, "^", 0)                   # south ramp: (12,4) L0 -> (10,4) L1
    m.put(7, 10, "<", 0)                   # east ramp: (7,11) L0 -> (7,9) L1
    m.put(8, 6, "l", 1)                    # plaza cover
    m.put(9, 6, "l", 1)
    m.put(6, 8, "h", 1)
    m.zone(5, 10, 2, 9, "A")

    # --- site B: courtyard (level 0) + balcony (level 1) -------------------------------------------
    m.floor(5, 11, 20, 27)
    m.floor(5, 6, 23, 27, level=1)         # balcony
    m.put(7, 24, "^", 0)                   # balcony ramp from the courtyard
    m.put(6, 22, ">", 0)                   # balcony ramp from the west edge
    m.put(9, 21, "h", 0)                   # boxes
    m.put(9, 22, "h", 0)
    m.put(10, 26, "h", 0)
    m.put(8, 26, "l", 0)
    m.put(11, 23, "l", 0)
    m.zone(5, 11, 20, 27, "B")

    # --- mid -------------------------------------------------------------------------------------
    m.floor(13, 18, 11, 18)
    m.floor(15, 16, 14, 15, level=1)       # raised box
    m.put(17, 14, "^", 0)                  # box ramp: (18,14) L0 -> (16,14) L1
    m.put(14, 12, "h", 0)
    m.put(17, 17, "h", 0)
    m.put(13, 16, "l", 0)
    m.zone(13, 18, 11, 18, "m")
    m.floor(7, 12, 11, 12)                 # A link (choke) up to A's east ramp
    m.floor(8, 12, 18, 19)                 # B link (choke) into B courtyard
    m.floor(15, 15, 4, 10)                 # mid window into the west lane

    # --- lanes + attacker side ---------------------------------------------------------------
    m.floor(12, 24, 1, 3)                  # west lane
    m.floor(12, 12, 1, 6)                  # A main approach (to the south ramp)
    m.floor(12, 24, 26, 28)                # east lane (joins B courtyard at row 11)
    m.floor(19, 24, 12, 13)                # mid lane
    m.floor(23, 24, 1, 28)                 # back alley
    m.floor(25, 27, 11, 18)                # attacker spawn
    m.zone(25, 27, 12, 17, "a")

    # --- break up long sightlines: lane doglegs + staggered cover -----------------------------------
    for r, c in ((18, 1), (18, 2), (21, 2), (21, 3)):      # west lane zig-zag
        m.put(r, c, "#", 0)
    for r, c in ((17, 27), (17, 28), (20, 26), (20, 27)):  # east lane zig-zag
        m.put(r, c, "#", 0)
    m.put(23, 8, "h", 0)                   # back alley: staggered pillars
    m.put(24, 21, "h", 0)
    m.put(9, 3, "h", 1)                    # A plaza: extra hard cover
    m.put(6, 5, "l", 1)
    m.put(7, 7, "h", 1)
    m.put(8, 23, "h", 0)                   # B courtyard
    m.put(16, 17, "h", 0)                  # mid
    m.put(2, 8, "h", 0)                    # defender west corridor
    m.put(3, 22, "h", 0)                   # defender east corridor

    return m.spec("map01")


if __name__ == "__main__":
    save(build(), OUT)
    print(f"wrote {OUT}")
