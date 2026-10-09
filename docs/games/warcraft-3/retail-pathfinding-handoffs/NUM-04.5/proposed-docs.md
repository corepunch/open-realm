# Proposed text (not applied) — NUM-04.5

## TODO evidence (NUM-04.5)
"Seed producer chain recovered (A+L): -loadfile LoadFile_StartPendingFile 2e2850 reads WorldEdit preference
'Test Map - Fixed Random Seed' (absent -> default 1) into mapflag MAP_LOCK_RANDOM_SEED 0x8000 via descriptor +0x34
(0x02008000) and GameSetup_ApplyDescriptor 2a0c80; 29e300 then seeds the path owner and, through 64f9a0/693710, the 45
CRandData streams with 0x77617233, or with the setup-record seed (GetTickCount stamped by 2a46a0) when the flag is
clear. Before the first owner visit only PlayerSetup_ResolveRaces (one draw per RACE_PREF_RANDOM slot, 12 slots) and
public JASS queries consume the owner. Injection-free model reproduces every owner/stream word from setup through the
first movement and separation visits in 3 locked maps (8/4/0 race draws), 2 unlocked runs and repeats; observer-free
JASS controls identical."

## Ledger (`retail-pathfinding-engine.md`, after "locked startup branch is also a production producer")
Replace "Unlocked/lobby seed, the default lock-flag producer and canonical player payload78 remain NUM-04.5" with:

The default -loadfile/test-map producer is the WorldEdit preference `Test Map - Fixed Random Seed`
(HKCU\Software\Blizzard Entertainment\WorldEdit, absent -> 1): `2e2850` ORs 0x8000 into the local game flags,
`2a0c80` ORs descriptor word 0x02008000 into the JASS mapflags (0x300000 -> 0x2308000; 0x400 is added later for the
locked-alliance custom forces), and `29e300` seeds owner and streams with 0x77617233. With the preference 0 the flag is
clear and both are seeded with the setup-record seed, which `2a46a0` stamps with GetTickCount on every lobby record
refresh (live: equal to the last stamp in 9/9 probed startups; seeds 0x4e5d898 and 0x4e68426 in two unlocked runs).
The `29e5c4` rol3 fold only runs when no usable record exists (not reached by -loadfile). The boot owner seed
0x69707365 (PathOwner_Construct via 04eec0) is never drawn before the setup reseed. Order: config() (twice) -> lobby
stamps -> descriptor -> 29e300 seed -> 693710 reseed -> 1e9dd0 races (during load) -> JASS main -> first owner visit
(1024) -> unit creation (stream 2 only) -> first separation visit (1028) -> first mover update. No owner draw occurs
between the setup seed and the race draws. Observer slots (payload78 forcing Human) are not exercised.

## Engine doc / TODO note
The engine currently zeroes `level.setup.map_flags` at map load, so production startup never seeds
`level.pathing_random` (stays {0,0}) and never resolves random races; only tests force 0x8000.
