#!/usr/bin/env python3
"""ACC-01.1 research: adaptive expander branch inventory with exact preconditions.

1. Static inventory: every conditional jump in the 45 functions reached from
   PathAcc_BuildRoute 162cb0 (read-only Ghidra disassembly cache), each outcome
   annotated with its recovered condition and a reachability class.
2. Size-2/base-cell predicate oracle: the 16 original predicate routines are
   called unchanged for every pattern of the base cells they read, at low-edge
   and interior positions, four lanes, blocked class1 and mixed class2. Their
   results must equal the transcribed formula; the Jcc outcomes they reach
   classify the remaining bounds/null outcomes. Supplied class words are a
   control for the formula, not producer evidence (ACC-01.2 supplies that).
3. Join: coverage of the accepted original-code corpora measured by
   acc01_coverage_wrapper.py (passive block hook around the unchanged scripts).

No retail state is modified other than the supplied predicate-control cells.
"""
import argparse
import itertools
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from acc_research_harness import Retail, load_jccs, SEARCH_FUNCTIONS, GHIDRA_CACHE, sha256_file  # noqa: E402

# Reachability classes for one Jcc outcome.
#   R        reachable in ordinary producer-built searches
#   R-low    reachable only when a tested coordinate is below zero (map low edge)
#   R-gate   reachable only with Way Gate records/markers and warp enabled
#   U-null   cell pointer null test: map storage + 8*index is never 0
#   U-pad    high-side bound: constructor padding (>=9 base cells) exceeds the +2 offsets
#   U-entry  structurally unreachable (caller always establishes the opposite)
#   C-out    needs an out-of-map adaptive source (caller domain, ROUTE-01.2)
#   U-ctx    unreachable from search context (see CONTEXT invariants H/N/G below)
#   R-marker reachable, but only when a Way Gate source marker forces the level-1 block mixed
CORE = {
    # PathAcc_BuildRoute 162cb0
    0x6f162cc3: ('162cb0', 'route buffer empty (count0) -> skip clear', 'R', 'route buffer had points -> 14a760 clear', 'R'),
    0x6f162cee: ('162cb0', 'setup result !=1 (0/2) -> search', 'R', 'setup bypass result1 -> one exact goal point', 'R'),
    0x6f162d12: ('162cb0', 'search returned -1 -> partial handling', 'R', 'goal popped -> reconstruct, return1', 'R'),
    0x6f162d3c: ('162cb0', 'nearest != source -> node-centre partial', 'R', 'nearest == source -> exact source point, return0', 'R'),
    # PathAcc_SetupSearch 164c30
    0x6f164cfe: ('164c30', 'source x >= base width -> return0', 'C-out', 'source x in map', 'R'),
    0x6f164d0d: ('164c30', 'source y >= base height -> return0', 'C-out', 'source y in map', 'R'),
    0x6f164d20: ('164c30', 'null source cell -> return0', 'U-null', 'cell pointer nonnull', 'R'),
    0x6f164d2c: ('164c30', 'source x != goal x -> lookups', 'R', 'same x -> compare y', 'R'),
    0x6f164d3a: ('164c30', 'same base cell -> return1 without stamp increment', 'R', 'different y -> lookups', 'R'),
    0x6f164d58: ('164c30', 'source node valid -> goal lookup', 'R', 'source -1 (blocked/mixed base) -> goal=-1, return1', 'R'),
    0x6f164d91: ('164c30', 'source node == goal node (same promoted node) -> return1', 'R', 'distinct -> nearest=source, d2, budget, return2', 'R'),
    # PathAcc_Search 163f50
    0x6f163f62: ('163f50', 'heap empty at entry -> -1', 'U-entry', 'heap holds start (164a20 always enqueues)', 'R'),
    0x6f163f85: ('163f50', 'work counter >= budget -> -1 (counter already incremented)', 'R', 'pop', 'R'),
    0x6f163fa7: ('163f50', 'stale heap generation -> discard (charged)', 'R', 'current record', 'R'),
    0x6f163fc1: ('163f50', 'popped node is goal -> return index', 'R', 'expand', 'R'),
    0x6f163fd3: ('163f50', 'node level byte22 !=0 -> coarse 1644d0', 'R', 'level0 -> base 1643d0 with sourceID20', 'R'),
    0x6f164000: ('163f50', 'heap nonempty -> loop', 'R', 'heap exhausted -> -1', 'R'),
    # PathAcc_RelaxNode 164020 / RelaxSpecialEdge 165220
    0x6f1640b6: ('164020', 'fresh node (state -1) -> nearest test then enqueue', 'R', 'previously enqueued', 'R'),
    0x6f1640be: ('164020', 'candidate g >= stored g -> return (no change)', 'R', 'cheaper', 'R'),
    0x6f1640c6: ('164020', 'open (-2) -> generation++, links -1', 'R', 'closed (index/-3) -> 1641d0 unlink (reopen)', 'R'),
    0x6f164107: ('164020', 'squared distance >= nearest (ties keep earlier) ', 'R', 'strictly nearer -> nearest=this', 'R'),
    0x6f165287: ('165220', 'fresh destination node', 'R-gate', 'previously enqueued destination', 'R-gate'),
    0x6f16528f: ('165220', 'parent g+1 >= stored g -> return', 'R-gate', 'cheaper', 'R-gate'),
    0x6f165297: ('165220', 'open destination -> generation++', 'R-gate', 'closed destination -> unlink (reopen)', 'R-gate'),
    0x6f1652d8: ('165220', 'not strictly nearer', 'R-gate', 'strictly nearer -> nearest=destination', 'R-gate'),
    # closed-list unlink 1641d0
    0x6f164200: ('1641d0', 'prev == -1 (list head) -> head=next', 'R', 'interior/tail -> prev.next=next', 'R'),
    0x6f164217: ('1641d0', 'next == -3: unlinking the closed-list tail, which is always the start node (closed first; g=0 cannot be improved by unsigned g+edge or g+1)', 'U-entry', 'next.prev=prev', 'R'),
    # close node 162730 (prepend to closed list)
    0x6f162743: ('162730', 'closed list empty (head -3): first closed node, the start', 'R', 'head.prev = node', 'R'),
    # PathAcc_CreateCellNode 163ef0
    0x6f163f22: ('163ef0', 'warp mode d8 == 0 -> node20 = 0', 'R', 'warp enabled -> node20 = cell byte6 marker', 'R-gate'),
    # PathAcc_FindCellNode 1625f0
    0x6f162616: ('1625f0', 'x>>level >= width -> -1 (negative coordinates wrap unsigned)', 'R-low', 'in range', 'R'),
    0x6f16261f: ('1625f0', 'y>>level >= height -> -1', 'R-low', 'in range', 'R'),
    0x6f162635: ('1625f0', 'null cell -> -1', 'U-null', 'nonnull', 'R'),
    0x6f162650: ('1625f0', 'class1 blocked -> -1', 'R', 'not class1', 'R'),
    0x6f16265b: ('1625f0', 'class0 (or class3, unreachable: ACC-02.2) -> promotion', 'R', 'class2 -> -2 above base, -1 at base', 'R'),
    0x6f162676: ('1625f0', 'query level >= 3 -> no promotion', 'R', 'level<3 -> examine parents', 'R'),
    0x6f162698: ('1625f0', 'parent x out of range -> NULL parent dereference', 'U-pad', 'parent in range', 'R'),
    0x6f16269d: ('1625f0', 'parent y out of range -> NULL parent dereference', 'U-pad', 'parent in range', 'R'),
    0x6f1626c2: ('1625f0', 'parent class nonzero -> stop at current level', 'R', 'parent class0 -> promote one level', 'R'),
    0x6f1626cf: ('1625f0', 'next level < 4 -> continue promotion', 'R', 'promoted to level3 -> stop', 'R'),
    0x6f1626de: ('1625f0', 'cell stamp == search stamp -> existing node index', 'R', 'stamp differs -> 163ef0 create with first-lookup representative', 'R'),
    # PathAcc_ExpandBaseNode 1643d0 (cardinals N,E,S,W first; corners NE,SE,SW,NW; then special)
    0x6f164429: ('1643d0', 'N not relaxed -> skip NE', 'R', 'N relaxed', 'R'),
    0x6f16442f: ('1643d0', 'N relaxed, E not -> skip NE/SE', 'R', 'N&E -> NE corner 1638c0', 'R'),
    0x6f164447: ('1643d0', 'E not relaxed -> skip SE', 'R', 'E relaxed', 'R'),
    0x6f16444d: ('1643d0', 'E relaxed, S not -> skip SE/SW', 'R', 'E&S -> SE corner 1642b0', 'R'),
    0x6f164465: ('1643d0', 'S not relaxed -> skip SW', 'R', 'S relaxed', 'R'),
    0x6f16446b: ('1643d0', 'S relaxed, W not -> skip SW/NW', 'R', 'S&W -> SW corner 164670', 'R'),
    0x6f16447f: ('1643d0', 'W not relaxed -> skip NW', 'R', 'W relaxed', 'R'),
    0x6f164485: ('1643d0', 'W relaxed, N not -> skip NW', 'R', 'W&N -> NW corner 163a40', 'R'),
    0x6f16449a: ('1643d0', 'node sourceID20 == 0 -> no special edge', 'R', 'nonzero sourceID (warp enabled, marked cell)', 'R-gate'),
    0x6f1644a6: ('1643d0', 'record active bit0 clear -> no special edge', 'R-gate', 'active record', 'R-gate'),
    0x6f1644af: ('1643d0', 'record x != node x -> special lookup', 'R-gate', 'same x -> compare y', 'R-gate'),
    0x6f1644b8: ('1643d0', 'record destination == node cell -> no special edge', 'R-gate', 'different y -> special lookup', 'R-gate'),
    # base cardinals
    0x6f163afb: ('163ae0', 'N lookup -1 -> return0', 'R', 'node', 'R'),
    0x6f163b04: ('163ae0', 'stored size !=2 -> relax', 'R', 'size2 checks', 'R'),
    0x6f163b13: ('163ae0', 'size2 right cell (x+1,y) not clear -> 0', 'R', 'right clear', 'R'),
    0x6f163b18: ('163ae0', 'x even -> relax', 'R', 'x odd -> promoted test', 'R'),
    0x6f163b25: ('163ae0', 'found node level 0 -> relax', 'R', 'found node promoted (level>0) -> 163d40', 'R'),
    0x6f163b34: ('163ae0', '163d40 rejects (neither (x+1,y-2) nor (x-1,y) clear) -> 0', 'R', 'accepted -> relax', 'R'),
    0x6f163199: ('163180', 'E lookup -1 -> 0', 'R', 'node', 'R'),
    0x6f1631a2: ('163180', 'size !=2 -> relax', 'R', 'size2 strip 1631e0', 'R'),
    0x6f1631b3: ('163180', '1631e0 rejects -> 0', 'R', 'accepted -> relax', 'R'),
    0x6f164829: ('164810', 'S lookup -1 -> 0', 'R', 'node', 'R'),
    0x6f164832: ('164810', 'size !=2 -> relax', 'R', 'size2 strip 164870', 'R'),
    0x6f164843: ('164810', '164870 rejects -> 0', 'R', 'accepted -> relax', 'R'),
    0x6f164fac: ('164f90', 'W lookup -1 -> 0', 'R', 'node', 'R'),
    0x6f164fb9: ('164f90', 'size !=2 -> relax', 'R', 'size2 checks', 'R'),
    0x6f164fcc: ('164f90', 'down cell x out of range', 'U-pad', 'in range', 'R'),
    0x6f164fd7: ('164f90', 'down cell y+1 out of range', 'U-pad', 'in range', 'R'),
    0x6f164fe7: ('164f90', 'null down cell', 'U-null', 'nonnull', 'R'),
    0x6f164ff9: ('164f90', 'down cell (x,y+1) not clear -> 0', 'R', 'clear', 'R'),
    0x6f164ffe: ('164f90', 'y even -> relax', 'R', 'y odd -> promoted test', 'R'),
    0x6f16500b: ('164f90', 'found node level0 -> relax', 'R', 'promoted -> 1653e0', 'R'),
    0x6f165018: ('164f90', '1653e0 rejects (neither (x-2,y+1) nor (x,y-1) clear) -> 0', 'R', 'accepted -> relax', 'R'),
    # base corners
    0x6f1638d9: ('1638c0', 'NE lookup -1', 'R', 'node', 'R'),
    0x6f1638e2: ('1638c0', 'size !=2 -> relax', 'R', 'size2 163910', 'R'),
    0x6f1638f3: ('1638c0', '163910 rejects (x+1,y) of corner', 'R', 'relax', 'R'),
    0x6f1642c9: ('1642b0', 'SE lookup -1', 'R', 'node', 'R'),
    0x6f1642d2: ('1642b0', 'size !=2 -> relax', 'R', 'size2 164300', 'R'),
    0x6f1642e3: ('1642b0', '164300 rejects (x+1,y+1) of corner', 'R', 'relax', 'R'),
    0x6f16468c: ('164670', 'SW lookup -1', 'R', 'node', 'R'),
    0x6f164695: ('164670', 'size !=2 -> relax', 'R', 'size2 down check', 'R'),
    0x6f1646a0: ('164670', 'down cell x out of range', 'U-pad', 'in range', 'R'),
    0x6f1646a5: ('164670', 'down cell y+1 out of range', 'U-pad', 'in range', 'R'),
    0x6f1646b4: ('164670', 'null down cell', 'U-null', 'nonnull', 'R'),
    0x6f1646c6: ('164670', 'down cell (x,y+1) of corner not clear', 'R', 'relax', 'R'),
    0x6f163a56: ('163a40', 'NW lookup -1', 'R', 'node -> relax (no size2 check)', 'R'),
    0x6f1653b6: ('1653a0', 'special destination base lookup -1 (blocked/mixed/outside)', 'R-gate', 'node (possibly promoted) -> 165220', 'R-gate'),
    # coarse corners: first lookup at the node level, descend one level on -2
    0x6f16398c: ('163970', 'NE first lookup node -> relax path', 'R', '-1/-2', 'R'),
    0x6f16398e: ('163970', 'NE -1 -> return', 'R', '-2 -> descend one level, same coordinate', 'R'),
    0x6f1639a4: ('163970', 'NE after descent -1/-2 -> recheck', 'R', 'NE after descent node', 'R'),
    0x6f1639ad: ('163970', 'size !=2 -> relax', 'R', 'size2 1639e0', 'R'),
    0x6f1639be: ('163970', '1639e0 rejects (x+1,y)', 'R', 'relax', 'R'),
    0x6f16436c: ('164350', 'SE first lookup node', 'R', '-1/-2', 'R'),
    0x6f16436e: ('164350', 'SE -1 -> return', 'R', '-2 -> descend', 'R'),
    0x6f164384: ('164350', 'SE after descent -1/-2', 'R', 'SE after descent node', 'R'),
    0x6f16438d: ('164350', 'size !=2 -> relax', 'R', 'size2 163370', 'R'),
    0x6f16439e: ('164350', '163370 rejects', 'R', 'relax', 'R'),
    0x6f16474d: ('164730', 'SW lookup -1/-2', 'R', 'SW node', 'R'),
    0x6f16474f: ('164730', 'SW -1 -> return', 'R', '-2 -> descend', 'R'),
    0x6f164763: ('164730', 'SW after descent -1/-2 -> recheck', 'R', 'node', 'R'),
    0x6f16476c: ('164730', 'size !=2 -> relax', 'R', 'size2 down check', 'R'),
    0x6f16477a: ('164730', 'down cell x out of range', 'U-pad', 'in range', 'R'),
    0x6f16477f: ('164730', 'down cell y+1 out of range', 'U-pad', 'in range', 'R'),
    0x6f16478e: ('164730', 'null down cell', 'U-null', 'nonnull', 'R'),
    0x6f1647a0: ('164730', 'down cell (x,y+1) of corner not clear', 'R', 'relax', 'R'),
    0x6f163a9b: ('163a80', 'NW lookup node -> relax (no size2 check)', 'R', '-1/-2', 'R'),
    0x6f163a9d: ('163a80', 'NW -1 -> return', 'R', '-2 -> descend', 'R'),
    0x6f163aaf: ('163a80', 'NW after descent -1/-2', 'R', 'node', 'R'),
}
# Four side walkers share one layout: (lookup node?, -1 vs -2, second-half?, l'==0?,
# loop, size2?, interior/boundary, predicate, flags).
WALKERS = {'163260': ('E', '163370', '1635b0', (0x6f16328f, 0x6f163291, 0x6f1632a8, 0x6f1632b2, 0x6f1632fd, 0x6f163309, 0x6f163324, 0x6f163334, 0x6f163346)),
           '163bc0': ('N', '163cd0', '163dd0', (0x6f163bef, 0x6f163bf1, 0x6f163c08, 0x6f163c12, 0x6f163c5d, 0x6f163c69, 0x6f163c84, 0x6f163c94, 0x6f163ca6)),
           '1648f0': ('S', '163370', '164e50', (0x6f16491f, 0x6f164921, 0x6f164938, 0x6f164942, 0x6f16498d, 0x6f164999, 0x6f1649b4, 0x6f1649c4, 0x6f1649d6)),
           '165090': ('W', '1651a0', '165470', (0x6f1650bf, 0x6f1650c1, 0x6f1650d8, 0x6f1650e2, 0x6f16512d, 0x6f165139, 0x6f165154, 0x6f165164, 0x6f165176))}
