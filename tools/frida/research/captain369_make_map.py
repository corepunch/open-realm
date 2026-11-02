#!/usr/bin/env python3
"""Public Captain target selection and immediate Clear cancellation."""
import importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('captain369_builder',HERE/'captain358_make_map.py')
wrapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(wrapper)
builder=wrapper.builder
builder.TASK='captain369'
SETUP='call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)\ncall SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)\ncall SetUnitPosition(udg_RshEnemy,1000.0,1000.0)\ncall RshMark("label=selection-enemy")'
builder.VARIANTS={
 'player':dict(members=6,end=220,schedule=[(90,('native',SETUP)),(100,('cmd',8)),(150,('cmd',7))]),
 'units':dict(members=6,end=220,schedule=[(90,('native',SETUP)),(100,('cmd',9)),(150,('cmd',7))]),
 'clear':dict(members=6,end=220,schedule=[(90,('native',SETUP)),(100,('cmd',10))]),
 'reachableplayer':dict(members=6,end=220,schedule=[(90,('native',SETUP.replace('1000.0,1000.0','-1936.0,-1424.0'))),(100,('cmd',8)),(150,('cmd',7))]),
 'reachableunits':dict(members=6,end=220,schedule=[(90,('native',SETUP.replace('1000.0,1000.0','-1936.0,-1424.0'))),(100,('cmd',9)),(150,('cmd',7))]),
}
if __name__=='__main__':builder.main()
