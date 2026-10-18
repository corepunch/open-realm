#include "cl_control_groups.h"
#include "client.h"

#include <string.h>
#include <stdio.h>

#define CL_GROUP_SAVE_MAGIC 0x31475243u /* CRG1 */

typedef struct {
    uint32_t magic;
    uint32_t group_count;
    uint32_t capacity;
    uint32_t counts[MAX_CONTROL_GROUPS];
    uint32_t ids[MAX_CONTROL_GROUPS][MAX_SELECTED_ENTITIES];
} clGroupSave_t;

/* The engine owns paths and names, not the game module. Never allow a
 * sidecar name to escape the normal manual-save namespace. */
static bool CL_GroupSavePath(cstring_t name, string_t path, size_t size) {
    PATHSTR save;
    if (!name || !*name || !strcmp(name, ".") || !strcmp(name, "..") ||
        strpbrk(name, "/\\:*?\"<>|")) return false;
    FS_SavePath(name, save, sizeof(save));
    if (!save[0] || strlen(save) + sizeof(".groups") > size) return false;
    snprintf(path, size, "%s.groups", save);
    return true;
}

bool CL_ControlGroupsSave(cstring_t name) {
    PATHSTR path, temporary;
    clGroupSave_t *saved;
    FILE *file;
    bool ok;
    size_t path_length;
    if (!CL_GroupSavePath(name, path, sizeof(path)) ||
        strlen(path) + sizeof(".tmp") > sizeof(temporary)) return false;
    path_length = strlen(path);
    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, ".tmp", sizeof(".tmp"));
    saved = MemAlloc(sizeof(*saved));
    memset(saved, 0, sizeof(*saved));
    saved->magic = CL_GROUP_SAVE_MAGIC;
    saved->group_count = MAX_CONTROL_GROUPS;
    saved->capacity = MAX_SELECTED_ENTITIES;
    FOR_LOOP(g, MAX_CONTROL_GROUPS) {
        uint32_t count = MIN(cl.groups[g].num_selected, MAX_SELECTED_ENTITIES);
        saved->counts[g] = count;
        memcpy(saved->ids[g], cl.groups[g].entity_nums, count * sizeof(uint32_t));
    }
    file = fopen(temporary, "wb");
    ok = file && fwrite(saved, sizeof(*saved), 1, file) == 1;
    if (file && fclose(file)) ok = false;
    if (ok && rename(temporary, path)) ok = false;
    if (!ok) remove(temporary);
    MemFree(saved);
    return ok;
}

void CL_ControlGroupsLoad(cstring_t name) {
    PATHSTR path;
    clGroupSave_t *saved;
    FILE *file;
    bool valid;
    /* Always start empty: a missing, old or damaged sidecar is not allowed
     * to carry control groups from the game that was loaded previously. */
    CL_ControlGroupsReset();
    if (!CL_GroupSavePath(name, path, sizeof(path))) return;
    file = fopen(path, "rb");
    if (!file) return;
    saved = MemAlloc(sizeof(*saved));
    valid = fread(saved, sizeof(*saved), 1, file) == 1 && fgetc(file) == EOF &&
        saved->magic == CL_GROUP_SAVE_MAGIC &&
        saved->group_count == MAX_CONTROL_GROUPS && saved->capacity == MAX_SELECTED_ENTITIES;
    if (!fclose(file) && valid) {
        FOR_LOOP(g, MAX_CONTROL_GROUPS) {
            uint32_t count = saved->counts[g];
            if (count > MAX_SELECTED_ENTITIES) { valid = false; break; }
            FOR_LOOP(i, count) if (!saved->ids[g][i] ||
                saved->ids[g][i] >= MAX_CLIENT_ENTITIES) { valid = false; break; }
            if (!valid) break;
        }
        if (valid) FOR_LOOP(g, MAX_CONTROL_GROUPS) {
            cl.groups[g].num_selected = saved->counts[g];
            memcpy(cl.groups[g].entity_nums, saved->ids[g],
                   saved->counts[g] * sizeof(uint32_t));
        }
    }
    MemFree(saved);
}


uint32_t CL_ControlGroupAppendUnique(uint32_t *group, uint32_t count, uint32_t capacity,
                                  uint32_t const *ids, uint32_t num_ids) {
    if (!group || capacity == 0) return 0;
    if (count > capacity) count = capacity;
    if (!ids) return count;

    FOR_LOOP(i, num_ids) {
        bool duplicate = false;
        FOR_LOOP(j, count) {
            if (group[j] == ids[i]) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && count < capacity) {
            group[count++] = ids[i];
        }
    }
    return count;
}

void CL_ControlGroupsReset(void) {
    memset(cl.groups, 0, sizeof(cl.groups));
    cl.group_last = MAX_CONTROL_GROUPS;
    cl.group_last_ms = 0;
}

#ifdef BZ_TESTS
#include "shared/test.h"

TEST(client_groups, save_load_roundtrip) {
    cstring_t slot = "bz-test-control-groups";
    PATHSTR path, old_home;
    uint8_t saved_groups[sizeof(cl.groups)];
    uint32_t last = cl.group_last, last_ms = cl.group_last_ms;
    FILE *file;
    memcpy(saved_groups, cl.groups, sizeof(saved_groups));
    strlcpy(old_home, FS_HomePath(), sizeof(old_home));
    /* ROC/TFT shards and concurrent worktrees must never share a real user save slot. */
    FS_SetHomeDirectory(Test_TempPath("control-groups-home"));
    CL_ControlGroupsReset();
    cl.groups[2].num_selected = 2;
    cl.groups[2].entity_nums[0] = 7;
    cl.groups[2].entity_nums[1] = 19;
    T_ASSERT(CL_ControlGroupsSave(slot));
    CL_ControlGroupsReset();
    T_EQ(cl.groups[2].num_selected, 0);
    CL_ControlGroupsLoad(slot);
    T_EQ(cl.groups[2].num_selected, 2);
    T_EQ(cl.groups[2].entity_nums[0], 7);
    T_EQ(cl.groups[2].entity_nums[1], 19);
    T_ASSERT(CL_GroupSavePath(slot, path, sizeof(path)));
    file = fopen(path, "wb");
    T_NOT_NULL(file);
    if (file) { fputs("bad", file); fclose(file); }
    CL_ControlGroupsLoad(slot);
    T_EQ(cl.groups[2].num_selected, 0);
    remove(path);
    cl.groups[2].num_selected = 1;
    CL_ControlGroupsLoad(slot);
    T_EQ(cl.groups[2].num_selected, 0);
    T_EQ(cl.group_last, MAX_CONTROL_GROUPS);
    T_ASSERT(!CL_ControlGroupsSave("../outside"));
    if (old_home[0]) FS_SetHomeDirectory(old_home);
    memcpy(cl.groups, saved_groups, sizeof(saved_groups));
    cl.group_last = last;
    cl.group_last_ms = last_ms;
}

#endif
