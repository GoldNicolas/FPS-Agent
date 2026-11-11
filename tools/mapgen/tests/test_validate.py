from mapgen.spec import MapSpec
from mapgen.validate import validate


def base():
    # 14x14 valid toy: attacker south, defender north, A west, B east, mid centre,
    # a level-1 platform in A reached by a ramp; two wall bars hide the spawns from each other.
    tiles = [
        "##############",
        "#............#",
        "#............#",
        "#...######...#",
        "#...######...#",
        "#.^..........#",
        "#............#",
        "#............#",
        "#............#",
        "#...######...#",
        "#...######...#",
        "#............#",
        "#............#",
        "##############",
    ]
    heights = ["00000000000000"] * 14
    heights[3] = "01110000000000"
    heights[4] = "01110000000000"
    zones = [
        "..............",
        ".....dddd.....",
        ".....dddd.....",
        ".AAA......BBB.",
        ".AAA......BBB.",
        ".AAA..mmm.BBB.",
        "......mmm.....",
        "......mmm.....",
        "......mmm.....",
        "..............",
        "..............",
        ".....aaaa.....",
        ".....aaaa.....",
        "..............",
    ]
    return MapSpec(name="toy", tile_cm=400, level_cm=100, wall_cm=600, tiles=tiles, heights=heights, zones=zones)


def errors_with(spec, key):
    return [e for e in validate(spec, check_sightlines=False) if e.startswith(key)]


def test_toy_is_valid():
    assert validate(base(), check_sightlines=False) == []


def test_open_border():
    s = base(); s.tiles[0] = "#.############"
    assert errors_with(s, "border")


def test_bad_ramp():
    s = base(); s.heights[5] = "00100000000000"   # ramp low end now level 1 -> high side must be 2
    assert errors_with(s, "ramps")


def test_dead_end_drop():
    s = base(); s.tiles[5] = "#............#"     # remove the ramp: platform becomes drop-only
    assert errors_with(s, "connectivity")


def test_missing_site_b():
    s = base(); s.zones = [z.replace("B", ".") for z in s.zones]
    assert errors_with(s, "zones")


def test_spawns_see_each_other():
    s = base()
    for r in (3, 4, 9, 10):                       # open a 2-wide gap straight through both wall bars
        s.tiles[r] = s.tiles[r][:6] + ".." + s.tiles[r][8:]
    assert errors_with(s, "spawn_los")
