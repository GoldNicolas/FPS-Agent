from pathlib import Path

from mapgen.spec import load
from mapgen.validate import validate

MAP01 = Path(__file__).resolve().parents[3] / "Game/FPSAgent/Content/MapSpecs/map01.json"


def test_map01_valid_with_sightlines():
    assert validate(load(MAP01)) == []


def test_map01_has_elevation_and_ramps():
    s = load(MAP01)
    assert max(int(ch) for row in s.heights for ch in row) >= 2
    assert sum(row.count(ch) for row in s.tiles for ch in "^v<>") >= 4


def test_map01_scale():
    s = load(MAP01)
    assert 24 <= len(s.tiles) <= 36 and 24 <= len(s.tiles[0]) <= 36   # ~100-140 m across


def test_map01_matches_authoring_script():
    from maps.map01 import build
    assert build().tiles == load(MAP01).tiles      # JSON is regenerated from the script, never hand-edited
