// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Work254Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f497330","Attack_ArmGuardReturn","Payoff254: OR Attack20.400; cancel control280; read authored Misc.GuardReturnTime and arm nonperiodic d01ae on the same primary control. Live neutral Footman first arrival1316 rearms5s; subsequent public Move1324 does not cancel it; expiry1482 reissues the retained guard point. Four read-only runs preserve all618 follower and624 target raw rows from Payoff252 unchanged."},
 {"6f49d220","Attack_StartGuardEvaluation","Payoff254: unless forced, test495610; within guard range arms periodic2s d01ad using startup00b570/d6bf60. Outside arms497330. Distinct guard polling and return phases share control280; no path-owner counter heuristic."},
 {"6f495610","Attack_IsOutsideGuardRange","Payoff254: mobile path compares predicted source against captured guard X2ac/Y2b4 and authored GuardDistance27c through05b440 (source collision included). Structure-disabled Move branch instead compares heading; that branch is separately scoped. Live range600, anchor1568/288; periodic checks1157/1224 inside,1290 outside."},
 {"6f499030","Attack_OnGuardPoll","Payoff254: d01ad checks495610 and only arms497330 when outside. Periodic2s polls continue when inside. Live first outside1290 arms5s; natural point arrival1316 replaces that deadline before its expiry."},
 {"6f497fc0","Attack_OnGuardReturn","Payoff254: d01ae creates Move d0012 at retained2ac/2b4, replacement admission680320 mode1/dispatch1; then appends immediate d0013, whose498ff0 consumer requests SleepAlways when attached. Stop/invalidate commits pending physical pose and zeros velocity between owners. Timer rearm uses the request deadline, while predicted-pose/range and Stop use the current primary source-clock quantum; conflating these clocks overshoots the retained position words. Original target first changes1483; do not normalize the duplicate old-owner visit or overwrite the retained trajectory fixture. Early neutral-passive global gates and structure-disabled branch remain separate."},
 {"6f49e880","Attack_HandleGuardTask","Payoff254: d014a sets2000000 and clears400. Player<12 non-structure captures predicted current guard anchor and cancels control280; neutral/structure keeps anchor and calls49d220(0). Point task precedes this task: arrival re-evaluates guard while the next point command leaves its timer live. Proven by read-only handler/arm/call-chain rows, including first arrival1316 and expiry1482."},
 {"6f497c50","Attack_AttachGuardPolicy","Payoff254: owner>=12 or structure separation category4 starts49d220(0) during Attack attachment; then retains owner-change subscription. Ordinary player units take the other guard-anchor policy. Initial neutral creation and d014a both arm2s in Work254."},
 {"6f49a330","Attack_CancelGuardEvaluation","Payoff254: cancel control280 and clear flags2000000/400. Do not equate every point-order replacement with this separate cleanup producer."},
 {"6f498ff0","Attack_HandleGuardSleepContinuation","Payoff254: d0013 continuation resolves optional Asla (SleepAlways) and calls524e00(1) only when attached. It is not the public Stop command; do not fake a queued Stop to model guard return."},
 {"6f00b570","Attack_InitializeGuardPollPeriod","Payoff254: startup writes software2 into d6bf60; live0608d0 input40000000. Periodic guard evaluation is independent of primary Move cadence and authored GuardReturnTime."}
 };
 public void run()throws Exception {
  if(getScriptArgs().length!=1)throw new Exception("new output required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  var dm=currentProgram.getDataTypeManager();
  var t=new StructureDataType(new CategoryPath("/WC3/Pathfinding"),"WC3AttackGuardPrefix",0x2b8);
  t.replaceAtOffset(0,PointerDataType.dataType,4,"vtable",null);
  t.replaceAtOffset(0x20,UnsignedIntegerDataType.dataType,4,"flags","400 returning;2000000 guard task evaluated.");
  t.replaceAtOffset(0x30,PointerDataType.dataType,4,"unit",null);
  t.replaceAtOffset(0x27c,FloatDataType.dataType,4,"guard_range","Authored Misc.GuardDistance captured by weapon binding.");
  t.replaceAtOffset(0x2ac,FloatDataType.dataType,4,"guard_x",null);
  t.replaceAtOffset(0x2b4,FloatDataType.dataType,4,"guard_y",null);
  dm.addDataType(t,DataTypeConflictHandler.DEFAULT_HANDLER);
  StringBuilder out=new StringBuilder();
  out.append("TYPE ").append(dm.getDataType("/WC3/Pathfinding/WC3AttackGuardPrefix")).append("\n");
  for(String[] row:NOTES){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("COMMENT ").append(f.getComment()).append("\n");
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
