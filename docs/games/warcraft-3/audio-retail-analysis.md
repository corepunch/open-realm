# Retail audio analysis (1.27.1.7085)

## Binary and workspace

Analyzed local `game.dll`, file version `1.27.1.7085`, product version
`1.27.1.7085 (1225d28)`, image base `0x6f000000`, SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.
Addresses below are preferred-image VAs, not portable offsets for other builds.

Local tools and durable analysis live under `/GitHub`:

- `ghidra-mcp`: bethington/ghidra-mcp, commit `5c8446d932746c2c4ba9d8abd729f7fe54339f69`, built with Gradle and local JDK 21.
- `re-tools/ghidra_12.1.4_PUBLIC`: official Ghidra release dated 2026-09-21.
- `radare2`: 6.2.2, commit `ad27058877024389292fddf12e1db6e13824ba34`.
- `r2ghidra`: commit `763477467d5c74e5a6aeb72640b5544c85f8c909`; `r2` and `pdg` installed under `~/.local`.
- `wc3-analysis/projects/WC3Audio.gpr`: persistent Ghidra project.
- `wc3-analysis/reports`: local decompilations, disassembly, source-string xrefs, channel table, tool logs.
- `warcraftIII-structures`: TinkerWorX headers, reference only. Their offsets were not blindly applied.

The original binary stays in the game installation. Decompiled retail bodies and
extracted retail SLKs stay in the local analysis workspace, not in this repository.
The Ghidra MCP backend binds to `127.0.0.1:8089`. Codex has a `ghidra` stdio MCP
entry pointing to `/GitHub/ghidra-mcp/.venv/bin/bridge-mcp-ghidra`. New sessions
can discover it; HTTP endpoints can also be called directly locally.

## Analysis status and navigation

The broad headless pass saved 102,913 discovered functions and 161,389 symbols,
but reached its 900-second analysis limit. These are discovery counts, not a
claim that all boundaries or prototypes are correct. RTTI analysis ran; some
exception-handler disassembly diagnostics remain. The focused mapping and
selected decompilations subsequently ran successfully with `-noanalysis`.
The resumed broad pass completed and saved on 2026-09-25 at 09:47:47 UTC
in 364 seconds, without another timeout.

Recovered RTTI anchors include `CSoundWar3::vftable` at `6fa9efc0`, its complete
object locator at `6fbf0464`, and `CSoundListener::vftable` at `6fbdb124`.
`SoundDBChannel` has a type descriptor at `6fd36230`; this does not by itself
establish its complete layout. The manually recovered backend configuration
is therefore named separately, rather than assuming those types are identical.

The project contains three types under `/WC3AudioRecovered`:

- `WC3SoundInstancePartial`: filename +0x34, runtime flags +0x13c, unsigned
  priority +0x140, user ID +0x148, Miles sample +0x160, channel +0x178,
  start ticks +0x17c, state +0x1ac. Its size is a known prefix, not a recovered
  complete allocation size.
- `WC3SoundLabelPartial`: file count +0x20, filenames +0x24, authored flags
  +0x40, distance parameters +0x44/+0x48, squared cutoff +0x50, last variant +0x70.
- `WC3SoundChannelConfig`: full 20-byte table row; the 16-entry static array
  has been typed and named `Audio_DefaultChannelConfigs`.

`Audio_AdmitInstance` has an explicitly mapped ECX receiver and stack logger
parameter, so its decompilation uses named structure fields. Unknown bytes remain
undefined. The other recovered names are navigation aids; their complete ABI
has not all been repaired.

Reproducible scripts are in `tools/ghidra/`: `MapAudio.java` checks the binary
SHA256 before applying names, comments, types and the scheduler signature;
`AudioAnchors.java` exports string/RTTI xrefs and nearby decompilations;
`AudioFunctions.java` exports selected functions and callers/callees.

```sh
# The backend owns the project lock. Stop it before GUI/headless edits.
systemctl --user stop wc3-ghidra-mcp.service
JAVA_HOME='/GitHub/re-tools/jdk-21.0.12.1+1' \
  /GitHub/re-tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless \
  /GitHub/wc3-analysis/projects WC3Audio -process game.dll -noanalysis \
  -scriptPath tools/ghidra \
  -postScript MapAudio.java /GitHub/wc3-analysis/reports/structures.txt
systemctl --user start wc3-ghidra-mcp.service
curl -s 'http://127.0.0.1:8089/decompile_function?address=6f0af5e0'
curl -s 'http://127.0.0.1:8089/get_xrefs_to?address=6f0af5e0'
```

