"""Combat (S0) job helper for cluster/slurm/combat.sbatch (request L-007).

Usage (la-train python; -I keeps the run folder off the import path):
    combat_report.py step <folder>                  markdown summary of one step: game logs, trainer, eval JSON
    combat_report.py level <folder>                 mean over <folder>/stats_*.csv of the last row's levelMean
    combat_report.py progress <run> [B]             timeline of <run>/stats_*.csv in blocks of B windows (default 30):
                                                    episodes, clear/death/timeout rates, return, kills/ep, acc, hs,
                                                    clear time, and the mean level (episode-weighted over games)
    combat_report.py chainstate <run> <L> <W> [C] [M]
                                                    "<end_level> <promoted 0|1> <windows> <clearTop> <topN>" for a
                                                    chain's <run>/stats_*.csv. Promoted (D-031 (6), (11); L-009 (0)) =
                                                    mean levelMean >= L over the last W windows of all games AND the
                                                    pooled clearTop over the same windows and games,
                                                    sum(clearRateTop * episodesTop) / sum(episodesTop), >= C (0.70),
                                                    with at least M (50) gate-level episodes. CSVs without clearTop
                                                    columns (build A) never promote; clearTop prints as -1.
Log formats: README -> "Game log lines to grep"; CSV columns: Learning/CombatStats.cpp (CsvHeader).
"""
import csv
import glob
import gzip
import json
import os
import re
import statistics
import sys

PREFIX = re.compile(r"^\[(\d{4})\.(\d{2})\.(\d{2})-(\d{2})\.(\d{2})\.(\d{2}):(\d{3})\]")
STATS = re.compile(r"CombatStats: gameTime=(\d+)s episodes=(\d+) clear=([\d.]+)% died=([\d.]+)% timeout=([\d.]+)% "
                   r"avgReturn=(-?[\d.]+) kills/ep=([\d.]+) acc=([\d.]+)% hs=([\d.]+)% clearTime=([\d.]+)s "
                   r"level=([\d.]+)\(([\d.]+)\.\.([\d.]+)\) decisions/s\(wall\)=(\d+)"
                   r"(?: clearTop=([\d.]+)%\(n=(\d+)\) botSpeed=(\d+)cm/s)?")
ITER = re.compile(r"^(\d+\.\d+) .*Iter:\s+(\d+) \| Avg Rew: (\S+) \| Avg Rew Sum:\s+(\S+) \| Avg Return: (\S+) \| "
                  r"Avg Episode Len:\s+(\S+) \| Batch Size: (\d+)")
PROFILE = re.compile(r"Profile\| (.+?)\s+(\d+)ms")
KEY_LINES = ("Combat: ready", "Combat: ppo", "Combat: trainer settings", "Settings overrides", "Combat: exiting",
             "SpikeC probe:", "SpikeC: exiting", "exiting (code", "ArenaGameMode:")


def read_info(d):
    info = {}
    p = os.path.join(d, "info.txt")
    if os.path.exists(p):
        with open(p) as f:
            for k, v in re.findall(r"(\w+)=(.*?)(?=\s+\w+=|$)", f.read().replace("\n", " ")):
                info[k] = v.strip()
    return info


def csv_rows(path):
    with open(path, newline="") as f:
        return [r for r in csv.DictReader(f) if r.get("gameTime") not in (None, "", "gameTime")]


def last_levels(folder):
    out = []
    for p in sorted(glob.glob(os.path.join(folder, "stats_*.csv"))):
        rows = csv_rows(p)
        if rows:
            out.append((p, rows))
    return out


def cmd_level(folder):
    files = last_levels(folder)
    print(f"{statistics.fmean(float(r[-1]['levelMean']) for _, r in files):.3f}" if files else "0")


def pooled_top(rows):
    """-> (pooled clearTop, gate-level episodes) over CSV rows; (None, 0) for build-A CSVs without the columns"""
    if not rows or "clearRateTop" not in rows[0]:
        return None, 0
    n = sum(int(x["episodesTop"] or 0) for x in rows)
    clears = sum(float(x["clearRateTop"] or 0) * int(x["episodesTop"] or 0) for x in rows)
    return (clears / n if n else 0.0), n


def cmd_chainstate(run, level, windows, clear_top=0.70, min_top=50):
    files = last_levels(run)
    if not files:
        print("0 0 0 -1 0")
        return
    end = statistics.fmean(float(r[-1]["levelMean"]) for _, r in files)
    n = min(len(r) for _, r in files)
    last = [x for _, r in files for x in r[-windows:]]
    top, top_n = pooled_top(last)
    promoted = (n >= windows and statistics.fmean(float(x["levelMean"]) for x in last) >= level
                and top is not None and top_n >= min_top and top >= clear_top)
    print(f"{end:.3f} {int(promoted)} {n} {-1 if top is None else round(top, 3)} {top_n}")


