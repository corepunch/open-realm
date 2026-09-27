#!/usr/bin/env python3
"""Build an isolated retail cursor scene from a supplied Human01 terrain archive."""
import argparse
import struct
import subprocess
import tempfile
from pathlib import Path


def map_info(data, race):
    if struct.unpack_from('<I', data)[0] != 18:
        raise ValueError('base map must use the Human01 version-18 W3I schema')
    offset = 12
    for _ in range(4):
        offset = data.index(b'\0', offset) + 1
    offset += 48 + 8 + 4 + 1 + 4
    for _ in range(3):
        offset = data.index(b'\0', offset) + 1
    offset += 4
    for _ in range(3):
        offset = data.index(b'\0', offset) + 1
    result = data[:offset] + struct.pack('<I', 3)
    race_id = {'human': 1, 'orc': 2, 'undead': 3, 'nightelf': 4}[race]
    for i in range(3):
        result += struct.pack('<4I', i, 1 if i == 0 else 2, race_id, 1)
        result += f'Player {i}'.encode() + b'\0' + struct.pack('<2f2I', -2880, 1984, 0, 0)
    result += struct.pack('<I', 3)
    for i in range(3):
        result += struct.pack('<2I', 0, 1 << i) + f'Team {i}'.encode() + b'\0'
    return result + struct.pack('<3I', 0, 0, 0)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', type=Path, required=True, help='extracted Maps/Campaign/Human01.w3m')
    p.add_argument('--tool', type=Path, default=Path('build/bin/mpqtool'))
    p.add_argument('--race', choices=['human', 'orc', 'undead', 'nightelf'], default='human')
    p.add_argument('--cursor', help='optional CustomSkin Cursor model path')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    script = r'''
globals
    dialog auditDialog = null
endglobals
function AuditHideDialog takes nothing returns nothing
    call DialogDisplay(Player(0),auditDialog,false)
    call DestroyTimer(GetExpiredTimer())
endfunction
function AuditShowDialog takes nothing returns nothing
    call DialogDisplay(Player(0),auditDialog,true)
    call TimerStart(CreateTimer(),5,false,function AuditHideDialog)
endfunction
function AuditUnit takes player owner, integer kind, real x, real y returns nothing
    local unit u = CreateUnit(owner,kind,x,y,270)
    call PauseUnit(u,true)
    call SetUnitState(u,UNIT_STATE_LIFE,GetUnitState(u,UNIT_STATE_MAX_LIFE)*0.75)
    set u = null
endfunction
function main takes nothing returns nothing
    local unit hero
    local quest q = CreateQuest()
    local trigger t = CreateTrigger()
    call QuestSetTitle(q,"Cursor audit")
    call QuestSetDescription(q,"Exercise cursor restoration through the quest journal.")
    call QuestSetRequired(q,true)
    set auditDialog = DialogCreate()
    call DialogSetMessage(auditDialog,"Cursor audit dialog")
    call DialogAddButton(auditDialog,"Close",27)
    call TriggerRegisterPlayerChatEvent(t,Player(0),"-dialog",true)
    call TriggerAddAction(t,function AuditShowDialog)
    call SetCameraBounds(-4800,-4600,4000,4400,-4800,4400,4000,-4600)
    call SetDayNightModels("Environment\\DNC\\DNCLordaeron\\DNCLordaeronTerrain\\DNCLordaeronTerrain.mdl", "Environment\\DNC\\DNCLordaeron\\DNCLordaeronUnit\\DNCLordaeronUnit.mdl")
    call FogEnable(false)
    call FogMaskEnable(false)
    call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
    call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,true)
    call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,true)
    set hero = CreateUnit(Player(0),'Hpal',-2880,1984,270)
    call SetUnitAcquireRange(hero,0)
    call IssueImmediateOrder(hero,"holdposition")
    call SetHeroLevel(hero,10,false)
    call SelectHeroSkill(hero,'AHhb')
    call UnitAddItemById(hero,'ratf')
    call AuditUnit(Player(0),'hfoo',-3150,2200)
    call AuditUnit(Player(1),'hfoo',-2880,2300)
    call AuditUnit(Player(2),'hfoo',-2600,2200)
    call AuditUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nshe',-3200,1850)
    call AuditUnit(Player(0),'hpea',-2500,1850)
    call CreateItem('ratf',-2750,1800)
    call SetCameraPosition(-2880,1984)
    call SelectUnit(hero,true)
endfunction
function config takes nothing returns nothing
    call SetMapName("Cursor parity audit")
    call SetMapDescription("Isolated cursor rendering and interaction scene")
    call SetPlayers(3)
    call SetTeams(3)
    call SetGamePlacement(MAP_PLACEMENT_USE_MAP_SETTINGS)
'''
    for i in range(3):
        control = 'USER' if i == 0 else 'COMPUTER'
        script += f'''
    call DefineStartLocation({i},-2880,1984)
    call SetPlayerStartLocation(Player({i}),{i})
    call ForcePlayerStartLocation(Player({i}),{i})
    call SetPlayerColor(Player({i}),ConvertPlayerColor({i}))
    call SetPlayerRacePreference(Player({i}),RACE_PREF_{a.race.upper()})
    call SetPlayerRaceSelectable(Player({i}),false)
    call SetPlayerController(Player({i}),MAP_CONTROL_{control})
    call SetPlayerTeam(Player({i}),{i})
'''
    script += 'endfunction\n'
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='wc3-cursor-map-') as temp:
        root = Path(temp)
        members = subprocess.check_output([str(a.tool), '-mpq', str(a.base), 'ls']).decode().splitlines()
        command = [str(a.tool), '-mpq', str(a.output), 'pack']
        for i, member in enumerate(members):
            path = root / str(i)
            if member.lower() == 'war3map.j':
                path.write_text(script)
            else:
                data = subprocess.check_output([str(a.tool), '-mpq', str(a.base), 'cat', member])
                path.write_bytes(map_info(data, a.race) if member.lower() == 'war3map.w3i' else data)
            command.extend([str(path), member])
        if a.cursor:
            path = root / 'skin'
            path.write_text('[CustomSkin]\nCursor=' + a.cursor + '\n')
            command.extend([str(path), 'war3mapSkin.txt'])
        subprocess.run(command, check=True)
    # Retail's map launch path reads this 512-byte user header before the MPQ.
    header = b'HM3W' + struct.pack('<I', 0) + b'Cursor parity audit\0' + struct.pack('<II', 0x1c69, 3)
    a.output.write_bytes(header.ljust(512, b'\0') + a.output.read_bytes())


if __name__ == '__main__':
    main()
