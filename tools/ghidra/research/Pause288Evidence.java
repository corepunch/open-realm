// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;

public class Pause288Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f605aa0","ModalAbility_CancelAndPublishOff","Payoff288: retained temporary off protocol integrated through a generic engine packet owner. Six frozen public scenes (inactive/active Defend, active Move, runtime-added Defend, detach before and during suspension) preserve stance and movement speed until packet activation, including cold saves. Selector is procedure-owned, notification precedes retention, activation re-notifies before applying inverse without public native revalidation. Per-ability ordered disable counters and reentrant compositions remain outside this six-scene projection."},
 {"6f688790","Unit_DispatchTransientOrder","Payoff288:32 guarded original immediate-packet cases certify notify before dead gate, flag20.2, positive signed Unit198 plus dispatch_suspended0 retention, direct execution/fallback otherwise, and balanced RET8. Helpers only record ABI and stores. Engine packets retain rawcode/issuer/order independently of live membership; ordinary moving units carry no packet allocation."},
 {"6f691c70","Unit_InsertTransientOrder","Payoff288: retained forced commands prepend after the suspended head ahead of previous successors; their eventual activation precedes the retained point Move. Current original source confirms existing-count>500 rejection. Broader mixed user/transient queue capacity and reentrant ownership require their own full compositions; six-scene engine projection is not a universal queue proof."},
 {"6f48b4c0","Unit_AttachAbilityAndInheritAvailability","Payoff288 investigation: writes old Unit1dc/1e0 into new ability24/28, then passes new ability in stack argument and Unit1dc in ECX to224310 at48b58c..48b58f: prepend membership. Existing contributions1b8/1c4 are inherited after attachment. Temporary-disable callbacks must remain separate from detach. Ordered full membership/counter walk remains next integration scope."},
 {"6f4076e0","Ability_AddDisableContribution","Payoff288: generic counted record commits3c before availability refresh c4; order40 is incremented only after the refresh returns. For concrete Defend vtable b47bd0, e0=407470 calls2f4(0), whose42bca0 implementation is a no-op; c4=605aa0 emits the off packet. Two fresh read-only1043-event repeats preserve the exact original815-event chronology and285 public markers/control. Engine ref-held records survive removal during the trailing40 store. Full ordered ability membership/command gating remains open."},
 {"6f40bb00","Ability_RemoveDisableContribution","Payoff288: immediate Unpause releases the ability contribution before physical resume: subtract40 first, then3c. Concrete Defend e4=40b850 calls2f4(1), which is a no-op. The retained off packet still waits for physical release.81 original counted/retirement cases execute these unmodified bodies with ABI/store-only refresh endpoints."},
 {"6f3f4c70","Ability_RetireAvailability","Payoff288:4076e0(orders0,delta1) adds a counted non-order disable contribution. Zero-to-positive invokes e0 then c4. Regardless of the transition, retirement clears20.2 then invokes c4 AGAIN at3f4c9c (return3f4ca2), cancels request58, sets20.40. Thus scene11 has two off producers at3c1; scene12 already had3c1, retirement reaches3c2 and emits only its unconditional c4. Never encode this as a duplicated arbitrary notification."},
 {"6f3f5080","Ability_DetachAvailability","Payoff288: retire3f4c70 before virtual2f4(0). Generic concrete owner records preserve the zero-to-positive producer separately from the unconditional detach refresh, and are unlinked by owner identity after callbacks."},
 {"6f5c56a0","Defend_DetachAndRemoveModifiers","Payoff288:3f5080 retirement/refresh precedes conditional Defend_RemoveModifiers when20.80 is set; then cancel request84 and unregister events. Fresh Frida callbacks show3f4c70->4076e0->605aa0 at40771e for zero transition and the unconditional605aa0 at3f4ca2. The retire-enter caller may be an Interceptor trampoline, which is explicitly not treated as a retail address. Broader active-removal public lifetime remains separate."},
 {"6f407470","Ability_DisableStateChanged","Payoff288: virtual2f4(0); concrete Defend42bca0 is a no-op. Off notification is its c4 availability refresh, not this e0 callback."},
 {"6f40b850","Ability_EnableStateChanged","Payoff288: virtual2f4(1); concrete Defend42bca0 is a no-op. Retained modal off is independent of immediate ability enable."},
 {"6f3fa480","Ability_CountedAvailabilityEnabled","Payoff288: concrete Defend virtualcc returns1, enabling signed counted3c/40 operations."},
 {"6f42bca0","Ability_IgnoreAvailabilityToggle","Payoff288: concrete Defend virtual2f4 has no state effects; original body balances its argument. This does not suppress c4 off notifications."}
 };
 public void run()throws Exception {
  if(getScriptArgs().length!=1 || !currentProgram.getName().equals("game.dll"))throw new Exception("game.dll and fresh output required");
  var output=Path.of(getScriptArgs()[0]);if(Files.exists(output))throw new Exception("exists");
  var dtm=currentProgram.getDataTypeManager();var path=new CategoryPath("/WarcraftIII/Pathfinding");
  var ability=new StructureDataType(path,"WC3TransientOrderLinkPrefix288",0x34,dtm);
  ability.replaceAtOffset(0,new Pointer32DataType(),4,"vtable",null);
  ability.replaceAtOffset(0x20,UnsignedIntegerDataType.dataType,4,"flags",null);
  ability.replaceAtOffset(0x2c,UnsignedIntegerDataType.dataType,4,"next_slot",null);
  ability.replaceAtOffset(0x30,UnsignedIntegerDataType.dataType,4,"next_generation",null);
  var stored=dtm.addDataType(ability,DataTypeConflictHandler.REPLACE_HANDLER);
  var counters=new StructureDataType(path,"WC3AbilityAvailabilityPrefix288",0x44,dtm);
  counters.replaceAtOffset(0,new Pointer32DataType(),4,"vtable",null);
  counters.replaceAtOffset(0x20,UnsignedIntegerDataType.dataType,4,"flags",null);
  counters.replaceAtOffset(0x30,new Pointer32DataType(),4,"unit",null);
  counters.replaceAtOffset(0x3c,IntegerDataType.dataType,4,"disable_depth",null);
  counters.replaceAtOffset(0x40,IntegerDataType.dataType,4,"order_disable_depth",null);
  dtm.addDataType(counters,DataTypeConflictHandler.REPLACE_HANDLER);
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