for fn, (side, interior, boundary, jccs) in WALKERS.items():
    texts = [
        (f'{side} side lookup node -> relax path', 'R', '-1/-2', 'R'),
        (f'{side} -1 (blocked/outside) -> return, no flags', 'R', "-2 mixed -> subdivide: level'=level-1", 'R'),
        (f'{side} tangent >= mid: first half uses mid-(1 at level0 else stored size), second half = tangent', 'R', 'tangent < mid: first half = tangent, second half = mid', 'R'),
        (f"{side} level'==0 -> first-half offset 1", 'R', "level'>0 -> offset stored size", 'R'),
        (f'{side} second-half lookup -1/-2 -> recheck', 'R', 'second-half node', 'R'),
        (f'{side} size !=2 -> relax', 'R', 'size2 predicate', 'R'),
        (f'{side} tangent is last coordinate of its level square -> boundary {boundary}', 'R', f'interior -> {interior}', 'R'),
        (f'{side} size2 predicate rejects -> return, no flags', 'R', 'accepted -> relax', 'R'),
        (f'{side} walker level 0 -> relax without end flags', 'R', 'level>0 -> set both end flags (corner gates)', 'R'),
    ]
    for va, (t, tr, f, fr) in zip(jccs, texts):
        CORE[va] = (fn, t, tr, f, fr)
