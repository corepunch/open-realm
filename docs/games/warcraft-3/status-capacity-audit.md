# Status cast-capacity audit (incremental)

Cripple, Soul Burn, Barkskin and Cyclone now use the authored buff ID and
caster ability level to reject invalid or full status applications in cast
validation. Cyclone also avoids interrupting target movement when its
impact-time status insertion fails.

This is not a universal mana or cooldown rollback. Area, projectile, and
delayed abilities can have partial or no effect after a cast starts. Their
allocation, resource and user-feedback policy needs separate per-ability
review and regression fixtures. Roar, anti-magic shell and other status
producers remain to be audited.
