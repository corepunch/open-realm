// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Availability260Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f6510b0","Unit_BroadcastTargetAvailable","Payoff260 GROUP-03.2: ECX source Unit. Stack CEvent code d01a5, source+c. Queries source world pose then WidgetQuery_DispatchPointCircle with radius d70744, flags4bd0000 and callback6504a0. Original0153f0 initializes radius from integer1100. This is an availability transition, not an ordinary movement-commit notification. Public owner changes call at6991a2 after d01a2 subscribers; Blink calls after clearing800000. Two read-only owner-transfer repeats and unhooked control retain ten identical public markers; twenty Attack notifications and five49e351 exemption entries per repeat."},
 {"6f6504a0","WidgetQuery_NotifyTargetAvailable","Payoff260: fastcall ECX candidate widget, EDX event pointer. Checks d01a5 subscription through072190, dispatches candidate vtable10 with stack event if subscribed, returns1 regardless. Complete body6504a0..6504c3; plain RET. Do not treat callback result as acquisition acceptance."},
 {"6f0153f0","Unit_InitTargetAvailabilityRadius","Payoff260: MOV EDX,44c; MOV ECX,d70744; tail070d80 integer-to-scalar constructor. Fixed radius1100 world units, confirmed live44898000. Not a per-frame maximum-acquisition-range scan."},
 {"6f49e130","Attack_HandleTargetAvailable","Payoff260: thiscall ECX Attack, stack4 CEvent pointer, RET4. Requires ability20.4000, unit20.200 and signed prevention220<=0; absent-world bit1 requires resolvable task identity2bc/2c0. Rejects self, identity relation42cf60, dead/absent target and allied ownership. First05b580 range at49e256 uses selector0 (committed centers), then movable/weapon and minimum-range guards and4959f0 full admission. Success calls49bc40 at49e34c BEFORE49e3a0 target ranking; ranking rejection does not undo exemption. Public idle and Attack Move owner-transition recipients acquire synchronously; explicit Move has4000 clear; paused actor is absent from query. Full49d680 ranking and broader captain domains are not certified by this bounded producer integration."},
 {"6f498e50","Attack_GetAvailabilityRange","Payoff260: retained target identity6c/70 can select adjusted range; otherwise244 acquisition range, with maximum-enabled-weapon clamp for movement-ineligible owner. Public actors retain300; source units set to0 publicly have500 at delivery, so zero was not an isolation mechanism. No engine expectation treats that probe setup as zero acquisition."},
 {"6f49e3a0","Attack_ShouldReplaceAcquiredTarget","Payoff260: compares candidate/current target49d680 packed priorities, then05b1c0 distances for ties; same target returns0. Called only AFTER availability speed-cap exemption begins. The complete priority model remains outside Payoff260 engine scope."},
 {"6f4959f0","Attack_CanAcquireAvailableTarget","Payoff260: deeper admission includes499640/498a60, target life/state,496b80 weapon mask, immobile configured range, visibility66fdd0 mode4 and neutral-specific home/policy guards. Live successful returns precede49e351; static edges are recorded, not all public guard domains claimed."},
 {"6f698ce0","Unit_SetPlayerOwner","Payoff260:69919d synchronously delivers d01a2;6991a2 broadcasts d01a5;6991ab disables AI mode and6991b6 reenrolls owner pool. Same-owner early return does not broadcast. Engine availability publication follows retained-target owner subscribers and precedes AI reenrollment."},
 {"6f49b750","Attack_SetAcquisitionNotifications","Payoff260: toggles owner agent subscriptions including d01a5. Availability is a subscribed owner event, not a target-radius movement watcher. Engine producer visits indexed nearby units only on an explicit availability transition."},
 };
 public void run()throws Exception {
  if(getScriptArgs().length!=1)throw new Exception("new output required");
  Path p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  var dm=currentProgram.getDataTypeManager();var category=new CategoryPath("/WarcraftIII/Pathfinding127");
  var t=(Structure)dm.getDataType(category,"WC3TargetAvailableEvent");
  if(t==null)t=(Structure)dm.addDataType(new StructureDataType(category,"WC3TargetAvailableEvent",16),DataTypeConflictHandler.DEFAULT_HANDLER);
  var ptr=new PointerDataType(VoidDataType.dataType,4);
  t.replaceAtOffset(0,ptr,4,"vtable","CEvent");t.replaceAtOffset(4,UnsignedIntegerDataType.dataType,4,"refs","Stack event");
  t.replaceAtOffset(8,UnsignedIntegerDataType.dataType,4,"code","d01a5");t.replaceAtOffset(12,ptr,4,"source","Published available Unit");
  for(String address:new String[]{"6f6504a0","6f0153f0"})if(getFunctionAt(toAddr(address))==null){
   if(getFunctionContaining(toAddr(address))!=null)throw new Exception("Overlapping function "+address);
   if(createFunction(toAddr(address),null)==null)throw new Exception("Cannot create "+address);
  }
  var f=getFunctionAt(toAddr("6f49e130"));f.setCallingConvention("__thiscall");f.setReturnType(VoidDataType.dataType,SourceType.USER_DEFINED);
  f.replaceParameters(Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,true,SourceType.USER_DEFINED,new ParameterImpl("event",new PointerDataType(t,4),currentProgram));
  StringBuilder out=new StringBuilder();out.append("TYPE ").append(t).append('\n');
  for(String[] row:NOTES){f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("COMMENT ").append(f.getComment()).append('\n');out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
