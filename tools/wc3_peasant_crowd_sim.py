#!/usr/bin/env python3
"""
Human02 Peasant crowd-routing simulation.

Purpose
-------
Reproduce the worker corridor seen in OpenRealm Human02 logs and compare
simple local-collision policies without touching the engine.

Logged constants used here:
  Gold Mine centre     (-4736, -3840)
  Gold Mine footprint  256 x 256  (half size 128)
  Town Hall centre     (-3776, -4032)
  Town Hall footprint  384 x 384  (half size 192)
  Peasant radius       16
  Peasant step/tick    19

The Town Hall footprint size is recoverable from log samples.  For example
worker (-4009.6, -3833.8) is 233.6 x 198.2 from the Town Hall centre and
reports footprint distance 42.1:
    hypot(233.6 - 192, 198.2 - 192) ~= 42.1

Candidate algorithm: queue_pass_right
-------------------------------------
1. Aim at the closest point on the destination's interaction rectangle.
   Never aim at the building centre.
2. Try the direct step.
3. If another Peasant blocks the step and is moving broadly the same
   direction, WAIT for up to four consecutive blocked ticks.  Do not weave
   around a normal moving queue.
4. Opposing/crossing traffic may sidestep immediately.  A same-direction
   worker may use the same escape only after four blocked ticks, preventing
   a pinned queue from becoming a permanent deadlock.
5. Try right-hand deflections of 15, 30, 45, 60, 75, then 90 degrees.
   Only if all fail, try the opposite side.
6. Reject normal sidesteps more than 5 Peasant radii from the original
   direct trip segment.  After eight blocked ticks, widen that emergency
   bound to 6 radii.
7. On every tick try the exact direct path first again.  No cached lane and
   no left/right hysteresis are needed.

The collision test is a swept-circle check, matching the important property
of OpenRealm's move_is_valid(): a Peasant cannot jump through another one
between ticks.

The Python implementation intentionally uses an O(N^2) neighbour scan.
With N=30 this is already cheap.  The engine should keep using BoxEdicts as
its broad phase, so the C implementation need only inspect local neighbours.
"""

from dataclasses import dataclass, field
import argparse
import csv
import math
import random
import statistics
import time

MINE_CENTER = (-4736.0, -3840.0)
MINE_HALF = (128.0, 128.0)
TOWN_CENTER = (-3776.0, -4032.0)
TOWN_HALF = (192.0, 192.0)

PEASANT_RADIUS = 16.0
STEP = 19.0
ARRIVAL_MARGIN = PEASANT_RADIUS + STEP
NORMAL_MAX_DEVIATION = 5.0 * PEASANT_RADIUS
ESCAPE_MAX_DEVIATION = 6.0 * PEASANT_RADIUS
QUEUE_ESCAPE_TICKS = 4
HARD_ESCAPE_TICKS = 8

ANGLE_STEPS = (15, 30, 45, 60, 75, 90)

# Exact return-path positions appearing in the gameplay logs.
LOG_RETURN_STARTS = (
    (-4575.3, -3865.7),
    (-4575.2, -3934.1),
    (-4576.0, -3901.2),
)


def add(a, b):
    return (a[0] + b[0], a[1] + b[1])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1])


def mul(a, s):
    return (a[0] * s, a[1] * s)


def length(a):
    return math.hypot(a[0], a[1])


def norm(a):
    n = length(a)
    return (a[0] / n, a[1] / n) if n > 1e-9 else (0.0, 0.0)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1]


def rotate(v, radians):
    c = math.cos(radians)
    s = math.sin(radians)
    return (v[0] * c - v[1] * s,
            v[0] * s + v[1] * c)


def point_segment_distance(a, b, p):
    ab = sub(b, a)
    ap = sub(p, a)
    ab2 = dot(ab, ab)
    t = dot(ap, ab) / ab2 if ab2 > 1e-9 else 0.0
    t = max(0.0, min(1.0, t))
    q = add(a, mul(ab, t))
    return length(sub(p, q))


