#!/usr/bin/env python3
"""Shared original-code (Unicorn) oracle for SEP-01.2..SEP-04.3 research handoffs.

Executes unmodified game.dll 1.27.1.7085 instructions; nothing is stubbed:
  * 6f004790 settings initializer (shipped CRT isdigit, default locale branch);
  * Separate_Update pair slice 6f170359..6f170367 + 6f1703e0..6f170518 (one neighbor);
  * Separate_Update tail 6f170525..RET 4 (damping, deadzone, cooldown7 via 6f16ebd0, cap, 6f15fc70);
  * setters 6f171040 / 6f1710c0 / 6f1711e0 in the order used by Mover_ConfigureSeparation 6f1710e0;
  * Separate_FilterCandidate 6f16e830 (ECX spatial object, EDX query; plain RET).
The frame layout mirrors tools/ghidra/verify_wc3_pathing_repulsion.py (accepted SEP-02.4 evidence).
"""
import hashlib
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from wc3_shipped_crt import load_crt  # noqa: E402
from verify_wc3_pathing_numeric import initialize_runtime_scalars  # noqa: E402

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
OWNER_GLOBAL = 0x6fd53a48
SETTINGS = 0x6fd54398


class Oracle:
    def __init__(self, binary):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
        from unicorn import x86_const as X
        self.X = X
        b = Path(binary).read_bytes()
        self.sha = hashlib.sha256(b).hexdigest()
        if self.sha != SHA:
            raise ValueError('unsupported DLL')
        pe = struct.unpack_from('<I', b, 60)[0]
        opt = pe + 24
        base, size = [struct.unpack_from('<I', b, opt + n)[0] for n in (28, 56)]
        u = self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        u.mem_map(base, (size + 4095) & ~4095)
        for i in range(struct.unpack_from('<H', b, pe + 6)[0]):
            s = opt + struct.unpack_from('<H', b, pe + 20)[0] + 40 * i
            va, n, off = struct.unpack_from('<III', b, s + 12)
            if n:
                u.mem_write(base + va, b[off:off + n])
        u.mem_map(0x10000000, 0x20000)
        u.mem_map(0x20000000, 0x10000)
        self.system, self.stack, self.stop = 0x10000000, 0x20008000, 0x30000000
        initialize_runtime_scalars(u, self.stack, self.stop)
        crt = load_crt(u, Path(binary).parent / 'msvcr120.dll')
        self.crt_sha = crt['sha256']
        self.write(0x6fa7c4fc, crt['exports']['isdigit'])
        self.call(0x6f004790)
        self.settings = [self.read(SETTINGS + i * 20, 5) for i in range(16)]
        self.sep, self.config, self.owner = [self.system + n for n in (0x3000, 0x4000, 0x5000)]
        self.frame = self.stack - 0x1000
        self.write(OWNER_GLOBAL, self.owner)

    # memory helpers -------------------------------------------------------
    def write(self, at, *words):
        self.u.mem_write(at, struct.pack('<' + 'I' * len(words), *(w & 0xffffffff for w in words)))

    def read(self, at, n):
        return list(struct.unpack('<' + 'I' * n, self.u.mem_read(at, n * 4)))

    def call(self, entry, ecx=0, *args, edx=None):
        X = self.X
        self.write(self.stack, self.stop, *args)
        self.u.reg_write(X.UC_X86_REG_ESP, self.stack)
        self.u.reg_write(X.UC_X86_REG_ECX, ecx)
        if edx is not None:
            self.u.reg_write(X.UC_X86_REG_EDX, edx)
        self.u.emu_start(entry, self.stop, count=2000000)
        if self.u.reg_read(X.UC_X86_REG_EIP) != self.stop:
            raise RuntimeError('original routine exceeded budget')
        return self.u.reg_read(X.UC_X86_REG_EAX), self.u.reg_read(X.UC_X86_REG_ESP)

    # Separate_Update slices ------------------------------------------------
    def _frame(self, row_words):
        X = self.X
        self.write(self.config, *row_words)
        self.write(self.frame - 8, self.config)
        self.u.reg_write(X.UC_X86_REG_EBP, self.frame)
        self.u.reg_write(X.UC_X86_REG_EBX, self.sep)
        self.u.reg_write(X.UC_X86_REG_ESI, self.config)

    def pair(self, row_words, owner_state, source, candidate, vector):
        """One neighbor: words in, (owner_state_after, vector_after, random_branch) out."""
        X = self.X
        self.write(self.owner, *owner_state)
        self.write(self.sep + 0x18, *vector)
        self.write(self.frame - 0x68, *source)
        self.write(self.frame - 0x60, *candidate)
        self._frame(row_words)
        self.u.reg_write(X.UC_X86_REG_ESP, self.frame - 0x100)
        self.u.emu_start(0x6f170359, 0x6f170367, count=20000)
        self.u.emu_start(0x6f1703e0, 0x6f170518, count=200000)
        if self.u.reg_read(X.UC_X86_REG_EIP) != 0x6f170518:
            raise RuntimeError('pair slice did not reach 6f170518')
        after = self.read(self.owner, 2)
        return after, self.read(self.sep + 0x18, 2), after != list(owner_state)

    def tail(self, row_words, vector, packed):
        """After the neighbor loop: (vector_after, packed_after)."""
        X = self.X
        self.write(self.sep + 0x18, *vector)
        self.write(self.sep + 0x20, packed)
        self._frame(row_words)
        self.write(self.frame, 0, self.stop)
        self.u.reg_write(X.UC_X86_REG_ESP, self.frame - 0x7c)
        self.u.emu_start(0x6f170525, self.stop, count=200000)
        if self.u.reg_read(X.UC_X86_REG_EIP) != self.stop:
            raise RuntimeError('tail did not return')
        w = self.read(self.sep + 0x18, 3)
        return w[:2], w[2]

    def update_body(self, row_index, owner_state, vector, packed, source, candidates):
        """Compose original pair slices in enumeration order, then the original tail.

        Returns per-neighbor rows and the final state; this is the body of Separate_Update after
        the cooldown/speed gates and after the retained-vector application attempt (6f16ffa0),
        which needs the live fine system and is compared separately.
        """
        row = self.settings[row_index]
        state, vec, steps = list(owner_state), list(vector), []
        for cand in candidates:
            state_after, vec_after, drew = self.pair(row, state, source, cand, vec)
            steps.append(dict(candidate=list(cand), before=vec, after=vec_after,
                              ownerBefore=state, ownerAfter=state_after, randomBranch=drew))
            state, vec = state_after, vec_after
        final, packed_after = self.tail(row, vec, packed)
        return dict(steps=steps, accumulated=vec, final=final, packed=packed_after, owner=state)

    # setters / filter ------------------------------------------------------
    def configure_words(self, initial, selector, category, rank):
        """6f171040(sel) -> 6f1710c0(cat) -> 6f1711e0(rank), as in 6f1710e0; returns word +20."""
        self.write(self.sep + 0x20, initial)
        for entry, value in ((0x6f171040, selector), (0x6f1710c0, category), (0x6f1711e0, rank)):
            _, esp = self.call(entry, self.sep, value)
            if esp != self.stack + 8:
                raise RuntimeError('setter is not RET 4')
        return self.read(self.sep + 0x20, 1)[0]
