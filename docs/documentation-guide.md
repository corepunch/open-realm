# Documentation Guide For Agents

Documentation is part of implementation and investigation work. If an agent had to inspect code, data, history, logs, external
references, or runtime behavior to learn something that was not obvious from the existing documentation, that knowledge must be
written down before the task is complete.

## What Must Be Captured

Document facts that would otherwise force a future agent to repeat the investigation, including:

- ownership and lifecycle across modules;
- authoritative data sources and lookup chains;
- binary/text schema fields, offsets, keys, sentinel values, and version differences;
- state packing, saved-data formats, network contracts, and default-value semantics;
- commands used to reproduce, inspect, diagnose, or visually verify behavior;
- root causes confirmed by targeted logs and the evidence that distinguished them from plausible alternatives;
- history discovered with `git blame`, `git log -p`, or issue/PR research when it explains a constraint;
- failed approaches or misleading assumptions that are likely to waste time again;
- tool limitations and the reliable workaround or extension point.

Do not copy transient terminal output wholesale. Convert it into stable facts, commands, tables, lookup chains, and warnings. Do not
document guesses as facts; identify hypotheses and unresolved questions explicitly.

## Where Knowledge Belongs

1. Extend the subsystem's existing dedicated document when one exists.
2. Create a focused file under `docs/`; use `docs/architecture/` for engine architecture and `docs/games/<game>/` for game-specific material.
3. Keep `AGENTS.md` to durable rules and a short Further Reading index. Do not put subsystem manuals there.
4. Add the document to the nearest table of contents: `AGENTS.md`, a game `readme.md`, an architecture index, or another parent guide.
5. Add reciprocal "See also" links when two documents cover adjacent entry points, so the knowledge is discoverable from either one.

## Recommended Shape

Use only the sections that help future work:

```markdown
# Topic

## Contract
What owns the behavior, which data is authoritative, and what values mean.

## Data Flow
Source -> parser -> runtime state -> consumer.

## Schema Or Mapping
Compact tables for fields, IDs, flags, versions, or exact relationships.

## Diagnostic Workflow
Copyable bounded commands, expected evidence, and relevant file paths.

## Known Pitfalls
Incorrect assumptions, version differences, silent-failure risks, and why prior bugs happened.

## Verification
Tests and runtime scenes that cover the behavior.
```

Prefer exact symbols and repository-relative paths so `rg` finds the documentation alongside code searches. Include representative
values only when they explain the contract; label local-client observations as such rather than implying universal format behavior.

## Completion Checklist

Before finishing work that required investigation:

- [ ] Stable findings are in a dedicated document, not only in conversation or comments.
- [ ] Commands are bounded (`+com_frame_limit N` where applicable) and copyable.
- [ ] Authoritative sources and version-specific observations are distinguished.
- [ ] The relevant table of contents links to the document.
- [ ] Adjacent workflow/reference documents cross-link it when useful.
- [ ] Code comments explain only local constraints; broader context lives in documentation.
- [ ] Documentation matches the final implementation and tests, not an abandoned hypothesis.
