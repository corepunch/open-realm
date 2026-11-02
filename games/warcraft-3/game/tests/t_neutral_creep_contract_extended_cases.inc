/* Generated from neutral-creep-ability-test-matrix.csv: source rows and scenarios.
 * Representative ability code is an executable fixture baseline, NOT verified retail
 * unit ownership unless the evidence column explicitly confirms the alias. */
static creepExtendedCase_t const extendedCases[] = {
    { 5, "Dark Troll Warlord", "Trueshot Aura", "Increases the damage of nearby friendly ranged attacks by 10%.", "https://classic.battle.net/war3/neutral/darktrolls.shtml", "AEar", "ACat", CE_AURA_TRUESHOT },
    { 9, "Dark Troll High Priest", "Sleep", "Sends an enemy unit to sleep until it is attacked or the effect ends.", "https://classic.battle.net/war3/neutral/darktrolls.shtml", "AUsl", "ACsl", CE_UNIT_SLEEP },
    { 14, "Furbolg Elder Shaman", "Lightning Shield", "Shrouds a unit in electricity that harms nearby units for 20 damage per second.", "https://classic.battle.net/war3/neutral/furbolgs.shtml", "Alsh", NULL, CE_UNIT_DOT_AOE },
    { 16, "Furbolg Ursa Warrior", "Command Aura", "Nearby friendly units deal 10% more attack damage.", "https://classic.battle.net/war3/neutral/furbolgs.shtml", "AOac", NULL, CE_AURA_COMMAND },
    { 17, "Furbolg Ursa Warrior", "War Stomp", "Damages nearby enemies for 25 and stuns them for roughly 3 seconds.", "https://classic.battle.net/war3/neutral/furbolgs.shtml", "AOws", NULL, CE_SELF_DAMAGE_STUN },
    { 20, "Green Dragon", "Devour", "Swallows a nonhero enemy; the victim takes 5 damage per second and escapes if the devourer dies first.", "https://classic.battle.net/war3/neutral/dragons.shtml", "Adev", NULL, CE_UNIT_DEVOUR },
    { 22, "Lightning Lizard", "Purge", "Removes magical buffs, greatly slows the target briefly, and damages summoned units.", "https://classic.battle.net/war3/neutral/lightninglizards.shtml", "Aprg", NULL, CE_UNIT_PURGE },
    { 23, "Thunder Lizard", "Slam", "Strikes nearby enemy land units for 70 damage and briefly stuns them.", "https://classic.battle.net/war3/neutral/lightninglizards.shtml", "ACtc", "ACt2", CE_SELF_DAMAGE_STUN },
    { 24, "Storm Wyrm", "Devour", "Swallows a nonhero enemy; the victim takes 5 damage per second and escapes if the devourer dies first.", "https://classic.battle.net/war3/neutral/lightninglizards.shtml", "Adev", "ACdv", CE_UNIT_DEVOUR },
    { 25, "Storm Wyrm", "Lightning Shield", "Shrouds a unit in electricity that harms nearby units for 20 damage per second.", "https://classic.battle.net/war3/neutral/lightninglizards.shtml", "Alsh", NULL, CE_UNIT_DOT_AOE },
    { 26, "Storm Wyrm", "Chain Lightning", "Strikes an enemy and jumps to other targets, dealing progressively less lightning damage.", "https://classic.battle.net/war3/neutral/lightninglizards.shtml", "AOcl", NULL, CE_UNIT_CHAIN },
    { 29, "Murloc Nightcrawler", "Shadow Meld", "Makes a stationary, nonattacking unit invisible at night.", "https://classic.battle.net/war3/neutral/murloc.shtml", "Ashm", NULL, CE_SHADOW_MELD },
    { 30, "Murloc Nightcrawler", "Hide", "Orders the unit to hold position and refrain from attacking, preserving its nighttime invisibility.", "https://classic.battle.net/war3/neutral/murloc.shtml", "Ahid", NULL, CE_HIDE },
    { 31, "Murloc Nightcrawler", "Envenomed Weapon", "Poison from attacks deals approximately 2 damage per second for 20 seconds.", "https://classic.battle.net/war3/neutral/murloc.shtml", "Aven", NULL, CE_HIT_POISON },
    { 33, "Satyr Soulstealer", "Mana Burn", "Removes up to 100 mana from a target and damages it in proportion to the mana drained.", "https://classic.battle.net/war3/neutral/satyrs.shtml", "AEmb", NULL, CE_UNIT_MANA_BURN },
    { 34, "Satyr Soulstealer", "Raise Dead (Autocast)", "Creates two Skeleton Warriors from a suitable corpse.", "https://classic.battle.net/war3/neutral/satyrs.shtml", "Arai", NULL, CE_SUMMON_CORPSE },
    { 36, "Satyr Hellcaller", "Unholy Aura", "Increases nearby allied movement speed and hit-point regeneration.", "https://classic.battle.net/war3/neutral/satyrs.shtml", "AUau", NULL, CE_AURA_UNHOLY },
    { 37, "Satyr Hellcaller", "Animate Dead", "Temporarily reanimates up to six dead units to fight.", "https://classic.battle.net/war3/neutral/satyrs.shtml", "AUan", NULL, CE_SUMMON_CORPSE },
    { 38, "Satyr Trickster", "Purge", "Removes magical buffs, greatly slows the target briefly, and damages summoned units.", "https://classic.battle.net/war3/neutral/satyrs.shtml", "Aprg", NULL, CE_UNIT_PURGE },
    { 39, "Satyr Shadowdancer", "Shadow Meld", "Makes a stationary, nonattacking unit invisible at night.", "https://classic.battle.net/war3/neutral/satyrs.shtml", "Ashm", NULL, CE_SHADOW_MELD },
    { 40, "Satyr Shadowdancer", "Hide", "Orders the unit to hold position and refrain from attacking, preserving its nighttime invisibility.", "https://classic.battle.net/war3/neutral/satyrs.shtml", "Ahid", NULL, CE_HIDE },
    { 44, "Giant Spider", "Envenomed Weapon", "Poison from attacks deals approximately 2 damage per second for 20 seconds.", "https://classic.battle.net/war3/neutral/spiders.shtml", "Aven", NULL, CE_HIT_POISON },
    { 45, "Spitting Spider", "Envenomed Weapon", "Poison from attacks deals approximately 2 damage per second for 20 seconds.", "https://classic.battle.net/war3/neutral/spiders.shtml", "Aven", NULL, CE_HIT_POISON },
    { 47, "Brood Mother", "Envenomed Weapon", "Poison from attacks deals approximately 2 damage per second for 20 seconds.", "https://classic.battle.net/war3/neutral/spiders.shtml", "Aven", NULL, CE_HIT_POISON },
    { 49, "Giant Wolf", "Critical Strike (Passive)", "Provides a chance for an attack to inflict twice its normal damage.", "https://classic.battle.net/war3/neutral/wolves.shtml", "AOcr", NULL, CE_HIT_CRITICAL },
    { 51, "Dire Wolf", "Critical Strike (Passive)", "Provides a chance for an attack to inflict twice its normal damage.", "https://classic.battle.net/war3/neutral/wolves.shtml", "AOcr", NULL, CE_HIT_CRITICAL },
    { 52, "Bronze Dragon Whelp", "Lightning Attack", "A bronze dragon\u2019s regular attack uses lightning.", "https://classic.battle.net/war3/neutral/dragons.shtml", "Alit", NULL, CE_HIT_LIGHTNING },
    { 53, "Bronze Drake", "Lightning Attack", "A bronze dragon\u2019s regular attack uses lightning.", "https://classic.battle.net/war3/neutral/dragons.shtml", "Alit", NULL, CE_HIT_LIGHTNING },
    { 54, "Bronze Dragon", "Lightning Attack", "A bronze dragon\u2019s regular attack uses lightning.", "https://classic.battle.net/war3/neutral/dragons.shtml", "Alit", NULL, CE_HIT_LIGHTNING },
    { 55, "Bronze Dragon", "Devour", "Swallows a nonhero enemy; the victim takes 5 damage per second and escapes if the devourer dies first.", "https://classic.battle.net/war3/neutral/dragons.shtml", "Adev", NULL, CE_UNIT_DEVOUR },
    { 60, "Centaur Impaler", "Searing Arrows (Autocast)", "Adds bonus fire damage to ranged attacks at the cost of mana.", "https://classic.battle.net/war3/neutral/centaurs.shtml", "AHfa", NULL, CE_HIT_SEARING },
    { 63, "Centaur Khan", "War Stomp", "Damages nearby enemies for 25 and stuns them for roughly 3 seconds.", "https://classic.battle.net/war3/neutral/centaurs.shtml", "AOws", NULL, CE_SELF_DAMAGE_STUN },
    { 64, "Centaur Khan", "Endurance Aura", "Passively improves allied movement speed and attack rate nearby.", "https://classic.battle.net/war3/neutral/centaurs.shtml", "AOae", NULL, CE_AURA_ENDURANCE },
    { 65, "Centaur Khan", "Reincarnation (Passive)", "Automatically returns this unit to life after it is killed, if the ability is ready.", "https://classic.battle.net/war3/neutral/centaurs.shtml", "AOre", NULL, CE_PASSIVE_REINCARNATION },
    { 69, "Harpy Stormhag", "Sleep", "Sends an enemy unit to sleep until it is attacked or the effect ends.", "https://classic.battle.net/war3/neutral/harpies.shtml", "AUsl", NULL, CE_UNIT_SLEEP },
    { 71, "Harpy Queen", "Cyclone", "Lifts the target into the air, making it unable to act while suspended.", "https://classic.battle.net/war3/neutral/harpies.shtml", "Acyc", NULL, CE_UNIT_CYCLONE },
    { 74, "Berserk Wildkin", "War Stomp", "Damages nearby enemies for 25 and stuns them for roughly 3 seconds.", "https://classic.battle.net/war3/neutral/wildkin.shtml", "AOws", NULL, CE_SELF_DAMAGE_STUN },
    { 80, "Razormane Medicine Man", "Healing Ward", "Places a ward that heals nearby friendly units by 2% of maximum health per second.", "https://classic.battle.net/war3/neutral/razormanes.shtml", "Ahwd", NULL, CE_POINT_WARD },
    { 81, "Razormane Medicine Man", "Feral Spirit", "Summons two temporary Spirit Pigs to fight.", "https://classic.battle.net/war3/neutral/razormanes.shtml", "AOsf", NULL, CE_SUMMON_NO_TARGET },
    { 82, "Razormane Chieftain", "Hurl Boulder", "Deals 100 damage to an enemy with a magical boulder and stuns it for 2 seconds.", "https://classic.battle.net/war3/neutral/razormanes.shtml", "ACtb", NULL, CE_UNIT_STUN_DAMAGE },
    { 83, "Razormane Chieftain", "Thorns Aura", "Reflects a portion of incoming melee damage back to nearby friendly units\u2019 attackers.", "https://classic.battle.net/war3/neutral/razormanes.shtml", "AEah", NULL, CE_AURA_THORNS },
    { 85, "Salamander", "Firebolt", "Hits a target with fire for 100 damage and a 2-second stun.", "https://classic.battle.net/war3/neutral/salamanders.shtml", "ANfb", NULL, CE_UNIT_STUN_DAMAGE },
    { 86, "Salamander", "Immolation", "Burns nearby enemies for about 10 damage per second while consuming mana.", "https://classic.battle.net/war3/neutral/salamanders.shtml", "AEim", NULL, CE_SELF_IMMOLATION },
    { 88, "Bandit", "Shadow Meld", "Makes a stationary, nonattacking unit invisible at night.", "https://classic.battle.net/war3/neutral/bandits.shtml", "Ashm", NULL, CE_SHADOW_MELD },
    { 89, "Bandit", "Hide", "Orders the unit to hold position and refrain from attacking, preserving its nighttime invisibility.", "https://classic.battle.net/war3/neutral/bandits.shtml", "Ahid", NULL, CE_HIDE },
    { 90, "Rogue", "Shadow Meld", "Makes a stationary, nonattacking unit invisible at night.", "https://classic.battle.net/war3/neutral/bandits.shtml", "Ashm", NULL, CE_SHADOW_MELD },
    { 91, "Rogue", "Hide", "Orders the unit to hold position and refrain from attacking, preserving its nighttime invisibility.", "https://classic.battle.net/war3/neutral/bandits.shtml", "Ahid", NULL, CE_HIDE },
    { 92, "Enforcer", "Shadow Meld", "Makes a stationary, nonattacking unit invisible at night.", "https://classic.battle.net/war3/neutral/bandits.shtml", "Ashm", NULL, CE_SHADOW_MELD },
    { 93, "Enforcer", "Hide", "Orders the unit to hold position and refrain from attacking, preserving its nighttime invisibility.", "https://classic.battle.net/war3/neutral/bandits.shtml", "Ahid", NULL, CE_HIDE },
    { 95, "Brigand", "Shadow Meld", "Makes a stationary, nonattacking unit invisible at night.", "https://classic.battle.net/war3/neutral/bandits.shtml", "Ashm", NULL, CE_SHADOW_MELD },
    { 96, "Brigand", "Hide", "Orders the unit to hold position and refrain from attacking, preserving its nighttime invisibility.", "https://classic.battle.net/war3/neutral/bandits.shtml", "Ahid", NULL, CE_HIDE },
    { 97, "Assassin", "Shadow Meld", "Makes a stationary, nonattacking unit invisible at night.", "https://classic.battle.net/war3/neutral/bandits.shtml", "Ashm", NULL, CE_SHADOW_MELD },
    { 98, "Assassin", "Hide", "Orders the unit to hold position and refrain from attacking, preserving its nighttime invisibility.", "https://classic.battle.net/war3/neutral/bandits.shtml", "Ahid", NULL, CE_HIDE },
    { 99, "Assassin", "Envenomed Weapon", "Poison from attacks deals approximately 2 damage per second for 20 seconds.", "https://classic.battle.net/war3/neutral/bandits.shtml", "Aven", NULL, CE_HIT_POISON },
    { 100, "Bandit Lord", "Shadow Meld", "Makes a stationary, nonattacking unit invisible at night.", "https://classic.battle.net/war3/neutral/bandits.shtml", "Ashm", NULL, CE_SHADOW_MELD },
    { 101, "Bandit Lord", "Hide", "Orders the unit to hold position and refrain from attacking, preserving its nighttime invisibility.", "https://classic.battle.net/war3/neutral/bandits.shtml", "Ahid", NULL, CE_HIDE },
    { 102, "Bandit Lord", "Divine Shield", "Makes the unit invulnerable to damage and spells for 10 seconds.", "https://classic.battle.net/war3/neutral/bandits.shtml", "AHds", NULL, CE_SELF_INVULNERABLE },
    { 103, "Bandit Lord", "Devotion Aura", "Grants nearby allied units 3 additional armor.", "https://classic.battle.net/war3/neutral/bandits.shtml", "AHad", NULL, CE_AURA_DEVOTION },
    { 104, "Burning Archer", "Searing Arrows (Autocast)", "Adds bonus fire damage to ranged attacks at the cost of mana.", "https://classic.battle.net/war3/neutral/undead.shtml", "AHfa", NULL, CE_HIT_SEARING },
    { 107, "Heretic", "Raise Dead (Autocast)", "Creates two Skeleton Warriors from a suitable corpse.", "https://classic.battle.net/war3/neutral/heretics.shtml", "Arai", NULL, CE_SUMMON_CORPSE },
    { 108, "Heretic", "Carrion Swarm", "Sends a wave of dark energy along a line, damaging units in its path.", "https://classic.battle.net/war3/neutral/heretics.shtml", "AUcs", NULL, CE_POINT_DAMAGE },
    { 112, "Forest Troll Warlord", "Trueshot Aura", "Increases the damage of nearby friendly ranged attacks by 5%.", "https://classic.battle.net/war3/neutral/foresttrolls.shtml", "AEar", NULL, CE_AURA_TRUESHOT },
    { 117, "Forest Troll High Priest", "Inner Fire (Autocast)", "Raises a friendly unit\u2019s damage by 10% and armor by 5.", "https://classic.battle.net/war3/neutral/foresttrolls.shtml", "Ainf", NULL, CE_UNIT_FRIEND_BUFF },
    { 120, "Kobold Taskmaster", "Command Aura", "Nearby friendly units deal 10% more attack damage.", "https://classic.battle.net/war3/neutral/kobolds.shtml", "AOac", NULL, CE_AURA_COMMAND },
    { 126, "Rock Golem", "Hurl Boulder", "Deals 100 damage to an enemy with a magical boulder and stuns it for 2 seconds.", "https://classic.battle.net/war3/neutral/golems.shtml", "ACtb", NULL, CE_UNIT_STUN_DAMAGE },
    { 128, "Granite Golem", "Hurl Boulder", "Deals 100 damage to an enemy with a magical boulder and stuns it for 2 seconds.", "https://classic.battle.net/war3/neutral/golems.shtml", "ACtb", NULL, CE_UNIT_STUN_DAMAGE },
    { 129, "Granite Golem", "Slam", "Deals 70 area damage to nearby enemy units.", "https://classic.battle.net/war3/neutral/golems.shtml", "ACtc", NULL, CE_SELF_DAMAGE_STUN },
    { 135, "Rogue Wizard", "Frost Armor", "Gives an ally 3 extra armor; melee attackers are temporarily slowed.", "https://classic.battle.net/war3/neutral/wizards.shtml", "AUfa", NULL, CE_UNIT_FRIEND_ARMOR },
    { 136, "Renegade Wizard", "Purge", "Removes magical buffs, greatly slows the target briefly, and damages summoned units.", "https://classic.battle.net/war3/neutral/wizards.shtml", "Aprg", NULL, CE_UNIT_PURGE },
    { 137, "Renegade Wizard", "Lightning Shield", "Shrouds a unit in electricity that harms nearby units for 20 damage per second.", "https://classic.battle.net/war3/neutral/wizards.shtml", "Alsh", NULL, CE_UNIT_DOT_AOE },
    { 138, "Dark Wizard", "Raise Dead (Autocast)", "Creates two Skeleton Warriors from a suitable corpse.", "https://classic.battle.net/war3/neutral/wizards.shtml", "Arai", NULL, CE_SUMMON_CORPSE },
    { 139, "Dark Wizard", "Brilliance Aura", "Regenerates an additional 1 mana per second for nearby friendly units.", "https://classic.battle.net/war3/neutral/wizards.shtml", "AHab", NULL, CE_AURA_BRILLIANCE },
    { 140, "Dark Wizard", "Polymorph", "Changes a nonhero target into a sheep unable to attack; summoned targets are destroyed.", "https://classic.battle.net/war3/neutral/wizards.shtml", "Aply", NULL, CE_UNIT_POLYMORPH },
    { 142, "Berserk Elemental", "Frost Nova", "Strikes a target for 50 cold damage and deals 50 more to surrounding enemies, slowing them.", "https://classic.battle.net/war3/neutral/elementals.shtml", "AUfn", NULL, CE_UNIT_FROST_NOVA },
    { 146, "Poison Treant", "Entangling Roots", "Roots a victim in place and inflicts damage over time.", "https://classic.battle.net/war3/neutral/corruptedents.shtml", "AEer", NULL, CE_UNIT_ROOT },
    { 147, "Poison Treant", "Envenomed Weapon", "Poison from attacks deals approximately 2 damage per second for 20 seconds.", "https://classic.battle.net/war3/neutral/corruptedents.shtml", "Aven", NULL, CE_HIT_POISON },
    { 148, "Plague Treant", "Entangling Roots", "Roots a victim in place and inflicts damage over time.", "https://classic.battle.net/war3/neutral/corruptedents.shtml", "AEer", NULL, CE_UNIT_ROOT },
    { 149, "Plague Treant", "Disease Cloud", "Infects nearby enemies with damage over time; the disease does not deliver a lethal final blow.", "https://classic.battle.net/war3/neutral/corruptedents.shtml", "Aap3", NULL, CE_PASSIVE_DISEASE },
    { 150, "Doom Guard", "Dispel Magic", "Dispels magical effects over an area and damages summoned units.", "https://classic.battle.net/war3/neutral/demons.shtml", "Adis", NULL, CE_POINT_DISPEL },
    { 151, "Doom Guard", "War Stomp", "Damages nearby enemies for 25 and stuns them for roughly 3 seconds.", "https://classic.battle.net/war3/neutral/demons.shtml", "AOws", NULL, CE_SELF_DAMAGE_STUN },
    { 152, "Doom Guard", "Cripple", "Cuts enemy movement speed by 75%, attack speed by 50%, and attack damage by 50%.", "https://classic.battle.net/war3/neutral/demons.shtml", "Acri", NULL, CE_UNIT_CRIPPLE },
    { 153, "Doom Guard", "Rain of Fire", "Calls down six waves of fire over an area; each wave deals 25 damage.", "https://classic.battle.net/war3/neutral/demons.shtml", "ANrf", NULL, CE_POINT_DAMAGE },
    { 155, "Fel Stalker", "Mana Burn", "Removes up to 100 mana from a target and damages it in proportion to the mana drained.", "https://classic.battle.net/war3/neutral/feldemons.shtml", "AEmb", NULL, CE_UNIT_MANA_BURN },
    { 156, "Fel Ravager", "Black Arrow", "Adds damage to attacks and raises a skeleton from a unit killed by the effect.", "https://classic.battle.net/war3/neutral/feldemons.shtml", "ANba", NULL, CE_HIT_BLACK_ARROW },
    { 157, "Fel Ravager", "Devour Magic", "Consumes magical effects to replenish the caster\u2019s health and mana, damaging summons.", "https://classic.battle.net/war3/neutral/feldemons.shtml", "Advm", NULL, CE_POINT_DISPEL_DRAIN },
    { 158, "Infernal", "Permanent Immolation", "Continually damages nearby units for around 10 damage per second without toggling.", "https://classic.battle.net/war3/undead/units/infernal.shtml", "ANpi", NULL, CE_SELF_IMMOLATION },
    { 160, "Infernal", "Resistant Skin", "Reduces the duration or effectiveness of hostile spells on the unit.", "https://classic.battle.net/war3/undead/units/infernal.shtml", "Arsk", NULL, CE_PASSIVE_RESISTANT },
    { 161, "Murloc Plaguebearer", "Disease Cloud", "Infects nearby enemies with damage over time; the disease does not deliver a lethal final blow.", "https://classic.battle.net/war3/neutral/murloc.shtml", "Aap1", NULL, CE_PASSIVE_DISEASE },
    { 162, "Murloc Flesheater", "Cannibalize", "Consumes a corpse to regenerate approximately 10 hit points each second.", "https://classic.battle.net/war3/neutral/murloc.shtml", "Acan", NULL, CE_SUMMON_CORPSE_HEAL },
    { 163, "Murloc Mutant", "Cripple", "Cuts enemy movement speed by 75%, attack speed by 50%, and attack damage by 50%.", "https://classic.battle.net/war3/neutral/murloc.shtml", "Acri", NULL, CE_UNIT_CRIPPLE },
    { 170, "Arachnathid Earth-borer", "Envenomed Weapon", "Poison from attacks deals approximately 2 damage per second for 20 seconds.", "https://classic.battle.net/war3/neutral/arachnathid.shtml", "Aven", NULL, CE_HIT_POISON },
    { 171, "Arachnathid Earth-borer", "Shadow Strike", "Deals initial and continuing poison damage while slowing the target.", "https://classic.battle.net/war3/neutral/arachnathid.shtml", "AEsh", NULL, CE_UNIT_DOT },
    { 172, "Arachnathid Overlord", "Command Aura", "Nearby friendly units deal 10% more attack damage.", "https://classic.battle.net/war3/neutral/arachnathid.shtml", "AOac", NULL, CE_AURA_COMMAND },
    { 173, "Blue Dragon Whelp", "Frost Breath", "A dragon\u2019s frosty attacks slow the struck unit for about 10 seconds.", "https://classic.battle.net/war3/neutral/dragons.shtml", "Afrb", NULL, CE_UNIT_FROST_NOVA },
    { 174, "Blue Drake", "Frost Breath", "A dragon\u2019s frosty attacks slow the struck unit for about 10 seconds.", "https://classic.battle.net/war3/neutral/dragons.shtml", "Afrb", NULL, CE_UNIT_FROST_NOVA },
    { 175, "Blue Dragon", "Frost Breath", "A dragon\u2019s frosty attacks slow the struck unit for about 10 seconds.", "https://classic.battle.net/war3/neutral/dragons.shtml", "Afrb", NULL, CE_UNIT_FROST_NOVA },
    { 176, "Blue Dragon", "Devour", "Swallows a nonhero enemy; the victim takes 5 damage per second and escapes if the devourer dies first.", "https://classic.battle.net/war3/neutral/dragons.shtml", "Adev", NULL, CE_UNIT_DEVOUR },
    { 182, "Blue Dragonspawn Sorcerer", "Frost Nova", "Strikes a target for 50 cold damage and deals 50 more to surrounding enemies, slowing them.", "https://classic.battle.net/war3/neutral/bluedragonspawn.shtml", "AUfn", NULL, CE_UNIT_FROST_NOVA },
    { 184, "Blue Dragonspawn Overseer", "Breath of Frost", "Inflicts frost damage, with additional damage over time to the affected enemy.", "https://classic.battle.net/war3/neutral/bluedragonspawn.shtml", "ACbf", NULL, CE_UNIT_FROST_NOVA },
    { 186, "Blue Dragonspawn Overseer", "Devotion Aura", "Grants nearby allied units 3 additional armor.", "https://classic.battle.net/war3/neutral/bluedragonspawn.shtml", "AHad", NULL, CE_AURA_DEVOTION },
    { 188, "Faceless One Trickster", "Purge", "Removes magical buffs, greatly slows the target briefly, and damages summoned units.", "https://classic.battle.net/war3/neutral/facelessones.shtml", "Aprg", NULL, CE_UNIT_PURGE },
    { 189, "Faceless One Terror", "Sleep", "Sends an enemy unit to sleep until it is attacked or the effect ends.", "https://classic.battle.net/war3/neutral/facelessones.shtml", "AUsl", NULL, CE_UNIT_SLEEP },
    { 190, "Faceless One Terror", "Mana Shield", "Uses mana to absorb a proportion of incoming damage.", "https://classic.battle.net/war3/neutral/facelessones.shtml", "ANms", NULL, CE_SELF_MANA_SHIELD },
    { 191, "Faceless One Deathbringer", "Silence", "Prevents enemy units in an area from casting spells temporarily.", "https://classic.battle.net/war3/neutral/facelessones.shtml", "ANsi", NULL, CE_POINT_SILENCE },
    { 192, "Faceless One Deathbringer", "Animate Dead", "Temporarily reanimates up to six dead units to fight.", "https://classic.battle.net/war3/neutral/facelessones.shtml", "AUan", NULL, CE_SUMMON_CORPSE },
    { 196, "Ice Troll Warlord", "Trueshot Aura", "Increases the damage of nearby friendly ranged attacks by 5%.", "https://classic.battle.net/war3/neutral/icetrolls.shtml", "AEar", "ACat", CE_AURA_TRUESHOT },
    { 201, "Ice Troll High Priest", "Frost Armor", "Gives an ally 3 extra armor; melee attackers are temporarily slowed.", "https://classic.battle.net/war3/neutral/icetrolls.shtml", "AUfa", NULL, CE_UNIT_FRIEND_ARMOR },
    { 208, "Nerubian Seer", "Raise Dead (Autocast)", "Creates two Skeleton Warriors from a suitable corpse.", "https://classic.battle.net/war3/neutral/nerubians.shtml", "Arai", NULL, CE_SUMMON_CORPSE },
    { 209, "Nerubian Queen", "Raise Dead (Autocast)", "Creates two Skeleton Warriors from a suitable corpse.", "https://classic.battle.net/war3/neutral/nerubians.shtml", "Arai", NULL, CE_SUMMON_CORPSE },
    { 210, "Nerubian Queen", "Carrion Swarm", "Sends a wave of dark energy along a line, damaging units in its path.", "https://classic.battle.net/war3/neutral/nerubians.shtml", "AUcs", NULL, CE_POINT_DAMAGE },
    { 211, "Nerubian Queen", "Unholy Aura", "Increases nearby allied movement speed and hit-point regeneration.", "https://classic.battle.net/war3/neutral/nerubians.shtml", "AUau", NULL, CE_AURA_UNHOLY },
    { 212, "Nerubian Webspinner", "Web", "Brings a targeted flying enemy to ground where ground attacks can hit it.", "https://classic.battle.net/war3/neutral/nerubians.shtml", "Aweb", NULL, CE_UNIT_WEB },
    { 213, "Nerubian Webspinner", "Raise Dead (Autocast)", "Creates two Skeleton Warriors from a suitable corpse.", "https://classic.battle.net/war3/neutral/nerubians.shtml", "Arai", NULL, CE_SUMMON_CORPSE },
    { 220, "Polar Furbolg Shaman", "Frost Armor", "Gives an ally 3 extra armor; melee attackers are temporarily slowed.", "https://classic.battle.net/war3/neutral/polarfurbolgs.shtml", "AUfa", NULL, CE_UNIT_FRIEND_ARMOR },
    { 221, "Polar Furbolg Elder Shaman", "Frost Nova", "Strikes a target for 50 cold damage and deals 50 more to surrounding enemies, slowing them.", "https://classic.battle.net/war3/neutral/polarfurbolgs.shtml", "AUfn", NULL, CE_UNIT_FROST_NOVA },
    { 222, "Polar Furbolg Elder Shaman", "Healing Wave", "Heals 215 hit points on the first target, bouncing four times with smaller heals.", "https://classic.battle.net/war3/neutral/polarfurbolgs.shtml", "AOhw", NULL, CE_UNIT_HEAL_CHAIN },
    { 224, "Polar Furbolg Ursa Warrior", "Command Aura", "Nearby friendly units deal 10% more attack damage.", "https://classic.battle.net/war3/neutral/polarfurbolgs.shtml", "AOac", NULL, CE_AURA_COMMAND },
    { 225, "Polar Furbolg Ursa Warrior", "War Stomp", "Damages nearby enemies for 25 and stuns them for roughly 3 seconds.", "https://classic.battle.net/war3/neutral/polarfurbolgs.shtml", "AOws", NULL, CE_SELF_DAMAGE_STUN },
    { 227, "Magnataur Reaver", "Frost Bolt", "Hits a unit for 100 frost damage and stuns it for 5 seconds.", "https://classic.battle.net/war3/neutral/magnataurs.shtml", "ACfb", NULL, CE_UNIT_STUN_DAMAGE },
    { 229, "Magnataur Destroyer", "Frost Bolt", "Hits a unit for 100 frost damage and stuns it for 5 seconds.", "https://classic.battle.net/war3/neutral/magnataurs.shtml", "ACfb", NULL, CE_UNIT_STUN_DAMAGE },
    { 230, "Magnataur Destroyer", "Slam", "Deals 70 area damage to nearby enemy units.", "https://classic.battle.net/war3/neutral/magnataurs.shtml", "ACtc", NULL, CE_SELF_DAMAGE_STUN },
    { 238, "Tuskarr Sorcerer", "Inner Fire (Autocast)", "Raises a friendly unit\u2019s damage by 10% and armor by 5.", "https://classic.battle.net/war3/neutral/tuskarr.shtml", "Ainf", NULL, CE_UNIT_FRIEND_BUFF },
    { 239, "Tuskarr Chieftain", "Command Aura", "Nearby friendly units deal 10% more attack damage.", "https://classic.battle.net/war3/neutral/tuskarr.shtml", "AOac", NULL, CE_AURA_COMMAND },
    { 242, "Unbroken Darkweaver", "Unholy Frenzy", "Greatly increases attack speed but drains 4 health per second.", "https://classic.battle.net/war3/neutral/unbroken.shtml", "Auhf", NULL, CE_UNIT_FRIEND_FRENZY },
    { 244, "Gnoll Assassin", "Envenomed Weapon", "Poison from attacks deals approximately 2 damage per second for 20 seconds.", "https://classic.battle.net/war3/neutral/gnolls.shtml", "Aven", NULL, CE_HIT_POISON },
    { 247, "Gnoll Warden", "Purge", "Removes magical buffs, greatly slows the target briefly, and damages summoned units.", "https://classic.battle.net/war3/neutral/gnolls.shtml", "Aprg", NULL, CE_UNIT_PURGE },
    { 248, "Gnoll Overseer", "Command Aura", "Adds 1 attack damage to nearby allied units.", "https://classic.battle.net/war3/neutral/gnolls.shtml", "AOac", NULL, CE_AURA_COMMAND },
    { 252, "Ogre Lord", "Shock Wave", "Sends a wave along the ground, dealing 75 damage to units it passes through.", "https://classic.battle.net/war3/neutral/ogres.shtml", "AOsh", NULL, CE_POINT_DAMAGE },
    { 253, "Ogre Lord", "Devotion Aura", "Grants nearby allied units 3 additional armor.", "https://classic.battle.net/war3/neutral/ogres.shtml", "AHad", NULL, CE_AURA_DEVOTION },
    { 256, "Red Dragon", "Devour", "Swallows a nonhero enemy; the victim takes 5 damage per second and escapes if the devourer dies first.", "https://classic.battle.net/war3/neutral/dragons.shtml", "Adev", NULL, CE_UNIT_DEVOUR },
    { 260, "Black Dragon", "Devour", "Swallows a nonhero enemy; the victim takes 5 damage per second and escapes if the devourer dies first.", "https://classic.battle.net/war3/neutral/dragons.shtml", "Adev", NULL, CE_UNIT_DEVOUR },
    { 263, "Giant Frost Wolf", "Critical Strike (Passive)", "Provides a chance for an attack to inflict twice its normal damage.", "https://classic.battle.net/war3/neutral/wolves.shtml", "AOcr", NULL, CE_HIT_CRITICAL },
    { 264, "Ghost", "Possession", "Takes control of an eligible enemy organic unit, consuming the caster.", "https://classic.battle.net/war3/neutral/undead.shtml", "Apos", NULL, CE_UNIT_POSSESSION },
    { 266, "Wraith", "Possession", "Takes control of an eligible enemy organic unit, consuming the caster.", "https://classic.battle.net/war3/neutral/undead.shtml", "Apos", NULL, CE_UNIT_POSSESSION },
    { 267, "Frost Revenant", "Blizzard", "Calls down six waves of ice that damage enemies in a targeted area.", "https://classic.battle.net/war3/neutral/undead.shtml", "AHbz", NULL, CE_POINT_DAMAGE },
    { 268, "Ice Revenant", "Frost Nova", "Strikes a target for 50 cold damage and deals 50 more to surrounding enemies, slowing them.", "https://classic.battle.net/war3/neutral/undead.shtml", "AUfn", NULL, CE_UNIT_FROST_NOVA },
    { 269, "Ice Revenant", "Vampiric Aura", "Lets nearby allied melee attackers recover hit points based on the damage they inflict.", "https://classic.battle.net/war3/neutral/undead.shtml", "AUav", NULL, CE_AURA_VAMPIRIC },
    { 272, "Skeletal Marksman", "Cold Arrows (Autocast)", "Adds frost to ranged attacks, slowing victims at a mana cost.", "https://classic.battle.net/war3/neutral/undead.shtml", "AHca", NULL, CE_HIT_COLD_ARROW },
    { 275, "Ancient Wendigo", "War Stomp", "Damages nearby enemies for 25 and stuns them for roughly 3 seconds.", "https://classic.battle.net/war3/neutral/wendigo.shtml", "AOws", NULL, CE_SELF_DAMAGE_STUN },
    { 277, "Ancient Wendigo", "Reincarnation (Passive)", "Automatically returns this unit to life after it is killed, if the ability is ready.", "https://classic.battle.net/war3/neutral/wendigo.shtml", "AOre", NULL, CE_PASSIVE_REINCARNATION },
    { 283, "Draenei Darkslayer", "Immolation", "Burns nearby enemies for about 10 damage per second while consuming mana.", "https://classic.battle.net/war3/neutral/Draenei.shtml", "AEim", NULL, CE_SELF_IMMOLATION },
    { 286, "Draenei Harbinger", "Ray of Disruption", "Bouncing magical energy strips buffs from multiple units.", "https://classic.battle.net/war3/neutral/Draenei.shtml", "Andm", NULL, CE_POINT_DISPEL },
    { 288, "Draenei Seer", "Healing Wave", "Heals 215 hit points on the first target, bouncing four times with smaller heals.", "https://classic.battle.net/war3/neutral/Draenei.shtml", "AOhw", NULL, CE_UNIT_HEAL_CHAIN },
    { 289, "Draenei Seer", "Brilliance Aura", "Regenerates an additional 1 mana per second for nearby friendly units.", "https://classic.battle.net/war3/neutral/Draenei.shtml", "AHab", NULL, CE_AURA_BRILLIANCE },
    { 291, "Eredar Sorceror", "Sleep", "Sends an enemy unit to sleep until it is attacked or the effect ends.", "https://classic.battle.net/war3/neutral/eredarwarlock.shtml", "AUsl", NULL, CE_UNIT_SLEEP },
    { 292, "Eredar Diabolist", "Parasite", "Inflicts periodic damage; if the affected unit dies, it produces a summoned minion.", "https://classic.battle.net/war3/neutral/eredarwarlock.shtml", "ANpa", NULL, CE_UNIT_DOT },
    { 293, "Eredar Diabolist", "Firebolt", "Hits a target with fire for 100 damage and a 2-second stun.", "https://classic.battle.net/war3/neutral/eredarwarlock.shtml", "ANfb", NULL, CE_UNIT_STUN_DAMAGE },
    { 294, "Eredar Warlock", "Finger of Pain", "Inflicts 250 damage on a targeted unit or structure.", "https://classic.battle.net/war3/neutral/eredarwarlock.shtml", "ANfd", NULL, CE_UNIT_DAMAGE },
    { 295, "Eredar Warlock", "Mana Shield", "Uses mana to absorb a proportion of incoming damage.", "https://classic.battle.net/war3/neutral/eredarwarlock.shtml", "ANms", NULL, CE_SELF_MANA_SHIELD },
    { 298, "Bloodfiend", "Cleaving Attack (Passive)", "A melee strike damages additional enemies around its primary target.", "https://classic.battle.net/war3/neutral/felguards.shtml", "ANca", NULL, CE_HIT_SPLASH },
    { 299, "Overlord", "Howl of Terror", "Lowers the attack damage dealt by nearby enemy units temporarily.", "https://classic.battle.net/war3/neutral/felguards.shtml", "ANht", NULL, CE_SELF_ENEMY_DEBUFF },
    { 300, "Overlord", "Cleaving Attack (Passive)", "A melee strike damages additional enemies around its primary target.", "https://classic.battle.net/war3/neutral/felguards.shtml", "ANca", NULL, CE_HIT_SPLASH },
    { 301, "Overlord", "Vampiric Aura", "Lets nearby allied melee attackers recover hit points based on the damage they inflict.", "https://classic.battle.net/war3/neutral/felguards.shtml", "AUav", NULL, CE_AURA_VAMPIRIC },
    { 303, "Infernal Machine", "Breath of Fire", "Breathes a cone of fire that damages enemies in front of the caster.", "https://classic.battle.net/war3/neutral/infernalmachines.shtml", "ANbf", NULL, CE_POINT_DAMAGE },
    { 304, "Infernal Juggernaut", "Inferno", "Summons an Infernal that damages and stuns nearby enemies when it lands.", "https://classic.battle.net/war3/neutral/infernalmachines.shtml", "AUin", NULL, CE_SUMMON_POINT },
    { 308, "Nether Dragon", "Cripple", "Cuts enemy movement speed by 75%, attack speed by 50%, and attack damage by 50%.", "https://classic.battle.net/war3/neutral/netherdragons.shtml", "Acri", NULL, CE_UNIT_CRIPPLE },
    { 310, "Vile Tormentor", "Silence", "Prevents enemy units in an area from casting spells temporarily.", "https://classic.battle.net/war3/neutral/succubi.shtml", "ANsi", NULL, CE_POINT_SILENCE },
    { 312, "Maiden of Pain", "Life Drain", "Transfers hit points from an enemy target to the caster over several seconds.", "https://classic.battle.net/war3/neutral/succubi.shtml", "ANdr", NULL, CE_UNIT_LIFE_DRAIN },
    { 313, "Maiden of Pain", "Shadow Strike", "Deals initial and continuing poison damage while slowing the target.", "https://classic.battle.net/war3/neutral/succubi.shtml", "AEsh", NULL, CE_UNIT_DOT },
    { 314, "Queen of Suffering", "Death Coil", "Damages a living target or restores health to a friendly Undead target.", "https://classic.battle.net/war3/neutral/succubi.shtml", "AUdc", NULL, CE_UNIT_DEATH_COIL },
    { 315, "Queen of Suffering", "Unholy Aura", "Increases nearby allied movement speed and hit-point regeneration.", "https://classic.battle.net/war3/neutral/succubi.shtml", "AUau", NULL, CE_AURA_UNHOLY },
    { 316, "Queen of Suffering", "Charm", "Permanently takes control of an eligible nonhero enemy unit.", "https://classic.battle.net/war3/neutral/succubi.shtml", "ANch", NULL, CE_UNIT_CHARM },
    { 318, "Voidwalker", "Cold Arrows (Autocast)", "Adds frost to ranged attacks, slowing victims at a mana cost.", "https://classic.battle.net/war3/neutral/voidwalkers.shtml", "AHca", NULL, CE_HIT_COLD_ARROW },
    { 319, "Greater Voidwalker", "Chain Lightning", "Strikes an enemy and jumps to other targets, dealing progressively less lightning damage.", "https://classic.battle.net/war3/neutral/voidwalkers.shtml", "AOcl", NULL, CE_UNIT_CHAIN },
    { 320, "Greater Voidwalker", "Frost Armor", "Gives an ally 3 extra armor; melee attackers are temporarily slowed.", "https://classic.battle.net/war3/neutral/voidwalkers.shtml", "AUfa", NULL, CE_UNIT_FRIEND_ARMOR },
    { 321, "Elder Voidwalker", "Forked Lightning", "Lightning strikes as many as three nearby opponents at once.", "https://classic.battle.net/war3/neutral/voidwalkers.shtml", "ANfl", NULL, CE_UNIT_CHAIN },
    { 322, "Elder Voidwalker", "Devour Magic", "Consumes magical effects to replenish the caster\u2019s health and mana, damaging summons.", "https://classic.battle.net/war3/neutral/voidwalkers.shtml", "Advm", NULL, CE_POINT_DISPEL_DRAIN },
    { 323, "Hydra Hatchling", "Slow Poison", "Poisoned attacks inflict periodic damage and slow attack and movement speeds.", "https://classic.battle.net/war3/neutral/hydras.shtml", "Aspo", NULL, CE_HIT_SLOW_POISON },
    { 324, "Hydra", "Slow Poison", "Poisoned attacks inflict periodic damage and slow attack and movement speeds.", "https://classic.battle.net/war3/neutral/hydras.shtml", "Aspo", NULL, CE_HIT_SLOW_POISON },
    { 326, "Ancient Hydra", "Devour", "Swallows a nonhero enemy; the victim takes 5 damage per second and escapes if the devourer dies first.", "https://classic.battle.net/war3/neutral/hydras.shtml", "Adev", NULL, CE_UNIT_DEVOUR },
    { 327, "Ancient Hydra", "Slow Poison", "Poisoned attacks inflict periodic damage and slow attack and movement speeds.", "https://classic.battle.net/war3/neutral/hydras.shtml", "Aspo", NULL, CE_HIT_SLOW_POISON },
    { 328, "Ancient Hydra", "War Stomp", "Damages nearby enemies for 25 and stuns them for roughly 3 seconds.", "https://classic.battle.net/war3/neutral/hydras.shtml", "AOws", NULL, CE_SELF_DAMAGE_STUN },
    { 331, "Enraged Jungle Stalker", "Slam", "Deals 70 area damage to nearby enemy units.", "https://classic.battle.net/war3/neutral/junglestalkers.shtml", "ACtc", NULL, CE_SELF_DAMAGE_STUN },
    { 334, "Makrura Tidecaller", "Frost Armor", "Gives an ally 3 extra armor; melee attackers are temporarily slowed.", "https://classic.battle.net/war3/neutral/makrura.shtml", "AUfa", NULL, CE_UNIT_FRIEND_ARMOR },
    { 336, "Makrura Deepseer", "Entangling Roots", "Roots a victim in place and inflicts damage over time.", "https://classic.battle.net/war3/neutral/makrura.shtml", "AEer", NULL, CE_UNIT_ROOT },
    { 337, "Makrura Deepseer", "Summon Prawns", "Calls two temporary Makrura Prawns to assist in combat.", "https://classic.battle.net/war3/neutral/makrura.shtml", "AOsf", NULL, CE_SUMMON_NO_TARGET },
    { 339, "Makrura Tidal Lord", "Cleaving Attack (Passive)", "A melee strike damages additional enemies around its primary target.", "https://classic.battle.net/war3/neutral/makrura.shtml", "ANca", NULL, CE_HIT_SPLASH },
    { 340, "Makrura Tidal Lord", "Devotion Aura", "Grants nearby allied units 3 additional armor.", "https://classic.battle.net/war3/neutral/makrura.shtml", "AHad", NULL, CE_AURA_DEVOTION },
    { 346, "Mur'gul Marauder", "Command Aura", "Nearby friendly units deal 10% more attack damage.", "https://classic.battle.net/war3/neutral/murgul.shtml", "AOac", NULL, CE_AURA_COMMAND },
    { 348, "Mur'gul Shadowcaster", "Hex", "Temporarily transforms an enemy into a harmless critter.", "https://classic.battle.net/war3/neutral/murgul.shtml", "AOhx", NULL, CE_UNIT_POLYMORPH },
    { 351, "Sea Giant", "Pulverize", "Provides a chance for melee attacks to damage nearby units as well.", "https://classic.battle.net/war3/neutral/seagiants.shtml", "Awar", "ACpv", CE_HIT_SPLASH },
    { 352, "Sea Giant Hunter", "Pulverize", "Provides a chance for melee attacks to damage nearby units as well.", "https://classic.battle.net/war3/neutral/seagiants.shtml", "Awar", "ACpv", CE_HIT_SPLASH },
    { 354, "Sea Giant Behemoth", "Pulverize", "Provides a chance for melee attacks to damage nearby units as well.", "https://classic.battle.net/war3/neutral/seagiants.shtml", "Awar", "ACpv", CE_HIT_SPLASH },
    { 355, "Sea Giant Behemoth", "War Stomp", "Damages nearby enemies for 25 and stuns them for roughly 3 seconds.", "https://classic.battle.net/war3/neutral/seagiants.shtml", "AOws", NULL, CE_SELF_DAMAGE_STUN },
    { 356, "Sea Giant Behemoth", "Hurl Boulder", "Deals 100 damage to an enemy with a magical boulder and stuns it for 2 seconds.", "https://classic.battle.net/war3/neutral/seagiants.shtml", "ACtb", NULL, CE_UNIT_STUN_DAMAGE },
    { 357, "Sea Turtle Hatchling", "Spiked Shell (Passive)", "Reflects 30% of melee attack damage back to the attacker.", "https://classic.battle.net/war3/neutral/seaturtles.shtml", "Aspi", NULL, CE_HIT_REFLECT },
    { 358, "Sea Turtle", "Spiked Shell (Passive)", "Reflects 30% of melee attack damage back to the attacker.", "https://classic.battle.net/war3/neutral/seaturtles.shtml", "Aspi", NULL, CE_HIT_REFLECT },
    { 359, "Giant Sea Turtle", "Spiked Shell (Passive)", "Reflects 30% of melee attack damage back to the attacker.", "https://classic.battle.net/war3/neutral/seaturtles.shtml", "Aspi", NULL, CE_HIT_REFLECT },
    { 360, "Gargantuan Sea Turtle", "Spiked Shell (Passive)", "Reflects 30% of melee attack damage back to the attacker.", "https://classic.battle.net/war3/neutral/seaturtles.shtml", "Aspi", NULL, CE_HIT_REFLECT },
    { 361, "Dragon Turtle", "Devour", "Swallows a nonhero enemy; the victim takes 5 damage per second and escapes if the devourer dies first.", "https://classic.battle.net/war3/neutral/seaturtles.shtml", "Adev", "ACdv", CE_UNIT_DEVOUR },
    { 362, "Dragon Turtle", "Crushing Wave", "Launches a damaging wave through enemies in a line.", "https://classic.battle.net/war3/neutral/seaturtles.shtml", "ACc2", "ACc2", CE_POINT_DAMAGE },
    { 363, "Dragon Turtle", "Spiked Shell (Passive)", "Reflects 30% of melee attack damage back to the attacker.", "https://classic.battle.net/war3/neutral/seaturtles.shtml", "Aspi", NULL, CE_HIT_REFLECT },
    { 366, "Revenant of the Depths", "Summon Sea Elemental", "Conjures a temporary Sea Elemental to fight for the caster.", "https://classic.battle.net/war3/neutral/revenants.shtml", "ACwe", NULL, CE_SUMMON_NO_TARGET },
    { 367, "Deeplord Revenant", "Crushing Wave", "Launches a damaging wave through enemies in a line.", "https://classic.battle.net/war3/neutral/revenants.shtml", "ACc2", NULL, CE_POINT_DAMAGE },
    { 368, "Deeplord Revenant", "Frost Armor", "Gives an ally 3 extra armor; melee attackers are temporarily slowed.", "https://classic.battle.net/war3/neutral/revenants.shtml", "AUfa", NULL, CE_UNIT_FRIEND_ARMOR },
    { 371, "Skeletal Orc Champion", "Death Coil", "Damages a living target or restores health to a friendly Undead target.", "https://classic.battle.net/war3/neutral/skeletalorcs.shtml", "AUdc", NULL, CE_UNIT_DEATH_COIL },
    { 376, "Stormreaver Hermit", "Purge", "Removes magical buffs, greatly slows the target briefly, and damages summoned units.", "https://classic.battle.net/war3/neutral/stormreavers.shtml", "Aprg", NULL, CE_UNIT_PURGE },
    { 378, "Stormreaver Necrolyte", "Chain Lightning", "Strikes an enemy and jumps to other targets, dealing progressively less lightning damage.", "https://classic.battle.net/war3/neutral/stormreavers.shtml", "AOcl", NULL, CE_UNIT_CHAIN },
    { 379, "Stormreaver Warlock", "Monsoon", "Channels repeated waves of lightning damage into the target area.", "https://classic.battle.net/war3/neutral/stormreavers.shtml", "ANmo", NULL, CE_POINT_DAMAGE },
    { 380, "Stormreaver Warlock", "Animate Dead", "Temporarily reanimates up to six dead units to fight.", "https://classic.battle.net/war3/neutral/stormreavers.shtml", "AUan", NULL, CE_SUMMON_CORPSE },
    { 382, "Elder Sasquatch", "Force of Nature", "Converts nearby trees into three temporary Treant units.", "https://classic.battle.net/war3/neutral/sasquatch.shtml", "AEfn", NULL, CE_SUMMON_POINT },
    { 386, "Ancient Sasquatch", "Slam", "Hits nearby enemy units for 70 damage; some variants also slow or stun.", "https://classic.battle.net/war3/neutral/sasquatch.shtml", "ACtc", NULL, CE_SELF_DAMAGE_STUN },
    { 387, "Ancient Sasquatch", "Force of Nature", "Converts nearby trees into three temporary Treant units.", "https://classic.battle.net/war3/neutral/sasquatch.shtml", "AEfn", NULL, CE_SUMMON_POINT },
    { 388, "Ancient Sasquatch", "Reincarnation (Passive)", "Automatically returns this unit to life after it is killed, if the ability is ready.", "https://classic.battle.net/war3/neutral/sasquatch.shtml", "AOre", NULL, CE_PASSIVE_REINCARNATION },
};