For GUI navigation, stop the service, run `ghidra`, open `WC3Audio.gpr`, and use
Go To with a named function or address above. Close the GUI project before
restarting the service. The extension is installed in the Ghidra user profile.
MCP was verified through a real stdio client: initialize, tool listing, metadata,
and typed scheduler decompilation. HTTP xrefs resolve `Audio_StartInstance` as
its caller at `6f0afe6b`.

## Confirmed function map

These are recovered descriptive names, not original PDB symbols.

| Address | Role | Evidence |
| --- | --- | --- |
| `6f3593e0` | Play unit response | Enabled/ownership/fog checks; builds label using response kind |
| `6f34b150` | Play sound label | Resolves row, filters mode/distance/zoom, chooses variant, forwards backend request |
| `6f34fee0` | Choose variant | Sequential, explicit index and random-with-repeat-avoidance paths |
| `6f340140` | Convert SLK flags | Authored bit layout differs from backend bit layout |
| `6f0823d0` | Create/play backend sound | Allocates HSOUND and calls the admission/start path |
| `6f0afe00` | Start instance | Admission followed by MIDI/2D/3D backend dispatch |
| `6f0af5e0` | Admit instance | Duplicate checks, channel/global caps, conditional preemption; diagnostic strings explain branches |
| `6f0abd50` | Find lowest-priority channel head | Walks per-channel heads, compares unsigned priority |
| `6f0af9b0` | Start 2D sample | Miles sample file/rate/loop/EOS/start/resume imports |
| `6f35b0b0` | Unit cooldown check | Hash key is caller's unit pointer; compares entry +0x18 with GetTickCount |
| `6f690ea0` | What response | Response kind 0; advances accepted selection responses |
| `6f690dd0` | Pissed response | Kind 1, index = selection count - 3 |
| `6f690d70` | Yes response | Kind 2; resets selection counter |
| `6f690cf0` | YesAttack response | Kind 3; resets selection counter |
| `6f690ef0` | Warcry response | Kind 5; randomized global request countdown |
| `6f6955c0` | Reset selection counters | Clears counters and remembered unit identity pair |
| `6f688b70` | Increment selection counter | Increments global at `6fd707f0` |
| `6f092a10` | Millisecond clock | Direct GetTickCount import thunk |

The suffix pointer table at `6fce5734` is exactly
`What, Pissed, Yes, YesAttack, Ready, Warcry`. Assembly register setup confirms
ECX carries the unit and EDX the response kind. A nearby function-prologue search
is insufficient for discovery: several relevant leaf functions omit EBP frames.

## Admission policy recovered from the binary

`6f0af5e0` uses unsigned priority comparisons. Greater priority is stronger, but
priority alone does not authorize eviction. Its observed decisions are:

1. Duplicate user ID checks can reject, or preempt when explicitly permitted.
   The diagnostic term "username" denotes a numeric request identity.
2. Duplicate filenames have separate reject/preempt flags. Duplicate preemption
   requires a strictly lower-priority existing instance in the examined branch.
3. Below 24 active instances, inspect the requested channel's list and cap.
   Oldest-preemption candidates may have **equal** priority. The ordinary channel
   preemption branch requires strictly lower priority at the channel head.
   A separate per-filename count of four also participates in admission.
4. At the global limit of 24, global preemption flags govern replacement.
   The priority-based path can replace equal priority; another path selects the
   global list head without that comparison. Do not substitute the engine's
   generic first-free/lowest-priority policy for these branches.

Static default channel configuration is at `6fab5ec8`, 16 rows of 20 bytes:
`maxSounds, minPriority, maxPriority, volumeScale(float), flags`.
Caps in channel-index order are:
`16, 3, 3, 3, 3, 8, 2, 3, 5, 3, 3, 8, 1, 6, 2, 2`.
All rows initially have priority range 0..UINT_MAX and volume scale 1.
The initialization call at `6f35772e` supplies that table and count 16; the
copy routine at `6f07f9d0` uses stride 0x14. These are defaults, not proof that
no later code changes channel configuration.

