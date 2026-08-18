# ROC Ability Checklist

Source: `Units\AbilityData.slk` from `data/Warcraft III/War3.mpq` (Reign of Chaos data).

This checklist is grouped by Warcraft ability base `code`, because OpenWarcraft3 registers behavior by base code in `games/warcraft-3/game/skills/s_skills.c`. The `Aliases` column lists ROC `AbilityData.slk` aliases that share that base behavior.

Status values:

- `Partial`: some behavior is wired in OpenWarcraft3, but not full Warcraft III parity.
- `Stub`: the code is registered but does not perform the Warcraft III behavior yet.
- `TODO`: no local behavior registered for this base code.

ROC totals: 397 ability aliases, 244 unique base codes.

| Status | Code | Kind | ROC name/comment | Aliases | Notes |
|---|---|---|---|---|---|
| `Partial` | `AHad` | Hero, Item | Paladin - Devotion Aura | AHad, ACav, AIad | Visual/status stub; aura stats not applied. |
| `Partial` | `AHhb` | Hero | Paladin - Holy Light | AHhb | Target spell: range/mana/heal/half-damage/target art; filters/cooldown incomplete. |
| `Partial` | `AHtb` | Hero | Mountain King - Thunder Bolt | AHtb | Target spell: missile/damage/stun/range/mana; filters/cooldown incomplete. |
| `Partial` | `ANfb` | Hero | Fire Bolt | ANfb, Awfb, ACfb | Shares Thunder Bolt behavior with Fire Bolt art/data; filters/cooldown incomplete. |
| `Partial` | `Agld` | Unit/Common | Gold Mine | Agld | Basic gold mine worker loop exists. |
| `Partial` | `Ahar` | Unit/Common | Harvest | Ahar | Worker lumber/gold order path exists; not full War3 split variants. |
| `Stub` | `Amil` | Unit/Common | Militia | Amil | Registered only. |
| `Partial` | `Arep` | Unit/Common | Repair (Human) | Ahrp, Arep | Repair helper exists; command/targeting incomplete. |
| `TODO` | `ACad` | Unit/Common | Animate Dead (creep) | ACad |  |
| `TODO` | `ACdv` | Unit/Common | Devour (Creep) | ACdv |  |
| `TODO` | `ACrn` | Unit/Common | Reincarnation (creep) | ACrn |  |
| `TODO` | `ACtb` | Unit/Common | Thunder Bolt (Creep) | ACtb |  |
| `TODO` | `ACtc` | Unit/Common | Thunder Clap (Creep) | ACtc, ACt2 |  |
| `TODO` | `AEah` | Hero | Keeper of the Grove - Thorns Aura | AEah, ACah |  |
| `TODO` | `AEar` | Hero, Item | Moon Priestess - Trueshot Aura | AEar, ACat, AIar |  |
| `TODO` | `AEer` | Hero | Keeper of the Grove - Entangling Roots | AEer, Aenr |  |
| `TODO` | `AEev` | Hero, Item | Demon Hunter - Evasion | AEev, ACev, ACes, AIev |  |
| `TODO` | `AEfn` | Hero | Keeper of the Grove - Force of Nature | AEfn, ACfr |  |
| `Stub` | `AEim` | Hero | Demon Hunter - Immolation | AEim, ACim | Toggle status only; no mana drain/area damage yet. |
| `TODO` | `AEmb` | Hero | Demon Hunter - Mana Burn | AEmb, Amnb |  |
| `TODO` | `AEme` | Hero | Demon Hunter - Metamorphosis | AEme, AEIl |  |
| `TODO` | `AEsf` | Hero | Moon Priestess - Starfall | AEsf, AEsb |  |
| `TODO` | `AEst` | Hero | Moon Priestess - Scout | AEst |  |
| `TODO` | `AEtq` | Hero | Keeper of the Grove - Tranquility | AEtq |  |
| `TODO` | `AHab` | Hero, Item | Arch Mage - Brilliance Aura | AHab, ACba, AIba |  |
| `TODO` | `AHav` | Hero | Mountain King - Avatar | AHav |  |
| `TODO` | `AHbh` | Hero | Mountain King - Bash | AHbh, ACbh |  |
| `Partial` | `AHbz` | Hero | Arch Mage - Blizzard | AHbz, ACbz | Point-target periodic area damage; visuals/channel details incomplete. |
| `Stub` | `AHca` | Hero | Ranger - Cold Arrows | AHca, ACcw | Registered passive/autocast stub. |
| `TODO` | `AHds` | Hero | Paladin - Divine Shield | AHds, ACds |  |
| `TODO` | `AHfa` | Hero | Moon Priestess - Searing Arrows | AHfa, ACsa |  |
| `TODO` | `AHmt` | Hero | Arch Mage - Mass Teleport | AHmt |  |
| `TODO` | `AHre` | Hero | Paladin - Resurrection | AHre |  |
| `TODO` | `AHtc` | Hero | Mountain King - Thunder Clap | AHtc |  |
| `Partial` | `AHwe` | Hero | Arch Mage - Summon Water Elemental | AHwe | No-target summon with timed life. |
| `Stub` | `AIab` | Item | AgilityBonus (+1) | AIa1, AIa3, AIa4, AIa6, AIx5, AIx1, AIx2, AIs1, AIs3, AIs4, AIs6, AIi1, AIi3, AIi4, AIi6 | Registered item stat stub. |
| `Stub` | `AIam` | Item | AgilityMod | AIam, AIgm | Registered permanent stat stub. |
| `TODO` | `AIan` | Item | Animate Dead | AIan |  |
| `TODO` | `AIas` | Item | Attack Speed Increase | AIsx |  |
| `Stub` | `AIat` | Item | AttackBonus | AIat, AIt6, AIt9, AItc, AItf | Registered item stat stub. |
| `TODO` | `AIcf` | Item | ItemCloakOfFlames | AIcf |  |
| `Partial` | `AIco` | Item | ItemCommand | AIco | Shares Charm ownership-transfer handler. |
| `TODO` | `AIda` | Item | ItemDefenseAoe | AIda |  |
| `Stub` | `AIde` | Item | DefenseBonus (+1) | AId1, AId2, AId3, AId4, AId5 | Registered item armor stub. |
| `TODO` | `AIdi` | Item | ItemDispelAoe | AIdi |  |
| `TODO` | `AIdm` | Item | ItemDamageAoe | AIdm |  |
| `Stub` | `AIem` | Item | ExperienceMod | AIem, AIe2 | Registered item XP stub. |
| `TODO` | `AIfb` | Item | FireDamageBonus | AIfb |  |
| `TODO` | `AIfd` | Item | FigurineRedDrake | AIfd |  |
| `TODO` | `AIff` | Item | FigurineFurbolg | AIff |  |
| `TODO` | `AIfh` | Item | FigurineFelHound | AIfh |  |
| `TODO` | `AIfl` | Item | Flag | AIfl |  |
| `TODO` | `AIfr` | Item | FigurineRockGolem | AIfr |  |
| `Stub` | `AIfs` | Item | FigurineSkeleton | AIfs | Registered figurine summon stub. |
| `TODO` | `AIfu` | Item | FigurineDoomGuard | AIfu |  |
| `TODO` | `AIha` | Item | ItemHealAoe | AIha |  |
| `Partial` | `AIhe` | Item | ItemHeal (Lesser) | AIh1, AIh2 | Inventory alias dispatch and selected-unit heal exist. |
| `TODO` | `AIil` | Item | ItemIllusion | AIil |  |
| `Stub` | `AIim` | Item | IntelligenceMod | AIim, AItm | Registered permanent stat stub. |
| `TODO` | `AIlb` | Item | LightningDamageBonus | AIlb |  |
| `Stub` | `AIlm` | Item | LevelMod | AIlm | Registered item level stub. |
| `TODO` | `AIlp` | Item | LightningPurge | AIlp |  |
| `Partial` | `AIma` | Item | ItemManaRestore (Lesser) | AIm1, AIm2 | Inventory alias dispatch and selected-unit mana restore exist. |
| `Partial` | `AImi` | Item | Permanent Hit point Bonus from charged item | AImh | Selected-unit max/current health gain exists. |
| `Stub` | `AIml` | Item | MaxLifeBonus (Least) | AIlf, AIl1, AIl2 | Registered item life bonus stub. |
| `Stub` | `AImm` | Item | MaxManaBonus (Least) | AImb, AIbm | Registered item mana bonus stub. |
| `TODO` | `AImr` | Item | ItemManaRestoreAoe | AImr |  |
| `TODO` | `AIms` | Item | MoveSpeedBonus | AIms |  |
| `TODO` | `AIob` | Item | FrostDamageBonus | AIob |  |
| `TODO` | `AIpm` | Item | ItemPlaceMine | AIpm |  |
| `TODO` | `AIra` | Item | ItemRestoreAoe | AIra |  |
| `TODO` | `AIrc` | Item | ItemReincarnation | AIrc |  |
| `TODO` | `AIre` | Item | ItemRestore | AIre |  |
| `TODO` | `AIrm` | Item | ItemRegenMana | AIrm, AIrn |  |
| `TODO` | `AIrs` | Item | Resurrection | AIrs |  |
| `TODO` | `AIrt` | Item | ItemRecall | AIrt |  |
| `TODO` | `AIsi` | Item | SightBonus | AIsi |  |
| `Stub` | `AIsm` | Item | StrengthMod | AIsm, AInm | Registered permanent stat stub. |
| `TODO` | `AIso` | Item | SoulTrap | AIso |  |
| `TODO` | `AIsp` | Item | ItemSpeed | AIsp |  |
| `TODO` | `AIta` | Item | ItemDetectAoe | AIta |  |
| `TODO` | `AItp` | Item | ItemTownPortal | AItp |  |
| `TODO` | `AIva` | Item | Vampiric attack | SCva, AIva |  |
| `TODO` | `AIvi` | Item | ItemInvis (Lesser) | AIv1, AIv2 |  |
| `TODO` | `AIvu` | Item | ItemInvul | AIvu |  |
| `Stub` | `AIxm` | Item | Permanent All + 1 | AIxm | Registered permanent stat stub. |
| `TODO` | `AIzb` | Item | FreezeDamageBonus | AIzb |  |
| `TODO` | `ANdc` | Hero | Malganis - Dark Conversion | ANdc, SNdc |  |
| `TODO` | `ANdp` | Hero | Dark Portal | ANdp |  |
| `TODO` | `ANfd` | Hero | Finger of Death | ANfd |  |
| `TODO` | `ANin` | Hero | Inferno | ANin |  |
| `TODO` | `ANpi` | Unit/Common | Permanent Immolation | ANpi |  |
| `TODO` | `ANrc` | Hero | Rain of Chaos | ANrc |  |
| `TODO` | `ANrf` | Hero | Rain of Fire | ANrf, ACrf |  |
| `TODO` | `ANsl` | Hero | Malganis - Soul Preservation | ANsl |  |
| `TODO` | `AOac` | Item | Aura - Command (Creep) | ACac, AIcd |  |
| `TODO` | `AOae` | Hero, Item | Tauren Chieftain - Endurance Aura | AOae, SCae, AIae |  |
| `TODO` | `AOcl` | Hero | Farseer - Chain Lightning | AOcl, ACcl |  |
| `TODO` | `AOcr` | Hero | Blade Master - Critical Strike | AOcr, ACct |  |
| `TODO` | `AOeq` | Hero | Farseer - Earthquake | AOeq, SNeq |  |
| `TODO` | `AOfs` | Hero | Farseer - Far Sight | AOfs |  |
| `TODO` | `AOmi` | Hero | Blade Master - Mirror Image | AOmi |  |
| `TODO` | `AOre` | Hero | Tauren Chieftain - Reincarnation | AOre, ANrn |  |
| `Partial` | `AOsf` | Hero | Farseer - Spirit Wolf | AOsf, ACsf, ACs9 | No-target multi-summon with timed life. |
| `TODO` | `AOsh` | Hero | Tauren Chieftain - Shock Wave | AOsh, ACsh, ACst |  |
| `TODO` | `AOwk` | Hero | Blade Master - Wind Walk | AOwk |  |
| `TODO` | `AOws` | Hero | Tauren Chieftain - War Stomp | AOws, Awrs |  |
| `TODO` | `AOww` | Hero | Blade Master - Bladestorm | AOww |  |
| `TODO` | `AUan` | Hero | Death Knight - Animate Dead | AUan |  |
| `TODO` | `AUau` | Hero, Item | Death Knight - Unholy Aura | AUau, ACua, AIau |  |
| `TODO` | `AUav` | Hero, Item | Dreadlord - Vampiric Aura | AUav, ACvp, AIav |  |
| `Partial` | `AUcs` | Hero | Dreadlord - Carrion Swarm | AUcs, ACca | Simple point-area damage; line missile/art incomplete. |
| `TODO` | `AUdc` | Hero | Death Knight - Death Coil | AUdc, ACdc |  |
| `TODO` | `AUdd` | Hero | Lich - Death and Decay | AUdd, SNdd |  |
| `TODO` | `AUdp` | Hero | Death Knight - Death Pact | AUdp |  |
| `TODO` | `AUdr` | Hero | Lich - Dark Ritual | AUdr |  |
| `TODO` | `AUds` | Hero | Tichondrius - Dark Summoning | AUds |  |
| `TODO` | `AUfa` | Hero | Lich - Frost Armor | AUfa, ACfa |  |
| `TODO` | `AUfn` | Hero | Lich - Frost Nova | AUfn, ACfn |  |
| `TODO` | `AUin` | Hero, Item | Dreadlord - Inferno | AUin, SNin, AIin |  |
| `TODO` | `AUsl` | Hero | Dreadlord - Sleep | AUsl, ACsl |  |
| `TODO` | `Aadm` | Unit/Common | Abolish Magic | Aadm, ACdm |  |
| `Stub` | `Aaha` | Unit/Common | Acolyte Harvest | Aaha | Registered harvest variant stub. |
| `TODO` | `Aakb` | Unit/Common | Aura - War Drums (Kodo beast) | Aakb |  |
| `TODO` | `Aalr` | Unit/Common | Alarm | Aalr |  |
| `TODO` | `Aami` | Item | Anti-magic Shield | AIxs |  |
| `TODO` | `Aams` | Unit/Common | Anti-magic Shield | Aams, ACam |  |
| `TODO` | `Aapl` | Unit/Common | Aura - Plague (Abomination) | Aap1, Aap2, Aap3, Aap4 |  |
| `TODO` | `Aarm` | Unit/Common | Neutral Regen (mana only) | ANre |  |
| `Stub` | `Abgm` | Unit/Common | Blighted Gold mine | Abgm | Registered mine variant stub. |
| `Stub` | `Abli` | Unit/Common | Blight Dispel (Small) | Abds, Abdl, Abgs, Abgl | Registered blight stub. |
| `TODO` | `Ablo` | Unit/Common | Bloodlust | Ablo, ACbl |  |
| `TODO` | `Abrf` | Unit/Common | Bearform | Abrf |  |
| `TODO` | `Abtl` | Unit/Common | Battlestations | Abtl, Sbtl |  |
| `Stub` | `Abun` | Unit/Common | Cargo Hold (Burrow) | Abun | Registered cargo stub. |
| `TODO` | `Acan` | Unit/Common | Cannibalize | Acan, ACcn |  |
| `Stub` | `Acar` | Unit/Common | Cargo Hold (Tank) | Sch4, Sch3 | Registered cargo stub. |
| `TODO` | `Acha` | Unit/Common | Chaos (Grunt) | Sca1, Sca2, Sca3, Sca4, Sca5, Sca6 |  |
| `TODO` | `Achd` | Unit/Common | Cargo Hold Death | Achd |  |
| `TODO` | `Achl` | Unit/Common | Chaos Cargo Load | Achl |  |
| `TODO` | `Acoa` | Unit/Common | Couple (Archer) | Acoa |  |
| `TODO` | `Acoh` | Unit/Common | Couple (Hippogryph) | Acoh |  |
| `TODO` | `Acor` | Unit/Common | Corrosive Breath | Acor |  |
| `TODO` | `Acri` | Unit/Common | Cripple | Acri, Scri, ACcr |  |
| `TODO` | `Acrs` | Unit/Common | Curse | Acrs, ACcs |  |
| `TODO` | `Acyc` | Item | Cyclone | Acyc, ACcy, SCc1, AIcy |  |
| `TODO` | `Adda` | Unit/Common | Death Damage AOE (sapper) | Adda, Amnx, Amnz |  |
| `TODO` | `Adef` | Unit/Common | Defend | Adef |  |
| `TODO` | `Adet` | Unit/Common | Detect (Sentry Ward) | Adt1 |  |
| `TODO` | `Adev` | Unit/Common | Devour | Adev |  |
| `TODO` | `Adis` | Unit/Common | Dispel Magic | Adis, Adsm |  |
| `Stub` | `Adri` | Unit/Common | Drop Instant | Adri | Registered cargo/drop stub. |
| `Stub` | `Adro` | Unit/Common | Drop | Adro | Registered cargo/drop stub. |
| `TODO` | `Adtn` | Unit/Common | Detonate | Adtn |  |
| `TODO` | `Adts` | Unit/Common | Detect (Magic Sentinel) | Adts |  |
| `TODO` | `Advc` | Unit/Common | Cargo Hold (Devour) | Advc |  |
| `Partial` | `Aeat` | Unit/Common | Eat Tree | Aeat | Tree target, self-heal, and tree removal exist. |
| `Stub` | `Aegm` | Unit/Common | Entangled Gold Mine | Aegm | Registered entangled mine stub. |
| `Stub` | `Aenc` | Unit/Common | Cargo Hold (Entangled Gold Mine) | Aenc | Registered entangled mine cargo stub. |
| `TODO` | `Aens` | Unit/Common | Ensnare | Aens, ACen |  |
| `Stub` | `Aent` | Unit/Common | Entangle | Aent | Registered entangle command stub. |
| `TODO` | `Aesn` | Unit/Common | Sentinel | Aesn |  |
| `TODO` | `Aeye` | Item | Sentry Ward | Aeye, AIsw |  |
| `TODO` | `Afae` | Unit/Common | Faerie Fire | Afae, ACff |  |
| `TODO` | `Afla` | Unit/Common | Flare | Afla |  |
| `TODO` | `Afrb` | Unit/Common | Frost Breath | Afrb |  |
| `TODO` | `Afrz` | Unit/Common | Freezing Breath | Afrz |  |
| `TODO` | `Agho` | Unit/Common | Ghost | Agho |  |
| `TODO` | `Agyb` | Unit/Common | Gyrocopter Bombs | Agyb |  |
| `TODO` | `Agyd` | Unit/Common | Graveyard | Agyd |  |
| `TODO` | `Agyv` | Unit/Common | Detect (Gyrocopter) | Agyv |  |
| `TODO` | `Ahea` | Unit/Common | Heal | Ahea |  |
| `Stub` | `Ahrl` | Unit/Common | Harvest Lumber | Ahrl, Ahr3, Ahr2 | Registered harvest variant stub; base local harvest remains under `Ahar`. |
| `TODO` | `Ahwd` | Item | Healing Ward | Ahwd, AChw, AIhw |  |
| `TODO` | `Aimp` | Unit/Common | Impaling Bolt | Aimp |  |
| `TODO` | `Ainf` | Unit/Common | Inner Fire | Ainf, ACif |  |
| `TODO` | `Aivs` | Unit/Common | Invisibility | Aivs |  |
| `TODO` | `Alam` | Unit/Common | Sacrifice (Acolyte) | Alam |  |
| `TODO` | `Alit` | Unit/Common | Lightning Attack | Alit |  |
| `Stub` | `Aloa` | Unit/Common | Load | Aloa, Sloa, Slo2 | Registered cargo/load stub. |
| `TODO` | `Alsh` | Item | Lightning Shield | Alsh, ACls, AIls |  |
| `Partial` | `Ambt` | Unit/Common | Mana Battery | Ambt | Friendly life transfer from well mana exists. |
| `TODO` | `Amed` | Unit/Common | Meat Drop | Amed |  |
| `TODO` | `Amel` | Unit/Common | Meat Load | Amel |  |
| `TODO` | `Amgl` | Unit/Common | Moon Glaive | Amgl |  |
| `TODO` | `Amic` | Unit/Common | Militia Conversion | Amic |  |
| `TODO` | `Amim` | Item | Magic Immunity | Amim, ACmi, ACm2, ACm3, AImx |  |
| `TODO` | `Amin` | Unit/Common | Mine | Amin |  |
| `TODO` | `Amtc` | Unit/Common | Cargo Hold (Meat Wagon) | Sch2 |  |
| `TODO` | `Andt` | Unit/Common | Neutral Detection (Reveal ability) | Andt |  |
| `Stub` | `Aneu` | Unit/Common | Neutral Building | Aneu | Registered neutral building stub. |
| `TODO` | `Anhe` | Unit/Common | Heal (Creep Normal) | Anh1, Anh2 |  |
| `TODO` | `Ansp` | Unit/Common | Neutral Spies | Ansp |  |
| `TODO` | `Aoar` | Unit/Common | Aura - Regeneration (Healing Ward) | Aoar, ACnr |  |
| `TODO` | `Apiv` | Unit/Common | Permanent Invisibility | Apiv |  |
| `TODO` | `Aply` | Unit/Common | Polymorph | Aply, ACpy |  |
| `TODO` | `Apoi` | Unit/Common | Poison Attack | Apoi |  |
| `TODO` | `Apos` | Unit/Common | Possession | Apos, ACps |  |
| `TODO` | `Aprg` | Unit/Common | Purge | Aprg, ACpu |  |
| `TODO` | `Apts` | Unit/Common | Plague Toss | Apts |  |
| `TODO` | `Arai` | Unit/Common | Raise Dead | Arai, ACrd |  |
| `TODO` | `Arav` | Unit/Common | Raven Form (Druid of the Talon) | Arav, Amrf |  |
| `TODO` | `Arej` | Unit/Common | Rejuvination | Arej, ACrj, ACr2 |  |
| `TODO` | `Arel` | Item | Regen Life | Arel, Arll |  |
| `Stub` | `Aren` | Unit/Common | Renew | Aren | Registered repair variant stub. |
| `TODO` | `Arev` | Unit/Common | Revive | Arev |  |
| `TODO` | `Arng` | Unit/Common | Revenge | Arng |  |
| `TODO` | `Aroa` | Item | Roar | Aroa, ACro, AIrr |  |
| `Partial` | `Aroo` | Unit/Common | Root (Ancients) | Aro1, Aro2 | Basic rooted movement toggle exists. |
| `Stub` | `Arst` | Unit/Common | Restoration | Arst | Registered repair variant stub. |
| `Stub` | `Artn` | Unit/Common | Return (Gold) | Argd, Argl, Arlm | Registered return resources stub. |
| `TODO` | `Asac` | Unit/Common | Sacrifice (Sacrificial Pit) | Asac |  |
| `TODO` | `Asal` | Unit/Common | Pillage | Asal |  |
| `TODO` | `Asds` | Unit/Common | Self Destruct | Asds |  |
| `TODO` | `Ashm` | Unit/Common | Shadow Meld | Ashm, Sshm |  |
| `TODO` | `Asla` | Unit/Common | Sleep Always | Asla |  |
| `TODO` | `Aslo` | Unit/Common | Slow | Aslo, ACsw |  |
| `TODO` | `Asod` | Unit/Common | Spawn On Death (skeleton) | Asod |  |
| `TODO` | `Asou` | Item | SoulPossession | Asou |  |
| `TODO` | `Aspa` | Unit/Common | Spider Attack | Aspa |  |
| `TODO` | `Aspd` | Unit/Common | Spawn Spider On Death | Aspd |  |
| `TODO` | `Aspi` | Unit/Common | Spiked Barricades | Aspi |  |
| `TODO` | `Aspo` | Unit/Common | Slow Poison | Aspo |  |
| `TODO` | `Assp` | Unit/Common | Spawn Spiderling On Death | Assp |  |
| `TODO` | `Asta` | Unit/Common | Stasis Trap | Asta |  |
| `Stub` | `Astd` | Unit/Common | Stand Down (Burrow Return to Work) | Astd | Registered burrow stand-down stub. |
| `TODO` | `Asth` | Unit/Common | Storm Hammers | Asth |  |
| `TODO` | `Astn` | Unit/Common | Stone Form | Astn |  |
| `TODO` | `Atdp` | Unit/Common | Drop Pilot | Atdp |  |
| `TODO` | `Atlp` | Unit/Common | Load Pilot | Atlp |  |
| `TODO` | `Atol` | Unit/Common | Tree of life (for attaching art) | Atol |  |
| `TODO` | `Atpi` | Unit/Common | Pilot Tank (Mortar Team) | Stpm, Stpr |  |
| `TODO` | `Atru` | Unit/Common | Detect (Shade) | Atru, Adtg |  |
| `TODO` | `Attu` | Unit/Common | Tank Turret | Attu |  |
| `TODO` | `Auhf` | Unit/Common | Unholy Frenzy | Auhf, Suhf, ACuf |  |
| `TODO` | `Ault` | Item | Ultravision | Ault, AIuv |  |
| `TODO` | `Auns` | Unit/Common | Unsummon | Auns |  |
| `TODO` | `Aven` | Unit/Common | Venom Spears | Aven, ACvs |  |
| `Partial` | `Avul` | Unit/Common | Invulnerable | Avul | Units with this ability spawn damage-immune. |
| `TODO` | `Awan` | Unit/Common | Wander | Awan |  |
| `TODO` | `Awar` | Unit/Common | Pulverize | Awar, ACpv |  |
| `TODO` | `Aweb` | Unit/Common | Web | Aweb, ACwb |  |
| `Stub` | `Awha` | Unit/Common | Wisp Harvest | Awha, Awh2 | Registered harvest variant stub. |
| `TODO` | `Awrp` | Unit/Common | Warp | Awrp |  |
