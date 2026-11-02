#!/usr/bin/env python3
"""SEP-01.2 original-code oracle: packed policy word, zero settings rows and harness self-check.

1. Harness self-check: reproduce every pair/tail word of the accepted SEP-02.4 fixture
   (tools/ghidra/fixtures/retail-pathfinding-repulsion-1.27.json) with sep_research_oracle.
2. Packed word: run original setters 6f171040/6f1710c0/6f1711e0 (order of 6f1710e0) for all
   selector bytes x all rank bytes, category words incl. >0xff, three prior words.
3. Inert configuration rows 5..15 (all-zero words): pair slice never accumulates, tail never
   installs cooldown; record exact outputs.
"""
import argparse, hashlib, itertools, json, math, random, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from sep_research_oracle import Oracle  # noqa: E402


def f2w(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--fixture', type=Path, default=Path(__file__).resolve().parents[1] / 'fixtures/retail-pathfinding-repulsion-1.27.json')
    p.add_argument('--report', type=Path, required=True)
    a = p.parse_args()
    o = Oracle(a.binary)
    fx = json.loads(a.fixture.read_text())
    assert o.settings == fx['settings'], 'settings rows differ from accepted fixture'
    # 1. harness self-check against accepted fixture words
    for case in fx['pairs']:
        i = case['input']
        owner, row, src, cand, vec = i[0:2], i[2:7], i[7:9], i[9:11], i[11:13]
        after, v, _ = o.pair(row, owner, src, cand, vec)
        assert after + v == case['output'], ('pair', case)
    for case in fx['tails']:
        i = case['input']
        v, packed = o.tail(i[2:7], i[0:2], i[7])
        assert v + [packed] == case['output'], ('tail', case)
    # 2. packed policy word
    rng = random.Random(0x5e0102)
    packed_cases = 0
    aliases = []
    for s, r in itertools.product(range(256), range(256)):
        c = rng.choice([rng.randrange(256), rng.randrange(0x10000)])
        w = rng.choice([0, 0xffffffff, rng.getrandbits(32)])
        got = o.configure_words(w, s, c, r)
        want = (w & 0xffff) | ((s & 15) << 16) | ((c & 0xff) << 20) | ((r & 15) << 28)
        assert got == want, (hex(w), s, c, r, hex(got), hex(want))
        packed_cases += 1
        if w == 0 and s in (1, 17, 5) and r in (0, 1, 16, 17) and len(aliases) < 64:
            aliases.append(dict(prior=w, selector=s, category=c, rank=r, word=got))
    # 3. inert rows 5..15
    zero = o.settings[5]
    assert all(o.settings[k] == zero for k in range(5, 16)) and zero == [0, 0, 0, 0, 0]
    inert_pairs, inert_tails = [], []
    for dist, ang, prior in itertools.product((0.0, 0.0005, 0.25, 1.0, 4.9, 9.9), (0.0, 2.1), ((0, 0), (0.1, -0.2))):
        src = [f2w(16 + dist * math.cos(ang)), f2w(16 + dist * math.sin(ang))]
        cand = [f2w(16.0), f2w(16.0)]
        vec = [f2w(prior[0]), f2w(prior[1])]
        state = [0x12345678, 0x9abcdef0]
        after, v, drew = o.pair(zero, state, src, cand, vec)
        inert_pairs.append(dict(source=src, candidate=cand, prior=vec, ownerBefore=state, ownerAfter=after, vector=v, randomBranch=drew))
        assert v == vec, 'inert row accumulated'
    for mag, packed in itertools.product((0.0, 0.005, 0.3, 5.0), (0x00050000, 0xabc50003)):
        vec = [f2w(mag * 0.6), f2w(-mag * 0.8)]
        v, pk = o.tail(zero, vec, packed)
        inert_tails.append(dict(vector=vec, packed=packed, vectorAfter=v, packedAfter=pk))
        assert pk == packed, 'inert row changed cooldown/policy word'
    result = dict(passed=True, binary_sha256=o.sha, crt_sha256=o.crt_sha,
                  fixture_sha256=hashlib.sha256(a.fixture.read_bytes()).hexdigest(),
                  harness_selfcheck=dict(pairs=len(fx['pairs']), tails=len(fx['tails'])),
                  packed_word_cases=packed_cases,
                  packed_rule='word=(prior&0xffff)|((selector&15)<<16)|((category&0xff)<<20)|((rank&15)<<28)',
                  alias_examples=aliases[:24], inert_rows='5..15 all words 0x00000000',
                  inert_pairs=inert_pairs, inert_tails=inert_tails,
                  scope='Original setters, pair slice and tail only; 6f693d50/6f66fc50 producers, allocation (6f16e6e0), registration (6f170820), query and application excluded.')
    a.report.write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k not in ('inert_pairs', 'inert_tails', 'alias_examples')}, indent=1))


if __name__ == '__main__':
    main()
