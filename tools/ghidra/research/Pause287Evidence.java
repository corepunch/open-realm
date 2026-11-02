// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;

public class Pause287Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f697770","Unit_BeginScriptedPause","Payoff287 MAP-04.2/ORDER-01.13: public5c.200000 is set before688d30 admission and its issued POINT851973 callbacks. It is not the physical100000 hold or per-ability disabled40. During these callbacks Move validates0 and may install an internal task, causing67abe0 to skip the outer pause constructor. Repeated PauseUnit(true) is idempotent. Two read-only815-event observations and a285-marker observer-free control agree across20 scenes; engine core projection covers10 scenes, modal contribution ownership remains separate."},
 {"6f688d30","Unit_PushSuspensionOrder","Payoff287: zero-to-one54 creates69bd80 core851973 with RuntimeZero x/y and admits mode0/dispatch1. Although its command is a suspension selector, public callbacks are POINT with0,0 and no target. Actual OrderId(\"stunned\") returns0; no public string alias should be invented. Nested Unpause during this notification clears200000 before the outer constructor; completed outer order retires to0 without ability disabling."},
 {"6f67abe0","Unit_DispatchUserOrderHead","Payoff287: script notification precedes the internal-task-head test for suspension admission and resumed retained Move alike. Nested Move installs a task and wins. Instant Stop removes its user head but leaves no task, so the original outer Move constructor resumes with public head0. Public pause flag by itself must not gate a replacement task when suspension construction was skipped. New repeated captures correct the earlier Payoff256 claim that resumed retained Move emits no issued events: it publishes both POINT families before constructing its fresh physical group."},
 {"6f69c490","Unit_OnSuspendBeginEvent","Payoff287: physical100000 is set and separation suppression acquired before scripted ability disabling6977a0. This stage runs only if no reentrant internal task won after issued callbacks. Ability-disable producers are ordered and independently counted; the core engine stage must not call A_DISABLE (ability removal) as a temporary-pause substitute."},
 {"6f6977c0","Unit_EndScriptedPause","Payoff287: if200000, enable ability contributions first when100000, pop54 next, then clear200000. Separation remains physically held through the delayed69c580 event. Nested Unpause in initial POINT callback has no100000 owner and no delayed resume. Nested Unpause inside forced modal-off can clear200000 while100000 retains the pending end request."},
 {"6f678590","Unit_PopSuspensionOrder","Payoff287: decrement54; only zero plus100000 schedules69c620 with actual Chaos_CommitDelay scalar cd53a4 and argument0. Existing direct two-primary-quantum engine deadline is retained. Public unpause and physical release must not be collapsed into one operation; raw captures retain the precise resume dispatch chronology."},
 {"6f69c580","Unit_OnSuspendEndEvent","Payoff287: refresh separation while100000 is still set, then clear100000 and release its independent separation contribution. Physical resumption precedes re-dispatch of retained user heads. Engine core regression cold-saves pending deadlines and checks the contribution is held before the second primary quantum and released at that boundary."},
 {"6f48c8c0","Unit_AdjustAbilityDisableContributions","Payoff287 broader retained evidence: increment Unit1c4 BEFORE ordered per-ability visits; next identity is captured before callbacks. Footman list Stop,Defend,Attack,Move means global1c4==1 does not imply unvisited Move40>0. During forced undefend callback Move validates0 and is retained by positive198, while Stop40 already1 and Defend counters are3c=1,40=0. Core pause projection does not certify this later modal/availability producer implementation."},
 {"6f4076e0","Ability_AddDisableContribution","Payoff287: virtualcc eligibility; increment3c first; zero-to-positive calls virtuale0 (Defend605aa0), clears ability20 bit2 and callsc4; only afterward increment40 when orders argument set. Nested callbacks therefore observe different per-ability availability stages. Do not substitute one global paused boolean or global1c4 validator guard."},
 {"6f40bb00","Ability_RemoveDisableContribution","Payoff287: inverse decrements40 BEFORE3c. Positive-to-nonpositive3c invokes virtuale4, may set ability20.2/c8 when Unit5c.4 permits, then401fa0 refresh. Availability retirement and ability detach are distinct producers; removed Defend can still have a retained forced-off packet. This broader evidence remains outside the10-scene core engine projection."},
 {"6f605aa0","ModalAbility_CancelAndPublishOff","Payoff287: cancel request, select virtual318 off command, create immediate user order, call688790 with dispatch_suspended0. It notifies even for inactive Defend. Temporary pause retains this packet beneath suspension; do not erase active stance early or call public native validation on its eventual execution. Runtime removal separately emits an off selector; captured scenes11/12 retain the distinct notification counts."},
 {"6f688790","Unit_DispatchTransientOrder","Payoff287: notify BEFORE dead gate. If alive, order20|2; positive Unit198 plus dispatch_suspended0 inserts via691c70(order,1,1), otherwise execute transient directly without replacing user head. A nested Move/Stop admitted by its notification can precede outer insertion; forced off is inserted ahead of that new successor. Broader modal owner/packet integration is retained for subsequent engine work, not claimed by core pause projection."},
 {"6f691c70","Unit_InsertTransientOrder","Payoff287: parameter3 nonzero inserts after current user head, ahead of existing successor. After transient notification callbacks, this positions forced undefend before nested Move/Stop. Empty internal queue can dispatch immediately; stored packet owner survives removal and re-notifies on resume activation. Existing head/tail identity relationships and complete native chronology are frozen."}
 };
 public void run()throws Exception {
  if(getScriptArgs().length!=1 || !currentProgram.getName().equals("game.dll"))throw new Exception("game.dll and fresh output required");
  var output=Path.of(getScriptArgs()[0]);if(Files.exists(output))throw new Exception("exists");
  var dtm=currentProgram.getDataTypeManager();var path=new CategoryPath("/WarcraftIII/Pathfinding");
  var ability=new StructureDataType(path,"WC3UnitSuspensionPrefix287",0x1d0,dtm);
  ability.replaceAtOffset(0,new Pointer32DataType(),4,"vtable",null);
  ability.replaceAtOffset(0x54,UnsignedIntegerDataType.dataType,4,"suspension_depth",null);
  ability.replaceAtOffset(0x5c,UnsignedIntegerDataType.dataType,4,"order_admission_flags",null);
  ability.replaceAtOffset(0x198,IntegerDataType.dataType,4,"execution_suppression",null);
  ability.replaceAtOffset(0x1b8,IntegerDataType.dataType,4,"disable_first",null);
  ability.replaceAtOffset(0x1c4,IntegerDataType.dataType,4,"disable_orders",null);
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
