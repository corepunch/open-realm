# Ability Commit Compliance Review and Fix Plan

## Scope and Evidence

Reviewed `b0177a83` (ability and sound regressions) and `106c0f55` (Arthas abilities)
against the repository `AGENTS.md` and `CONTRIBUTING.md` on September 20, 2026.
This is a review and proposed implementation sequence; production code is unchanged.

The current combined baseline passes `make test`, including 1,636 WC3 tests /
29,665 assertions in each archive mode. `git diff HEAD~2 HEAD --check` passes.
`python3 tools/wc3_ability_class_audit.py --format=coverage` completes and reports
262 registered class IDs, 155 registered TODOs, and 73 unregistered retail IDs
(including abstract classes). Registration is not behavioral verification.
These checks do not establish historical test-first ordering or the debug build matrix.

## Findings

1. **Fixed: save compatibility changed.** The corpse lifecycle flags now reuse
   spare persisted `aiflags` bits, so `edict_t` size and save format 35 remain
   unchanged. The new save test covers both bits together.
2. **Fixed: active Divine Shield is not saveable.** Its modified implementation
   assigns `divine_shield_think` to an edict. The callback is now exported and
   appended to `save_cfunctions[]`; the shield test saves, reloads, and expires
   the live thinker through `G_RunEntity`.
3. **Medium: ability ownership leaks into shared machinery.** `106c0f55` adds
   `S_UpdateHeroAuraEffects` directly to `G_RunEntity` and recognizes
   `CAbilityAnimateDead` inside generic `s_spell.c` dispel policy. `b0177a83`
   adds generic timed-life dispel policy inside `s_cyclone.c`. These extend
   spell-specific coupling instead of routing policy through owning procedures.
   The existing `A_UPDATE` dispatch is available for persistent behavior.
4. **Medium: lifecycle tests stop short of the production sequence.** The new
   Devotion/Unholy presentation tests manually call both update helpers; Death
   Coil manually invokes `currentmove->endfunc`. Animate Dead and Death Pact
   assert corpse flags without advancing timed death/death animation/decay to
   removal. Dispel tests retain `BTLF` without proving subsequent expiration.
   The new corpse-field save tests are scalar round trips, not continuation of
   those live lifecycles. These miss the scheduler, inverse, and observable-result
   requirements. Mass Teleport and Blizzard do have live save/load tests; the
   Divine Shield test now does as well.
5. **Medium: ROC and stock-shaped fixture coverage is incomplete.** New Arthas
   tests inject `DataA1`/`DataB1`/`BuffID1` rows regardless of archive mode.
   Aura presentation tests substitute `Biml` for actual aura buffs. Those custom
   cases are useful, but do not replace explicit ROC column/omission cases and
   stock BuffID/target masks. The existing ROC Blizzard test does not cover the
   added Arthas data contracts.
6. **Medium: new silent defaults and unresolved sound data.** Death Coil's new
   speed helper silently substitutes 1000 for absent/nonpositive `Missilespeed`.
   New aura presentation substitutes base BuffID for blank alias data. The sound
   registration path returns zero for a requested alias with no usable row/file.
   Establish authored inheritance and sentinel semantics; optional absent sounds
   are different from an unresolved nonempty alias. Valid absence needs no error,
   but invalid requested data needs a diagnostic, and genuine compatibility
   workarounds need the required TODO/HACK explanation.
7. **Low: local coding discipline.** New `BLIZZARD_*` numeric macros lack `BZ_`
   prefixes and trailing units/rationale comments. `heroAuraPresentation_t` is
   declared after functions and lacks the prescribed struct/pointer typedefs.
   New four-argument helpers (`corpse_preferred`, `hero_aura_sync_overlay`)
   violate the parameter-grouping rule. New sound calls split argument lists
   across physical lines. Several nontrivial new helpers lack contract comments.

## Proposed Fix Sequence

1. **Reproduce before editing behavior.** Preserve the baseline above. Add a live
   Divine Shield save reproducer first and confirm failure. Add deterministic
   scheduler-driven aura membership/removal, projectile travel/impact, temporary
   summon expiry, sacrifice cleanup, and save/resume scenarios. Include entity
   slot reuse and interruption where those owners retain references. Add actual
   ROC and TFT fixtures, stock shapes and non-stock values. Keep helper tests as
   supplementary coverage. Record which tests fail versus merely extend coverage.
2. **Repair persistence.** Append the shield callback to the roster and prove
   save/load/expiry with the real scheduler. Keep the corpse policy in the
   persisted `aiflags` bitmask and test the unchanged version-35 envelope.
3. **Restore ownership.** Route aura work through generic ability update dispatch,
   retaining the shared cadence/cache mechanism under `skills/`. Move Animate
   Dead's dispel policy to its procedure through a typed query, and keep generic
   timed-life policy out of Cyclone. Avoid runtime descriptor inheritance or
   additional central spell-identity branches. Verify cache hit/miss, removal,
   death, reset and load reconstruction through the real entry points.
4. **Resolve authored data and diagnostics.** Trace profile inheritance and blank
   sentinels before changing defaults. Test a distinctive missile speed, omitted
   ROC fields, explicit blank alias fields, valid sound-table aliases and an
   unresolved nonempty sound alias. Diagnose genuine errors without adding
   per-frame investigative traces. Apply the local style corrections while
   touching these functions.
5. **Validate and document.** Run focused regressions, both WC3 archive modes,
   `make test`, affected builds with/without relevant debug defines, class audit,
   and diff checks. No shared engine paths currently changed, so do not invent
   an unrelated cross-game renderer build requirement. Update Arthas, sound,
   save/load, dispel and coverage docs to reflect measured behavior and limits.
   Fold durable findings into those docs, then remove this plan and its index
   entry. Use additive commits; propose the split above without rewriting the
   reviewed history. State network and save-format impacts separately.

## References

- [Agent rules](../../../AGENTS.md)
- [Contribution rules](../../../CONTRIBUTING.md)
- [Arthas abilities](arthas-abilities.md)
- [Save/load](save-load.md)
- [Ability implementation](ability-implementation-plan.md)
- [Existing ability verification](ability-verification-review.md)
