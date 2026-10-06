#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"
#include "jass/jass.h"
#include <stdio.h>

void setup_test_world(void);
bool run_test_jass(cstring_t script);

TEST(wc3_dialog, create_add_clear_destroy_retains_stable_handles) {
    jassDialog_t *dialog;
    jassDialogButton_t *first, *second;
    setup_test_world();
    dialog = G_JassDialogCreate();
    T_NOT_NULL(dialog);
    T_EQ(G_JassDialogById(dialog->id), dialog);
    first = G_JassDialogAddButton(dialog, "Long Route", 0, false, false);
    second = G_JassDialogAddButton(dialog, "Short Route", 0, false, false);
    T_NOT_NULL(first); T_NOT_NULL(second);
    T_NE(first, second);
    T_EQ(first->dialog_id, dialog->id);
    T_STREQ(first->text, "Long Route");
    G_JassDialogClear(dialog);
    T_NULL(G_JassDialogButton(first));
    T_NULL(G_JassDialogButton(second));
    T_EQ(G_JassDialog(dialog), dialog);
    second = G_JassDialogAddButton(dialog, "New Route", 0, false, false);
    T_NOT_NULL(second);
    T_NE(first, second);
    G_JassDialogDestroy(dialog);
    T_NULL(G_JassDialog(dialog));
    T_NULL(G_JassDialogButton(second));
}

TEST(wc3_dialog, save_round_trip_restores_dialog_handles_and_visibility) {
    cstring_t filename = "/tmp/openrealm-wc3-jass-dialog-save.bin";
    jassDialog_t *dialog;
    jassDialogButton_t *button;
    setup_test_world();
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    dialog = G_JassDialogCreate();
    T_NOT_NULL(dialog);
    snprintf(dialog->message, sizeof(dialog->message), "Choose a route");
    button = G_JassDialogAddButton(dialog, "Long", 0, false, false);
    T_NOT_NULL(button);
    dialog->visible_players = 1;
    T_ASSERT(WriteGame(filename));
    G_JassDialogClear(dialog);
    T_NULL(G_JassDialogButtonById(1));
    T_ASSERT(ReadGame(filename));
    dialog = G_JassDialogById(1);
    button = G_JassDialogButtonById(1);
    T_NOT_NULL(dialog); T_NOT_NULL(button);
    T_STREQ(dialog->message, "Choose a route");
    T_STREQ(button->text, "Long");
    T_EQ(dialog->visible_players, 1u);
    remove(filename);
}

TEST(wc3_dialog, jass_natives_allocate_real_dialog_and_button_handles) {
    setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local dialog d = DialogCreate()\n"
        "  local button a\n"
        "  local button b\n"
        "  call BJassAssert(d != null, \"dialog must be allocated\")\n"
        "  call DialogSetMessage(d, \"Choose route\")\n"
        "  set a = DialogAddButton(d, \"Long\", 0)\n"
        "  set b = DialogAddButton(d, \"Short\", 0)\n"
        "  call BJassAssert(a != null and b != null and a != b, \"distinct buttons\")\n"
        "  call DialogClear(d)\n"
        "  call DialogDestroy(d)\n"
        "endfunction\n"));
    T_EQ(level.dialog_count, 1u);
    T_EQ(level.dialog_button_count, 2u);
    T_NULL(G_JassDialogById(1));
}

TEST(wc3_dialog, clicking_publishes_only_matching_registrations) {
    jassDialog_t *dialog;
    jassDialogButton_t *long_button, *short_button;
    uint32_t n;
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        " dialog route = null\n"
        " button longer = null\n"
        " button shorter = null\n"
        " integer selection = 0\n"
        " integer dialogEvents = 0\n"
        " integer buttonEvents = 0\n"
        "endglobals\n"
        "function onDialog takes nothing returns nothing\n"
        " set dialogEvents = dialogEvents + 1\n"
        " if GetClickedDialog() == route and GetTriggerPlayer() == Player(0) then\n"
        "  if GetClickedButton() == longer then\n"
        "   set selection = 1\n"
        "  elseif GetClickedButton() == shorter then\n"
        "   set selection = 2\n"
        "  endif\n"
        " endif\n"
        "endfunction\n"
        "function onButton takes nothing returns nothing\n"
        " set buttonEvents = buttonEvents + 1\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        " local trigger t = CreateTrigger()\n"
        " local trigger b = CreateTrigger()\n"
        " set route = DialogCreate()\n"
        " set longer = DialogAddButton(route, \"Long\", 0)\n"
        " set shorter = DialogAddButton(route, \"Short\", 0)\n"
        " call TriggerRegisterDialogEvent(t, route)\n"
        " call TriggerAddAction(t, function onDialog)\n"
        " call TriggerRegisterDialogButtonEvent(b, longer)\n"
        " call TriggerAddAction(b, function onButton)\n"
        "endfunction\n"
        "function verify takes nothing returns nothing\n"
        " call BJassAssert(selection == 2, \"short branch\")\n"
        " call BJassAssert(dialogEvents == 1, \"one dialog event\")\n"
        " call BJassAssert(buttonEvents == 0, \"long-only button handler\")\n"
        "endfunction\n"));
    dialog = G_JassDialogById(1);
    long_button = G_JassDialogButtonById(1);
    short_button = G_JassDialogButtonById(2);
    T_NOT_NULL(dialog); T_NOT_NULL(long_button); T_NOT_NULL(short_button);
    game.clients[0].ps.number = 0;
    g_edicts[0].client = game.clients;
    dialog->visible_players = 1;
    n = level.events.write;
    G_JassDialogClick(g_edicts, dialog->id, short_button->id);
    T_EQ(level.events.write, n + 1);
    G_JassDialogClick(g_edicts, dialog->id, long_button->id); /* duplicate rejected */
    T_EQ(level.events.write, n + 1);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_dialog, click_visibility_uses_player_number_not_client_slot) {
    jassDialog_t *dialog;
    jassDialogButton_t *button;
    uint32_t const player_number = 5;
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        " dialog route = null\n"
        " button choice = null\n"
        " integer selection = 0\n"
        "endglobals\n"
        "function onDialog takes nothing returns nothing\n"
        " if GetClickedDialog() == route and GetClickedButton() == choice and GetTriggerPlayer() == Player(5) then\n"
        "  set selection = 1\n"
        " endif\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        " local trigger t = CreateTrigger()\n"
        " set route = DialogCreate()\n"
        " set choice = DialogAddButton(route, \"Continue\", 0)\n"
        " call TriggerRegisterDialogEvent(t, route)\n"
        " call TriggerAddAction(t, function onDialog)\n"
        "endfunction\n"
        "function verify takes nothing returns nothing\n"
        " call BJassAssert(selection == 1, \"player number is preserved\")\n"
        "endfunction\n"));
    dialog = G_JassDialogById(1);
    button = G_JassDialogButtonById(1);
    T_NOT_NULL(dialog); T_NOT_NULL(button);
    game.clients[0].ps.number = player_number;
    g_edicts[0].client = game.clients;
    dialog->visible_players = 1u << player_number;
    G_JassDialogClick(g_edicts, dialog->id, button->id);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}
#endif