# Coarse expansion 1644d0: only corner gates are Jccs; size2 clamp is CMOVG (event level).
CORE.update({
    0x6f16454b: ('1644d0', 'size !=2 -> no representative clamp', 'R', 'size2 -> clamp x/y to square end-2 (CMOVG)', 'R'),
    0x6f1645ec: ('1644d0', 'North east-end flag 0 -> skip NE', 'R', 'set', 'R'),
    0x6f1645f2: ('1644d0', 'East north-end flag 0 -> skip NE', 'R', 'both -> NE 163970(level,X0+s,Y0-1)', 'R'),
    0x6f164607: ('1644d0', 'East south-end flag 0 -> skip SE', 'R', 'set', 'R'),
    0x6f16460d: ('1644d0', 'South east-end flag 0 -> skip SE', 'R', 'both -> SE 164350(level,X0+s,Y0+s)', 'R'),
    0x6f164622: ('1644d0', 'South west-end flag 0 -> skip SW', 'R', 'set', 'R'),
    0x6f164628: ('1644d0', 'West south-end flag 0 -> skip SW', 'R', 'both -> SW 164730(level,X0-1,Y0+s)', 'R'),
    0x6f16463d: ('1644d0', 'West north-end flag 0 -> skip NW', 'R', 'set', 'R'),
    0x6f164643: ('1644d0', 'North west-end flag 0 -> skip NW', 'R', 'both -> NW 163a80(level,X0-1,Y0-1)', 'R'),
})

