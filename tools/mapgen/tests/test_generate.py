from mapgen.generate import generate
from mapgen.validate import validate


def test_generated_maps_are_valid():
    for seed in range(12):
        assert validate(generate(seed)) == [], f"seed {seed}"


def test_deterministic():
    a, b = generate(7), generate(7)
    assert a.tiles == b.tiles and a.heights == b.heights and a.zones == b.zones


def test_variety_and_elevation():
    specs = [generate(s) for s in range(8)]
    assert len({tuple(s.tiles) for s in specs}) == 8
    assert all(max(int(c) for row in s.heights for c in row) >= 1 for s in specs)
    assert all(sum(row.count(c) for row in s.tiles for c in "^v<>") >= 2 for s in specs)
