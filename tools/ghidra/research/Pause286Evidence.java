// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;

public class Pause286Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f69dc80","CUnit_ValidateImmediateOrder","Payoff286 ORDER-02.2: thiscall unit, stack(command,player,ownerContext), RET0c. Core d0005 returns0; d0004 returns0 unless Unit5c death100 then DD. Other commands walk ability identities1dc/1e0, virtuala0 owner context then virtual264(command,player,3). A zero validator answer is overridden by positive ability40 -> DD, else positive3c/44 -> B9. Nonzero virtual answers bypass those counters. Live paused Hold returns DD, Stop0; ordinary admission must validate before replacing suspended work."},
 {"6f69dd60","CUnit_ValidatePointOrder","Payoff286: thiscall stack(command,player,ownerContext,x*,y*), RET14, virtual25c(command,player,x*,y*,3). Same signed disabled counters3c/40/44 aggregation as69dc80. Paused Move/Attack/Patrol return DD221 with no680320 admission; retained current task and successors are unchanged. Original body executed across controlled virtual answers/counters; ability-specific validator bodies remain separate owners."},
 {"6f69df70","CUnit_ValidateTargetOrder","Payoff286: ordinary target dispatcher207160 calls THIS validator, not69de10/69dec0. thiscall stack(command,player,ownerContext,widget), RET10; virtual260(command,player,widget,3), same signed disabled-counter aggregation. Paused target Move/Attack return DD; subsequent point fallback also DD. Initial observer used other widget validators and is retained only as rejected diagnostic evidence."},
 {"6f206b20","Jass_DispatchImmediateOrder","Payoff286: validates69dc80 before factory690930 and680320 replacement. Only0/DC pass; paused Stop is core-valid and retained, Hold fails DD before mutation. Shared680320 suspension branch alone does NOT imply ordinary paused orders are accepted."},
 {"6f206f00","Jass_DispatchPointOrder","Payoff286: native flags3b6a20 then69dd60; only0/DC reach user factory/admission. Repeated paused Move/Attack/Patrol rejection has no FIFO/public notification/task mutation, preserving suspended old Move. Two read-only native repeats555 events each agree with224-marker unhooked control."},
 {"6f207160","Jass_DispatchTargetOrder","Payoff286: target validation69df70, point fallback69dd60, then user admission only on0/DC. Paused target Move/Attack DD in both validators; no queued allocation/admission or issued event. Do not infer admission from positive-suppression680320 without tracing this producer."},
 {"6f680320","Unit_AdmitOrder","Payoff286: positive signed Unit198 holds execution; mode0 prepends, mode1 cancels successors673e80 then693490 appends. Live paused Stop cancels forced undefend and old Move, retaining pause851973 as head and Stop as sole successor. Events occur at delayed resume activation, not admission. Ordinary paused natives are rejected upstream by ability validators. Full Shift/control-bit producer inventory remainsORDER02.2."},
 {"6f697770","Unit_BeginScriptedPause","Payoff286: sets200000 then688d30 pushes suspension d0005. Live Footman Pause emits player/unit851973 then forced852056 undefend callbacks while current851973. Existing engine admission projection does not yet implement these pause-entry/modal callbacks; retained full capture documents remaining MAP04.2/ORDER01.13 scope."},
 {"6f688d30","Unit_PushSuspensionOrder","Payoff286: increments Unit54; zero-to-one constructs69bd80 d0005 and admits mode0, preserving old user order beneath the suspension. This producer has different admission/notification semantics from user Shift. Public pause head851973 is not a hardcoded Move replacement."},
 {"6f6977a0","Unit_DisableForScriptedPause","Payoff286:48e460(1), then48c8c0(unit,1,1,0,0). Unit1c4 increments and per-ability40 disables ordinary orders. Disabling Footman Defend emits forced undefend even when inactive; this inserts flags2 user order beneath pause. Do not substitute ability removal A_DISABLE wholesale for temporary counted suspension."},
 {"6f6977f0","Unit_EnableAfterScriptedPause","Payoff286:48c8c0(unit,1,0,0,0), then48e460(0), then caller678590 schedules suspension pop. Delayed Stop activation publishes both issued families at656,656 and retires to0. Rejected ordinary orders preserve old Move, which later reissues; full modal/resumed-Move notification integration remains open."},
 {"6f48c8c0","Unit_AdjustAbilityDisableContributions","Payoff286: four arguments select signed counted ability disable contributions; observed script pause1,1,0,0 and resume1,0,0,0 affect Unit1c4 then ability40 via4076e0. This is an authoritative ability availability producer before native validation, not a queue admission limit. Other contribution producers/ability virtual bypasses retain wider scope."}
 };
 public void run()throws Exception {
  if(getScriptArgs().length!=1 || !currentProgram.getName().equals("game.dll"))throw new Exception("game.dll and fresh output required");
  var output=Path.of(getScriptArgs()[0]);if(Files.exists(output))throw new Exception("exists");
  var dtm=currentProgram.getDataTypeManager();var path=new CategoryPath("/WarcraftIII/Pathfinding");
  var ability=new StructureDataType(path,"WC3AbilityValidationPrefix286",0x48,dtm);
  ability.replaceAtOffset(0,new Pointer32DataType(),4,"vtable",null);
  ability.replaceAtOffset(0x24,UnsignedIntegerDataType.dataType,4,"next_handle",null);
  ability.replaceAtOffset(0x28,UnsignedIntegerDataType.dataType,4,"next_generation",null);
  ability.replaceAtOffset(0x3c,IntegerDataType.dataType,4,"disable_first",null);
  ability.replaceAtOffset(0x40,IntegerDataType.dataType,4,"disable_orders",null);
  ability.replaceAtOffset(0x44,IntegerDataType.dataType,4,"disable_third",null);
  var stored=dtm.addDataType(ability,DataTypeConflictHandler.REPLACE_HANDLER);
  StringBuilder out=new StringBuilder("TYPE "+stored.getPathName()+" length="+stored.getLength()+"\n");
  for(var c:((Structure)stored).getDefinedComponents())out.append("FIELD ").append(c.getOffset()).append(' ').append(c.getFieldName()).append(' ').append(c.getDataType().getName()).append('\n');
  for(String[] row:NOTES){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("COMMENT ").append(f.getComment()).append('\n');
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(output,out);println("Saved "+output);
 }
}
