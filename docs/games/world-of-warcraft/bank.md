# Personal bank

Right-click a nearby friendly NPC with the banker flag to open a personal, server-owned 16-slot bank. The left column deposits backpack stacks and the right withdraws bank stacks. Each row transfers the whole stack; Half transfers half (rounded down). Known items merge up to their catalog limit and unknown items remain separate. Transfers fill available stacks and slots atomically; when the full amount does not fit, neither side changes.

Every transfer checks the live banker, range, player and banker life, source slot, quantity and the revision of the displayed window. The window closes when its banker is no longer valid. Map transitions clear banker and merchant sessions and preserve backpack and bank contents within the current game session. Starting items are granted only once per game initialization.

Selecting a saved character restores its bank contents. This is a fixed-size prototype rather than the retail bank bag and purchase system.
