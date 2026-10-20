# Proposed text (not applied) — ACC-04.1

## TODO evidence sentence (ACC-04.1)

Evidence: ordinary adaptive costs (research ACC-04.1): integer distance equals floor(sqrt) on 135,981
inputs; all 188 goal routes over 336 producer-built requests (84 maps, transposes, two sizes) equal the
Dijkstra optimum of the search's own explored graph; differences to an 8-neighbour grid optimum (135 above,
44 below, max 6.7%) and four transposition cost differences are explained by first-lookup representatives,
any-angle coarse edges and asymmetric side/heap order; 157 budget cases confirm budget+1 work, strict
discovery-time nearest selection and representative+0.5 partial endpoints. No optimality claim.

## retail-pathfinding-search.md, "Complete adaptive request oracle" — append

Adaptive A* is exact on the graph it builds: every goal route equals Dijkstra over its own relaxations.
That graph is order-dependent (representatives are first-lookup coordinates; N,E,S,W side order and
first-half recursion are not symmetric under transposition), so a transposed map can cost a few units
more or less (880/891, 845/849, 894/895, 905/920). Coarse edges are Euclidean between representatives, so
retail can beat an 8-neighbour grid optimum. Equal-g relaxations keep the first parent; nearest-node
selection uses unscaled squared distance at first discovery with strict comparison.
