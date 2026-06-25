# Coding Style

Follow the C coding style used in the Quake 2 source code (id Software style).

## Conventions

- Use the same patterns for module organization, data structures, and naming conventions as in Quake 2.
- Prefer simple, flat, and data-oriented design over complex object-oriented abstractions.
- Keep the code readable, compact, and close to the metal — minimize unnecessary indirection.
- Use `snake_case` for functions and variables, `ALL_CAPS` for constants and macros, matching Quake 2 conventions.
- Use the `BZ_` prefix for project-private compile-time macros, generated binding helpers, environment toggles, and namespaced constants that need a project prefix.

## Parsing

- For trusted binary game data, prefer memory-mapped/file-shaped structs with trailing arrays wherever possible. Read the blob, allocate/copy it as one block if ownership is needed, and point consumers at that struct instead of decoding, cropping, or post-processing into parallel runtime arrays.
- Prefer table-driven parsing for keyed/text formats such as XML, FDF, catalogs, and similar game data. Define a small schema table first, for example `{ name, offsetof(struct, field), type }`, then run one generic parser over that table. This is also called data-driven or array-driven parsing: the field mapping is data, and the parser is a tiny machine that applies it.
- Prefer format-driven parsing when the data has a fixed syntax. Configure the parser with the format and launch it, for example `sscanf(text, "%f,%f,%f", ...)` for SC2 comma-separated vectors, instead of hand-writing character walkers, separator loops, and ad hoc token logic.
- Do not bury schema in long manual `if`/`else` or `switch` ladders when a compact table can describe the same work. Put the mapping beside the target struct, keep the interpreter small, and let adding a field mean adding one table row instead of adding custom logic in the parser body.

## Structs and Data

- Do not use several booleans to represent mutually exclusive state. If only one mode/kind/type can be active, define and pass an enum, then dispatch from that enum. For example, use one `sc2ObjectType_t` value instead of separate `is_unit`, `is_doodad`, and `is_camera` flags.
- Keep runtime structs concise and organized by grouping related fields and repeated shapes.
- Prefer small helper structs for repeated concepts (for example point/size/color groups) instead of repeating scalar fields across large structs.
- When several fields share the same type and semantic family, declare them together on one line (for example `int id, parent, relative_to, draw_layer;` or `fsize_t size, edge, tile;`).
- For many same-kind string fields, prefer enum-indexed arrays plus tiny access helpers over many separate named string members.
- Avoid fixed-size inline string buffers in runtime structs when the data is variable-length. Prefer owned pointers with one clear setter/append/free path.
- Prefer bit flags (`DWORD flags`) for many independent boolean properties instead of scattering many standalone `BOOL` fields.
- Keep struct field ownership explicit: pair every dynamic struct field family with local helpers for set/append/free and use one cleanup loop when possible.

## Helpers

- Put pure, reusable local helpers in a small nearby utils header, such as `sc2_utils.h`, as `static` functions. Keep subsystem-owned helpers that touch globals, allocation hosts, file handles, or runtime state in the `.c` file that owns that state.
- Write tiny parsing/utility helpers only when they remove real duplication or clarify a call site. Keep them brutally short; prefer simple standard C library calls (`strchr`, `strspn`, `strtoul`, etc.) over hand-written multi-line loops unless the format genuinely requires custom logic. Do not add ceremonial blank lines inside tiny helpers, and keep trivial statement bodies on one line when that is clearer, e.g. `if (*p == '"') quoted = !quoted;`. Avoid temporary success variables for tiny wrappers; branch directly on the call and return explicit `true`/`false` when that is shorter and clearer.
- Follow a strict Don't Repeat Yourself (DRY) rule: do not duplicate logic or repeat the same data literal in multiple places. If the same path/key/constant appears more than once (for example `Interface\\GlueXML\\AccountLogin.xml`), centralize it as one named constant or one shared loader path and reuse it.
- When fixing warnings for short, future-facing hooks such as one-line static moves, extern declarations, or placeholder assignments, prefer commenting them out over deleting them. Add a short comment explaining the warning being fixed and when the line should come back, for example that Linux `-Wall` warns while the hook is unused.

