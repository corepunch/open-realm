# Retail shop item contracts — audit facts, acceptance rules and gaps

`games/warcraft-3/game/tests/t_retail_shop_contract.c` registers 45 shop listing
contracts and indexes 62 **shop-specific** gameplay effects in separate case
files. It uses `shared/test.h` through the game's test harness. The latest full
`make test` run passes the shop contract suite in both classic and TFT modes.

## Source priority

1. Real `War3.mpq`/`War3x.mpq` object-data values (user's archive audit below).
2. Actual production OpenRealm logic.
3. Blizzard shop and item web pages for behavioral descriptions where the data
   does not specify semantics.
4. Synthetic SLKs **only as controlled test fixture transport**; do not infer
   retail values from deliberately changed regression inputs.

The archived audit confirms item rawcodes, `abilList`, gold/lumber, stock maximum,
replenishment and start. **It does not confirm all ability fields, actual
`ItemFunc.txt` `Requires`, cooldowns, UI behavior or every neutral-building offer.**
A passing stock test cannot substitute for a behavioral item-effect test.

## Rawcode and stock oracles (both locally checked ROC and TFT)

| Item | Item ID | Exact `abilList` | Gold | Lumber | Max | Replenish s | Start s |
|---|---|---|---:|---:|---:|---:|---:|
| Anti-Magic Potion | `pams` | `AIxs` | 100 | 0 | 2 | 120 | 0 |
| Boots of Speed | `bspd` | `AIms` | 250 | 0 | 1 | 60 | 220 |
| Circlet of Nobility | `cnob` | `AIx2` | 175 | 0 | 1 | 120 | 0 |
| Dust of Appearance | `dust` | `AItb` | 75 | 0 | 1 | 60 | 0 |
| Healing Salve | `hslv` | `AIrl` | 100 | 0 | 2 | 60 | 0 |
| Ivory Tower | `tsct` | `AIbt` | 40 | 20 | 3 | 30 | 0 |
| Lesser Clarity Potion | `plcl` | `AIpl` | 70 | 0 | 2 | 30 | 0 |
| Mechanical Critter | `mcri` | `Amec` | 50 | 0 | 2 | 60 | 0 |
| Moonstone | `moon` | `AIct` | 50 | 0 | 1 | 90 | 0 |
| Orb of Corruption | `ocor` | `AIcb` | 375 | 0 | 1 | 120 | 0 |
| Orb of Fire | `ofir` | `AIfb` | 275 | 0 | 1 | 120 | 0 |
| Orb of Lightning | `oli2` | `AIll` | 375 | 0 | 1 | 120 | 0 |
| Orb of Venom | `oven` | `AIpb,Apo2` | 325 | 0 | 1 | 120 | 0 |
| Periapt of Vitality | `prvt` | `AIlf` | 350 | 0 | 1 | 120 | 0 |
| Potion of Healing | `phea` | `AIh1` | 150 | 0 | 3 | 120 | 440 |
| Potion of Invisibility | `pinv` | `AIv1` | 100 | 0 | 1 | 120 | 440 |
| Potion of Lesser Invulnerability | `pnvl` | `AIvl` | 150 | 0 | 1 | 120 | 440 |
| Potion of Mana | `pman` | `AIm1` | 200 | 0 | 2 | 120 | 440 |
| Rod of Necromancy | `rnec` | `AIrd` | 150 | 0 | 1 | 60 | 0 |
| Sacrificial Skull | `skul` | `Ablp` | 50 | 0 | 2 | 60 | 0 |
| Scroll of Healing | `shea` | `AIha` | 250 | 0 | 2 | 120 | 440 |
| Scroll of Protection | `spro` | `AIda` | 150 | 0 | 2 | 120 | 440 |
| Scroll of Regeneration | `sreg` | `AIsl` | 100 | 0 | 2 | 90 | 0 |
| Scroll of Speed | `shas` | `AIsa` | 50 | 0 | 1 | 60 | 0 |
| Scroll of Town Portal | `stwp` | `AItp` | 350 | 0 | 2 | 120 | 440 |
| Staff of Preservation | `spre` | `ANpr` | 150 | 0 | 1 | 120 | 0 |
| Staff of Sanctuary | `ssan` | `ANsa` | 250 | 0 | 1 | 120 | 0 |
| Staff of Teleportation | `stel` | `AImt` | 150 | 0 | 1 | 120 | 220 |
| Tiny Great Hall | `tgrh` | `AIbg` | 600 | 185 | 1 | 120 | 0 |
| Tome of Retraining | `tret` | `Aret` | 300 | 0 | 1 | 120 | 440 |

## Shop listing oracles

| Shop unit | Field | Item IDs (ordered) | Source |
|---|---|---|---|
| `hvlt` | `Makeitems` | `sreg,mcri,plcl,phea,pman,stwp,tsct,ofir,ssan` | https://classic.battle.net/war3/human/arcanevault.shtml |
| `ovln` | `Makeitems` | `shas,hslv,plcl,phea,pman,stwp,tgrh,oli2` | https://classic.battle.net/war3/orc/voodoolounge.shtml |
| `eden` | `Makeitems` | `moon,plcl,dust,phea,pman,stwp,spre,oven,pams` | https://classic.battle.net/war3/nightelf/ancientofwonders.shtml |
| `utom` | `Makeitems` | `rnec,dust,skul,phea,pman,stwp,ocor,shea` | https://classic.battle.net/war3/undead/tombofrelics.shtml |
| `ngme` | `Sellitems` | `stwp,bspd,dust,tret,prvt,cnob,stel,pnvl,shea,spro,pinv` | https://classic.battle.net/war3/neutral/buildings.shtml |

## Behavior verification

The `retail_shop_contract` group exercises each of the 45 entries with an
actual shop, actual `G_UpdateShopItemStock`, and start/replenish boundaries;
rejects unaffordable transactions without stealing stock/resources; exercises
real `G_ShopPurchaseItem`, checks the item identity and ability list and checks
resource deductions. It does **not** substitute a web tooltip for an item
ability or grant shop tech for free; when a retail `Requires` record cannot be
reconstructed from this audit, agents must add its actual dependency before
calling a failure a gameplay regression.

The `retail_item_effect` group contains focused **red/green executable** behavior checks documented in [Hero Item Reference Tests](hero-item-reference-tests.md); the remaining 62 shop-specific effects are source-indexed acceptance cases. Positive cases must
change real HP, mana, buff state, owner, position, attack output, time-of-day,
item charges, etc. Unsupported outcomes should remain failed tests. The current
fixture includes high-confidence active effects directly and preserves the
other item effects in a machine-readable matrix for expansion. A `TODO` must
never count as a test pass.

### Particularly important cases

- `oli2` → **`AIll`** (`code=AIsb`, not `AIlb`). Verified `DataA=5`,
  `DataB=35`, `DataC=10`, `DataD=35`, `DataE=2`, `UnitID=AIpg`;
  verify precise `DataB` chance semantics before statistical 35% assertions.
- `ocor` → `AIcb`: `DataA=5`, `DataB=5`, `DataE=2`, `Dur=5`, `BuffID=BIcb`.
  **Armor has to actually drop**, not just add a status; repository
  `item-orbs.md` says the armor consumer is not yet implemented.
- `oven` → **`AIpb,Apo2`**: damage and poison are separate effects;
  `Apo2 DataA=9`, `DataD=1`, `Dur=6`, `BuffID=BIpb,BIpd`.
- `pams` → `AIxs` (`code=Aami`): installed ROC/TFT ability rows both
  reported 15-second `Bams/Bam2`, cooldown 30; disregard the older 90-second
  documentation **until reconciled with actual archive provenance**.
- `tsct` → `AIbt` → `hwtw` and `tgrh` → `AIbg` → `htow`; tests must
  also check real placement/pathing, cancellation and construction timing.

## Services still requiring retail data verification

The user confirmed `ngad` (`ngsp,nzep,ngir`), `nshp` (`nbot`), `ntav`
(`Nalc,Ntin,Nfir,Nngs,Nbrn,Npbm,Nplh,Nbst`), one `nmer`
(`nftb,nfsp,nogm,ngrk`), `nmr2`–`nmrf`, and `ndro`
(`nnht,nndk,nndr`). The source offering CSV also contains Reveal,
Tavern resurrection, Marketplace, fountains, and Way Gates: **their action,
price, field values and map links were not confirmed in that audit**. Do not
invent rawcodes or assert invented field values. Follow with unit/balance/map
fixtures after extracting actual tables.

## Future source files

- `Units/ItemData.slk` and `Units/AbilityData.slk` (plus `AbilityBuffData.slk`)
- `Units/HumanUnitFunc.txt`, `Units/OrcUnitFunc.txt`, `Units/NightElfUnitFunc.txt`, `Units/UndeadUnitFunc.txt`, `Units/NeutralUnitFunc.txt`
- `Units/UnitAbilities.slk`, `Units/UnitBalance.slk`, `Units/UnitUI.slk`, `Units/UnitData.slk`
- `Units/ItemFunc.txt` and race/neutral `UnitStrings.txt` where applicable
- Representative `war3map.w3i`, `war3map.w3u`, `war3map.w3t`, `war3map.w3a`

Run the focused shop listing contracts and item effects with:

```sh
make test-wc3-engine WC3_PATTERN='retail_shop_contract.*'
make test-wc3-engine WC3_PATTERN='retail_item_effect.*'
```

## Additional source coverage

The five Blizzard item-category pages (charged, power-up, consumed, permanent, artifact) are captured in [Hero Item Reference Tests](hero-item-reference-tests.md), with 133 source contracts and 266 positive/negative acceptance scenarios. Retail rawcodes remain authoritative.