The authored flag-name table at `6fab7c50` maps:

| Flag | SLK bit | Backend bit after `6f340140` |
| --- | --- | --- |
| WANT3D | 0x1 | 0x2 |
| CHANNELFULLPREEMPT | 0x2 | 0x4 |
| CHANNELFULLPREEMPTOLDEST | 0x4 | 0x8 |
| LISTFULLPREEMPT | 0x8 | 0x10 |
| LISTFULLPREEMPTOLDEST | 0x10 | 0x20 |
| NODUPLICATES | 0x20 | 0x40 |
| DUPLICATEPREEMPT | 0x40 | 0x80 |
| NODUPEUSERNAMES | 0x80 | 0x400 |
| DUPUSERNAMEPREEMPT | 0x100 | 0x800 |
| IGNOREUSERNAME | 0x8000 | 0x8000 |

FootmanWhat in the local ROC UnitAckSounds table has priority 1000, channel 1,
and `WANT3D,NODUPEUSERNAMES,CHANNELFULLPREEMPT,RANDOMPITCH`. Channel 1's default
cap is **three**, not one. The combination matters; a global one-bark gate is not
established by the retail data.

## Bark timing and variant selection

The response wrappers call `6f35b0b0` with the same unit pointer later passed to
unit playback. That function hashes the pointer and compares the entry's deadline
against GetTickCount. This confirms a **per-unit wall-clock** eligibility check;
it is not a per-player simulation-time check. The follow-up trace established
`6f353220`: insert/find the unit-keyed entry and write `GetTickCount() + 0xfa`
to entry +0x18. Its caller `6f35b500` is registered in callback slots 1 and 3
(table `6fab7714`); admission preemption dispatches slot 3 through `6f0abcf0`.
Thus the delay is 250 ms after the completion/preemption notification, not
250 ms from the original request. Global `6fd6a72c` gates deadline updates: manager initialization `6f357520`
sets it to 1; shutdown `6f35aab0` clears it before stopping sounds. Those are
the two recovered write references. `6f0b0c50` omits IGNOREUSERNAME instances
from the user hash; incoming NODUPEUSERNAMES checks still consult tracked
instances even when the incoming request itself has IGNOREUSERNAME.

`6f0ae4b0` inserts channel members in ascending unsigned priority order, before
equal priorities (newest first). `6f0abd50` examines channel heads in ascending
channel order and retains the first on equal priority. `6f0af94f` confirms that
hitting the filename cap with CHANNELFULLPREEMPTOLDEST replaces the first
matching global-list entry, without a priority comparison in that branch.

Selection handling at `6f697a10` resets counters when the selected identity
changes, then invokes Pissed after three responses. `6f34fee0` supports explicit
and sequential indexing; random mode retries to avoid the previous index, with
a bounded retry count. `6f34b150` restores the previous variant index when the
backend rejects playback. Failed requests should not blindly advance the cycle.

## Implications for OpenRealm

The implementation now sends Channel/Flags/32-bit Priority and response identity
in a generic explicit sound policy (protocol 12). The game owns recovered
channel defaults; the mixer owns admission, sample completion and the 250 ms
post-completion/preemption clock. It uses 24 slots and the recovered duplicate,
channel and global preemption branches. Request-correlated client playback
receipts now replace the predicted unit portrait deadline.

Focused regressions reproduced the previous policy and clock failures and cover
transport, three channel-1 voices, duplicate identity/file handling, strict/equal
preemption, global and filename limits, real completion/preemption, wrap and
map reset. See the sound implementation document for remaining presentation,
non-admission fidelity gaps and simultaneous-request RNG limits. The server now
commits What/Pissed progress on client admission and switches portrait talking
on client start/end receipts.

See also [sound implementation](../../../games/warcraft-3/sounds.md) and
[entity sound architecture](../../../architecture/sound.md).

## Unattended broad-analysis continuation

`wc3-ghidra-analysis.service` resumes analysis of the existing project with an
8 GiB Java heap, eight analysis workers and a 24-hour per-file limit. It stops
MCP to release the project lock, backs up the annotated project under
`/GitHub/wc3-analysis/backups/<UTC timestamp>/`, then processes `game.dll` without
reimporting it. It refreshes the selected typed decompilations after analysis.
The service restarts MCP on exit, including failure; check the saved status/log
rather than interpreting an available MCP endpoint as successful analysis.

