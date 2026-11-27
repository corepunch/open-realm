# Backpack

The server owns sixteen inventory slots. Open the backpack and select a slot to see its name and Use and Split controls. Select another slot to move, merge matching known item IDs up to their catalog stack limit, or swap different items. Split selects half the stack (rounded down) for an empty destination. Closing the window clears selection.

The server accepts `bag_use <slot>`, `bag_move <from> <to>` and `bag_split <from> <to>` with zero-based slot numbers. It validates indices, contents and counts. Minor Healing Potion (item 118) and Tough Jerky (117) currently restore 35 and 20 health respectively, up to the current 100-health player maximum; they are consumed only when the living player needs health. Linen and Heavy Linen Bandages provide interruptible healing over time. These effects are provisional gameplay values.

Known loot and consumables carry stable item entries and explicit stack limits. Insertion fills partial stacks before empty slots and is transactional: if the whole item quantity cannot fit, no slot changes. Unknown IDs and legacy presentation-only items are kept as separate stacks and never merged by name or icon. Loot uses the same insertion routine as merchant transactions.

The catalog is a small prototype, not a complete item database. Equipment, item cooldowns, food channeling, and comprehensive item effects are not implemented.
