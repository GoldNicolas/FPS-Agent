"""CLI:  python -m mapgen validate <files...>
          python -m mapgen preview <files...> --out DIR
          python -m mapgen generate --count N --seed S --out DIR [--sheet PNG]
Run from tools/mapgen (or with tools/mapgen on PYTHONPATH)."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .spec import load, save
from .validate import sightline_bands, validate


def _cmd_validate(args) -> int:
    failed = 0
    for f in args.files:
        errs = validate(load(f))
        if errs:
            failed += 1
            print(f"FAIL {f}")
            for e in errs:
                print(f"     {e}")
        else:
            b = sightline_bands(load(f))
            print(f"OK   {f}  (close {b['close']:.2f}  mid {b['mid']:.2f}  long {b['long']:.2f})")
    return 1 if failed else 0


def _cmd_preview(args) -> int:
    from .preview import render

    for f in args.files:
        spec = load(f)
        b = sightline_bands(spec)
        out = Path(args.out) / f"{spec.name}.png"
        render(spec, out, subtitle=f"sightlines close {b['close']:.2f} / mid {b['mid']:.2f} / long {b['long']:.2f}")
        print(f"wrote {out}")
    return 0


def _cmd_generate(args) -> int:
    from .generate import generate
    from .preview import contact_sheet

    out = Path(args.out)
    names, specs = [], []
    for i in range(args.count):
        spec = generate(args.seed + i)
        spec.name = f"gen_{i:03d}"
        save(spec, out / f"{spec.name}.json")
        names.append(spec.name)
        specs.append(spec)
    (out / "pool.json").write_text(json.dumps({"maps": names}, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {len(names)} maps + pool.json to {out}")
    if args.sheet:
        contact_sheet(specs[:12], args.sheet)
        print(f"wrote {args.sheet}")
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="mapgen")
    sub = p.add_subparsers(dest="cmd", required=True)
    v = sub.add_parser("validate"); v.add_argument("files", nargs="+"); v.set_defaults(fn=_cmd_validate)
    pr = sub.add_parser("preview"); pr.add_argument("files", nargs="+"); pr.add_argument("--out", required=True)
    pr.set_defaults(fn=_cmd_preview)
    g = sub.add_parser("generate"); g.add_argument("--count", type=int, default=50); g.add_argument("--seed", type=int, default=0)
    g.add_argument("--out", required=True); g.add_argument("--sheet"); g.set_defaults(fn=_cmd_generate)
    args = p.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
