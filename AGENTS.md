# Agent Instructions

This is the single source of truth for all AI agent and coding assistant instructions.
Read this file before making any changes. Load the reference docs below only when your task touches that area.

## Reference Docs

- `ARCHITECTURE.md` — module layout, engine/game boundary, struct/API discipline, network contracts. Read when adding or changing engine modules, structs, function tables, or cross-module interfaces.
- `CONTRIBUTING.md` — test fixture rules, MPQ asset conventions, build and linking rules. Read when writing tests, adding fixtures, or modifying the build.
- `docs/wow-character.md` — WoW character appearance, DBC fields, M2 skin sections, component textures. Read when touching WoW character rendering or equipment.
- `docs/diagnostic-tools.md` — mpqtool, mdxtool, UI text renderer, time profiler. Read when investigating asset, rendering, or performance bugs.
- `docs/ui-authoring.md` — FDF/screen authoring conventions, UI layout rules. Read when writing or modifying UI screens or FDF-driven frames.

---

## Project Context

This codebase is inspired by **Quake 2**. The developer is deeply familiar with the Quake 2 architecture and wants to build a **real-time strategy (RTS) game** following the same style, conventions, and design philosophy.

Do not make assumptions — prefer to use Ghidra for parity when you can, to be as close to the original as possible. 1:1 is ideal.

## Debugging Discipline

**Find the actual root cause. Do not introduce workarounds.**

When something is broken, your first job is to understand *why* — not to make the symptom go away. Defensive guards, null checks, early returns, and fallback branches that paper over a failure without fixing its source are hacks. They hide bugs, complicate future maintenance, and are not acceptable here.

Concrete examples of what NOT to do:

- Adding a null-check guard around a call that should never receive null — instead, find why null is being produced upstream and fix that.
- Skipping a code path because it crashes — instead, find why the input reaching that path is invalid and fix the input.
- Catching or swallowing a parse error by returning early — instead, find why the input is malformed and handle it correctly at the source.

The right approach:

- Read the actual failing input, trace it through the code, and find the first place the invariant breaks.
- Check whether the issue is in the caller, the data, the format assumption, or the subsystem boundary.
- Fix the root cause in the appropriate layer. For example, if a file format variant is not being decoded correctly, decode it correctly — do not defend every downstream consumer against the bad bytes.
- Add a test that exercises the actual broken case through the same code path that was broken in production. A test that calls an internal helper directly, bypassing the broken layer, does not prove the fix works.

When you propose a fix and it touches symptoms rather than causes, stop and ask yourself: *what produces this bad state in the first place?* Investigate that instead.

## Coding Style

- Follow the C coding style used in the Quake 2 source code (id Software style).
- Use the same patterns for module organization, data structures, and naming conventions as in Quake 2.
- Prefer simple, flat, and data-oriented design over complex object-oriented abstractions.
- Keep the code readable, compact, and close to the metal — minimize unnecessary indirection.
- For trusted binary game data, prefer memory-mapped/file-shaped structs with trailing arrays wherever possible. Read the blob, allocate/copy it as one block if ownership is needed, and point consumers at that struct instead of decoding, cropping, or post-processing into parallel runtime arrays.
- Prefer table-driven parsing for keyed/text formats such as XML, FDF, catalogs, and similar game data. Define a small schema table first, for example `{ name, offsetof(struct, field), type }`, then run one generic parser over that table.
- Prefer format-driven parsing when the data has a fixed syntax. Configure the parser with the format and launch it, for example `sscanf(text, "%f,%f,%f", ...)`, instead of hand-writing character walkers, separator loops, and ad hoc token logic.
- Do not bury schema in long manual `if`/`else` or `switch` ladders when a compact table can describe the same work.
- Do not use several booleans to represent mutually exclusive state. Define and pass an enum, then dispatch from it.
- Put pure, reusable local helpers in a small nearby utils header as `static` functions. Keep subsystem-owned helpers that touch globals or runtime state in the `.c` file that owns that state.
- Write tiny parsing/utility helpers only when they remove real duplication or clarify a call site. Keep them brutally short; prefer standard C library calls (`strchr`, `strspn`, `strtoul`, etc.) over hand-written loops. Keep trivial statement bodies on one line, e.g. `if (*p == '"') quoted = !quoted;`.
- Use `snake_case` for functions and variables, `ALL_CAPS` for constants and macros.
- Use the `BZ_` prefix for project-private compile-time macros, generated binding helpers, and namespaced constants.
- When fixing warnings for unused future-facing hooks, prefer commenting them out over deleting them. Add a short comment explaining the warning and when the line should come back.

## Domain

- This is a **real-time strategy game** (RTS). Game logic must account for unit management, pathfinding, resource gathering, building construction, and large numbers of entities — adapted from the Quake 2 entity/server model where applicable.
