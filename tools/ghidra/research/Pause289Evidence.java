// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;

public class Pause289Evidence extends GhidraScript {
 static final String[][] NOTES={ {"6f48c8c0", "Unit_AdjustAbilityDisableContributions", "Payoff289: Unit1c4 is incremented before ordered visits. Capture next identity before each callback; membership additions prepend and inherit contributions separately. Read-only Payoff288 scene9 validates Move0 while Defend3c1/400; scene14 inverse visits unvisited Attack/Move producing temporary -1/-1, then the interrupted add restores0/0. Original48c8c0/4076e0/40bb00/69dd60 execute composed walks in30 guarded cases. Earlier note naming the first Footman owner Stop was inaccurate: original vtable b28330 classgetter55c520 returns AInv. Live Footman order is Inventory,Defend,Attack,Move. Engine lazy ordered availability records retain this observation boundary; non-script contribution families/44 and full morph membership remain separate."},
 {"6f6977a0", "Unit_DisableForScriptedPause", "Payoff289:48e460(1) precedes48c8c0(orders1,add1,filter0,filter0). Ability availability is counted per attached owner; global1c4 is not itself a native admission gate."},
 {"6f6977f0", "Unit_EnableAfterScriptedPause", "Payoff289:48c8c0(orders1,add0,filter0,filter0) precedes48e460(0). Nested inverse inside c4 traverses the complete list before the interrupted add reaches its next owner."},
 {"6f69c490", "Unit_OnSuspendBeginEvent", "Payoff289: begin event publishes physical flag100000 and acquires separation suppression before scripted availability callbacks. A Move accepted in a callback remains a user successor beneath this physical suspension, without immediate point notification."},
 {"6f48b4c0", "Unit_AttachAbilityAndInheritAvailability", "Payoff289: prepend and inherited Unit1c4 contribution explain runtime-added modal owners ahead of authored owners. Compiled registry intrinsic order mirrors final Move/Attack construction order; authored/runtime lists are reverse attachment order. Wider special attachment producers and morph reconstruction remain open."},
 {"6f69dd60", "CUnit_ValidatePointOrder", "Payoff289: native Point Move succeeds during an earlier modal c4 refresh because concrete Move3c/40/44 remain zero. Same validation rejects after Move has been visited.30 composed original ordered walks run this body inside the c4 observation boundary; terminal validator answers and identity payload lookup are supplied inputs, not a replacement counter walk."},
 {"6f55c520", "Inventory_GetAbilityClass", "Payoff289: original classgetter returns AInv (0x41496e76). Read-only list first owner vtable b28330 is Inventory, not Stop; earlier unverified descriptive note on48c8c0 is corrected by this instruction witness."},
 {"6f5e66f0", "Defend_GetAbilityClass", "Payoff289: original classgetter returns Adef (0x41646566); observed vtable b47bd0 owner."},
 {"6f4992a0", "Attack_GetAbilityClass", "Payoff289: original classgetter returns Aatk (0x4161746b); observed vtable adb5a4 owner."},
 {"6f5fcd40", "Move_GetAbilityClass", "Payoff289: original classgetter returns Amov (0x416d6f76); observed vtable b62794 owner."} };;
 public void run()throws Exception {
  if(getScriptArgs().length!=1 || !currentProgram.getName().equals("game.dll"))throw new Exception("game.dll and fresh output required");
  var output=Path.of(getScriptArgs()[0]);if(Files.exists(output))throw new Exception("exists");
  var dtm=currentProgram.getDataTypeManager();var path=new CategoryPath("/WarcraftIII/Pathfinding");
  var ability=new StructureDataType(path,"WC3AbilityOrderedLinkPrefix289",0x44,dtm);
  ability.replaceAtOffset(0,new Pointer32DataType(),4,"vtable",null);
  ability.replaceAtOffset(0x20,UnsignedIntegerDataType.dataType,4,"flags",null);
  ability.replaceAtOffset(0x30,new Pointer32DataType(),4,"unit",null);
  ability.replaceAtOffset(0x24,UnsignedIntegerDataType.dataType,4,"next_slot",null);
  ability.replaceAtOffset(0x28,UnsignedIntegerDataType.dataType,4,"next_generation",null);
  var stored=dtm.addDataType(ability,DataTypeConflictHandler.REPLACE_HANDLER);
  var counters=new StructureDataType(path,"WC3AbilityAvailabilityPrefix289",0x44,dtm);
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