def cmd_progress(run, block):
    files = last_levels(run)
    if not files:
        print("no stats_*.csv in", run)
        return
    n = min(len(r) for _, r in files)
    print(f"{len(files)} games, {n} windows each (10 s of game time per window; gameTime restarts in each chained job)\n")
    print("| windows | episodes | clear % | died % | timeout % | avg return | kills/ep | acc % | hs % | clear s | level mean (min..max) | clearTop % (n) | botSpeed cm/s | decisions/s per game |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|")
    for a in range(0, n, block):
        rows = [x for _, r in files for x in r[a:min(a + block, n)]]
        ep = sum(int(x["episodes"]) for x in rows)
        w = lambda k: sum(float(x[k]) * int(x["episodes"]) for x in rows) / ep if ep else 0.0
        clears = sum(float(x["clearRate"]) * int(x["episodes"]) for x in rows)
        cs = sum(float(x["avgClearSeconds"]) * float(x["clearRate"]) * int(x["episodes"]) for x in rows) / clears if clears else 0.0
        last = [r[min(a + block, n) - 1] for _, r in files]
        lm = statistics.fmean(float(x["levelMean"]) for x in last)
        lo = min(float(x["levelMin"]) for x in last)
        hi = max(float(x["levelMax"]) for x in last)
        dps = statistics.fmean(float(x["decisionsPerSecond"]) for x in rows)
        top, top_n = pooled_top(rows)
        topc = "n/a" if top is None else f"{100 * top:.1f} ({top_n})"
        bs = f"{w('botSpeed'):.0f}" if "botSpeed" in rows[0] and ep else "n/a"
        print(f"| {a + 1}-{min(a + block, n)} | {ep} | {100 * w('clearRate'):.1f} | {100 * w('deathRate'):.1f} | {100 * w('timeoutRate'):.1f} | "
              f"{w('avgReturn'):.3f} | {w('killsPerEpisode'):.2f} | {100 * w('accuracy'):.1f} | {100 * w('headshotRate'):.1f} | {cs:.1f} | "
              f"{lm:.2f} ({lo:.2f}..{hi:.2f}) | {topc} | {bs} | {dps:,.0f} |")


def game_log(path):
    keys, stats, evals, resets, warn, err, lines, first_t, last_t = [], [], [], 0, [], [], 0, None, None
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt", errors="replace") as f:
        for line in f:
            lines += 1
            p = PREFIX.match(line)
            if p:
                t = tuple(int(x) for x in p.groups())
                first_t = first_t or t
                last_t = t
            if "Resetting Agents" in line:
                resets += 1
                continue
            if (m := STATS.search(line)):
                stats.append(m)
            elif "CombatEval:" in line:
                evals.append(line.split("CombatEval:", 1)[1].strip())
            elif any(k in line for k in KEY_LINES):
                keys.append(re.sub(r"^(\[[^\]]*\]){1,2}", "", line.rstrip()).strip())
            elif ": Error: " in line or "Fatal" in line:
                err.append(line.rstrip())
            elif ": Warning: " in line:
                warn.append(line.rstrip())
    return dict(keys=keys, stats=stats, evals=evals, resets=resets, warn=warn, err=err, lines=lines,
                bytes=os.path.getsize(path), first_t=first_t, last_t=last_t)


def short_stats(m):
    g = m.groups()
    return (f"t={g[0]}s ep={g[1]} clear={g[2]}% died={g[3]}% timeout={g[4]}% ret={g[5]} kills/ep={g[6]} "
            f"acc={g[7]}% hs={g[8]}% clearTime={g[9]}s level={g[10]}({g[11]}..{g[12]}) dec/s={g[13]}"
            + (f" clearTop={g[14]}%(n={g[15]}) botSpeed={g[16]}cm/s" if g[14] is not None else ""))


def trainer_log(path):
    iters, prof = [], {}
    with open(path, errors="replace", newline="\n") as f:  # "INFO: \rIter: ..." must stay one line
        for line in f:
            if (m := ITER.search(line)):
                iters.append(m)
            elif (m := PROFILE.search(line)):
                prof.setdefault(m.group(1), []).append(int(m.group(2)) / 1000.0)
    return iters, prof


def gpu(path, t0, t1):
    import time
    util = []
    if not os.path.exists(path):
        return None
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
    return (statistics.median(util), max(util)) if util else None


