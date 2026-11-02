#!/usr/bin/env python3
"""Build the public TeleportCaptain probe without replacing existing maps."""
import importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('captain_builder',HERE/'GROUP-03.4.7.3_make_map.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
b.HERE=HERE;b.TASK='teleport290'
b.VARIANTS={'teleport':dict(members=6,end=200,schedule=[(20,('cmd',1)),(40,('cmd',2)),(50,('cmd',3)),(70,('cmd',4)),(80,('cmd',4)),(100,('cmd',5)),(115,('cmd',6)),(130,('remove',0,5)),(140,('cmd',7)),(150,('cmd',1)),(160,('cmd',8))])}
if __name__=='__main__':b.main()
