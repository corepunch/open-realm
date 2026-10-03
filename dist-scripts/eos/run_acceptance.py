#!/usr/bin/env python3
"""Run live EOS adapter checks in isolated Linux guest installations."""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid


SCENARIOS = {
    "solo": (("solo",), 1),
    "default": (("host", "guest"), 0),
    "relay": (("host", "guest"), 1),
    "crash": (("crash-host", "guest"), 1),
    "game-default": (("game-host", "game-guest"), 0),
    "game-relay": (("game-host", "game-guest"), 1),
    "game-crash": (("game-crash-host", "game-guest"), 1),
    "game-guest-crash": (("game-survivor", "game-crash-guest"), 1),
}
DEADLINE = 220  # seconds; includes container startup around the 180-second SDK watchdog


def run_scenario(root, image, run_id, scenario):
    roles, relay = SCENARIOS[scenario]
    room = f"ci-{run_id}-{scenario}"
    containers = []
    processes = []
    deadline = time.monotonic() + DEADLINE
    print(f"EOS live check: {scenario} ({room})", flush=True)
    try:
        for role in roles:
            name = f"eos-{run_id}-{scenario}-{role}"
            containers.append(name)
            # Mount only the build/config read-only. Each container owns its
            # writable home and native guest store; the guest rejects equal IDs.
            command = [
                "docker", "run", "--rm", "--name", name,
                "--mount", f"type=bind,source={root},target=/workspace,readonly",
                "--workdir", "/workspace",
                "--env", "OPENREALM_EOS_CONFIG=/workspace/data/eos/eos.cfg",
                "--env", "SDL_VIDEODRIVER=offscreen",
                "--env", "SDL_AUDIODRIVER=dummy",
                "--env", "LIBGL_ALWAYS_SOFTWARE=1",
                image, "build/bin/openwarcraft3-tests", "-data", "build/tests",
                "+dedicated", "1", "+online_force_relay", str(relay),
                "+online_acceptance", role, room,
            ]
            processes.append((role, subprocess.Popen(command)))
        for role, process in processes:
            result = process.wait(timeout=max(0.01, deadline - time.monotonic()))
            if result:
                raise RuntimeError(f"{scenario}: {role} exited with status {result}")
    except subprocess.TimeoutExpired:
        raise RuntimeError(f"{scenario}: container deadline exceeded") from None
    finally:
        # Also stop the surviving peer on failure/cancellation. Never keep guest
        # storage or publish SDK/config/test artifacts from a live run.
        for name in containers:
            cleanup = subprocess.run(["docker", "rm", "--force", name],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if cleanup.returncode and b"No such container" not in cleanup.stderr:
                print(f"EOS container cleanup failed: {name}", file=sys.stderr)
        for _, process in processes:
            process.wait(timeout=10)
    print(f"EOS live check: {scenario} PASS", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", default="ghcr.io/corepunch/open-realm-ci:ubuntu-24.04")
    parser.add_argument("--run-id", default=uuid.uuid4().hex[:16])
    parser.add_argument("--scenario", choices=SCENARIOS)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9-]{1,40}", args.run_id):
        parser.error("run-id must contain 1-40 lowercase letters, digits or hyphens")
    root = Path.cwd().resolve()
    for required in ("build/bin/openwarcraft3-tests", "build/tests/tests.mpq", "data/eos/eos.cfg"):
        if not (root / required).is_file():
            parser.error("Missing live check input: " + required)
    try:
        for scenario in (args.scenario,) if args.scenario else SCENARIOS:
            run_scenario(root, args.image, args.run_id, scenario)
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        print(f"EOS live checks FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
