# Spellbook and action slots

The server authors the Spellbook panel from the spells currently implemented by the game. Attack and Healing Touch are available to all classes; Mage additionally has Fireball, Frostbolt and Fire Blast. These lists reflect the current prototype and are not a complete class progression system.

Select a spell to see its mana cost, cast time, range and cooldown. Cast uses the same targeting, movement, mana and cooldown checks as an action button. The numbered buttons assign the selected spell to an action slot; Clear empties a slot. Multiple slots can hold the same spell. The server validates every selection and assignment against the player's class.

Cooldowns belong to the spell on the player entity, not to the action slot. Reassigning or clearing a slot does not reset an active cooldown, and every slot containing that spell displays the same remaining cooldown. Saved characters restore their action-slot bindings on login.

Mage Fireball Rank 2 can be learned from the book at 400 total XP. This is a prototype training milestone, not a level or trainer simulation; the initial 120 XP counts toward it. Learning upgrades existing Rank 1 action slots, while Rank 1 remains selectable. Rank 2 is rejected for casting and binding until learned. The character save records the learned rank.

Rank 2 uses spell ID 143 from the installed `dbc.MPQ` `DBFilesClient/Spell.dbc`: mana cost 45, effect base points 30 and die sides 15, yielding 31–45 projectile damage. Its cast-time index 5 resolves to 2000 ms in `SpellCastTimes.dbc`. Rank 1 retains its existing prototype behavior; this does not yet implement the rank's secondary damage-over-time effect or general class progression.
