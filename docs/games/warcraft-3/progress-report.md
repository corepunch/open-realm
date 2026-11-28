# Warcraft III Progress Report

OpenRealm is a playable reimplementation of Warcraft III built from the game data and campaign scripts. It supports both **Reign of Chaos** and **The Frozen Throne** campaign content.

It has been tested with the original Reign of Chaos and The Frozen Throne CDs, and with the legacy version 1.29 available through the Reforged download. The Reforged version itself is not supported yet.

OpenRealm is still a work in progress, so changes may occasionally introduce regressions.

## Performance

The frame rate is limited to 60 FPS by default. On very low-end hardware, such as an RG40xx H, simple levels run at around 30 FPS. Most computers should handle 60 FPS easily, and Apple silicon Macs should be able to handle more than 120 FPS easily when the frame-rate limit is raised.

## Current status

- All Reign of Chaos and The Frozen Throne campaign levels are playable.
- Core base-building mechanics work for all four races: Human, Orc, Undead, and Night Elf.
- Warcraft III abilities and items are implemented and work in gameplay.

## Differences from retail

OpenRealm is a rewrite, not a decompilation of Blizzard's original executable. Some behavior therefore differs from retail, and individual systems can still have bugs or compatibility gaps. In particular:

- Campaign and computer-player AI may behave differently and can be broken in some situations.
- Abilities with complex damage paths may resolve differently.
- Pathfinding can produce different movement and routing.
- Particle and other visual effects may differ.

These differences can affect fidelity, but the campaigns are generally playable from beginning to end.

## How to help

- Test the game and compare its behavior with retail Warcraft III; report differences and bugs.
- Try custom maps and report compatibility issues.
- Contribute fixes for gameplay and presentation disparities.
- Join the conversation on our [friendly Discord](https://discord.gg/MxkUMWsKGs).
