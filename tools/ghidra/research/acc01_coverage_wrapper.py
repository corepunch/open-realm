#!/usr/bin/env python3
"""ACC-01.1 research wrapper: run an existing, unchanged oracle script and record
which adaptive-search conditional-jump outcomes its original-code requests execute.

Usage (from the worktree root):
  python tools/ghidra/research/acc01_coverage_wrapper.py --coverage-out OUT.json -- \
      tools/ghidra/verify_wc3_pathing_adaptive.py --binary ... --report ...

The wrapped script runs through runpy as __main__ with its own arguments. The
only change is that every Unicorn engine it constructs also receives a passive
UC_HOOK_BLOCK over 6f1625f0..6f165600. The hook never writes registers or
memory, so the wrapped script's own frozen assertions still decide pass/fail.
"""
import argparse
import json
import runpy
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from acc_research_harness import load_jccs  # noqa: E402

# Complete adaptive requests: route 162cb0 and the two distance queries 1627e0/162910.
# Outcomes executed outside these (direct predicate/lookup controls) are kept separately.
REQUEST_ENTRIES = (0x6f162cb0, 0x6f1627e0, 0x6f162910)
REQUEST_RETURNS = (0x6f162d05, 0x6f162d2d, 0x6f162d4f, 0x6f162d90, 0x6f162894, 0x6f1628b1, 0x6f1628d5, 0x6f162901, 0x6f162934)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--coverage-out', type=Path, required=True)
    parser.add_argument('script', type=Path)
    parser.add_argument('script_args', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    import unicorn
    jccs = load_jccs()
    coverage, outside = {}, {}
    original_init = unicorn.Uc.__init__

    def patched(self, *a, **k):
        original_init(self, *a, **k)
        state = {'last': None, 'request_esp': None}
        cache = {}

        def enter(uc, address, size, user):
            if state['request_esp'] is None:
                state['request_esp'] = uc.reg_read(unicorn.x86_const.UC_X86_REG_ESP)

        def leave(uc, address, size, user):
            if state['request_esp'] == uc.reg_read(unicorn.x86_const.UC_X86_REG_ESP):
                state['request_esp'] = None

        def block(uc, address, size, user):
            last = state['last']
            if last is not None:
                _, target, fall, _ = jccs[last]
                kind = 'taken' if address == target else 'fall' if address == fall else f'other:{address:x}'
                table = coverage if state['request_esp'] is not None else outside
                table[f'{last:08x}:{kind}'] = table.get(f'{last:08x}:{kind}', 0) + 1
            end = cache.get((address, size), -1)
            if end == -1:
                end = None
                for jcc, (_, _, fall, _) in jccs.items():
                    if address <= jcc < address + size and fall == address + size:
                        end = jcc
                cache[(address, size)] = end
            state['last'] = end
        self.hook_add(unicorn.UC_HOOK_BLOCK, block, begin=0x6f1625f0, end=0x6f165600)
        for entry in REQUEST_ENTRIES:
            self.hook_add(unicorn.UC_HOOK_CODE, enter, begin=entry, end=entry)
        for ret in REQUEST_RETURNS:
            self.hook_add(unicorn.UC_HOOK_CODE, leave, begin=ret, end=ret)

    unicorn.Uc.__init__ = patched
    sys.argv = [str(args.script)] + [a for a in args.script_args if a != '--']
    sys.path.insert(0, str(args.script.resolve().parent))
    status = 0
    try:
        runpy.run_path(str(args.script), run_name='__main__')
    except SystemExit as exit_:
        status = exit_.code if isinstance(exit_.code, int) else (0 if exit_.code is None else 1)
    args.coverage_out.parent.mkdir(parents=True, exist_ok=True)
    args.coverage_out.write_text(json.dumps(dict(script=str(args.script), argv=sys.argv[1:], exit_status=status,
                                                 request_entries=[f'{e:08x}' for e in REQUEST_ENTRIES],
                                                 outcomes=dict(sorted(coverage.items())),
                                                 outside_request_outcomes=dict(sorted(outside.items()))), indent=1) + '\n')
    print(f'coverage outcomes {len(coverage)}; wrapped exit {status}')
    return status


if __name__ == '__main__':
    raise SystemExit(main())