User lingering is enabled so this job survives logout. A sleep inhibitor exists
only while the job runs. The analysis service is started on demand, not enabled
for repeated runs at login.

```sh
systemctl --user show wc3-ghidra-analysis.service -p ActiveState -p SubState
cat /GitHub/wc3-analysis/reports/broad-resume-status.txt
tail -n 30 /GitHub/wc3-analysis/reports/broad-resume-latest.log
```

## Second-pass verification

The second audit found and reproduced two omissions: IGNOREUSERNAME instances
were incorrectly considered duplicate users/preemption victims, and a sound
ending exactly at a mixer block boundary remained active until another callback.
Both have failing-before/passing-after regression tests and fixes. Authored flag
registration also has fixture coverage. This completion check is the mixer's
sample boundary, not a claim to measure physical speaker/output-device latency.

`tools/ghidra/verify_audio_admission.py` executes the **original x86**
`6f0af5e0` routine in Unicorn, including its real channel-head search helper.
It compares acceptance and preemption victims against production `S_AdmitSound`
compiled through `audio_admission_probe.c`. Seed 20260925 produced 12,000
cases with zero mismatches after fixes, including unsigned priorities through
UINT_MAX, full/global/channel/filename limits, duplicate IDs and mixed flags.

The oracle shims logger output, filename comparison/count, user-hash queries,
and stop/notification callbacks. It supplies synthetic linked lists sorted as
confirmed by `6f0ae4b0`; it does not execute Miles, SLK loading, device timing,
or unit/UI callers. It is an independent check of admission control flow and
victim selection, **not** proof of end-to-end retail audio parity. Known
pitch/distance, concurrent variant choice and JASS gaps remain. The matching binary is local input and is never distributed.

```sh
# Optional local investigation; Unicorn is not a build/test dependency.
cc -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -I. -Ishared -Ishared/types -DTRUE=1 -DFALSE=0 \
  tools/ghidra/audio_admission_probe.c -o /tmp/audio-admission-probe
# Run with a Python environment containing unicorn:
python tools/ghidra/verify_audio_admission.py --binary "$WC3DATA/game.dll" \
  --probe /tmp/audio-admission-probe --report /tmp/audio-admission-report.json
```


## Live Frida playback evidence

