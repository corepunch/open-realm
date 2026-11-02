#!/usr/bin/env python3
"""Public ClearCaptainTargets after a hostile-source engagement."""
import importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('captain358_clear_builder',HERE/'captain358_engagement_make_map.py')
wrapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(wrapper)
builder=wrapper.builder
builder.TASK='captain358_clear'
builder.VARIANTS={
 'clear':dict(members=6,end=360,schedule=[(100,('cmd',6)),(160,('native',wrapper.CONTACT.replace('-1936.0,-1424.0','-2100.0,-1700.0'))),(220,('cmd',7))]),
 'pointclear':dict(members=6,end=180,schedule=[(100,('cmd',6)),(110,('cmd',7))]),
}
if __name__=='__main__':builder.main()
