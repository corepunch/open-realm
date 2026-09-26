#!/usr/bin/env python3
"""Generate C source files from serverdata CSV files.

The CSV files (produced by extract_server_data.py) are the source of truth.
This script converts them to static C arrays compiled into the game binary.

Usage:
  python3 games/world-of-warcraft/serverdata/gen_serverdata_c.py [--output-dir build/generated]
"""

import csv
import argparse
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))
DATA_DIR = os.path.join(ROOT_DIR, "games", "world-of-warcraft", "serverdata")
DEFAULT_OUTPUT_DIR = os.path.join(ROOT_DIR, "build", "generated")


def read_csv(filename):
    """Read CSV, skipping comment lines starting with #."""
    path = os.path.join(DATA_DIR, filename)
    rows = []
    with open(path, 'r', encoding='utf-8') as f:
        lines = [l for l in f if not l.startswith('#')]
    reader = csv.reader(lines)
    for row in reader:
        if row:
            rows.append(row)
    return rows


def read_named_csv(filename):
    """Read a generated CSV whose commented first row is its schema."""
    path = os.path.join(DATA_DIR, filename)
    with open(path, 'r', encoding='utf-8') as f:
        header = next(f).removeprefix('#').strip()
        return list(csv.DictReader(f, fieldnames=next(csv.reader([header]))))


def escape_c(s):
    return s.replace('\\', '\\\\').replace('??', '?\\?').replace('"', '\\"').replace('\n', '\\n')


def c_string(value):
    return 'NULL' if value == r'\N' else f'"{escape_c(value)}"'


def c_float(value):
    if value == r'\N': return '0.0f'
    out = f'{float(value):.9g}'
    return out + ('.0f' if '.' not in out and 'e' not in out.lower() else 'f')


def c_long(value):
    return '-1' if value == r'\N' else value


# ============================================================================
# PLAYERCREATEINFO (race/class spawn points)
# ============================================================================

def gen_playercreateinfo(output_dir):
    """Generate build/generated/g_playercreateinfo.c from playercreateinfo.csv."""
    rows = read_csv("playercreateinfo.csv")
    print(f"  playercreateinfo.csv: {len(rows)} rows")

    lines = ['#include "g_wow_local.h"', '#include "common/wow_character_utils.h"', '']
    lines.append('static const wowSpawnPoint_t wow_spawn_points[] = {')
    for r in rows:
        race, cls, map_id, x, y, z, facing = r[:7]
        lines.append(f'    {{ {race}, {cls}, {map_id}, {c_float(x)}, {c_float(y)}, {c_float(z)}, {c_float(facing)} }},')
    lines.extend([
        '};', '',
        'uint32_t Wow_SpawnCount(void) { return sizeof(wow_spawn_points) / sizeof(wow_spawn_points[0]); }', '',
        'wowSpawnPoint_t const * Wow_SpawnByIndex(uint32_t index) {',
        '    return index < Wow_SpawnCount() ? &wow_spawn_points[index] : NULL;',
        '}', '',
        '/* Race string + class -> spawn index on the currently loaded map, or ~0u. */',
        'uint32_t Wow_SelectSpawnPoint(cstring_t race, uint32_t class_id) {',
        '    uint32_t race_num = Wow_RaceNumber(race);',
        '    uint32_t map_id = CM_WowGetMapId();',
        '    if (!race_num) return ~0u;',
        '    FOR_LOOP(i, Wow_SpawnCount())',
        '        if (wow_spawn_points[i].race == race_num && wow_spawn_points[i].cls == class_id &&',
        '            wow_spawn_points[i].map == map_id)',
        '            return i;',
        '    return ~0u;',
        '}', '',
        '/* Race string + class -> map id, or ~0u. */',
        'uint32_t Wow_PlayerCreateMap(cstring_t race, uint32_t class_id) {',
        '    uint32_t race_num = Wow_RaceNumber(race);',
        '    if (!race_num) return ~0u;',
        '    FOR_LOOP(i, Wow_SpawnCount())',
        '        if (wow_spawn_points[i].race == race_num && wow_spawn_points[i].cls == class_id)',
        '            return wow_spawn_points[i].map;',
        '    return ~0u;',
        '}', '',
        'vector3_t const * Wow_GetSpawnPos(uint32_t idx) {',
        '    static vector3_t v;',
        '    if (idx >= Wow_SpawnCount()) return NULL;',
        '    v.x = wow_spawn_points[idx].x;',
        '    v.y = wow_spawn_points[idx].y;',
        '    v.z = wow_spawn_points[idx].z;',
        '    return &v;',
        '}', '',
        '/* True if playercreateinfo has ANY entry for map_id (any race/class). */',
        '/* Used to distinguish a dungeon map (no entries at all) from a race mismatch. */',
        'bool Wow_HasSpawnForMap(uint32_t map_id) {',
        '    FOR_LOOP(i, Wow_SpawnCount())',
        '        if (wow_spawn_points[i].map == map_id) return true;',
        '    return false;',
        '}', '',
    ])

    path = os.path.join(output_dir, "g_playercreateinfo.c")
    with open(path, 'w') as f:
        f.write('\n'.join(lines))
    print(f"  Wrote {path}")


