# Proposed text (not applied) — NUM-04.6

## TODO evidence (NUM-04.6)
"693710 is RandData_ReseedStreams: CRandData (RTTI .?AVCRandData@@, 0x16c bytes) lives in TLS slot 0xd registry entry 3
(created per game session by 64f970 -> 693690 -> 65c7c0 with 45x Seed(0); destroyed by 64f900 -> 693620). Every stream is
reseeded from one word: the game seed (29e300 -> 64f9a0, same word as the path owner) or SetRandomSeed's first owner draw
(214140 tail). Consumers address stream table+4+8*i via 693660 (range), 6936a0 (unit real) and 695c70 (dice). Static
table: 39 fixed indices owned by abilities/buffs/effects/JASS ChooseRandom*/AI captain; live: unit-creation attack jitter
and damage dice on stream 2, ReactionDelay on stream 9, ChooseRandomCreep/NPBuilding/Item/ItemEx on 32/34/35/35 — all
words model-exact, independent of owner draws (identical across 8- vs 0-race-draw maps). Path/separation/retry/race and
public GetRandom* use only the owner. Audio (AudioRandom 6fd6a674, GetTickCount seed) and sprite animation
(6fd685ac) are client-local generators with run-dependent draw counts."

## Ledger (`retail-pathfinding-engine.md`, "Deterministic owner random state reaches public natives")
Replace "The full 693710 tail reseeds45 separate unit streams; those streams remain unported." with the CRandData
description above and the stream table (index -> owner) from the NUM-04.6 handoff, and state that the engine must keep
three classes separate: path owner (synced), CRandData streams (synced, 45 saved states), presentation/audio generators
(client-local, never saved, never seeded from the game seed).
