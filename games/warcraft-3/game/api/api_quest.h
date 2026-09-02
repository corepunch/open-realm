DWORD CreateQuest(LPJASS j) {
    LPQUEST quest = G_MakeQuest();
    return jass_pushlighthandle(j, quest, "quest");
}
DWORD DestroyQuest(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    G_RemoveQuest(whichQuest);
    return 0;
}
DWORD QuestSetTitle(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    LPCSTR title = jass_checkstring(j, 2);
    whichQuest->title = strdup(title);
    return 0;
}
DWORD QuestSetDescription(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    LPCSTR description = jass_checkstring(j, 2);
    whichQuest->description = strdup(description);
    return 0;
}
DWORD QuestSetIconPath(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    LPCSTR iconPath = jass_checkstring(j, 2);
    whichQuest->iconPath = strdup(iconPath);
    return 0;
}
DWORD QuestSetRequired(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    whichQuest->required = jass_checkboolean(j, 2);
    return 0;
}
DWORD QuestSetCompleted(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    whichQuest->completed = jass_checkboolean(j, 2);
    return 0;
}
DWORD QuestSetDiscovered(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    whichQuest->discovered = jass_checkboolean(j, 2);
    return 0;
}
DWORD QuestSetFailed(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    whichQuest->failed = jass_checkboolean(j, 2);
    return 0;
}
DWORD QuestSetEnabled(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    whichQuest->enabled = jass_checkboolean(j, 2);
    return 0;
}
DWORD IsQuestRequired(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    return jass_pushboolean(j, whichQuest->required);
}
DWORD IsQuestCompleted(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    return jass_pushboolean(j, whichQuest->completed);
}
DWORD IsQuestDiscovered(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    return jass_pushboolean(j, whichQuest->discovered);
}
DWORD IsQuestFailed(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    return jass_pushboolean(j, whichQuest->failed);
}
DWORD IsQuestEnabled(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    return jass_pushboolean(j, whichQuest->enabled);
}
DWORD QuestCreateItem(LPJASS j) {
    LPQUEST whichQuest = jass_checkhandle(j, 1, "quest");
    LPQUESTITEM item = gi.MemAlloc(sizeof(QUESTITEM));
    ADD_TO_LIST(item, whichQuest->items);
    return jass_pushlighthandle(j, item, "questitem");
}
DWORD QuestItemSetDescription(LPJASS j) {
    LPQUESTITEM whichQuestItem = jass_checkhandle(j, 1, "questitem");
    LPCSTR description = jass_checkstring(j, 2);
    whichQuestItem->description = strdup(description);
    return 0;
}
DWORD QuestItemSetCompleted(LPJASS j) {
    LPQUESTITEM whichQuestItem = jass_checkhandle(j, 1, "questitem");
    whichQuestItem->completed = jass_checkboolean(j, 2);
    return 0;
}
DWORD IsQuestItemCompleted(LPJASS j) {
    LPQUESTITEM whichQuestItem = jass_checkhandle(j, 1, "questitem");
    return jass_pushboolean(j, whichQuestItem->completed);
}
DWORD CreateDefeatCondition(LPJASS j) {
    return jass_pushnullhandle(j, "defeatcondition");
}
DWORD DestroyDefeatCondition(LPJASS j) {
    //HANDLE whichCondition = jass_checkhandle(j, 1, "defeatcondition");
    return 0;
}
DWORD DefeatConditionSetDescription(LPJASS j) {
    //HANDLE whichCondition = jass_checkhandle(j, 1, "defeatcondition");
    //LPCSTR description = jass_checkstring(j, 2);
    return 0;
}
DWORD FlashQuestDialogButton(LPJASS j) {
    /* TODO: the server-authored frame wire does not currently serialize the
     * stock button pulse/highlight state.  Do not fake the effect with quest
     * state or an unrelated HUD flag. */
    (void)j;
    return 0;
}
DWORD ForceQuestDialogUpdate(LPJASS j) {
    /* Refresh only an already-open quest dialog and preserve its selection.
     * The native is an update request, not an instruction to open the journal. */
    (void)j;
    FILTER_EDICTS(ent, ent->client && ent->client->connected) UI_RefreshQuests(ent);
    return 0;
}