# Transcribed size-2 / base-cell predicates. c(dx,dy) = inside map and selected
# lane class pair exactly zero. Arguments are the candidate cell (x,y).
PREDICATES = {
    '6f1631e0': ('base east strip', [(1, 0), (1, 1)], lambda c: c(1, 0) and c(1, 1)),
    '6f163370': ('east occupancy (also south interior, SE corner)', [(1, 0), (0, 1), (1, 1)], lambda c: c(1, 0) and c(0, 1) and c(1, 1)),
    '6f1635b0': ('east boundary', [(1, 0), (0, 1), (1, 1), (0, -1), (1, -1), (-2, 1), (-1, 1)],
                 lambda c: c(1, 0) and c(0, 1) and c(1, 1) and ((c(0, -1) and c(1, -1)) or (c(-2, 1) and c(-1, 1)) or (c(-1, 1) and c(0, -1)))),
    '6f163910': ('base NE right', [(1, 0)], lambda c: c(1, 0)),
    '6f1639e0': ('coarse NE right', [(1, 0)], lambda c: c(1, 0)),
    '6f163b60': ('base north right', [(1, 0)], lambda c: c(1, 0)),
    '6f163cd0': ('north interior right', [(1, 0)], lambda c: c(1, 0)),
    '6f163d40': ('base north promoted odd-x', [(1, -2), (-1, 0)], lambda c: c(1, -2) or c(-1, 0)),
    '6f163dd0': ('north boundary', [(1, 0), (1, 1), (1, 2), (-1, 0)], lambda c: c(1, 0) and c(1, 1) and (c(1, 2) or c(-1, 0))),
    '6f164300': ('base SE diagonal', [(1, 1)], lambda c: c(1, 1)),
    '6f164870': ('base south strip', [(0, 1), (1, 1)], lambda c: c(0, 1) and c(1, 1)),
    '6f164e50': ('south boundary', [(1, 0), (0, 1), (1, 1), (-1, 0), (-1, 1), (1, -2), (1, -1)],
                 lambda c: c(1, 0) and c(0, 1) and c(1, 1) and ((c(-1, 0) and c(-1, 1)) or (c(1, -2) and c(1, -1)) or (c(1, -1) and c(-1, 0)))),
    '6f1651a0': ('west interior down', [(0, 1)], lambda c: c(0, 1)),
    '6f1653e0': ('base west promoted odd-y', [(-2, 1), (0, -1)], lambda c: c(-2, 1) or c(0, -1)),
    '6f165470': ('west boundary', [(0, 1), (1, 1), (2, 1), (0, -1)], lambda c: c(0, 1) and c(1, 1) and (c(2, 1) or c(0, -1))),
    '6f162b80': ('test base cell', [(0, 0)], lambda c: c(0, 0)),
}