# ============================================================================
# WEAPONS
# ============================================================================

def gen_weapons(output_dir):
    """Generate build/generated/g_weapons.c from weapons.csv."""
    rows = read_csv("weapons.csv")
    print(f"  weapons.csv: {len(rows)} rows")

    lines = ['#include "g_wow_local.h"', '#include <stdio.h>', '#include <stdlib.h>', '']
    lines.append(f'static const wowWeapon_t wow_weapons[] = {{')
    for r in rows:
        entry, name, subclass, displayid, inv_type, ilvl, rlvl, dmin, dmax, dtype, delay = r[:11]
        lines.append(f'    {{ {entry}, "{escape_c(name)}", {subclass}, {displayid}, '
                     f'{inv_type}, {ilvl}, {rlvl}, {float(dmin):.1f}f, {float(dmax):.1f}f, {dtype}, {delay} }},')
    lines.append('};')
    lines.append('')
    lines.append('wowWeapon_t const * Wow_WeaponByEntry(uint32_t entry) {')
    lines.append('    FOR_LOOP(i, sizeof(wow_weapons) / sizeof(wow_weapons[0]))')
    lines.append('        if (wow_weapons[i].entry == entry) return &wow_weapons[i];')
    lines.append('    return NULL;')
    lines.append('}')
    lines.append('')
    lines.append('uint32_t Wow_RollWeaponDamage(uint32_t entry) {')
    lines.append('    static uint32_t last_missing = ~0u;')
    lines.append('    wowWeapon_t const * weapon = Wow_WeaponByEntry(entry);')
    lines.append('    uint32_t min_damage, max_damage;')
    lines.append('')
    lines.append('    if (!entry) return 1;')
    lines.append('    if (!weapon) {')
    lines.append('        if (last_missing != entry) {')
    lines.append('            fprintf(stderr, "WoW: missing server weapon data for item %u; using 1 damage\\n", entry);')
    lines.append('            last_missing = entry;')
    lines.append('        }')
    lines.append('        return 1;')
    lines.append('    }')
    lines.append('    min_damage = (uint32_t)weapon->damage_min;')
    lines.append('    max_damage = (uint32_t)weapon->damage_max;')
    lines.append('    return min_damage + (max_damage > min_damage ? (uint32_t)(rand() % (max_damage - min_damage + 1)) : 0);')
    lines.append('}')
    lines.append('')

    path = os.path.join(output_dir, "g_weapons.c")
    with open(path, 'w') as f:
        f.write('\n'.join(lines))
    print(f"  Wrote {path}")


# ============================================================================
# QUESTS
# ============================================================================

