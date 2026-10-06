#!/usr/bin/env python3
"""ACC-02.2 research: static scan for stores into 8-byte adaptive cell fields.

Linear capstone disassembly of every Ghidra function in .text (function starts from
research/_ghidra/functions.json, read-only export). A store is reported when its
destination is [base+index*8+4..7] or [reg+4..7] where reg was produced by
LEA reg,[base+index*8] earlier in the same function. Cell pointers passed as call
arguments (15d0e0, 15d1c0, 163ef0) are not visible to this heuristic; they are added
from the xref closure in the handoff. This is a candidate finder, not a proof of
absence; every reported site is classified by hand from assembly.

Requires capstone (not in the shared verify venv):
  uv pip install --python /GitHub/wc3-analysis/verify-venv/bin/python --target /tmp/accw/pylib capstone==5.0.9
  PYTHONPATH=/tmp/accw/pylib /GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/acc02_2_cell_writer_scan.py --out X.json
"""
import argparse
import json
import sys
from pathlib import Path

RESEARCH = Path('/GitHub/wc3-analysis/reports/pathfinding-1.27/research')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(RESEARCH / '_ghidra'))
    import capstone
    from capstone import x86_const as X
    import pe  # hash-guarded read-only DLL reader
    functions = json.loads((RESEARCH / '_ghidra/functions.json').read_text())['functions']
    starts = sorted(int(f['address'], 16) for f in functions)
    names = {int(f['address'], 16): f['name'] for f in functions}
    text = [s for s in pe.secs if s[0] == '.text'][0]
    _, tstart, tsize, _, _ = text
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    found = []
    for i, start in enumerate(starts):
        if not tstart <= start < tstart + tsize:
            continue
        end = min(starts[i + 1] if i + 1 < len(starts) else tstart + tsize, start + 40000)
        lea8, stores = set(), []
        for ins in md.disasm(pe.read(start, end - start), start):
            if ins.mnemonic == 'lea' and ins.operands[1].mem.scale == 8 and ins.operands[1].mem.index != 0:
                lea8.add(ins.operands[0].reg)
            if ins.operands and ins.operands[0].type == X.X86_OP_MEM and ins.operands[0].access & capstone.CS_AC_WRITE \
                    and ins.mnemonic not in ('push', 'call'):
                mem = ins.operands[0].mem
                if (mem.scale == 8 and mem.index != 0 and mem.disp in (4, 5, 6, 7)) or (mem.base in lea8 and mem.index == 0 and mem.disp in (4, 5, 6, 7)):
                    stores.append(dict(va=f'{ins.address:08x}', text=f'{ins.mnemonic} {ins.op_str}', width=ins.operands[0].size))
        if stores:
            found.append(dict(function=f'{start:08x}', name=names.get(start), stores=stores))
    args.out.write_text(json.dumps(dict(dll_sha256=pe.SHA, functions_scanned=len(starts), candidates=found), indent=1) + '\n')
    print(len(found), 'candidate functions')


if __name__ == '__main__':
    raise SystemExit(main())
