uint32_t CreateQuest(LPJASS j) {
    LPQUEST quest = G_MakeQuest();
    return jass_pushlighthandle(j, quest, "quest");
}
uint32_t DestroyQuest(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    if (G_QuestValid(whichQuest)) G_RemoveQuest(whichQuest);
    return 0;
}
uint32_t QuestSetTitle(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    cstring_t title = jass_checkstring(j, 2);
    if (!G_QuestValid(whichQuest)) return 0;
    whichQuest->title = strdup(title);
    return 0;
}
uint32_t QuestSetDescription(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    cstring_t description = jass_checkstring(j, 2);
    if (!G_QuestValid(whichQuest)) return 0;
    whichQuest->description = strdup(description);
    return 0;
}
uint32_t QuestSetIconPath(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    cstring_t iconPath = jass_checkstring(j, 2);
    if (!G_QuestValid(whichQuest)) return 0;
    whichQuest->iconPath = strdup(iconPath);
    return 0;
}
uint32_t QuestSetRequired(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    if (!G_QuestValid(whichQuest)) return 0;
    whichQuest->required = jass_checkboolean(j, 2);
    return 0;
}
uint32_t QuestSetCompleted(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    if (!G_QuestValid(whichQuest)) return 0;
    whichQuest->completed = jass_checkboolean(j, 2);
    return 0;
}
uint32_t QuestSetDiscovered(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    if (!G_QuestValid(whichQuest)) return 0;
    whichQuest->discovered = jass_checkboolean(j, 2);
    return 0;
}
uint32_t QuestSetFailed(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    if (!G_QuestValid(whichQuest)) return 0;
    whichQuest->failed = jass_checkboolean(j, 2);
    return 0;
}
uint32_t QuestSetEnabled(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    if (!G_QuestValid(whichQuest)) return 0;
    whichQuest->enabled = jass_checkboolean(j, 2);
    return 0;
}
uint32_t IsQuestRequired(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    if (!G_QuestValid(whichQuest)) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, whichQuest->required);
}
uint32_t IsQuestCompleted(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    if (!G_QuestValid(whichQuest)) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, whichQuest->completed);
}
uint32_t IsQuestDiscovered(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    if (!G_QuestValid(whichQuest)) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, whichQuest->discovered);
}
uint32_t IsQuestFailed(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    if (!G_QuestValid(whichQuest)) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, whichQuest->failed);
}
uint32_t IsQuestEnabled(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    if (!G_QuestValid(whichQuest)) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, whichQuest->enabled);
}
uint32_t QuestCreateItem(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    if (!G_QuestValid(whichQuest)) return jass_pushnullhandle(j, "questitem");
    FOR_LOOP(i, MAX_QUESTITEMS) if (!whichQuest->items[i].inuse) {
        LPQUESTITEM item = &whichQuest->items[i];
        memset(item, 0, sizeof(*item)); item->inuse = true; whichQuest->num_items++;
        return jass_pushlighthandle(j, item, "questitem");
    }
    fprintf(stderr, "WC3: quest item slot limit %u reached\n", MAX_QUESTITEMS);
    return jass_pushnullhandle(j, "questitem");
}
uint32_t QuestItemSetDescription(LPJASS j) {
    LPQUESTITEM whichQuestItem = jass_checkhandle(j, 1, "questitem");
    cstring_t description = jass_checkstring(j, 2);
    if (!G_QuestItemValid(whichQuestItem)) return 0;
    whichQuestItem->description = strdup(description);
    return 0;
}
uint32_t QuestItemSetCompleted(LPJASS j) {
    LPQUESTITEM whichQuestItem = jass_checkhandle(j, 1, "questitem");
    if (!G_QuestItemValid(whichQuestItem)) return 0;
    whichQuestItem->completed = jass_checkboolean(j, 2);
    return 0;
}
uint32_t IsQuestItemCompleted(LPJASS j) {
    LPQUESTITEM whichQuestItem = jass_checkhandle(j, 1, "questitem");
    if (!G_QuestItemValid(whichQuestItem)) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, whichQuestItem->completed);
}
uint32_t CreateDefeatCondition(LPJASS j) {
    return jass_pushnullhandle(j, "defeatcondition");
}
uint32_t DestroyDefeatCondition(LPJASS j) {
    //handle_t whichCondition = jass_checkhandle(j, 1, "defeatcondition");
    return 0;
}
uint32_t DefeatConditionSetDescription(LPJASS j) {
    //handle_t whichCondition = jass_checkhandle(j, 1, "defeatcondition");
    //cstring_t description = jass_checkstring(j, 2);
    return 0;
}
/* Blizzard.j requests this per recipient; MiscData owns the notification timeout. */
uint32_t FlashQuestDialogButton(LPJASS j) {
    cstring_t value = Stb_IniCacheFind(&game.config.misc, "QuestIndicatorTimeout", "QuestIndicatorTimeout");
    float seconds;
    (void)j;
    if (!value || sscanf(value, "%f", &seconds) != 1 || !isfinite(seconds) || seconds <= 0) {
        fprintf(stderr, "WC3: missing or invalid QuestIndicatorTimeout\n");
        return 0;
    }
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = &game.clients[i];
        if (!currentplayer || client == PLAYER_CLIENT(currentplayer))
            client->quest_until = level.time + (uint32_t)(seconds * 1000.0f);
    }
    return 0;
}
uint32_t ForceQuestDialogUpdate(LPJASS j) {
    /* Quest windows are send-once client presentation; the native has no server-side window to refresh. */
    (void)j;
    return 0;
}