def gen_quests(output_dir):
    """Generate build/generated/g_quests.c from quests.csv and quest_spawns.csv."""
    quest_rows = read_csv("quests.csv")
    spawn_rows = read_csv("quest_spawns.csv")
    print(f"  quests.csv: {len(quest_rows)} rows")
    print(f"  quest_spawns.csv: {len(spawn_rows)} rows")

    givers = [r for r in spawn_rows if r[0] == 'giver']
    objectives = [r for r in spawn_rows if r[0] == 'objective']

    giver_groups = {}
    for i, g in enumerate(givers):
        _, _, entry, _, x, y, z, _ = g
        giver_groups.setdefault((int(entry), float(x), float(y), float(z)), []).append(i)
    groups = list(giver_groups.values())
    row_group = {row: group for group, rows in enumerate(groups) for row in rows}

    lines = ['#include "g_wow_local.h"', '']

    # Quest givers
    lines.append(f'static const wowQuestGiver_t wow_quest_givers[] = {{')
    for g in givers:
        _, qid, entry, dispid, x, y, z, o = g
        lines.append(f'    {{ {qid}, {entry}, {dispid}, {{ {float(x):.4f}f, {float(y):.3f}f, {float(z):.4f}f }}, {float(o):.5f}f }},')
    lines.append('};')
    lines.append('')
    lines.append('typedef struct { uint32_t first, count; } wowQuestGiverGroup_t;')
    lines.append('typedef struct { uint32_t quest_id; vector2_t position; uint32_t group; } wowQuestGiverLookup_t;')
    lines.append('static const uint32_t wow_quest_giver_group_rows[] = {')
    for rows in groups:
        lines.append('    ' + ', '.join(str(i) for i in rows) + ',')
    lines.append('};')
    lines.append('static const wowQuestGiverGroup_t wow_quest_giver_groups[] = {')
    first = 0
    for rows in groups:
        lines.append(f'    {{ {first}, {len(rows)} }},')
        first += len(rows)
    lines.append('};')
    lines.append('static const wowQuestGiverLookup_t wow_quest_giver_lookup[] = {')
    lookup = sorted(((int(g[1]), float(g[4]), float(g[5]), row_group[i]) for i, g in enumerate(givers)))
    for qid, x, y, group in lookup:
        lines.append(f'    {{ {qid}, {{ {x:.4f}f, {y:.3f}f }}, {group} }},')
    lines.append('};')
    lines.append('')

    # Quest objectives
    lines.append(f'static const wowQuestObjective_t wow_quest_objectives[] = {{')
    for o in objectives:
        _, qid, _, _, x, y, _, _ = o
        lines.append(f'    {{ {qid}, {{ {float(x):.1f}f, {float(y):.1f}f }} }},')
    lines.append('};')
    lines.append('')

    # Quest details
    lines.append(f'static const wowQuestDetail_t wow_quest_details[] = {{')
    for r in quest_rows:
        qid = r[0]
        title = escape_c(r[1])
        desc = escape_c(r[2])
        obj_text = escape_c(r[3])
        reward_text = escape_c(r[4])
        xp, gold = r[5], r[6]
        item1, item2 = r[7], r[8]
        prev_quest, min_level = r[9], r[10]
        kills = [(r[11], r[12]), (r[13], r[14]), (r[15], r[16]), (r[17], r[18])]
        kill_count = sum(1 for d, c in kills if int(c) > 0)

        kill_init = "{{0}}"
        if kill_count > 0:
            parts = [f'{{{d}, {c}}}' for d, c in kills if int(c) > 0]
            kill_init = '{ ' + ', '.join(parts) + ' }'

        lines.append('    {')
        lines.append(f'        {qid}, "{title}",')
        lines.append(f'        "{desc}",')
        lines.append(f'        "{obj_text}",')
        lines.append(f'        "{reward_text}",')
        lines.append(f'        {xp}, {gold}, {{{item1}, {item2}}}, {prev_quest}, {min_level}, {kill_init}, {kill_count}')
        lines.append('    },')
    lines.append('};')
    lines.append('')

    # Accessor functions
    lines.append('uint32_t Wow_QuestGiverCount(void) { return sizeof(wow_quest_givers) / sizeof(wow_quest_givers[0]); }')
    lines.append('wowQuestGiver_t const * Wow_QuestGiver(uint32_t index) {')
    lines.append('    return index < Wow_QuestGiverCount() ? &wow_quest_givers[index] : NULL;')
    lines.append('}')
    lines.append('uint32_t Wow_QuestGiverGroup(uint32_t quest_id, vector2_t const * position) {')
    lines.append('    uint32_t lo = 0, hi = sizeof(wow_quest_giver_lookup) / sizeof(wow_quest_giver_lookup[0]);')
    lines.append('    if (!position) return WOW_QUEST_GIVER_GROUP_NONE;')
    lines.append('    while (lo < hi) {')
    lines.append('        uint32_t mid = lo + (hi - lo) / 2;')
    lines.append('        wowQuestGiverLookup_t const *cur = &wow_quest_giver_lookup[mid];')
    lines.append('        if (cur->quest_id < quest_id || (cur->quest_id == quest_id && (cur->position.x < position->x ||')
    lines.append('            (cur->position.x == position->x && cur->position.y < position->y)))) lo = mid + 1;')
    lines.append('        else hi = mid;')
    lines.append('    }')
    lines.append('    if (lo < sizeof(wow_quest_giver_lookup) / sizeof(wow_quest_giver_lookup[0])) {')
    lines.append('        wowQuestGiverLookup_t const *cur = &wow_quest_giver_lookup[lo];')
    lines.append('        if (cur->quest_id == quest_id && !memcmp(&cur->position, position, sizeof(*position))) return cur->group;')
    lines.append('    }')
    lines.append('    return WOW_QUEST_GIVER_GROUP_NONE;')
    lines.append('}')
    lines.append('uint32_t Wow_QuestGiverGroupCount(uint32_t group) {')
    lines.append('    return group < sizeof(wow_quest_giver_groups) / sizeof(wow_quest_giver_groups[0]) ? wow_quest_giver_groups[group].count : 0;')
    lines.append('}')
    lines.append('wowQuestGiver_t const * Wow_QuestGiverInGroup(uint32_t group, uint32_t index) {')
    lines.append('    wowQuestGiverGroup_t const *cur;')
    lines.append('    if (group >= sizeof(wow_quest_giver_groups) / sizeof(wow_quest_giver_groups[0])) return NULL;')
    lines.append('    cur = &wow_quest_giver_groups[group];')
    lines.append('    return index < cur->count ? &wow_quest_givers[wow_quest_giver_group_rows[cur->first + index]] : NULL;')
    lines.append('}')
    lines.append('uint32_t Wow_QuestObjectiveCount(void) {')
    lines.append('    return sizeof(wow_quest_objectives) / sizeof(wow_quest_objectives[0]);')
    lines.append('}')
    lines.append('wowQuestObjective_t const * Wow_QuestObjective(uint32_t index) {')
    lines.append('    return index < Wow_QuestObjectiveCount() ? &wow_quest_objectives[index] : NULL;')
    lines.append('}')
    lines.append('wowQuestDetail_t const * Wow_QuestDetail(uint32_t quest_id) {')
    lines.append('    FOR_LOOP(i, sizeof(wow_quest_details) / sizeof(wow_quest_details[0]))')
    lines.append('        if (wow_quest_details[i].quest_id == quest_id) return &wow_quest_details[i];')
    lines.append('    return NULL;')
    lines.append('}')
    lines.append('')

    path = os.path.join(output_dir, "g_quests.c")
    with open(path, 'w') as f:
        f.write('\n'.join(lines))
    print(f"  Wrote {path}")