Frida 17.18.0 / frida-tools 14.10.4 are installed in the local uv tool environment.
The matching Windows x86_64 server runs under Wine and injects the **ia32** agent
into retail `war3.exe`. Offsets in `tools/frida/wc3_audio.js` relocate from the
actual `game.dll` module base; the Python controller checks the original DLL's
SHA256 and the script checks its loaded path and admission prologue before hooking.
No DLL, MPQ, decoded audio, or decompiled implementation is included in the repo.
Installation sources: [Frida installation](https://frida.re/docs/installation/)
and [official releases](https://github.com/frida/frida/releases/tag/17.18.0).

The bounded Human01 capture on 2026-09-25 produced:

| Witness | Observed result |
|---|---|
| 60 backend-rejected label requests | Last-variant index restored, including non-sentinel previous indices |
| 14 rejected What calls | Selection counter unchanged; backend result 3 maps to label result 2 |
| 14 complete response lifecycles | Callback slot 0 starts portrait, slot 1 ends it; unit cooldown setter follows the end callback |
| Shared-file pair | SludgeMonsterWhat and SludgeMonsterReady both admit WaterElementalWhat1.wav, on channels 1 and 4 respectively |
| Queue-to-start delay | 0–254 ms in this capture; cached playback can start immediately |

The alias witness deliberately substitutes those two label names and explicit
variant zero for the first two genuine What requests. Everything after label
selection uses retail resolution, admission and playback. It is a controlled
alias experiment, not an observation that Arthas naturally has SludgeMonster
responses. The subsequent burst sends ordinary F1 selections. `--probe-aliases`
is optional; without it the script only observes audio and sends requested input.

Local evidence is in `/GitHub/wc3-analysis/reports/frida-final.jsonl` and
`frida-final-summary.json`. The trace SHA256 is
`bb0c90be93082d5b7351c77802c08e961ade8cd4dc3d86b3284c1854b191b13f`.
The unmodified control capture (`frida-control.jsonl`, no label or policy
substitution) independently has 42 variant rollbacks, five unchanged rejected
What counters, ten complete playback cycles and no checker violations.
A separate `frida-preempt.jsonl` capture enables DUPUSERNAMEPREEMPT on exactly
one genuine response while another response for that unit is playing. The old
ArthasWhat receives callback slot 3 after 216 ms of playback, and its unit
cooldown setter runs immediately afterward. This confirms the preemption
notification path with a controlled policy input; it does not claim ordinary
What requests naturally carry that flag.

A fresh admission differential run, `admission-differential-frida-pass.json`,
still has **12,000 comparisons and zero mismatches**. These are different checks:
Unicorn compares production C decisions to retail x86; Frida establishes the
live callers, variant mutation and playback notification contract.

`tools/frida/audio_response_probe.c` compiles the actual server response code
with fixture imports. Before the feedback fix it reported `queued=1`,
`talking_before_client_admission=1`, and `count_before_client_admission=1`.
After the fix it reports **`queued=1`, `talking=0`, `count=0`** before admission.
This is a server-state diagnostic, not a simulated mixer replay. Regression
tests now exercise the actual mixer lifecycle, request packet codec, reliable
receipt forwarding, game command handling, portrait payload and per-label
accepted-variant state. Duplicate, stale, foreign-owner and out-of-order receipts
do not advance or resurrect response state. Rejection does not commit a variant.

The feedback implementation follows the observed separation: admission commits
response progression, first sample starts the portrait, and completion/preemption
ends it. It removes the duration-based deadline rather than tuning an estimate.
The client sends generic request-correlated events; the game retains ownership
checks and unit/selection lifetime state. See the
[sound contract](../../../games/warcraft-3/sounds.md#authored-admission-and-response-timing).
This closes the premature state progression gap; it is not a claim of complete
retail audio parity. In particular, server-side random variant choice can precede
another unit's outstanding admission receipt for the same label.

The alias overwrite is fixed by `SoundIndexAlias`; filenames still identify
shared client samples. The earlier MPQ inventory found 35 filenames with
*different raw metadata cells*, not necessarily 35 semantic differences (an
omitted flags cell and explicit zero can mean the same thing). The live
SludgeMonster pair is an actual channel-policy difference, not such an omission.

### Reproduce the capture

Install the Python CLI with `uv tool install frida-tools`. Download the matching
`frida-server-17.18.0-windows-x86_64.exe.xz` from the official release and decompress
it to `/GitHub/wc3-analysis/frida/server.exe`. Keep server/client versions matched.
An isolated Xvfb display makes Wine input independent of the user's desktop focus:

```sh
Xvfb :97 -screen 0 1024x768x24 -nolisten tcp
# In another terminal, with the dedicated analysis Wine prefix:
DISPLAY=:97 WAYLAND_DISPLAY= WINEDEBUG=-all \
  WINEPREFIX="$HOME/.local/share/open-realm/wine-wc3" \
  wine /GitHub/wc3-analysis/frida/server.exe --listen=127.0.0.1:27043
# The Frida Python module is in uv's frida-tools environment:
"$HOME/.local/share/uv/tools/frida-tools/bin/python" \
  tools/frida/trace_wc3_audio.py --data "$WC3DATA" \
  --output /tmp/wc3-audio.jsonl --seconds 85 \
  --drive-input --burst-input --probe-aliases --x11-display :97
python3 tools/frida/analyze_audio_trace.py /tmp/wc3-audio.jsonl \
  --output /tmp/wc3-audio-summary.json --require-complete
```

Start Wine's analysis prefix on that display before its server; an already-running
Wine desktop keeps its original display. Do not terminate an unrelated user's
Wine prefix. The controller terminates only a process it spawns; `--pid` attaches
without taking ownership. `--x11-display` needs `xdotool`. Esc is sent at several
loading/cinematic checkpoints, followed by F1 selections every 350 ms. The run
is bounded by `--seconds`; a missed input or slow load must fail the witness
check, not be reported as parity. Instrumentation timings include hook overhead
and do not measure physical speaker latency.

```sh
cc -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -I. -Ishared -Ishared/types -Icommon -Iserver \
  -Igames/warcraft-3 -Igames/warcraft-3/common -Igames/warcraft-3/game \
  -DWC3 -DTOOL_COMMON_NO_MPQ tools/frida/audio_response_probe.c \
  -o /tmp/audio-response-probe
python3 tools/frida/analyze_audio_trace.py /tmp/wc3-audio.jsonl \
  --require-complete --response-probe /tmp/audio-response-probe
python3 tests/test_frida_audio_trace.py
```

### Structure and tracing pitfalls

- Label `+0x38` is priority and **`+0x3c` is channel**. `+0x2c` is volume;
  early exploratory traces incorrectly named that value channel. The final
  capture and updated `MapAudio.java` use the verified `+0x3c` offset.
- Instance `+0x244` contains four callback function pointers; `+0x254` contains
  their four ECX context pointers. `Audio_DispatchCallback` (`6f0abcf0`) takes
  its slot on the stack and dispatches that exact pair. Portrait context `+8`
  is the unit pointer and `+0xc` is the variant/animation index.
- Correlate callbacks by **sound instance and ordered lifetime**, not just unit.
  Two queued requests can share a unit; a sound allocation can also be reused.
  The checker has a regression for this misattribution.
- Native calls made inside Frida Interceptor callbacks did not produce the
  expected nested hook trace in the exploratory setup. Their standalone return
  values are excluded from the final evidence. The final alias probe changes
  the arguments of an existing call and observes its ordinary execution.
- `--probe-preemption` adds runtime flag `0x800` (DUPUSERNAMEPREEMPT) to one
  genuine What/Pissed request while a response with the same user is active.
  The mutation is logged as `probe-preemption-policy`. The original admission
  routine performs the preemption; no stop/callback is invoked by the script.
  Use `analyze_audio_trace.py ... --require-preemption` to require the resulting
  slot-3 portrait-end witness. Keep this controlled probe distinct from the
  unmodified control capture.


Validation after the alias fix: full `make test` passes, including ROC and TFT
**34,664 assertions / 1,904 tests each**, and `make build` passes. The first full
run exposed stale WoW module imports because the shared build schemas omitted
server headers from dependencies. Adding those dependencies rebuilt the module;
WoW's 397 engine assertions and the full rerun pass. The new trace checker also
passes its three asset-free tests. None of these results claim complete retail
audio parity.


## Playback-feedback implementation verification

The new queue/portrait regression failed before the implementation. A separate
index-731 regression reproduced the byte truncation in the private unit sound
cache/pending fields. They now retain full indices, with save version 46 guarding
the changed private layout. Protocol 12 transports the opaque request ID.

Mixer tests cover delayed first samples, duplicate rejection, preemption ordering,
exact last-sample completion, device/load rejection, stop-before-start and 300
undrained rejections. Client tests drain the real mixer receipts into the reliable
command buffer, including loading and backpressure. Game tests exercise the
`sound_event` command, stale/foreign/duplicate/out-of-order replies, selection
reset, overlapping voices, accepted-variant commit and actual portrait payloads.
The original x86 admission differential was rerun after adding feedback: 12,000
cases, zero mismatches (`admission-feedback-pass.json`).

Final feedback verification: `make test` passes with ROC and TFT **34,715/34,715
assertions in 1,911 tests each**, plus the mixer suite's **1,674/1,674 assertions
in 16 tests**. `make build` and the engine-boundary audit pass. The unused-client
slot regression additionally ensures that a disconnected slot with a default
player-zero identity cannot cancel a connected player's response. The diagnostic
output is retained in `response-feedback-state.json`, and the original retail
trace plus current production queue state in `frida-feedback-summary.json`.

Upstream-main integration verification: the frame scheduler runs response cleanup
once per frame, retaining the idle no-selection-scan check. Focus, entity reuse
and frame-scheduler tests now exercise receipts and disconnect cleanup instead
of predicted duration expiry. Full `make test` passes: ROC and TFT each
**34,771/34,771 assertions in 1,924 tests**, mixer **1,678/1,678 in 17 tests**.
`make build`, the engine-boundary audit and all three trace-checker tests pass.
The rebuilt admission probe again matches all 12,000 retail x86 cases
(`admission-pr-pass.json`).
