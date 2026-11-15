"""Spike B: summarize a spikeB.sbatch run folder into markdown tables and CSV files.

Usage (la-train python, -I keeps the run folder off the import path):
    ~/beanRant/envs/la-train/bin/python -I cluster/scripts/spikeB_summarize.py ~/beanRant/runs/spikeB-<date>-<job>

sim_n<N>_k<K>/   (policy playback, no trainer)
    steps/s per process = K * 60 * (game seconds) / (wall seconds) between its "SpikeA stats" lines,
    skipping the first 5 s after the first line (Spike C showed exactly one step per agent per 1/60 s).
    CPU cores per process = CPU seconds / wall seconds between the first and last `ps` sample.
train_n<N>_bs<B>/   (trainer --batch-size B + N games)
    batch period = mean time between the trainer's "Iter:" lines from the 3rd on (the 1st includes the
    critic warm-up). Experience used per batch = B * MaxStepNum (the game sends its replay buffer when
    it holds MaxStepNum steps; the value comes from the saved trainer config). Simulated = K * 60 *
    final game time, summed over games, so used/simulated shows how much was discarded as stale.
Writes sim.csv / train.csv into the run folder.
STAGE=S0 runs (L-007) read the "Combat: ready" / "CombatStats" lines instead: a step is one agent decision
(15 per game second, from decisionsPerSecond= in the ready line), and the train table adds the final ramp
level and clear rate of the last CombatStats line (mean over games).
"""
import calendar
import csv
import glob
import json
import os
import re
import statistics
import sys
import time

PREFIX = re.compile(r"^\[(\d{4})\.(\d{2})\.(\d{2})-(\d{2})\.(\d{2})\.(\d{2}):(\d{3})\]")
STATS = re.compile(r"(?:SpikeA stats|CombatStats): gameTime=(\d+)s .*(?:agentSteps|decisions)/s\(wall\)=(\d+)")
READY = re.compile(r"(?:SpikeA: ready\. arenas=(\d+)|Combat: ready\. stage=\S+ arenas=(\d+) .*decisionsPerSecond=([\d.]+))")
COMBAT = re.compile(r"CombatStats: .* clear=([\d.]+)% .* level=([\d.]+)\(")
ITER = re.compile(r"^(\d+\.\d+) .*Iter:\s+(\d+) \| Avg Rew: \S+ \| Avg Rew Sum:\s+(\S+) \| Avg Return: \S+ \| "
                  r"Avg Episode Len:\s+(\S+) \| Batch Size: (\d+)")
PROFILE = re.compile(r"Profile\| (.+?)\s+(\d+)ms")
TARGET = 9.0  # "learned": Avg Rew Sum >= 9 (scripted upper bound is about +10.25)


def ue_wall(m):
    y, mo, d, h, mi, s, ms = (int(x) for x in m.groups()[:7])
    return calendar.timegm((y, mo, d, h, mi, s, 0, 0, 0)) + ms / 1000.0  # UE log timestamps are UTC


def read_info(d):
    info = {}
    with open(os.path.join(d, "info.txt")) as f:
        # key=value pairs; a value may contain spaces ("game_exit_codes=0 0 0 0") up to the next key=
        for k, v in re.findall(r"(\w+)=(.*?)(?=\s+\w+=|$)", f.read().replace("\n", " ")):
            info[k] = v.strip()
    return info


def game_stats(path):
    """-> arenas, steps per game second per arena (60 SpikeA, 15 S0), [(wall, gameTime, steps/s)], last (clear %, level)"""
    arenas, hz, rows, last = None, 60.0, [], None
    with open(path, errors="replace") as f:
        for line in f:
            p = PREFIX.match(line)
            if not p:
                continue
            if (m := READY.search(line)):
                if m.group(1):
                    arenas = int(m.group(1))
                else:
                    arenas, hz = int(m.group(2)), float(m.group(3))
            elif (m := STATS.search(line)):
                rows.append((ue_wall(p), int(m.group(1)), int(m.group(2))))
                if (c := COMBAT.search(line)):
                    last = (float(c.group(1)), float(c.group(2)))
    return arenas, hz, rows, last