# ============================================================================
# CREATURES
# ============================================================================

def gen_creatures(output_dir):
    """Generate the complete typed creature_template/model table."""
    rows = read_named_csv("creatures.csv")
    creatures = []
    by_entry = {}

    for row in rows:
        entry = int(row['entry'])
        creature = by_entry.get(entry)
        if not creature:
            creature = {'row': row, 'models': []}
            by_entry[entry] = creature
            creatures.append(creature)
        if row['model_idx'] != r'\N': creature['models'].append(row)
    print(f"  creatures.csv: {len(rows)} joined rows, {len(creatures)} creatures")

    lines = ['#include "g_wow_local.h"', '']
    lines.append('static const wowCreature_t wow_creatures[] = {')
    for creature in creatures:
        r = creature['row']
        models = sorted(creature['models'], key=lambda model: int(model['model_idx']))
        model_values = []
        for model in models:
            index = int(model['model_idx'])
            if index >= 4: raise ValueError(f"creature {r['entry']} has invalid model index {index}")
            model_values.append(f'[{index}] = {{ {index}, {model["display_id"]}, {c_float(model["display_scale"])}, '
                                f'{c_float(model["model_probability"])}, {c_long(model["model_verified_build"])} }}')
        lines.append(
            f'    {{ {r["entry"]}, {{{r["difficulty_entry_1"]}, {r["difficulty_entry_2"]}, '
            f'{r["difficulty_entry_3"]}}}, {{{r["KillCredit1"]}, {r["KillCredit2"]}}}, '
            f'{c_string(r["name"])}, {c_string(r["subname"])}, '
            f'{c_string(r["IconName"])}, {r["gossip_menu_id"]}, '
            f'{r["minlevel"]}, {r["maxlevel"]}, {r["exp"]}, {r["faction"]}, {r["npcflag"]}, '
            f'{c_float(r["speed_walk"])}, {c_float(r["speed_run"])}, {c_float(r["speed_swim"])}, '
            f'{c_float(r["speed_flight"])}, {c_float(r["detection_range"])}, '
            f'{r["rank"]}, {r["dmgschool"]}, {c_float(r["DamageModifier"])}, {r["BaseAttackTime"]}, '
            f'{r["RangeAttackTime"]}, {c_float(r["BaseVariance"])}, {c_float(r["RangeVariance"])}, '
            f'{r["unit_class"]}, {r["unit_flags"]}, {r["unit_flags2"]}, {r["dynamicflags"]}, '
            f'{r["family"]}, {r["type"]}, {r["type_flags"]}, {r["lootid"]}, {r["pickpocketloot"]}, '
            f'{r["skinloot"]}, {r["PetSpellDataId"]}, {r["VehicleId"]}, {r["mingold"]}, {r["maxgold"]}, '
            f'{c_string(r["AIName"])}, {r["MovementType"]}, '
            f'{c_float(r["HoverHeight"])}, {c_float(r["HealthModifier"])}, {c_float(r["ManaModifier"])}, '
            f'{c_float(r["ArmorModifier"])}, {c_float(r["ExperienceModifier"])}, {r["RacialLeader"]}, '
            f'{r["movementId"]}, {r["RegenHealth"]}, {r["CreatureImmunitiesId"]}, {r["flags_extra"]}, '
            f'{c_string(r["ScriptName"])}, {c_long(r["VerifiedBuild"])}, '
            f'{{ {", ".join(model_values)} }}, {len(models)} }},'
        )
    lines.extend([
        '};', '',
        'uint32_t Wow_CreatureCount(void) { return sizeof(wow_creatures) / sizeof(wow_creatures[0]); }', '',
        '/* The generated table is entry-sorted, so server lookups stay logarithmic. */',
        'wowCreature_t const * Wow_CreatureByEntry(uint32_t entry) {',
        '    uint32_t first = 0, count = Wow_CreatureCount();',
        '    while (count) {',
        '        uint32_t step = count / 2, index = first + step;',
        '        if (wow_creatures[index].entry < entry) { first = index + 1; count -= step + 1; }',
        '        else count = step;',
        '    }',
        '    return first < Wow_CreatureCount() && wow_creatures[first].entry == entry ? &wow_creatures[first] : NULL;',
        '}', '',
    ])
    path = os.path.join(output_dir, "g_creatures.c")
    with open(path, 'w') as f: f.write('\n'.join(lines))
    print(f"  Wrote {path}")