def geometry(destination):
    return (MINE_CENTER, MINE_HALF) if destination == 0 else (TOWN_CENTER, TOWN_HALF)


def inside_interaction_rect(p, center, half):
    return (
        abs(p[0] - center[0]) <= half[0] + ARRIVAL_MARGIN
        and abs(p[1] - center[1]) <= half[1] + ARRIVAL_MARGIN
    )


def closest_interaction_point(p, center, half):
    hx = half[0] + ARRIVAL_MARGIN
    hy = half[1] + ARRIVAL_MARGIN
    return (
        min(max(p[0], center[0] - hx), center[0] + hx),
        min(max(p[1], center[1] - hy), center[1] + hy),
    )


@dataclass
class Agent:
    ident: int
    pos: tuple
    destination: int
    active: bool = True
    pause_ticks: int = 0
    path_length: float = 0.0
    max_deviation: float = 0.0
    stall_run: int = 0
    max_stall: int = 0
    start: tuple = None
    direct_goal: tuple = None
    trace: list = field(default_factory=list)

    def __post_init__(self):
        self.start = self.pos
        center, half = geometry(self.destination)
        self.direct_goal = closest_interaction_point(self.pos, center, half)
        self.trace.append(self.pos)


def preferred_direction(agent):
    center, half = geometry(agent.destination)
    target = closest_interaction_point(agent.pos, center, half)
    return norm(sub(target, agent.pos))


def swept_step_valid(index, start, candidate, agents):
    """Return (valid, blocker_index).  Dynamic circles only."""
    rr = 2.0 * PEASANT_RADIUS

    for j, other in enumerate(agents):
        if j == index or not other.active:
            continue

        segment_distance = point_segment_distance(start, candidate, other.pos)
        if segment_distance >= rr:
            continue

        # Match OpenRealm's important overlap escape rule: if two units already
        # overlap, allow a step that does not deepen the overlap.
        current_distance = length(sub(start, other.pos))
        if current_distance < rr and segment_distance >= current_distance - 0.5:
            continue

        return False, j

    return True, None


def direct_candidate(agent):
    center, half = geometry(agent.destination)
    target = closest_interaction_point(agent.pos, center, half)
    delta = sub(target, agent.pos)
    distance = length(delta)
    if distance <= 1e-9:
        return agent.pos, (0.0, 0.0), 0.0
    direction = norm(delta)
    step = min(STEP, distance)
    return add(agent.pos, mul(direction, step)), direction, step


def choose_direct_wait(agent, index, agents):
    candidate, direction, step = direct_candidate(agent)
    if step == 0.0:
        return agent.pos, "arrive"

    valid, _ = swept_step_valid(index, agent.pos, candidate, agents)
    return (candidate, "direct") if valid else (agent.pos, "wait")


def choose_alternating_slide(agent, index, agents):
    """Approximate the current +/- 15 degree ring search."""
    candidate, direction, step = direct_candidate(agent)
    if step == 0.0:
        return agent.pos, "arrive"

    valid, _ = swept_step_valid(index, agent.pos, candidate, agents)
    if valid:
        return candidate, "direct"

    for degrees in (15, -15, 30, -30, 45, -45, 60, -60, 75, -75, 90, -90):
        moved = rotate(direction, math.radians(degrees))
        candidate = add(agent.pos, mul(moved, step))
        valid, _ = swept_step_valid(index, agent.pos, candidate, agents)
        if valid:
            return candidate, "slide"

    return agent.pos, "wait"