/* Each registration calls the gameplay runner and checks the observed effect,
 * never a description/fixture-string comparison. These are strict tests:
 * unsupported effects fail until gameplay is implemented. */
#define CREEP_EXTENDED_TEST(ROW, INDEX) \
    TEST(wc3_creep_contract, row_##ROW) { contract_extended(&extendedCases[INDEX]); }
CREEP_EXTENDED_TEST(5, 0)
CREEP_EXTENDED_TEST(9, 1)
CREEP_EXTENDED_TEST(14, 2)
CREEP_EXTENDED_TEST(16, 3)
CREEP_EXTENDED_TEST(17, 4)
CREEP_EXTENDED_TEST(20, 5)
CREEP_EXTENDED_TEST(22, 6)
CREEP_EXTENDED_TEST(23, 7)
CREEP_EXTENDED_TEST(24, 8)
CREEP_EXTENDED_TEST(25, 9)
CREEP_EXTENDED_TEST(26, 10)
CREEP_EXTENDED_TEST(29, 11)
CREEP_EXTENDED_TEST(30, 12)
CREEP_EXTENDED_TEST(31, 13)
CREEP_EXTENDED_TEST(33, 14)
CREEP_EXTENDED_TEST(34, 15)
CREEP_EXTENDED_TEST(36, 16)
CREEP_EXTENDED_TEST(37, 17)
CREEP_EXTENDED_TEST(38, 18)
CREEP_EXTENDED_TEST(39, 19)
CREEP_EXTENDED_TEST(40, 20)
CREEP_EXTENDED_TEST(44, 21)
CREEP_EXTENDED_TEST(45, 22)
CREEP_EXTENDED_TEST(47, 23)
CREEP_EXTENDED_TEST(49, 24)
CREEP_EXTENDED_TEST(51, 25)
CREEP_EXTENDED_TEST(52, 26)
CREEP_EXTENDED_TEST(53, 27)
CREEP_EXTENDED_TEST(54, 28)
CREEP_EXTENDED_TEST(55, 29)
CREEP_EXTENDED_TEST(60, 30)
CREEP_EXTENDED_TEST(63, 31)
CREEP_EXTENDED_TEST(64, 32)
CREEP_EXTENDED_TEST(65, 33)
CREEP_EXTENDED_TEST(69, 34)
CREEP_EXTENDED_TEST(71, 35)
CREEP_EXTENDED_TEST(74, 36)
CREEP_EXTENDED_TEST(80, 37)
CREEP_EXTENDED_TEST(81, 38)
CREEP_EXTENDED_TEST(82, 39)
CREEP_EXTENDED_TEST(83, 40)
CREEP_EXTENDED_TEST(85, 41)
CREEP_EXTENDED_TEST(86, 42)
CREEP_EXTENDED_TEST(88, 43)
CREEP_EXTENDED_TEST(89, 44)
CREEP_EXTENDED_TEST(90, 45)
CREEP_EXTENDED_TEST(91, 46)
CREEP_EXTENDED_TEST(92, 47)
CREEP_EXTENDED_TEST(93, 48)
CREEP_EXTENDED_TEST(95, 49)
CREEP_EXTENDED_TEST(96, 50)
CREEP_EXTENDED_TEST(97, 51)
CREEP_EXTENDED_TEST(98, 52)
CREEP_EXTENDED_TEST(99, 53)
CREEP_EXTENDED_TEST(100, 54)
CREEP_EXTENDED_TEST(101, 55)
CREEP_EXTENDED_TEST(102, 56)
CREEP_EXTENDED_TEST(103, 57)
CREEP_EXTENDED_TEST(104, 58)
CREEP_EXTENDED_TEST(107, 59)
CREEP_EXTENDED_TEST(108, 60)
CREEP_EXTENDED_TEST(112, 61)
CREEP_EXTENDED_TEST(117, 62)
CREEP_EXTENDED_TEST(120, 63)
CREEP_EXTENDED_TEST(126, 64)
CREEP_EXTENDED_TEST(128, 65)
CREEP_EXTENDED_TEST(129, 66)
CREEP_EXTENDED_TEST(135, 67)
CREEP_EXTENDED_TEST(136, 68)
CREEP_EXTENDED_TEST(137, 69)
CREEP_EXTENDED_TEST(138, 70)
CREEP_EXTENDED_TEST(139, 71)
CREEP_EXTENDED_TEST(140, 72)
CREEP_EXTENDED_TEST(142, 73)
CREEP_EXTENDED_TEST(146, 74)
CREEP_EXTENDED_TEST(147, 75)
CREEP_EXTENDED_TEST(148, 76)
CREEP_EXTENDED_TEST(149, 77)
CREEP_EXTENDED_TEST(150, 78)
CREEP_EXTENDED_TEST(151, 79)
CREEP_EXTENDED_TEST(152, 80)
CREEP_EXTENDED_TEST(153, 81)
CREEP_EXTENDED_TEST(155, 82)
CREEP_EXTENDED_TEST(156, 83)
CREEP_EXTENDED_TEST(157, 84)
CREEP_EXTENDED_TEST(158, 85)
CREEP_EXTENDED_TEST(160, 86)
CREEP_EXTENDED_TEST(161, 87)
CREEP_EXTENDED_TEST(162, 88)
CREEP_EXTENDED_TEST(163, 89)
CREEP_EXTENDED_TEST(170, 90)
CREEP_EXTENDED_TEST(171, 91)
CREEP_EXTENDED_TEST(172, 92)
CREEP_EXTENDED_TEST(173, 93)
CREEP_EXTENDED_TEST(174, 94)
CREEP_EXTENDED_TEST(175, 95)
CREEP_EXTENDED_TEST(176, 96)
CREEP_EXTENDED_TEST(182, 97)
CREEP_EXTENDED_TEST(184, 98)
CREEP_EXTENDED_TEST(186, 99)
CREEP_EXTENDED_TEST(188, 100)
CREEP_EXTENDED_TEST(189, 101)
CREEP_EXTENDED_TEST(190, 102)
CREEP_EXTENDED_TEST(191, 103)
CREEP_EXTENDED_TEST(192, 104)
CREEP_EXTENDED_TEST(196, 105)
CREEP_EXTENDED_TEST(201, 106)
CREEP_EXTENDED_TEST(208, 107)
CREEP_EXTENDED_TEST(209, 108)
CREEP_EXTENDED_TEST(210, 109)
CREEP_EXTENDED_TEST(211, 110)
CREEP_EXTENDED_TEST(212, 111)
CREEP_EXTENDED_TEST(213, 112)
CREEP_EXTENDED_TEST(220, 113)
CREEP_EXTENDED_TEST(221, 114)
CREEP_EXTENDED_TEST(222, 115)
CREEP_EXTENDED_TEST(224, 116)
CREEP_EXTENDED_TEST(225, 117)
CREEP_EXTENDED_TEST(227, 118)
CREEP_EXTENDED_TEST(229, 119)
CREEP_EXTENDED_TEST(230, 120)
CREEP_EXTENDED_TEST(238, 121)
CREEP_EXTENDED_TEST(239, 122)
CREEP_EXTENDED_TEST(242, 123)
CREEP_EXTENDED_TEST(244, 124)
CREEP_EXTENDED_TEST(247, 125)
CREEP_EXTENDED_TEST(248, 126)
CREEP_EXTENDED_TEST(252, 127)
CREEP_EXTENDED_TEST(253, 128)
CREEP_EXTENDED_TEST(256, 129)
CREEP_EXTENDED_TEST(260, 130)
CREEP_EXTENDED_TEST(263, 131)
CREEP_EXTENDED_TEST(264, 132)
CREEP_EXTENDED_TEST(266, 133)
CREEP_EXTENDED_TEST(267, 134)
CREEP_EXTENDED_TEST(268, 135)
CREEP_EXTENDED_TEST(269, 136)
CREEP_EXTENDED_TEST(272, 137)
CREEP_EXTENDED_TEST(275, 138)
CREEP_EXTENDED_TEST(277, 139)
CREEP_EXTENDED_TEST(283, 140)
CREEP_EXTENDED_TEST(286, 141)
CREEP_EXTENDED_TEST(288, 142)
CREEP_EXTENDED_TEST(289, 143)
CREEP_EXTENDED_TEST(291, 144)
CREEP_EXTENDED_TEST(292, 145)
CREEP_EXTENDED_TEST(293, 146)
CREEP_EXTENDED_TEST(294, 147)
CREEP_EXTENDED_TEST(295, 148)
CREEP_EXTENDED_TEST(298, 149)
CREEP_EXTENDED_TEST(299, 150)
CREEP_EXTENDED_TEST(300, 151)
CREEP_EXTENDED_TEST(301, 152)
CREEP_EXTENDED_TEST(303, 153)
CREEP_EXTENDED_TEST(304, 154)
CREEP_EXTENDED_TEST(308, 155)
CREEP_EXTENDED_TEST(310, 156)
CREEP_EXTENDED_TEST(312, 157)
CREEP_EXTENDED_TEST(313, 158)
CREEP_EXTENDED_TEST(314, 159)
CREEP_EXTENDED_TEST(315, 160)
CREEP_EXTENDED_TEST(316, 161)
CREEP_EXTENDED_TEST(318, 162)
CREEP_EXTENDED_TEST(319, 163)
CREEP_EXTENDED_TEST(320, 164)
CREEP_EXTENDED_TEST(321, 165)
CREEP_EXTENDED_TEST(322, 166)
CREEP_EXTENDED_TEST(323, 167)
CREEP_EXTENDED_TEST(324, 168)
CREEP_EXTENDED_TEST(326, 169)
CREEP_EXTENDED_TEST(327, 170)
CREEP_EXTENDED_TEST(328, 171)
CREEP_EXTENDED_TEST(331, 172)
CREEP_EXTENDED_TEST(334, 173)
CREEP_EXTENDED_TEST(336, 174)
CREEP_EXTENDED_TEST(337, 175)
CREEP_EXTENDED_TEST(339, 176)
CREEP_EXTENDED_TEST(340, 177)
CREEP_EXTENDED_TEST(346, 178)
CREEP_EXTENDED_TEST(348, 179)
CREEP_EXTENDED_TEST(351, 180)
CREEP_EXTENDED_TEST(352, 181)
CREEP_EXTENDED_TEST(354, 182)
CREEP_EXTENDED_TEST(355, 183)
CREEP_EXTENDED_TEST(356, 184)
CREEP_EXTENDED_TEST(357, 185)
CREEP_EXTENDED_TEST(358, 186)
CREEP_EXTENDED_TEST(359, 187)
CREEP_EXTENDED_TEST(360, 188)
CREEP_EXTENDED_TEST(361, 189)
CREEP_EXTENDED_TEST(362, 190)
CREEP_EXTENDED_TEST(363, 191)
CREEP_EXTENDED_TEST(366, 192)
CREEP_EXTENDED_TEST(367, 193)
CREEP_EXTENDED_TEST(368, 194)
CREEP_EXTENDED_TEST(371, 195)
CREEP_EXTENDED_TEST(376, 196)
CREEP_EXTENDED_TEST(378, 197)
CREEP_EXTENDED_TEST(379, 198)
CREEP_EXTENDED_TEST(380, 199)
CREEP_EXTENDED_TEST(382, 200)
CREEP_EXTENDED_TEST(386, 201)
CREEP_EXTENDED_TEST(387, 202)
CREEP_EXTENDED_TEST(388, 203)
#undef CREEP_EXTENDED_TEST
