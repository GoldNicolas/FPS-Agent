"""Spike C: check the fixed 60 Hz step, the uncapped tick and the run speed from Spike A game logs.

Usage (la-train python, -I keeps the run folder off the import path):
    ~/beanRant/envs/la-train/bin/python -I cluster/scripts/spikeC_analyze.py <game logs...>

Inputs are `-abslog` files of `-Stage=SpikeA` games (train, -Infer or -Scripted).

1. Fixed step. Every "SpikeA stats" line comes after 10 s of game time. The engine log prefix carries
   the frame counter modulo 1000 (`[...][fff]`), so with one frame per 1/60 s two consecutive stats
   lines are 600 frames apart (mod 1000). The window's agent-steps (agentSteps/s(wall) x wall time
   between the lines) should equal arenas x 60 x 10.
2. Uncapped tick: game seconds per wall second, from the same lines.
3. Run speed (scripted logs only). Each process logs its first 6 finished episodes
   ("SpikeA: reset agent ..."). The episode return is
       R = (d0 - d_end)/100 * ProgressRewardPerMeter - TimePenaltyPerSecond * n/60 + ReachBonus * reached,
   so the straight-line distance walked in n steps is progress = 100 * (R + 0.5*n/60 - 5*reached) cm.
   A scripted agent walks straight at the target, so a least-squares fit progress = v*n + b gives
   v in cm per step; the speed is v * 60. Acceleration from rest only shifts b.
"""
import argparse
import math
import re
import statistics
import sys

PREFIX = re.compile(r"^\[(\d{4})\.(\d{2})\.(\d{2})-(\d{2})\.(\d{2})\.(\d{2}):(\d{3})\]\[\s*(\d+)\]")
READY = re.compile(r"SpikeA: ready\. arenas=(\d+) mode=(\w+)")
STATS = re.compile(r"SpikeA stats: gameTime=(\d+)s mode=(\w+) .*agentSteps/s\(wall\)=(\d+)")
RESET = re.compile(r"SpikeA: reset agent (\d+) \(episode (\d+)\): steps=(\d+) reached=(\d) distance=(\d+) return=(-?[\d.]+)")


def wall_seconds(m):
    y, mo, d, h, mi, s, ms = (int(x) for x in m.groups()[:7])
    # Same day is enough for these short runs; days keep it monotonic across midnight.
    return ((d * 24 + h) * 60 + mi) * 60 + s + ms / 1000.0


def parse(path):
    arenas, mode, stats, resets = None, None, [], []
    with open(path, errors="replace") as f:
        for line in f:
            p = PREFIX.match(line)
            if not p:
                continue
            if (m := READY.search(line)):
                arenas, mode = int(m.group(1)), m.group(2)
            elif (m := STATS.search(line)):
                stats.append((wall_seconds(p), int(p.group(8)), int(m.group(1)), int(m.group(3))))
            elif (m := RESET.search(line)):
                steps = int(m.group(3))
                if steps > 0:  # steps=0 lines are the first spawn of each agent
                    resets.append((steps, int(m.group(4)), float(m.group(5)), float(m.group(6))))
    return arenas, mode, stats, resets


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--run-speed", type=float, default=540.0, help="FMovementSettings::RunSpeed, cm/s")
    ap.add_argument("--hz", type=float, default=60.0)
    ap.add_argument("--stats-interval", type=int, default=10, help="FReachTaskSettings::StatsLogIntervalSeconds")
    ap.add_argument("--progress-per-m", type=float, default=1.0)
    ap.add_argument("--time-penalty", type=float, default=0.5)
    ap.add_argument("--bonus", type=float, default=5.0)
    ap.add_argument("--radius", type=float, default=100.0)
    a = ap.parse_args()

    ok = True
    episodes = []
    print(f"{'log':<58} {'mode':<9} {'K':>4} {'windows':>7} {'frames=600':>10} {'steps/expected':>14} {'game s/wall s':>13}")
    for path in a.logs:
        arenas, mode, stats, resets = parse(path)
        if mode == "scripted":
            episodes += resets
        if arenas is None or len(stats) < 3:
            print(f"{path[-58:]:<58} {mode or '?':<9} {'?':>4} {len(stats):>7}  (too short for the step checks)")
            continue
        # Skip the first window (startup) and any partial one (the last line of a training game is
        # logged at exit, after less than a full interval).
        windows = [(w1, w2) for w1, w2 in zip(stats[1:-1], stats[2:]) if w2[2] - w1[2] == a.stats_interval]
        frame_ok, ratios, speedups = 0, [], []
        for (t1, f1, g1, _), (t2, f2, g2, rate) in windows:
            dg, dt = g2 - g1, t2 - t1
            if (f2 - f1) % 1000 == round(a.hz * dg) % 1000:
                frame_ok += 1
            if dt > 0:
                ratios.append(rate * dt / (arenas * a.hz * dg))
                speedups.append(dg / dt)
        ratio = statistics.median(ratios) if ratios else float("nan")
        speedup = statistics.median(speedups) if speedups else float("nan")
        print(f"{path[-58:]:<58} {mode:<9} {arenas:>4} {len(windows):>7} {frame_ok:>5}/{len(windows):<4} {ratio:>14.4f} {speedup:>13.1f}")
        ok &= frame_ok == len(windows) and abs(ratio - 1) < 0.01

    reached = [e for e in episodes if e[1] == 1]
    if reached:
        n = [e[0] for e in reached]
        prog = [100 * (R + a.time_penalty * k / a.hz - a.bonus) / a.progress_per_m for (k, _, _, R) in reached]
        start = [p + d for p, (_, _, d, _) in zip(prog, reached)]
        mn, mp = statistics.fmean(n), statistics.fmean(prog)
        sxx = sum((x - mn) ** 2 for x in n)
        v = sum((x - mn) * (y - mp) for x, y in zip(n, prog)) / sxx
        b = mp - v * mn
        res = [y - (v * x + b) for x, y in zip(n, prog)]
        sd = statistics.pstdev(res)
        se = sd / math.sqrt(sxx) if sxx > 0 else float("nan")
        speed = v * a.hz
        err = 100 * (speed / a.run_speed - 1)
        print()
        print(f"run speed from {len(reached)} scripted episodes (of {len(episodes)} logged; steps {min(n)}..{max(n)}, "
              f"start distance {min(start):.0f}..{max(start):.0f} cm, end distance {min(e[2] for e in reached):.0f}..{max(e[2] for e in reached):.0f} cm)")
        print(f"  fit: progress = {v:.4f} cm/step * steps {b:+.1f} cm   (residual sd {sd:.2f} cm, slope se {se:.4f})")
        print(f"  speed = {speed:.2f} cm/s vs RunSpeed {a.run_speed:.0f} cm/s: {err:+.3f}%  -> {'PASS' if abs(err) < 1 else 'FAIL'} (1% criterion)")
        ok &= abs(err) < 1
    else:
        print("\nno scripted episodes found: run speed not checked")
    print(f"\noverall: {'PASS' if ok else 'FAIL'} (checks above)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
