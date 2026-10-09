# ORDER-01.10 addendum 1 — q1 acquisition anomaly (Mismatch 3) resolved as a probe artifact

`RS-ORDER-01.10-q2` (`order0110_queue_probe_b.j` `b197abc0…`, map `ceb697ac…`) is the q1 probe with `Player(0)` instead
of `GetLocalPlayer()` and **without** `SetCameraBounds(-1024,…,3072)`; everything else identical. Capture
`RS-ORDER-01.10-q2-observe-1-envC.jsonl` (`summary-q2.json`; public records identical to its own Preload stream):
the Attack Move of case 1 now acquires the paused enemy (acquisition sub-chain prepended at decision tick 167) and hits at 173/186 with source head **851983**,
KillUnit at 200 keeps 851983 (`kill_after`), and arrival at (996.461,1281.022) retires to 0 at 234. So the q1
non-acquisition is caused by one of the two probe differences (most likely the widened camera bounds), not by Attack
ownership or by the queued order. Both ORDER-01.18 patrol_queue-terrain scenes show the same artifact (p1 vs p2).
This b-variant's Shift inputs did not produce queued orders (pixel/camera mapping changed with the default bounds),
so q1 remains the queued-handoff witness; q2 is not used for queue claims. No frozen expected value changes.
