#!/usr/bin/env python3
"""Public interleaved Move/Captain/Attack subscriptions on one target; no gameplay instrumentation."""
import importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('target363_parent',HERE/'captain358_engagement_make_map.py')
wrapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(wrapper)
builder=wrapper.builder
builder.TASK='target363'
def native(text):return ('native',text)
def follower(i,order):
 return ('call RshMark("label=subscribe-before role='+order+str(i)+'")\n'
  +"set udg_RshExtra[%d]=CreateUnit(Player(2),'hfoo',-3100.0,200.0,270.0)\n"%i
  +'call IssueTargetOrder(udg_RshExtra[%d],"%s",udg_RshEnemy)\n'%(i,order)
  +'call RshMark("label=subscribe-after role='+order+str(i)+'")')
common=[(100,('cmd',3)),(110,native(follower(0,'move'))),(120,native(wrapper.CONTACT)),
 (140,native(follower(1,'attack'))),(150,native(follower(2,'move')))]
builder.VARIANTS={
 'remove':dict(members=6,end=230,schedule=common+[(180,native('call RshMark("label=mixed-remove-before")\ncall RemoveUnit(udg_RshEnemy)\ncall RshMark("label=mixed-remove-after")'))]),
 'owner':dict(members=6,end=230,schedule=common+[(180,native('call RshMark("label=mixed-owner-before")\ncall SetUnitOwner(udg_RshEnemy,Player(2),false)\ncall RshMark("label=mixed-owner-after")'))]),
}
if __name__=='__main__':builder.main()
