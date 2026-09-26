#!/usr/bin/env python3
"""Inventory native gaps in extracted Galaxy scripts without executing incomplete callbacks.

Usage: python3 tools/galaxy_audit.py /path/to/extracted/MapScript.galaxy /path/to/TriggerLibs/*.galaxy
Output is JSON; candidates marked placeholder are constant-return/no-op C bindings,
not proof of complete behavior for other bindings. Roots include every literal
TriggerCreate callback and InitMap, so this is coverage, not a runtime trace.
"""
import argparse
import json
import re
from pathlib import Path

TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', re.S)
HEADER = re.compile(r'\b(?:native\s+)?\w+\s+(\w+)\s*\([^;{}]*\)\s*([;{])')
CALL = re.compile(r'\b(\w+)\s*\(')
KEYWORDS = {'if', 'while', 'for', 'switch', 'return'}


def clean(text):
    return TOKEN.sub(lambda m: m[0] if m[0].startswith('"') else ' ', text)


def bodies(text):
    """Galaxy functions are flat declarations; strings are masked while matching braces."""
    masked = TOKEN.sub(lambda m: ' ' * len(m[0]), text)
    for match in HEADER.finditer(masked):
        if match[2] != '{':
            continue
        end, depth = match.end(), 1
        while depth and end < len(masked):
            depth += (masked[end] == '{') - (masked[end] == '}')
            end += 1
        if depth:
            raise ValueError(f'unclosed function {match[1]}')
        yield match[1], text[match.end():end - 1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('scripts', nargs='+', type=Path)
    args = parser.parse_args()
    source = '\n'.join(clean(p.read_text(encoding='utf-8-sig')) for p in args.scripts)
    functions = dict(bodies(source))
    roots = {'InitMap'} | set(re.findall(r'TriggerCreate\s*\(\s*"(\w+)"', source))
    reachable, calls, pending = set(), set(), list(roots)
    while pending:
        name = pending.pop()
        if name in reachable or name not in functions:
            continue
        reachable.add(name)
        body = TOKEN.sub(lambda m: ' ' * len(m[0]), functions[name])
        children = set(CALL.findall(body)) - KEYWORDS
        calls.update(children)
        pending.extend(children)
    host = Path(__file__).resolve().parents[1] / 'games/starcraft-2/game/galaxy'
    bindings = dict(re.findall(r'\{\s*"(\w+)"\s*,\s*(\w+)\s*}', (host / 'galaxy_host.c').read_text()))
    impl = '\n'.join(p.read_text() for p in host.glob('*.h'))
    placeholder = set(re.findall(
        r'static uint32_t (\w+)\(jass_t \* j\)\s*\{\s*(?:\(void\)j;\s*)?'
        r'return jass_push\w+\(j(?:,\s*(?:0|false|true|"[^"]*"))*\);\s*}', impl))
    external = calls - functions.keys()
    report = {
        'scripts': [str(p) for p in args.scripts],
        'reachable_functions': len(reachable),
        'missing_bindings': sorted(external - bindings.keys()),
        'placeholder_bindings': sorted(n for n in external & bindings.keys() if bindings[n] in placeholder),
    }
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
