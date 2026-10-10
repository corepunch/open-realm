# Siphon Mana and Life Drain channel pulses

The shared drain thinker validates the caster, target incarnation, range, and
channel serial before each scheduled pulse. Life Drain applies its health
component before its mana component, so it revalidates the channel and living
target after health damage. A fatal pulse or gameplay interruption ends the
thinker before it can transfer mana from a dead or stale target.

The lifecycle regressions cover a stun before a Siphon Mana pulse, a nonfatal
Life Drain pulse that proves authored mana transfer still occurs, and a lethal
Life Drain pulse with nonzero mana data that must leave the target's mana
unchanged.