# ============================================================================
# AREATRIGGER_TELEPORT (cross-map teleport destinations)
# ============================================================================

def gen_areatrigger_teleport(output_dir):
    """Generate build/generated/g_areatrigger_teleport.c from areatrigger_teleport.csv."""
    rows = read_csv("areatrigger_teleport.csv")
    print(f"  areatrigger_teleport.csv: {len(rows)} rows")

    lines = ['#include "g_wow_local.h"', '#include <string.h>', '']
    lines.append('static const wowAreatrigTeleport_t wow_areatrig_teleports[] = {')
    for r in rows:
        id_, name, target_map, tx, ty, tz, to_ = r[:7]
        lines.append(f'    {{ {id_}, "{escape_c(name)}", {target_map}, '
                     f'{c_float(tx)}, {c_float(ty)}, {c_float(tz)}, {c_float(to_)} }},')
    lines.extend([
        '};', '',
        'uint32_t Wow_AreaTrigTeleportCount(void) {',
        '    return sizeof(wow_areatrig_teleports) / sizeof(wow_areatrig_teleports[0]);',
        '}', '',
        'wowAreatrigTeleport_t const * Wow_AreaTrigTeleportById(uint32_t id) {',
        '    FOR_LOOP(i, Wow_AreaTrigTeleportCount())',
        '        if (wow_areatrig_teleports[i].id == id) return &wow_areatrig_teleports[i];',
        '    return NULL;',
        '}', '',
        '/* Case-insensitive substring match — warp command entry point. */',
        'wowAreatrigTeleport_t const * Wow_AreaTrigTeleportByName(cstring_t query) {',
        '    uint32_t qlen = (uint32_t)strlen(query), j, nlen;',
        '    FOR_LOOP(i, Wow_AreaTrigTeleportCount()) {',
        '        nlen = (uint32_t)strlen(wow_areatrig_teleports[i].name);',
        '        if (nlen < qlen) continue;',
        '        for (j = 0; j <= nlen - qlen; j++)',
        '            if (!strncasecmp(wow_areatrig_teleports[i].name + j, query, qlen))',
        '                return &wow_areatrig_teleports[i];',
        '    }',
        '    return NULL;',
        '}', '',
        '/* First areatrigger_teleport entry whose target_map matches map_id.',
        ' * Used as spawn-position fallback when loading a dungeon directly. */',
        'wowAreatrigTeleport_t const * Wow_AreaTrigSpawnForMap(uint32_t map_id) {',
        '    FOR_LOOP(i, Wow_AreaTrigTeleportCount())',
        '        if (wow_areatrig_teleports[i].target_map == map_id)',
        '            return &wow_areatrig_teleports[i];',
        '    return NULL;',
        '}', '',
    ])

    path = os.path.join(output_dir, "g_areatrigger_teleport.c")
    with open(path, 'w') as f:
        f.write('\n'.join(lines))
    print(f"  Wrote {path}")


def main():
    parser = argparse.ArgumentParser(description="Generate compiled WoW server-data tables")
    parser.add_argument('--only', choices=['playercreateinfo', 'weapons', 'quests', 'creatures',
                                           'areatrigger_teleport'])
    parser.add_argument('--output-dir', default=DEFAULT_OUTPUT_DIR)
    args = parser.parse_args()
    os.makedirs(args.output_dir, exist_ok=True)
    print("=== Generating C from CSV ===\n")
    if args.only in (None, 'playercreateinfo'): gen_playercreateinfo(args.output_dir)
    if args.only in (None, 'weapons'): gen_weapons(args.output_dir)
    if args.only in (None, 'quests'): gen_quests(args.output_dir)
    if args.only in (None, 'creatures'): gen_creatures(args.output_dir)
    if args.only in (None, 'areatrigger_teleport'): gen_areatrigger_teleport(args.output_dir)
    print("\n=== Done ===")


if __name__ == '__main__':
    main()