# Call-site context overrides for predicate outcomes. Invariants used:
#  H  hierarchy consistency: a clear level-l cell has only class0 (unmarked) base descendants
#     (ACC-02.2 writer inventory: every class/marker writer is followed by parent propagation);
#  N  the node-level byte read for a returned index belongs to that node (fails only under the
#     ushort node-index alias beyond 65,535 created nodes, ACC-05.1);
#  G  side geometry: East candidate x=X0+2^L>=2, West x=X0-1>=1, North y=Y0-1>=1, South y=Y0+2^L>=2
#     (coarse expansion only runs for level>=1 nodes; out-of-map neighbours fail the lookup first).
CONTEXT = {
    (0x6f163d0c, 'taken'): ('U-ctx', 'H: north interior candidate t is not the last column of its clear level-l cell, so (t+1,y) is in that clear cell'),
    (0x6f1651d8, 'taken'): ('U-ctx', 'H: west interior candidate y is not the last row of its clear level-l cell, so (x,y+1) is clear'),
    (0x6f163b34, 'taken'): ('U-ctx', 'H+N: odd x inside a promoted (level>=1, even-aligned) clear square implies (x-1,y) clear, so 163d40 accepts'),
    (0x6f163db9, 'taken'): ('U-ctx', 'H+N: (x-1,y) lies in the same promoted clear square'),
    (0x6f163d93, 'taken'): ('U-ctx', 'odd x >= 1 so x-1 >= 0'),
    (0x6f163d98, 'taken'): ('U-ctx', 'y equals the found node row, in range'),
    (0x6f165018, 'taken'): ('U-ctx', 'H+N: odd y inside a promoted clear square implies (x,y-1) clear, so 1653e0 accepts'),
    (0x6f165459, 'taken'): ('U-ctx', 'H+N: (x,y-1) lies in the same promoted clear square'),
    (0x6f165438, 'taken'): ('U-ctx', 'odd y >= 1 so y-1 >= 0'),
    (0x6f165433, 'taken'): ('U-ctx', 'x equals the found node column, in range'),
    (0x6f1635d8, 'taken'): ('U-ctx', 'G: candidate x in map'),
    (0x6f16360d, 'taken'): ('U-ctx', 'B row y-1 already passed the identical A row check'),
    (0x6f163643, 'taken'): ('U-ctx', 'G: East x>=2 so x-2>=0'),
    (0x6f163686, 'taken'): ('U-ctx', 'G: East x>=2 so x-1>=0'),
    (0x6f164ee8, 'taken'): ('U-ctx', 'G: South y>=2 so y-2>=0'),
    (0x6f164f2b, 'taken'): ('U-ctx', 'G: South y>=2 so y-1>=0'),
    (0x6f164ea8, 'taken'): ('U-ctx', 'second west cell x-1 already passed the first x-1 check'),
    (0x6f162b8d, 'taken'): ('U-ctx', 'H+G: TestBaseCell(x-1,..) with x=0 is only reached from 163dd0/164e50 after an alternative whose cells lie inside the expanding clear node (X0=0, side>=2), which accepts first'),
    (0x6f162b95, 'taken'): ('U-ctx', 'H+G: TestBaseCell(x,y-1) with y=0 is only reached from 1635b0/165470 after alternatives inside the expanding clear node (Y0=0, side>=2), which accept first'),
    (0x6f1635dd, 'taken'): ('R-marker', 'East boundary at y=0 needs a level-0 segment, i.e. a mixed level-1 block whose four base cells are the candidate and the three clear occupancy cells: only a nonzero byte6 marker makes it mixed'),
    (0x6f164e78, 'taken'): ('R-marker', 'South boundary at x=0: same argument; the level-1 block is fully clear in class and mixed only through a marker'),
}