def choose_queue_pass_right(agent, index, agents):
    candidate, direction, step = direct_candidate(agent)
    if step == 0.0:
        return agent.pos, "arrive"

    valid, blocker_index = swept_step_valid(index, agent.pos, candidate, agents)
    if valid:
        return candidate, "direct"

    if blocker_index is None:
        return agent.pos, "wait"

    blocker = agents[blocker_index]
    blocker_direction = preferred_direction(blocker)
    same_stream = dot(direction, blocker_direction) > 0.25

    # A same-direction worker is normally a queue, not something to weave
    # around.  Four blocked ticks is the bounded escape condition discovered by
    # the stress simulation: it handles a queue that is itself pinned by
    # crossing traffic without turning every brief pause into a lane change.
    if same_stream and agent.stall_run < QUEUE_ESCAPE_TICKS:
        return agent.pos, "wait"

    max_deviation = (
        NORMAL_MAX_DEVIATION
        if agent.stall_run < HARD_ESCAPE_TICKS
        else ESCAPE_MAX_DEVIATION
    )

    # Opposing/crossing traffic, or a genuinely stalled same-stream queue:
    # deterministic right-hand passing first.  Retry the direct path next tick.
    for sign in (-1, +1):  # clockwise/right first, then the other side
        for degrees in ANGLE_STEPS:
            moved = rotate(direction, sign * math.radians(degrees))
            candidate = add(agent.pos, mul(moved, step))

            if point_segment_distance(agent.start, agent.direct_goal, candidate) > max_deviation:
                continue

            valid, _ = swept_step_valid(index, agent.pos, candidate, agents)
            if valid:
                return candidate, "slide"

    return agent.pos, "wait"


ALGORITHMS = {
    "direct_wait": choose_direct_wait,
    "alternating_slide": choose_alternating_slide,
    "queue_pass_right": choose_queue_pass_right,
}


def make_logged_return_agents():
    """30 workers returning to the Human02 Town Hall."""
    starts = list(LOG_RETURN_STARTS)

    # Fill the rest with a compact but non-overlapping queue behind the logged
    # positions.  Rows are 36 units apart: only four units more than two
    # Peasant radii, so injected pauses force real interactions.
    rows = (-3988.0, -3952.0, -3916.0, -3880.0, -3844.0)
    columns = (-4576.0 + 36.0 * k for k in range(12))

    for x in columns:
        for y in rows:
            if len(starts) >= 30:
                break
            p = (x, y)
            if all(length(sub(p, q)) >= 34.0 for q in starts):
                starts.append(p)
        if len(starts) >= 30:
            break

    return [Agent(i, p, 1) for i, p in enumerate(starts[:30])]


def make_counterflow_agents():
    """15 workers to Town Hall and 15 to mine, crossing in the same corridor."""
    rows = tuple(-4060.0 + 36.0 * k for k in range(10))
    columns = (-4450.0, -4350.0, -4250.0)
    starts = [(x, y) for x in columns for y in rows]
    return [
        Agent(i, p, 1 if i % 2 == 0 else 0)
        for i, p in enumerate(starts)
    ]


def make_random_agents(seed):
    rng = random.Random(seed)
    starts = []

    while len(starts) < 30:
        p = (
            rng.uniform(-4520.0, -4050.0),
            rng.uniform(-4050.0, -3750.0),
        )
        if all(length(sub(p, q)) >= 34.0 for q in starts):
            starts.append(p)

    return [
        Agent(i, p, 1 if (i + seed) % 2 == 0 else 0)
        for i, p in enumerate(starts)
    ]


