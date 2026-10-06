#!/usr/bin/env python3
"""ACC-02.2 research: special-marker and object classification domains.

Producer-built (original code, no stubs): fine terrain setters 04d870->054000,
full/rectangle hierarchy producer 15d360, Way Gate source bridge
04e360->15c000->15bf60->15c030, and complete 162cb0 requests. Supplied: empty
64x64 fine storage, padded 41/20/10/5 headers, node/heap/route storage.

A  child-state reachability: all 54 ordinary four-lane base tuples, each with
   marker 0 and with a nonzero marker, in both producer orders.
B  per-lane reducer paths involving markers at level 1, every child position.
C  marker propagation through levels 1..3 (byte6 stays zero above the base).
D  four-lane parent tuple closure with markers (no new tuple beyond the 54).
E  search consequence: a marked clear base cell is only ever a level-0 node,
   node20 equals its marker iff warp is enabled.
F  object domain control: original 148e90 over supplied fine link records
   reduces object flags to the same effective four-bit cell set (control only;
   object eligibility/producers are FOOT-03).
"""
import argparse
import hashlib
import itertools
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from acc_research_harness import Retail  # noqa: E402

FLAG_BITS = (2, 4, 0x40, 0x80)
LANE_MASKS = (6, 0x80, 0x40, 4)   # lanes 0/2/4/6: ground, amphibious, floating, flight


def ordinary_witnesses():
    """Same enumeration as verify_wc3_pathing_adaptive.py --terrain-producer (ACC-02.1)."""
    witnesses = {}
    for pattern in itertools.product(range(16), repeat=4):
        flags = [sum(mask for bit, mask in enumerate(FLAG_BITS) if v & (1 << bit)) for v in pattern]
        classes = tuple(0 if not any(v & m for v in flags) else 1 if all(v & m for v in flags) else 2 for m in LANE_MASKS)
        witnesses.setdefault(classes, flags)
    assert len(witnesses) == 54
    return dict(sorted(witnesses.items()))


def lane_classes(word):
    return [(word >> (30 - lane)) & 3 for lane in (0, 2, 4, 6)]


def reduce_model(children):
    """Per-lane 15d1c0 model for in-range children: (class, marker) pairs, in visit order."""
    blocked = 0
    for cls, marker in children:
        if cls == 1:
            blocked += 1
        elif cls == 2:
            return 2
        elif marker:
            return 2
    return 0 if blocked == 0 else 1 if blocked == 4 else 2


def place(retail, base_xy, flags):
    """Write one base cell's 2x2 fine pattern through the original terrain setter."""
    bx, by = base_xy
    for index, flag in enumerate(flags):
        fx, fy = 2 * bx + index % 2, 2 * by + index // 2
        for mask in FLAG_BITS:
            if flag & mask:
                retail.terrain(fx, fy, mask, 1)


