# Anti-Magic Shell

## Authoritative Data

The active ROC and TFT archives both define `Aams` as the same one-level Undead unit spell:

- `code=Aams`, range `500`, mana cost `75`, `Dur1=90`, and `HeroDur1=90`.
- `targs=air,ground`; allegiance is not restricted by the row.
- `BuffID1=Bams,Bam2`; `Bams` is the recipient status used for immunity.
- `DataA` through `DataI` are empty/zero. There is no authored absorption amount.
- ROC and TFT `UndeadAbilityStrings.txt` both say the barrier stops spells from affecting the target and lasts `Dur1` seconds.

Verify the normalized rows with:

```sh
build/bin/ability_audit -data 'data/Warcraft III' -roc -raw Aams
build/bin/ability_audit -data 'data/Warcraft III' -tft -raw Aams
```

The TFT class extraction maps `Aams` to `CAbilityAntiMagicShell`, parent `AAsm`. It maps `Aami` to the distinct subclass
`CAbilityAntiMagicShellInstant`, parent `Aams`, but `Aami` has no active ROC/TFT `AbilityData` row in the installed archives.
Do not register `Aami` or infer item behavior until authoritative object data supplies its concrete semantics.

## Runtime Contract

`CAbilityAntiMagicShell` delegates ordinary cast handling to `CAbilitySimpleSpell` and applies the first authored buff ID
through `unit_addtimedstatus`. `S_UnitSpellImmune` recognizes active `Bams`, so the shared target-validation and
`S_SpellDamage` paths reject spells until status expiry. Recasting an already protected target is rejected by that same
generic immunity check; after expiry, the ordinary cast path can apply a fresh status.

Physical attacks continue through `S_ResolveAttackHit` and `T_Damage`. Anti-Magic Shell does not use
`S_ManaShieldDamage`, does not consume mana from the protected unit, and does not maintain an absorption pool.