#!/usr/bin/env python3
"""Audit shared engine diffs for game-specific symbols and preprocessor guards.

The audit compares the working tree with a git base revision. It is intentionally
small and conservative: terms are maintained here when a generic-sounding name
still belongs to one game's implementation.

Known limitations (see #476 Phase 7): this is a symbol-name screen, not a
substitute for review. It does not see build-level splits (a second game root
compiled alongside games/<game>/), behavior moved into generic modules (named
lifecycle policy behind game-agnostic call shapes), or renderer APIs whose only
game-specific part is a fiction noun the shared layer also uses generically
(e.g. Blight texture handles: the shared terrain-mask sync legitimately names
blight bits, so no blanket blight term exists here).

Examples:
  python3 tools/engine_boundary_audit.py
  python3 tools/engine_boundary_audit.py --base origin/main
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SHARED_PATHS = ("client", "common", "renderer", "server")
GAME_TERMS = {
    "WC3": ("gold", "lumber", "goldmine", "gold_mine"),
    "SC2": ("minerals", "vespene"),
    "WOW": ("talent", "talent_points"),
}
GAME_TERM_RE = re.compile(
    r"(?<![A-Za-z0-9_])(?:" + "|".join(
        re.escape(term) for terms in GAME_TERMS.values() for term in terms
    ) + r")(?![A-Za-z0-9_])",
    re.IGNORECASE,
)
# Component-aware fallback: whole-word matching misses compounds such as
# RESOURCE_GOLD_SOURCE or CL_BuildCursorTooCloseToGoldSource (#412). Split
# identifiers on underscores and camelCase boundaries, then match components
# exactly. Deliberately not a substring search: shared terrain-mask sync may
# name blight bits generically, so fiction nouns shared with generic infra
# stay out of GAME_TERMS (see docstring).
IDENT_RE = re.compile(r"[A-Za-z][A-Za-z0-9_]*")
CAMEL_SPLIT_RE = re.compile(r"(?<=[a-z])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])")
GAME_TERM_SET = {
    term.lower() for terms in GAME_TERMS.values() for term in terms
}
GAME_GUARD_RE = re.compile(r"^\+\s*#\s*ifdef\s+(WC3|SC2|WOW)\b")
ADDED_LINE_RE = re.compile(r"^\+(?!\+)")


def git_diff(base_revision: str) -> str:
    command = ["git", "diff", "--no-color", "--unified=0", base_revision, "--", *SHARED_PATHS]
    result = subprocess.run(command, cwd=ROOT, check=False, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or "git diff failed")
    return result.stdout


def identifier_components(content: str) -> list[str]:
    components = []
    for ident in IDENT_RE.findall(content):
        for part in ident.split("_"):
            components.extend(CAMEL_SPLIT_RE.split(part))
    return [component.lower() for component in components if component]


# Positive fixtures (must flag) and negative fixtures (must stay clean) for
# the matcher. Run with --self-test. The Blight texture API shape is an
# explicit negative: shared terrain-mask sync names blight bits generically,
# so that class of violation stays a manual-review item (see docstring).
POSITIVE_FIXTURES = (
    "+#ifdef WC3\n",
    "+int x = RESOURCE_GOLD_SOURCE_MIN_DISTANCE;\n",
    "+void CL_BuildCursorTooCloseToGoldSource(void);\n",
    "+int gold = 5;\n",
    "+float MineralsCarried = 0;\n",
)
NEGATIVE_FIXTURES = (
    "+void R_LoadTerrainTexture(BYTE tileset);\n",
    "+BYTE blight:1;\n",
    "+vert->blight = (flags & 0x20) != 0;\n",
    "+int marigold_count = 0;\n",
    "+void R_LoadBlightTexture(BYTE tileset);\n",
    "+LPCTEXTURE R_BlightTexture(void);\n",
)


def self_test() -> int:
    failures = 0
    for fixture in POSITIVE_FIXTURES:
        if not audit(fixture):
            print(f"self-test: missed positive fixture: {fixture.strip()}")
            failures += 1
    for fixture in NEGATIVE_FIXTURES:
        if audit(fixture):
            print(f"self-test: flagged negative fixture: {fixture.strip()}")
            failures += 1
    if failures:
        print(f"engine-boundary-audit self-test: {failures} failure(s)")
        return 1
    print("engine-boundary-audit self-test: PASS")
    return 0


def audit(diff: str) -> list[str]:
    violations = []
    for line in diff.splitlines():
        if not ADDED_LINE_RE.match(line):
            continue
        content = line[1:]
        guard_match = GAME_GUARD_RE.match(line)
        if guard_match:
            violations.append(f"new #ifdef {guard_match.group(1)} guard: {content.strip()}")
        word_hit = False
        for match in GAME_TERM_RE.finditer(content):
            term = match.group(0)
            violations.append(f"game-specific term {term!r} in added shared-engine line: {content.strip()}")
            word_hit = True
        if word_hit:
            continue
        for component in identifier_components(content):
            if component in GAME_TERM_SET:
                violations.append(f"game-specific component {component!r} in added shared-engine line: {content.strip()}")
                break
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="main", help="git revision to compare against (default: main)")
    parser.add_argument("--self-test", action="store_true", help="run embedded positive/negative matcher fixtures")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    try:
        violations = audit(git_diff(args.base))
    except RuntimeError as error:
        print(f"engine-boundary-audit: {error}", file=sys.stderr)
        return 2
    if violations:
        print("engine-boundary-audit: violations found:")
        for violation in violations:
            print(f"  {violation}")
        return 1
    print(f"engine-boundary-audit: clean against {args.base}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