def cmd_step(d):
    d = d.rstrip("/")
    info = read_info(d)
    print(f"\n#### `{os.path.basename(d)}`\n")
    print("- info: " + " ".join(f"{k}={v}" for k, v in info.items() if k not in ("start", "end", "launch", "games_end")))
    for p in sorted(glob.glob(os.path.join(d, "game*.log")) + glob.glob(os.path.join(d, "game*.log.gz"))):
        g = game_log(p)
        print(f"- **{os.path.basename(p)}**: {g['lines']:,} lines, {g['bytes'] / 1e6:.2f} MB, "
              f"`Resetting Agents` x {g['resets']:,}, warnings {len(g['warn'])}, errors {len(g['err'])}, CombatStats x {len(g['stats'])}")
        for k in g["keys"]:
            print(f"  - `{k[:400]}`")
        bsp = [int(m.group(17)) for m in g["stats"] if m.group(17) is not None]
        if bsp:
            print(f"  - botSpeed over {len(bsp)} stats lines: min {min(bsp)}, max {max(bsp)} cm/s, {sum(1 for v in bsp if v > 0)} lines > 0")
        if g["stats"]:
            print(f"  - first stats: `{short_stats(g['stats'][0])}`")
            if len(g["stats"]) > 1:
                print(f"  - last stats: `{short_stats(g['stats'][-1])}`")
        for e in g["evals"]:
            print(f"  - CombatEval: `{e}`")
        for w in sorted(set(x[x.find('Warning:'):][:200] for x in g["warn"]))[:8]:
            print(f"  - warning: `{w}`")
        for e in g["err"][:8]:
            print(f"  - error: `{e[:300]}`")
    tl = os.path.join(d, "trainer.log")
    if os.path.exists(tl):
        iters, prof = trainer_log(tl)
        times = [float(m.group(1)) for m in iters]
        steady = times[2:] if len(times) > 3 else times
        per = (steady[-1] - steady[0]) / (len(steady) - 1) if len(steady) > 1 else None
        print(f"- **trainer**: {len(iters)} Iter lines" + (f", {per:.2f} s per batch (from the 3rd)" if per else ""))
        for m in iters[:2] + (iters[-2:] if len(iters) > 3 else iters[2:]):
            print(f"  - Iter {m.group(2)}: Avg Rew Sum {m.group(4)}, Avg Return {m.group(5)}, Avg Episode Len {m.group(6)}, Batch Size {m.group(7)}")
        meds = {k: statistics.median(v[1:] if len(v) > 2 else v) for k, v in prof.items()}
        if meds:
            print("  - Profile medians (s): " + ", ".join(f"{k} {v:.2f}" for k, v in sorted(meds.items(), key=lambda kv: -kv[1])[:6]))
        cfgs = glob.glob(os.path.join(d, "trainer", "*", "Configs", "*.json"))
        if cfgs:
            print(f"  - saved configs: {', '.join(os.path.basename(c) for c in sorted(cfgs))}")
        try:
            u = gpu(os.path.join(d, "gpu.csv"), float(info["launch"]), float(info["games_end"]))
            if u:
                print(f"  - GPU utilisation while the games ran: median {u[0]:.0f}%, max {u[1]:.0f}%")
        except (KeyError, ValueError):
            pass
        with open(tl, errors="replace") as f:
            bad = [l.rstrip() for l in f if "Traceback" in l or "Error" in l]
        for b in bad[:5]:
            print(f"  - trainer: `{b[:300]}`")
    snap = os.path.join(d, "snapshots_final")
    if os.path.isdir(snap):
        print("- snapshots_final: " + ", ".join(f"{n} ({os.path.getsize(os.path.join(snap, n)):,} B)" for n in sorted(os.listdir(snap))))
    for p in sorted(glob.glob(os.path.join(d, "stats_*.csv"))):
        print(f"- {os.path.basename(p)}: {len(csv_rows(p))} rows")
    ej = os.path.join(d, "eval.json")
    if os.path.exists(ej):
        print(f"- eval.json: `{json.dumps(json.load(open(ej)))}`")


def main(argv):
    if len(argv) >= 2 and argv[0] == "step":
        cmd_step(os.path.expanduser(argv[1]))
    elif len(argv) == 2 and argv[0] == "level":
        cmd_level(os.path.expanduser(argv[1]))
    elif len(argv) in (2, 3) and argv[0] == "progress":
        cmd_progress(os.path.expanduser(argv[1]), int(argv[2]) if len(argv) == 3 else 30)
    elif len(argv) in (4, 5, 6) and argv[0] == "chainstate":
        cmd_chainstate(os.path.expanduser(argv[1]), float(argv[2]), int(argv[3]), *(float(a) for a in argv[4:5]), *(int(a) for a in argv[5:6]))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