def unit_box(retail, bx, by, width=1, height=1):
    """World rectangle whose published base footprint is exactly [bx,bx+w) x [by,by+h)."""
    ox, oy = retail.origin
    return (ox + 64 * bx + 1, oy + 64 * by + 1, ox + 64 * (bx + width - 1) + 31, oy + 64 * (by + height - 1) + 31)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, help='frozen expected JSON (compared when present)')
    args = parser.parse_args()
    witnesses = ordinary_witnesses()
    report = {}

    # ---- A: child-state reachability, both producer orders --------------------------
    child_states = []
    for order in ('terrain-then-marker', 'marker-then-terrain'):
        retail = Retail(args.binary)
        positions = {}
        for index, classes in enumerate(witnesses):
            positions[classes] = (2 + 2 * (index % 9), 2 + 3 * (index // 9))  # isolated base cells
        marked = {classes: (index % 254) + 1 for index, classes in enumerate(witnesses)}
        # unmarked twins one cell to the right
        if order == 'terrain-then-marker':
            for classes, xy in positions.items():
                place(retail, xy, witnesses[classes])
                place(retail, (xy[0] + 1, xy[1]), witnesses[classes])
            retail.rebuild()
            for classes, xy in positions.items():
                retail.publish_gate(marked[classes], unit_box(retail, *xy))
        else:
            for classes, xy in positions.items():
                retail.publish_gate(marked[classes], unit_box(retail, *xy))
            for classes, xy in positions.items():
                place(retail, xy, witnesses[classes])
                place(retail, (xy[0] + 1, xy[1]), witnesses[classes])
            retail.rebuild()
        markers = retail.marker_bytes()
        expected_marked = {xy: marked[c] for c, xy in positions.items()}
        side = retail.sides[0]
        for y in range(side):
            for x in range(side):
                want = expected_marked.get((x, y), 0)
                if markers[y * side + x] != want:
                    raise RuntimeError(('marker footprint differs', order, x, y, markers[y * side + x], want))
        rows = []
        for classes, xy in positions.items():
            word_m, word_u = retail.cell_word(0, *xy), retail.cell_word(0, xy[0] + 1, xy[1])
            if lane_classes(word_m) != list(classes) or lane_classes(word_u) != list(classes):
                raise RuntimeError(('base classes differ under marker', order, classes, hex(word_m), hex(word_u)))
            if (word_m >> 16) & 0xff != marked[classes] or (word_u >> 16) & 0xff:
                raise RuntimeError(('marker byte', order, classes))
            parent = lane_classes(retail.cell_word(1, xy[0] // 2, xy[1] // 2))
            rows.append(dict(classes=list(classes), fine_flags=witnesses[classes], base_xy=list(xy), marker=marked[classes],
                             marked_word=f'{word_m:08x}', unmarked_word=f'{word_u:08x}', level1_parent=parent))
        child_states.append(dict(order=order, witnesses=rows,
                                 level_bytes_sha256=[hashlib.sha256(b).hexdigest() for b in retail.class_bytes()],
                                 marker_bytes_sha256=hashlib.sha256(markers).hexdigest()))
    if [r['marked_word'] for r in child_states[0]['witnesses']] != [r['marked_word'] for r in child_states[1]['witnesses']]:
        raise RuntimeError('producer order changes marked base words')
    report['A_child_states'] = dict(reachable_child_states=108, orders=child_states,
                                    statement='54 ordinary four-lane base tuples x {marker 0, nonzero}; marker survives later full 15d360; classes unchanged by marker writes')

    # ---- B: per-lane reducer paths with markers, every child position --------------------
    retail = Retail(args.binary)
    clear, blocked_all = [0, 0, 0, 0], [0xc6, 0xc6, 0xc6, 0xc6]
    mixed = [0xc6, 0, 0, 0]
    ground_only = [2, 2, 2, 2]          # ground blocked, flight/amph/float clear
    kinds = {'clear': clear, 'blocked': blocked_all, 'mixed': mixed, 'ground_only': ground_only}
    cases = []
    case_specs = []
    for kind_set in (('clear',) * 4, ('blocked',) * 4, ('blocked', 'clear', 'clear', 'clear'),
                     ('mixed', 'clear', 'clear', 'clear'), ('ground_only',) * 4, ('ground_only', 'clear', 'clear', 'clear')):
        for marker_bits in range(16):
            case_specs.append((kind_set, marker_bits))
    # each case occupies one disjoint level-1 block (2x2 base cells), 16 per row
    def block(number):
        return number % 16, number // 16
    for number, (kind_set, marker_bits) in enumerate(case_specs):
        px, py = block(number)
        if 2 * px + 1 >= 32 or 2 * py + 1 >= 32:
            raise RuntimeError('case grid overflow')
        for k, kind in enumerate(kind_set):
            place(retail, (2 * px + k % 2, 2 * py + k // 2), kinds[kind])
    retail.rebuild()
    for number, (kind_set, marker_bits) in enumerate(case_specs):
        px, py = block(number)
        for k in range(4):
            if marker_bits & (1 << k):
                retail.publish_gate(7, unit_box(retail, 2 * px + k % 2, 2 * py + k // 2))
    for number, (kind_set, marker_bits) in enumerate(case_specs):
        px, py = block(number)
        children = [(2 * px + k % 2, 2 * py + k // 2) for k in range(4)]
        child_words = [retail.cell_word(0, *c) for c in children]
        parent = lane_classes(retail.cell_word(1, px, py))
        model = [reduce_model([((w >> (30 - lane)) & 3, (w >> 16) & 0xff) for w in child_words]) for lane in (0, 2, 4, 6)]
        if parent != model:
            raise RuntimeError(('reducer differs from model', kind_set, marker_bits, parent, model))
        unmarked_parent = [reduce_model([((w >> (30 - lane)) & 3, 0) for w in child_words]) for lane in (0, 2, 4, 6)]
        cases.append(dict(children=list(kind_set), marker_positions=[k for k in range(4) if marker_bits & (1 << k)],
                          child_words=[f'{w:08x}' for w in child_words], parent=parent, parent_without_markers=unmarked_parent,
                          marker_changed_lanes=[lane for lane, (a, b) in zip((0, 2, 4, 6), zip(parent, unmarked_parent)) if a != b]))
    report['B_reducer_paths'] = dict(cases=len(cases), rows=cases,
                                     statement='markers change a lane only when every child is clear in that lane (0 -> 2); blocked/mixed children ignore markers')

    # ---- C: propagation through levels and parent marker bytes -----------------------------
    retail = Retail(args.binary)
    retail.rebuild()
    retail.publish_gate(9, unit_box(retail, 13, 13))
    chain = []
    for level in range(4):
        x, y = 13 >> level, 13 >> level
        word = retail.cell_word(level, x, y)
        chain.append(dict(level=level, xy=[x, y], word=f'{word:08x}', classes=lane_classes(word), marker=(word >> 16) & 0xff))
    upper_markers = [sum(1 for b in retail.marker_bytes(level) if b) for level in (1, 2, 3)]
    if [c['classes'] for c in chain] != [[0] * 4, [2] * 4, [2] * 4, [2] * 4] or chain[0]['marker'] != 9 or any(upper_markers):
        raise RuntimeError(('marker propagation', chain, upper_markers))
    # erase (zero marker over same rectangle) restores promotion
    retail.publish_gate(0, unit_box(retail, 13, 13))
    erased = [lane_classes(retail.cell_word(level, 13 >> level, 13 >> level)) for level in range(4)]
    if erased != [[0] * 4] * 4:
        raise RuntimeError(('erase', erased))
    report['C_levels'] = dict(chain=chain, nonzero_marker_bytes_levels_1_3=upper_markers, after_zero_publication=erased)

    # ---- D: four-lane tuple closure ---------------------------------------------------------
    states = set()
    for classes in witnesses:
        for marker in (0, 1):
            states.add((classes, marker))
    lane_partial = {((False,) * 4, (0,) * 4)}
    for _ in range(4):
        nxt = set()
        for forced, counts in lane_partial:
            for classes, marker in states:
                f2, c2 = list(forced), list(counts)
                for i, cls in enumerate(classes):
                    if cls == 1:
                        c2[i] += 1
                    elif cls == 2 or marker:
                        f2[i] = True
                nxt.add((tuple(f2), tuple(c2)))
        lane_partial = nxt
    parent_tuples = sorted({tuple(2 if f else 0 if c == 0 else 1 if c == 4 else 2 for f, c in zip(forced, counts)) for forced, counts in lane_partial})
    no_marker_partial = {((False,) * 4, (0,) * 4)}
    for _ in range(4):
        nxt = set()
        for forced, counts in no_marker_partial:
            for classes in witnesses:
                f2, c2 = list(forced), list(counts)
                for i, cls in enumerate(classes):
                    if cls == 1:
                        c2[i] += 1
                    elif cls == 2:
                        f2[i] = True
                nxt.add((tuple(f2), tuple(c2)))
        no_marker_partial = nxt
    parent_plain = sorted({tuple(2 if f else 0 if c == 0 else 1 if c == 4 else 2 for f, c in zip(forced, counts)) for forced, counts in no_marker_partial})
    if parent_tuples != sorted(witnesses) or parent_plain != sorted(witnesses):
        raise RuntimeError('parent tuple closure differs from the 54 ordinary tuples')
    report['D_parent_closure'] = dict(parent_tuples_with_markers=len(parent_tuples), parent_tuples_without_markers=len(parent_plain),
                                      statement='level-1 four-lane parent tuples with/without markers are exactly the 54 ordinary tuples; reducer model checked against original 15d1c0 in B and by the accepted 20,736-case oracle')

    # ---- E: search consequences ---------------------------------------------------------------
    retail = Retail(args.binary)
    retail.rebuild()
    retail.publish_gate(5, unit_box(retail, 12, 12, 3, 3))
    retail.gate_destination(5, (64 * 25 + 32, 64 * 25 + 32))
    retail.gate_active(5, 1)
    searches = []
    for warp in (0, 1):
        for lane in (0, 2, 4, 6):
            for size_input in (0, 1):
                s = retail.search(lane, (4.25, 4.75), (27.25, 27.75), budget=400, size_input=size_input, warp=warp)
                marked_nodes = [n for n in s['nodes'] if 12 <= n['x'] < 15 and 12 <= n['y'] < 15]
                if any(n['level'] for n in marked_nodes):
                    raise RuntimeError('marked cell promoted')
                if any(n['source_marker'] != (5 if warp else 0) for n in marked_nodes):
                    raise RuntimeError('node20 marker copy differs')
                if any(n['source_marker'] for n in s['nodes'] if n not in marked_nodes):
                    raise RuntimeError('unmarked node with source marker')
                searches.append(dict(warp=warp, lane=lane, size_input=size_input, result=s['result'], work=s['work'], nodes=s['node_count'],
                                     marked_nodes=len(marked_nodes), warp_count=s['warp_count'], route_words=[f'{w:08x}' for w in s['route_words']]))
    # warp tag combinations: chained sources (gate 6 exit lies on gate 7's source) and a promoted exit
    retail = Retail(args.binary)
    retail.rebuild()
    retail.publish_gate(6, unit_box(retail, 6, 6))
    retail.publish_gate(7, unit_box(retail, 14, 4))
    retail.gate_destination(6, (64 * 14 + 32, 64 * 4 + 32))
    retail.gate_destination(7, (64 * 26 + 32, 64 * 26 + 32))
    retail.gate_active(6, 1)
    retail.gate_active(7, 1)
    tags = []
    for lane in (0, 2, 4, 6):
        for size_input in (0, 1):
            for warp in (0, 1):
                s = retail.search(lane, (6.25, 6.75), (27.25, 27.75), budget=400, size_input=size_input, warp=warp)
                nodes = s['nodes']
                combos = set()
                for n in nodes:
                    if n['source_marker'] and (n['level'] or not warp):
                        raise RuntimeError('node20 on promoted node or with warp off')
                    if n['incoming']:
                        parent = nodes[n['parent']]
                        if parent['source_marker'] != n['incoming'] or parent['level']:
                            raise RuntimeError('incoming tag not from a level-0 marked parent')
                    combos.add((n['level'], n['source_marker'], n['incoming']))
                tags.append(dict(lane=lane, size_input=size_input, warp=warp, result=s['result'], work=s['work'], nodes=s['node_count'],
                                 warp_count=s['warp_count'], level_source_incoming=sorted(combos), route_words=[f'{w:08x}' for w in s['route_words']]))
    report['E_search'] = dict(rows=searches, warp_tags=tags,
                              statement='marked clear cells only appear as level-0 nodes; node20 copies marker iff warp; node23 (incoming) equals the level-0 parent node20 and may sit on a marked or promoted node')

    # ---- F: object reduction control -----------------------------------------------------------
    retail = Retail(args.binary)
    fine = retail.fine
    links = retail.alloc(0x10000)
    objects = retail.alloc(0x10000)
    retail.write(fine + 0x78, links)
    retail.write(fine + 0xb4, 0)
    rows = []
    variants = []
    for bits in range(16):
        objflags = sum(m for i, m in enumerate(FLAG_BITS) if bits & (1 << i))
        for link_type, flag34, flag40, stamp, position in ((0x1000000, 0x1000000, 0x10000000, 0, 0), (0x1000000, 0x1000000, 0x70000000, 0, 0),
                                                            (0x2000000, 0x1000000, 0x10000000, 0, 0), (0x1000000, 0, 0x10000000, 0, 0),
                                                            (0x1000000, 0x1000000, 0x10000001, 0, 0), (0x1000000, 0x1000000, 0x10000000, -1, 0),
                                                            (0x1000000, 0x1000000, 0x10000000, 0, 48), (0x1000000, 0x1000000, 0x10000000, 0, 49)):
            variants.append((objflags, link_type, flag34, flag40, stamp, position))
    for objflags, link_type, flag34, flag40, stamp, position in variants:
        # chain of `position` neutral links (type 0x2000000) followed by the tested link
        for i in range(position):
            retail.write(links + 8 * (i + 1), 0x2000000 | (i + 2), objects + 0x100)
        last = position + 1
        retail.write(links + 8 * last, link_type | 0xffffff, objects)
        retail.write(objects + 0x34, flag34 | objflags)
        retail.write(objects + 0x38, stamp & 0xffffffff)
        retail.write(objects + 0x40, flag40)
        retail.write(objects + 0x100 + 0x38, 0xffffffff)
        retail.write(retail.cells, 1)  # fine cell (0,0): no high flags, first link index 1
        result = []
        for lane, mask in zip((0, 2, 4, 6), LANE_MASKS):
            retail.write(retail.points + 0x30, (mask << 24) | mask)
            retail.write(retail.points + 0x38, 0, 0)
            got = retail.run(0x6f1493d0, retail.fine_system, retail.points + 0x38, retail.points + 0x30)
            eligible = link_type == 0x1000000 and flag34 & 0x1000000 and flag40 & 0x10000000 and not flag40 & 0x8fffffff and stamp != -1 and position < 49
            expect = 0 if eligible and objflags & mask else 1
            if got != expect:
                raise RuntimeError(('object control', objflags, link_type, flag34, flag40, stamp, position, lane, got, expect))
            result.append(got)
        rows.append(dict(object34_low=objflags, link_type=f'{link_type:08x}', obj34_flag=f'{flag34:08x}', obj40=f'{flag40:08x}',
                         stamp=stamp, preceding_links=position, clear_by_lane=result))
    report['F_object_control'] = dict(cases=len(rows) * 4, rows=rows,
                                      statement='supplied link records (control): blocked(lane) iff eligible type-1 link within first 49 iterations has obj34 & lane mask; same per-cell effective {2,4,40,80} set as terrain flags, so no new four-lane tuple')

    report['binary_sha256'] = retail.digest
    report['scope'] = ('Original 04d870/054000, 15d360, 04e360->15c000->15bf60->15c030, 04e210, 04e550 and 162cb0 over supplied empty 64x64 fine storage and '
                       'padded 41/20/10/5 headers; F supplies fine link/object records as a control for 148e90 via 1493d0.')
    text = json.dumps(report, indent=1, sort_keys=True) + '\n'
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text)
    if args.fixture and args.fixture.exists():
        if json.loads(args.fixture.read_text()) != json.loads(text):
            raise SystemExit('frozen ACC-02.2 expectation differs')
    print('A', len(child_states[0]['witnesses']) * 2, 'child states; B', len(cases), 'reducer cases; D', len(parent_tuples),
          'parent tuples; E', len(searches), 'searches; F', len(rows) * 4, 'object controls')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