def run(agents, algorithm_name, max_ticks=400, interruptions=True, keep_trace=False):
    choose = ALGORITHMS[algorithm_name]
    start_time = time.perf_counter()

    for tick in range(max_ticks):
        # Deterministic temporary interruptions model animation / another worker
        # stopping in a route.  They are intentionally short; an algorithm that
        # treats same-stream workers as things to weave around will amplify them.
        if interruptions and tick and tick % 29 == 0:
            live = [a for a in agents if a.active]
            for offset in (0, 4, 9):
                if live:
                    live[(tick // 29 + offset) % len(live)].pause_ticks = 3 + offset % 3

        for index, agent in enumerate(agents):
            if not agent.active:
                continue

            center, half = geometry(agent.destination)
            if inside_interaction_rect(agent.pos, center, half):
                agent.active = False
                continue

            if agent.pause_ticks:
                agent.pause_ticks -= 1
                agent.stall_run += 1
                agent.max_stall = max(agent.max_stall, agent.stall_run)
                if keep_trace:
                    agent.trace.append(agent.pos)
                continue

            old = agent.pos
            new, move_kind = choose(agent, index, agents)
            travelled = length(sub(new, old))

            if travelled > 1e-9:
                agent.pos = new
                agent.path_length += travelled
                agent.max_deviation = max(
                    agent.max_deviation,
                    point_segment_distance(agent.start, agent.direct_goal, agent.pos),
                )
                agent.stall_run = 0
            else:
                agent.stall_run += 1
                agent.max_stall = max(agent.max_stall, agent.stall_run)

            if keep_trace:
                agent.trace.append(agent.pos)

        if all(not a.active for a in agents):
            break

    elapsed = time.perf_counter() - start_time
    completed = [a for a in agents if not a.active]

    ratios = []
    deviations = []
    for agent in completed:
        direct = max(length(sub(agent.direct_goal, agent.start)), 1e-9)
        ratios.append(agent.path_length / direct)
        deviations.append(agent.max_deviation)

    def percentile(values, p):
        if not values:
            return 0.0
        values = sorted(values)
        index = min(len(values) - 1, max(0, math.ceil(p * len(values)) - 1))
        return values[index]

    return {
        "algorithm": algorithm_name,
        "ticks": tick + 1,
        "completed": len(completed),
        "unfinished": len(agents) - len(completed),
        "max_stall_ticks": max(a.max_stall for a in agents),
        "mean_path_ratio": statistics.mean(ratios) if ratios else 0.0,
        "p95_path_ratio": percentile(ratios, 0.95),
        "mean_max_deviation": statistics.mean(deviations) if deviations else 0.0,
        "p95_max_deviation": percentile(deviations, 0.95),
        "max_deviation": max(deviations) if deviations else 0.0,
        "elapsed_ms": elapsed * 1000.0,
        "agents": agents,
    }


def stress(algorithm_name, seeds):
    results = []
    begin = time.perf_counter()

    for seed in range(seeds):
        results.append(run(make_random_agents(seed), algorithm_name, 400, True))

    elapsed = time.perf_counter() - begin
    failures = sum(bool(r["unfinished"]) for r in results)

    return {
        "algorithm": algorithm_name,
        "seeds": seeds,
        "failures": failures,
        "journeys": sum(r["completed"] for r in results),
        "worst_stall_ticks": max(r["max_stall_ticks"] for r in results),
        "mean_path_ratio": statistics.mean(r["mean_path_ratio"] for r in results),
        "mean_max_deviation": statistics.mean(r["mean_max_deviation"] for r in results),
        "worst_max_deviation": max(r["max_deviation"] for r in results),
        "mean_finish_ticks": statistics.mean(r["ticks"] for r in results),
        "elapsed_ms": elapsed * 1000.0,
    }


def print_result(title, result):
    print(
        f"{title:18} {result['algorithm']:18} "
        f"done={result['completed']:2d}/30 "
        f"ticks={result['ticks']:3d} "
        f"stall_max={result['max_stall_ticks']:3d} "
        f"path_mean={result['mean_path_ratio']:.3f}x "
        f"path_p95={result['p95_path_ratio']:.3f}x "
        f"dev_mean={result['mean_max_deviation']:.1f} "
        f"dev_p95={result['p95_max_deviation']:.1f} "
        f"dev_max={result['max_deviation']:.1f} "
        f"runtime={result['elapsed_ms']:.2f}ms"
    )


def plot_logged(result, output):
    try:
        import matplotlib.pyplot as plt
        from matplotlib.patches import Rectangle
    except ImportError:
        return False

    fig, ax = plt.subplots(figsize=(10, 6))

    for agent in result["agents"]:
        xs = [p[0] for p in agent.trace]
        ys = [p[1] for p in agent.trace]
        ax.plot(xs, ys, linewidth=0.8)

    ax.add_patch(Rectangle(
        (MINE_CENTER[0] - MINE_HALF[0], MINE_CENTER[1] - MINE_HALF[1]),
        MINE_HALF[0] * 2.0,
        MINE_HALF[1] * 2.0,
        fill=False,
        linewidth=2.0,
    ))
    ax.add_patch(Rectangle(
        (TOWN_CENTER[0] - TOWN_HALF[0], TOWN_CENTER[1] - TOWN_HALF[1]),
        TOWN_HALF[0] * 2.0,
        TOWN_HALF[1] * 2.0,
        fill=False,
        linewidth=2.0,
    ))

    ax.set_title("Human02: 30 Peasants using queue_pass_right")
    ax.set_xlabel("world X")
    ax.set_ylabel("world Y")
    ax.set_aspect("equal", adjustable="box")
    ax.grid(True, alpha=0.2)
    fig.tight_layout()
    fig.savefig(output, dpi=150)
    plt.close(fig)
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stress-seeds", type=int, default=100)
    parser.add_argument("--csv", default="openrealm_peasant_simulation_results.csv")
    parser.add_argument("--plot", default="openrealm_peasant_simulation.png")
    args = parser.parse_args()

    rows = []

    print("LOGGED RETURN SCENARIO")
    for algorithm in ALGORITHMS:
        result = run(
            make_logged_return_agents(),
            algorithm,
            max_ticks=400,
            interruptions=True,
            keep_trace=(algorithm == "queue_pass_right"),
        )
        print_result("return->TownHall", result)
        rows.append({
            "scenario": "logged_return_30",
            **{k: v for k, v in result.items() if k != "agents"},
        })
        if algorithm == "queue_pass_right":
            best_logged = result

    print("\nCOUNTERFLOW SCENARIO")
    for algorithm in ALGORITHMS:
        result = run(make_counterflow_agents(), algorithm, 400, True)
        print_result("mine<->TownHall", result)
        rows.append({
            "scenario": "counterflow_30",
            **{k: v for k, v in result.items() if k != "agents"},
        })

    print(f"\nRANDOM STRESS: {args.stress_seeds} seeds x 30 Peasants")
    stress_rows = []
    for algorithm in ("alternating_slide", "queue_pass_right"):
        result = stress(algorithm, args.stress_seeds)
        stress_rows.append(result)
        print(
            f"{algorithm:18} failures={result['failures']:3d}/{result['seeds']} "
            f"journeys={result['journeys']:4d} "
            f"stall_worst={result['worst_stall_ticks']:3d} "
            f"path_mean={result['mean_path_ratio']:.3f}x "
            f"dev_mean={result['mean_max_deviation']:.1f} "
            f"dev_worst={result['worst_max_deviation']:.1f} "
            f"finish_mean={result['mean_finish_ticks']:.1f} ticks "
            f"runtime={result['elapsed_ms']:.1f}ms"
        )

    with open(args.csv, "w", newline="") as f:
        fieldnames = [
            "scenario", "algorithm", "ticks", "completed", "unfinished",
            "max_stall_ticks", "mean_path_ratio", "p95_path_ratio",
            "mean_max_deviation", "p95_max_deviation", "max_deviation",
            "elapsed_ms",
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({k: row.get(k) for k in fieldnames})

    plotted = plot_logged(best_logged, args.plot)

    print("\nRECOMMENDATION")
    print("queue_pass_right")
    print("  - same-direction blocker: queue for four blocked ticks")
    print("  - opposing/crossing traffic, or a four-tick stalled queue: pass right")
    print("  - use the smallest safe 15-degree increment; other side only if needed")
    print("  - normal lateral cap: 5 radii; eight-tick emergency cap: 6 radii")
    print("  - retry the exact direct path every tick; do not cache an approach lane")
    print("  - use the authored footprint edge, never the building centre")
    print(f"\nWrote {args.csv}")
    if plotted:
        print(f"Wrote {args.plot}")


if __name__ == "__main__":
    main()
