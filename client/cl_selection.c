#include "cl_input_local.h"
#include "cl_control_groups.h"

/* Numbered control groups stored on cl.groups. Config binds `group N`. */
#define BZ_GROUP_TAP_MS 500 // milliseconds; deliberate double-tap window; controls group camera recenter

uint32_t CL_SelectionLimit(void) { return MAX(1, MIN(MAX_SELECTED_ENTITIES, Cvar_Integer("cl_selection_limit", MAX_SELECTED_ENTITIES))); }

static void CL_ResetGroupTap(void) {
    cl.group_last = MAX_CONTROL_GROUPS;
    cl.group_last_ms = 0;
}

static bool CL_GroupCenter(uint32_t const *ids, uint32_t n, vec2_t *center) {
    double x = 0.0, y = 0.0;
    uint32_t valid = 0;

    if (!ids || !center) return false;
    n = MIN(n, CL_SelectionLimit());
    FOR_LOOP(i, n) {
        uint32_t const number = ids[i];
        entityState_t const *state;
        if (!number || number >= MAX_CLIENT_ENTITIES) continue;
        state = &cl.ents[number].current;
        if (!state->model || state->stats[ENT_HEALTH] == 0 ||
            (state->flags & EF_NOT_SELECTABLE)) continue;
        x += state->origin.x;
        y += state->origin.y;
        valid++;
    }
    if (!valid) return false;
    center->x = (float)(x / valid);
    center->y = (float)(y / valid);
    return true;
}

/* The configured capacity bounds local hints; the server reconciles legality through svc_set_selection. */
void CL_ApplySelection(uint32_t const *ids, uint32_t n) {
    char buffer[1024];
    n = MIN(n, CL_SelectionLimit());
    strlcpy(buffer, n ? "select" : "select 0", sizeof(buffer));
    FOR_LOOP(i, n) {
        size_t used = strlen(buffer);
        snprintf(buffer + used, sizeof(buffer) - used, " %d", ids[i]);
    }
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", buffer);
    cl.selection.num_selected = n;
    memcpy(cl.selection.entity_nums, ids, sizeof(uint32_t) * n);
}

static void CL_GroupAssign(uint32_t g) {
    uint32_t n = cl.selection.num_selected;
    n = MIN(n, CL_SelectionLimit());
    cl.groups[g].num_selected = n;
    memcpy(cl.groups[g].entity_nums, cl.selection.entity_nums, sizeof(uint32_t) * n);
    CL_ResetGroupTap();
}

static void CL_GroupAdd(uint32_t g) {
    uint32_t n = cl.selection.num_selected;
    n = MIN(n, CL_SelectionLimit());
    cl.groups[g].num_selected = CL_ControlGroupAppendUnique(
        cl.groups[g].entity_nums, cl.groups[g].num_selected, CL_SelectionLimit(),
        cl.selection.entity_nums, n);
    CL_ResetGroupTap();
}

static void CL_GroupRecall(uint32_t g) {
    uint32_t now;
    bool center_on_group;
    vec2_t center;

    if (cl.groups[g].num_selected == 0) {
        CL_ResetGroupTap();
        return;
    }
    now = cl.time;
    center_on_group = cl.group_last == g &&
        (uint32_t)(now - cl.group_last_ms) <= BZ_GROUP_TAP_MS;
    CL_ApplySelection(cl.groups[g].entity_nums, cl.groups[g].num_selected);
    if (Cvar_Integer("cl_group_focus", 1) && center_on_group && CL_GroupCenter(cl.groups[g].entity_nums, cl.groups[g].num_selected, &center))
        CL_SetCameraPosition(center);
    cl.group_last = g;
    cl.group_last_ms = now;
}

static void CL_Group_f(void) {
    static struct { cstring_t name; uint32_t op; } const verbs[] = {
        { "assign", 1 },
        { "add", 2 },
        { NULL, 0 },
    };
    cstring_t a1 = Cmd_Argv(1);
    uint32_t g, op = 0;

    if (!CL_GameplayInputReady() || CL_WindowModalActive()) return;
    if (Cmd_Argc() < 2) {
        fprintf(stderr, "group [assign|add] <0-9>\n");
        return;
    }
    for (uint32_t i = 0; verbs[i].name; i++) {
        if (!strcasecmp(a1, verbs[i].name)) {
            op = verbs[i].op;
            a1 = Cmd_Argv(2);
            break;
        }
    }
    g = (uint32_t)atoi(a1);
    if (!a1 || a1[0] < '0' || a1[0] > '9' || a1[1] || g >= MAX_CONTROL_GROUPS) {
        fprintf(stderr, "group: %s is not a group number (0-9)\n", a1 ? a1 : "");
        return;
    }
    if (op == 1) CL_GroupAssign(g);
    else if (op == 2) CL_GroupAdd(g);
    else CL_GroupRecall(g);
}

void CL_ControlGroupsInit(void) {
    Cmd_AddCommand("group", CL_Group_f);
}


#ifdef BZ_TESTS
#include "shared/test.h"
TEST(client_input, single_selection_groups_recall_one_target_without_moving_camera) {
    uint8_t old_sel[sizeof(cl.selection)], old_group[sizeof(cl.groups[0])], data[256];
    sizeBuf_t old_msg = cls.netchan.message;
    menuExport_t old_menu = menu;
    uint32_t old_last = cl.group_last, old_ms = cl.group_last_ms, ids[] = { 7, 8 };
    char command[64];
    int limit = Cvar_Integer("cl_selection_limit", 64), focus = Cvar_Integer("cl_group_focus", 1);
    memcpy(old_sel, &cl.selection, sizeof(old_sel));
    memcpy(old_group, &cl.groups[0], sizeof(old_group));
    Cvar_Set("cl_selection_limit", "1");
    Cvar_Set("cl_group_focus", "0");
    SZ_Init(&cls.netchan.message, data, sizeof(data));
    CL_ApplySelection(ids, 2);
    T_EQ(cl.selection.num_selected, 1); T_EQ(cl.selection.entity_nums[0], 7);
    CL_GroupAssign(0);
    CL_ApplySelection(ids + 1, 1);
    CL_GroupAdd(0);
    T_EQ(cl.groups[0].num_selected, 1); T_EQ(cl.groups[0].entity_nums[0], 7);
    SZ_Clear(&cls.netchan.message);
    CL_GroupRecall(0);
    CL_GroupRecall(0);
    cls.netchan.message.readcount = 0;
    FOR_LOOP(i, 2) {
        T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
        MSG_ReadString(&cls.netchan.message, command);
        T_STREQ(command, "select 7");
    }
    T_EQ(cls.netchan.message.readcount, cls.netchan.message.cursize);
    Cvar_SetValue("cl_selection_limit", limit);
    Cvar_SetValue("cl_group_focus", focus);
    menu = old_menu;
    cls.netchan.message = old_msg;
    memcpy(&cl.selection, old_sel, sizeof(old_sel));
    memcpy(&cl.groups[0], old_group, sizeof(old_group));
    cl.group_last = old_last; cl.group_last_ms = old_ms;
}
#endif