## Formatting

- Minimize vertical space. Prefer fewer, denser lines over many short ones.
- Keep C source lines at or under 120 characters. Single-statement helpers may stay on one line when they fit, but do not chain long runs of API calls or argument-heavy expressions horizontally.
- Single-statement functions go on one line: `int f(void) { return 0; }`
- Omit braces for single-statement `if`/`else`/`while` bodies.
- Keep control-flow keywords at the start of their own line in normal code paths. Do not write chained forms like `...; if (...)` or `...; while (...)` on the same physical line.
- Add a short comment before each non-trivial function describing why it exists and what it does — not a restatement of the name, but the constraint or contract that isn't obvious from the signature alone.
- For any fallback, workaround, or partial implementation, prepend `/* HACK: */` or `/* TODO: */` and explain *why* the fallback is needed (what asset/variant is missing, what upstream bug forces it, or what the proper fix would be). Never leave a silent fallback undocumented.

## Packing Multiple Statements

- Chain sequential, logically related statements on one line with `;`:
  `lua_pushvalue(L, 2); lua_pushvalue(L, 1);`
- Stop packing when the line would exceed 120 characters or when the calls form a list of similar operations. In those cases, use one operation per line or a small table/helper so the repetition reads vertically.
- Merge declarations that belong to the same logical step:
  `int key_idx = lua_absindex(L, -2), val_idx = lua_absindex(L, -1);`

## Ternary + Comma Operator for Conditional Initialization

- When an assignment depends on a condition that also has side effects, use the comma operator inside the ternary branch to keep it a single expression:
```c
  int nargs = lua_isnoneornil(L, 2) ? 1 : (lua_pushvalue(L, 2), lua_xmove(L, co, 1), 2);
```
  The comma operator sequences the side-effect calls; the branch evaluates to the final value. Use this to avoid splitting a variable's initialization from its declaration.

## Braces

- Omit braces when the body is a single statement or a single comma-chained expression.
- Keep braces for multi-statement `while` bodies, and anything that would become ambiguous without them.

## Pointers and Casts

- Inline pointer-through-cast writes where the intent is clear:
  `*((struct Object **)lua_getextraspace(L)) = self;`

## WinAPI-style Typedefs for Structs

- Struct names are ALL CAPS, short, and descriptive (e.g., `PORTRAITFOG`, `PORTRAITDEF`). No `_t` suffix.
- Use WinAPI-style `LP`/`LPC` typedefs for struct pointer types (e.g., `LPCPORTRAITDEF`, `LPRECT`).
- `LP` = long pointer (non-const), `LPC` = long pointer to const.
- Define both in `tr_public.h` alongside the struct, using separate `typedef` lines so `LPC` is `const struct *`:
  ```c
  typedef struct _PORTRAITFOG {
      BOOL has_fog;
      COLOR32 fog_color;
      FLOAT fog_near;
      FLOAT fog_far;
  } PORTRAITFOG, *PPORTRAITFOG;
  typedef PORTRAITFOG const *LPCPORTRAITFOG;
  ```
- Use these typedefs in function signatures and call sites rather than bare `struct foo *`.

## What to Avoid

- Do not introduce helper variables just to name an intermediate result if the expression is already readable inline.
- Do not add blank lines between short, related statements.
- Do not split a declaration and its first assignment onto separate lines.

## WoW UI Code

- For WoW UI code (`games/world-of-warcraft/ui/`), do not fail silently. When a required script, handler, renderer resource, or fallback path is missing, emit a clear `UIWow:` log that explains what was skipped and why. Prefer one-time warnings for per-frame paths to avoid log spam.