def predicate_oracle(retail, low_pass):
    """Exhaustive original predicate calls; returns (cases, per-predicate summary).

    low_pass False: both coordinates interior (>=2). True: at least one coordinate 0/1."""
    side = retail.sides[0]
    last_clear = retail.fine_side // 2 - 1  # last base cell covering fine cells
    positions = [0, 1, 2, 3, last_clear - 2, last_clear - 1, last_clear]
    storage = retail.data[0]
    retail.uc.mem_write(storage, bytes(side * side * 8))
    summary, total = {}, 0
    for va, (name, offsets, formula) in PREDICATES.items():
        cases = 0
        for lane in (0, 2, 4, 6):
            retail.write(retail.system + 0xd4, lane)
            for x, y in itertools.product(positions, repeat=2):
                if (min(x, y) < 2) != low_pass:
                    continue
                for bits in range(1 << len(offsets)):
                    for blocked_class in (1, 2):
                        touched = []
                        blocked = set()
                        for bit, (dx, dy) in enumerate(offsets):
                            cx, cy = x + dx, y + dy
                            if bits & (1 << bit) and 0 <= cx < side and 0 <= cy < side:
                                retail.write(storage + 8 * (cy * side + cx) + 4, blocked_class << (30 - lane))
                                touched.append((cx, cy))
                                blocked.add((dx, dy))

                        def c(dx, dy):
                            cx, cy = x + dx, y + dy
                            return 0 <= cx < side and 0 <= cy < side and (dx, dy) not in blocked
                        expected = int(bool(formula(c)))
                        actual = retail.run(int(va, 16), retail.system, x, y)
                        if actual != expected:
                            raise RuntimeError(('predicate formula mismatch', va, lane, x, y, bits, blocked_class, actual, expected))
                        for cx, cy in touched:
                            retail.write(storage + 8 * (cy * side + cx) + 4, 0)
                        cases += 1
        summary[va] = dict(name=name, offsets=offsets, cases=cases, low_edge_pass=low_pass)
        total += cases
    return total, summary, positions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--existing-coverage', type=Path, required=True, help='directory of acc01_coverage_wrapper outputs')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, help='frozen expected JSON to compare against')
    args = parser.parse_args()
    jccs = load_jccs()
    retail = Retail(args.binary)
    retail.enable_coverage()
    total, summary, positions = predicate_oracle(retail, False)
    interior_cov = {f'{a:08x}:{d}' for (a, d) in retail.coverage}
    low_total, low_summary, _ = predicate_oracle(retail, True)
    total += low_total
    summary = {va: dict(summary[va], low_edge_cases=low_summary[va]['cases']) for va in summary}
    predicate_cov = {f'{a:08x}:{d}' for (a, d) in retail.coverage}
    existing = {}
    for path in sorted(args.existing_coverage.glob('*.json')):
        if path.name.endswith('.report.json') or 'fixture' in path.name:
            continue
        data = json.loads(path.read_text())
        existing[path.stem] = dict(outcomes=data['outcomes'], argv=data['argv'], exit_status=data['exit_status'], sha256=sha256_file(path))
    rows = []
    for va, (function, target, fall, text) in sorted(jccs.items()):
        for outcome in ('taken', 'fall'):
            key = f'{va:08x}:{outcome}'
            covered = sorted(name for name, data in existing.items() if key in data['outcomes'])
            if (va, outcome) in CONTEXT:
                reach, note = CONTEXT[(va, outcome)]
                meaning = (f'{PREDICATES[function][0]} {outcome}: ' if function in PREDICATES else CORE.get(va, ('', '', '', '', ''))[1 if outcome == 'taken' else 3] + ': ') + note
                source = 'context-proof'
            elif va in CORE:
                fn, t_text, t_class, f_text, f_class = CORE[va]
                meaning, reach = (t_text, t_class) if outcome == 'taken' else (f_text, f_class)
                source = 'core-annotation'
            elif function in PREDICATES:
                meaning = f'{PREDICATES[function][0]} {outcome}'
                if key in predicate_cov:
                    reach = 'R' if key in interior_cov else 'R-low'
                else:
                    reach = 'U-null' if 'TEST EAX,EAX' in _prev(function, va) else 'U-pad'
                source = 'predicate-oracle'
            else:
                meaning, reach, source = 'UNANNOTATED', '?', 'none'
            rows.append(dict(va=f'{va:08x}', function=function, function_role=SEARCH_FUNCTIONS[function], instruction=text,
                             outcome=outcome, target=f'{target:08x}' if outcome == 'taken' else f'{fall:08x}', meaning=meaning,
                             reach=reach, annotation=source, existing_corpora=covered, predicate_oracle=key in predicate_cov))
    missing = [r for r in rows if not r['existing_corpora']]
    unannotated = [r for r in rows if r['reach'] == '?']
    report = dict(
        binary_sha256=retail.digest,
        scope='Static Jcc inventory of 45 adaptive-search functions (Ghidra read-only disassembly), exhaustive original size2/base predicate formula oracle, '
              'and join with passive coverage of 14 accepted original-code corpora. Supplied predicate cells are controls.',
        disassembly_sha256={f: sha256_file(GHIDRA_CACHE / f'{f}-disassemble_function.txt') for f in SEARCH_FUNCTIONS},
        predicate_cases=total, predicate_positions=positions, predicates=summary,
        corpora={k: dict(argv=v['argv'], exit_status=v['exit_status'], coverage_sha256=v['sha256'], outcomes=len(v['outcomes'])) for k, v in existing.items()},
        jcc_count=len(jccs), outcome_count=len(rows),
        covered_by_existing=len(rows) - len(missing), missing_from_existing=len(missing),
        missing_by_reach={k: sum(1 for r in missing if r['reach'] == k) for k in sorted({r['reach'] for r in rows})},
        unannotated=len(unannotated), outcomes=rows)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=1) + '\n')
    if args.fixture:
        frozen = json.loads(args.fixture.read_text())
        for k in ('jcc_count', 'outcome_count', 'covered_by_existing', 'missing_from_existing', 'missing_by_reach', 'predicate_cases'):
            if frozen[k] != report[k]:
                raise SystemExit(f'frozen {k} differs: {frozen[k]} != {report[k]}')
        if [(r['va'], r['outcome'], r['reach'], r['existing_corpora']) for r in frozen['outcomes']] != [(r['va'], r['outcome'], r['reach'], r['existing_corpora']) for r in rows]:
            raise SystemExit('frozen outcome table differs')
    print(f"{len(jccs)} Jccs / {len(rows)} outcomes; predicate cases {total}; covered by existing corpora {len(rows) - len(missing)}; "
          f"missing {len(missing)} {report['missing_by_reach']}; unannotated {len(unannotated)}")
    return 1 if unannotated else 0


_PREV = {}


def _prev(function, va):
    if function not in _PREV:
        rows = sorted(json.loads((GHIDRA_CACHE / f'{function}-disassemble_function.txt').read_text())['instructions'], key=lambda r: int(r['address'], 16))
        _PREV[function] = {int(r['address'], 16): ' | '.join(x['instruction'] for x in rows[max(0, i - 2):i]) for i, r in enumerate(rows)}
    return _PREV[function][va]


if __name__ == '__main__':
    raise SystemExit(main())