def steady_rate(arenas, hz, rows, skip=5.0):
    if arenas is None or len(rows) < 2:
        return None
    rows = [r for r in rows if r[0] >= rows[0][0] + skip] or rows
    if len(rows) < 2 or rows[-1][0] <= rows[0][0]:
        return None
    return arenas * hz * (rows[-1][1] - rows[0][1]) / (rows[-1][0] - rows[0][0])


def cpu_samples(d):
    """pid -> list of (t, cpu_seconds, rss_kb, threads, args)"""
    out, t = {}, None
    path = os.path.join(d, "cpu.txt")
    if not os.path.exists(path):
        return out
    with open(path) as f:
        for line in f:
            if line.startswith("T "):
                t = float(line.split()[1])
                continue
            parts = line.split(None, 4)
            if t is None or len(parts) < 5:
                continue
            pid, times, rss, nlwp, args = parts
            try:
                out.setdefault(int(pid), []).append((t, float(times), int(rss), int(nlwp), args))
            except ValueError:
                pass
    return out


def cores(samples):
    if len(samples) < 2 or samples[-1][0] <= samples[0][0]:
        return None
    return (samples[-1][1] - samples[0][1]) / (samples[-1][0] - samples[0][0])


def fmt(x, nd=0):
    return "n/a" if x is None else f"{x:,.{nd}f}"


