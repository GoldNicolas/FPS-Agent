from mapgen.spec import MapSpec, tile_center_world
from mapgen.grid import floor_top_cm, has_los, scc_count, neighbors_graph


def tiny(tiles, heights=None, zones=None):
    rows = len(tiles)
    heights = heights or ["0" * len(tiles[0])] * rows
    zones = zones or ["." * len(tiles[0])] * rows
    return MapSpec(name="t", tile_cm=400, level_cm=100, wall_cm=600, tiles=tiles, heights=heights, zones=zones)


def test_tile_center_orientation():
    s = tiny(["####", "#..#", "#..#", "####"])
    x, y, z = tile_center_world(s, 0, 0)          # row 0 = north = largest X
    assert (x, y, z) == (1400.0, 200.0, 0.0)
    x2, y2, _ = tile_center_world(s, 3, 3)
    assert (x2, y2) == (200.0, 1400.0)


def test_floor_top_ramp_half_level():
    s = tiny(["#####", "#.^.#", "#####"], ["00000", "00010", "00000"])
    assert floor_top_cm(s, 1, 2) == 50.0            # ramp low level 0 -> +0.5 level
    assert floor_top_cm(s, 1, 3) == 100.0


def test_walls_block_los_low_cover_does_not():
    open_ = tiny(["#######", "#.....#", "#######"])
    wall = tiny(["#######", "#..#..#", "#######"])
    low = tiny(["#######", "#..l..#", "#######"])
    high = tiny(["#######", "#..h..#", "#######"])
    assert has_los(open_, (1, 1), (1, 5))
    assert not has_los(wall, (1, 1), (1, 5))
    assert has_los(low, (1, 1), (1, 5))
    assert not has_los(high, (1, 1), (1, 5))


def test_platform_edge_blocks_low_eye():
    # a 2 m platform between two level-0 tiles hides them from each other
    s = tiny(["#######", "#.....#", "#######"], ["0000000", "0022200", "0000000"])
    assert not has_los(s, (1, 1), (1, 5))


def test_drop_is_one_way_ramp_is_two_way():
    drop = tiny(["#####", "#...#", "#####"], ["00000", "01000", "00000"])
    g = neighbors_graph(drop)
    assert (1, 2) in g[(1, 1)] and (1, 1) not in g[(1, 2)]    # down yes, up no
    assert scc_count(drop) == 2
    ramp = tiny(["######", "#.>..#", "######"], ["000000", "000110", "000000"])
    assert scc_count(ramp) == 1