def summarize_sim(run):
    rows = []
    for d in sorted(glob.glob(os.path.join(run, "sim_n*_k*")), key=lambda p: tuple(int(x) for x in re.findall(r"\d+", os.path.basename(p)))):
        info = read_info(d)
        n, k = int(info["n"]), int(info["k"])
        stats = [game_stats(p) for p in glob.glob(os.path.join(d, "game_*.log"))]
        rates = [r for r in (steady_rate(*g[:3]) for g in stats) if r]
        hz = stats[0][1] if stats else 60.0  # steps per game second per arena: 60 SpikeA, 15 S0
        engines = {pid: s for pid, s in cpu_samples(d).items() if "Binaries/Linux/FPSAgent " in s[0][4]}
        c = [x for x in (cores(s) for s in engines.values()) if x is not None]
        rss = [max(x[2] for x in s) / 1024 for s in engines.values()]
        thr = [max(x[3] for x in s) for s in engines.values()]
        total = sum(rates) if rates else None
        per = statistics.fmean(rates) if rates else None
        rows.append({
            "n": n, "k": k, "level": info.get("level", ""), "agents": n * k, "processes_measured": len(rates),
            "total_steps_s": total, "per_process_steps_s": per,
            "ticks_s": per / k if per else None, "game_s_per_wall_s": per / k / hz if per else None,
            "cores_per_process": statistics.fmean(c) if c else None, "cores_total": sum(c) if c else None,
            "steps_s_per_core": total / sum(c) if total and c else None,
            "rss_mb_per_process": statistics.fmean(rss) if rss else None, "threads_per_process": max(thr) if thr else None,
        })
    if not rows:
        return
    print("\n### Simulation only (policy playback, no trainer)\n")
    print("| N proc | K arenas | level | agents | steps/s total | steps/s per process | steps/s per arena | game s per wall s | CPU cores per process | steps/s per core | RSS MB per process | threads |")
    print("|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for r in rows:
        print(f"| {r['n']} | {r['k']} | {r['level'] or '-'} | {r['agents']} | {fmt(r['total_steps_s'])} | {fmt(r['per_process_steps_s'])} | {fmt(r['ticks_s'])} | "
              f"{fmt(r['game_s_per_wall_s'], 1)} | {fmt(r['cores_per_process'], 2)} | {fmt(r['steps_s_per_core'])} | {fmt(r['rss_mb_per_process'])} | {r['threads_per_process']} |")
    write_csv(os.path.join(run, "sim.csv"), rows)


def find_key(obj, key):
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k == key:
                return v
            if (r := find_key(v, key)) is not None:
                return r
    elif isinstance(obj, list):
        for v in obj:
            if (r := find_key(v, key)) is not None:
                return r
    return None


def gpu_util(d, t0, t1):
    util, mem = [], []
    path = os.path.join(d, "gpu.csv")
    if not os.path.exists(path):
        return None, None, None
    with open(path) as f:
        for row in csv.reader(f):
            if len(row) < 3 or "%" not in row[1]:
                continue
            try:
                ts = time.mktime(time.strptime(row[0].strip().split(".")[0], "%Y/%m/%d %H:%M:%S"))
            except ValueError:
                continue
            if t0 <= ts <= t1:
                util.append(float(row[1].strip().split()[0]))
                mem.append(float(row[2].strip().split()[0]))
    if not util:
        return None, None, None
    return statistics.median(util), max(util), max(mem)


def summarize_train(run):
    rows = []
    for d in sorted(glob.glob(os.path.join(run, "train_*")), key=lambda p: tuple(int(x) for x in re.findall(r"\d+", os.path.basename(p)))):
        info = read_info(d)
        if "launch" not in info:
            continue
        n, bs, k = int(info["n"]), int(info["batch_size"]), int(info["arenas"])
        launch = float(info["launch"])
        end = float(info.get("games_end", "nan"))
        iters, prof, cpu_fallback = [], {}, False
        # newline="\n": the trainer writes "INFO: \rIter: ...", and default universal newlines would
        # split that line at the \r, away from its timestamp.
        with open(os.path.join(d, "trainer.log"), errors="replace", newline="\n") as f:
            for line in f:
                if (m := ITER.search(line)):
                    iters.append((float(m.group(1)), int(m.group(2)), float(m.group(3)), float(m.group(4)), int(m.group(5))))
                elif (m := PROFILE.search(line)):
                    prof.setdefault(m.group(1), []).append(int(m.group(2)) / 1000.0)
                elif "Defaulting to CPU training" in line:
                    cpu_fallback = True
        cfgs = glob.glob(os.path.join(d, "trainer", "*", "Configs", "*.json"))
        max_steps = find_key(json.load(open(cfgs[0])), "MaxStepNum") if cfgs else None
        sim, lasts = 0.0, []
        for p in glob.glob(os.path.join(d, "game_*.log")):
            arenas, hz, srows, last = game_stats(p)
            if arenas and srows:
                sim += arenas * hz * srows[-1][1]
            if last:
                lasts.append(last)
        steady = iters[2:] if len(iters) > 3 else iters
        period = (steady[-1][0] - steady[0][0]) / (len(steady) - 1) if len(steady) > 1 else None
        hit = next((it for it in iters if it[2] >= TARGET), None)
        used = len(iters) * bs * max_steps if max_steps else None
        engines = [s for s in cpu_samples(d).values() if "Binaries/Linux/FPSAgent " in s[0][4]]
        trainer = [s for s in cpu_samples(d).values() if "train.py" in s[0][4]]
        gmed, gmax, gmem = gpu_util(d, launch, end)
        def prof_median(name):  # trainer's own "Profile|" timing, median per batch (skips the first: warm-up)
            v = prof.get(name, [])
            v = v[1:] if len(v) > 2 else v
            return statistics.median(v) if v else None
        rows.append({
            "folder": os.path.basename(d), "seed0": info.get("seed0", ""), "avg_rew_sum_per_batch": " ".join(f"{it[2]:.2f}" for it in iters),
            "device": info.get("device", "gpu"), "poll": info.get("poll", "stock"),
            "trainer_ran_on": "cpu" if cpu_fallback else "gpu",
            "ppo_train_s": prof_median("PPO train"), "pull_experience_s": prof_median("Pull Experience"),
            "create_batches_s": prof_median("PPO create batches"),
            "n": n, "batch_size": bs, "arenas": k, "agents": n * k, "batches": len(iters),
            "batch_sizes_seen": "/".join(sorted({str(it[4]) for it in iters})),
            "wall_s": end - launch, "batch_period_s": period,
            "used_steps_per_batch": bs * max_steps if max_steps else None,
            "used_steps_s": bs * max_steps / period if max_steps and period else None,
            "used_steps_s_overall": used / (end - launch) if used else None,
            "simulated_steps": sim, "used_fraction": used / sim if used and sim else None,
            "final_avg_rew_sum": statistics.fmean(it[2] for it in iters[-3:]) if iters else None,
            "final_episode_len": statistics.fmean(it[3] for it in iters[-3:]) if iters else None,
            "batch_reaching_9": iters.index(hit) if hit else None, "wall_s_to_9": hit[0] - launch if hit else None,
            "gpu_util_median": gmed, "gpu_util_max": gmax, "gpu_mem_mib_max": gmem,
            "game_cores_total": sum(c for c in (cores(s) for s in engines) if c is not None) if engines else None,
            "trainer_cores": cores(trainer[0]) if trainer else None,
            "game_exit_codes": info.get("game_exit_codes", "?"), "limit_hit": info.get("limit_hit", "?"),
            "final_clear_pct": statistics.fmean(x[0] for x in lasts) if lasts else None,
            "final_level": statistics.fmean(x[1] for x in lasts) if lasts else None,
        })
    if not rows:
        return
    print("\n### Training (external trainer)\n")
    print("| first seed | N games | K arenas | --batch-size | trainer on | polls | batches | s per batch | PPO train s | Pull Experience s | create batches s | used steps/s | used / simulated | Avg Rew Sum (last 3) | batch, wall s to >= 9 | final clear % / level | GPU util median / max | trainer cores | game cores | exits |")
    print("|---:|---:|---:|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|---:|---:|---|")
    for r in rows:
        to9 = "never" if r["batch_reaching_9"] is None else f"{r['batch_reaching_9']}, {r['wall_s_to_9']:.0f} s"
        gpu = "n/a" if r["gpu_util_median"] is None else f"{r['gpu_util_median']:.0f}% / {r['gpu_util_max']:.0f}%"
        dev = r["trainer_ran_on"] + ("" if r["trainer_ran_on"] == r["device"] else f" (asked {r['device']}!)")
        fin = "n/a" if r["final_level"] is None else f"{r['final_clear_pct']:.0f}% / {r['final_level']:.2f}"
        print(f"| {r['seed0'] or 'job*1000'} | {r['n']} | {r['arenas']} | {r['batch_size']} | {dev} | {r['poll']} | {r['batches']} | {fmt(r['batch_period_s'], 2)} | "
              f"{fmt(r['ppo_train_s'], 2)} | {fmt(r['pull_experience_s'], 2)} | {fmt(r['create_batches_s'], 2)} | "
              f"{fmt(r['used_steps_s'])} | {fmt(r['used_fraction'], 2)} | {fmt(r['final_avg_rew_sum'], 2)} | "
              f"{to9} | {fin} | {gpu} | {fmt(r['trainer_cores'], 2)} | {fmt(r['game_cores_total'], 1)} | {r['game_exit_codes']}{' LIMIT' if r['limit_hit'] == '1' else ''} |")
    write_csv(os.path.join(run, "train.csv"), rows)


def write_csv(path, rows):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()), lineterminator="\n")
        w.writeheader()
        for r in rows:
            w.writerow({k: (round(v, 4) if isinstance(v, float) else v) for k, v in r.items()})
    print(f"\n(wrote {path})")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    run = os.path.expanduser(sys.argv[1])
    summarize_sim(run)
    summarize_train(run)
    return 0


if __name__ == "__main__":
    sys.exit(main())
