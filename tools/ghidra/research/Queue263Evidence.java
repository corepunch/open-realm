// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import java.nio.file.Files;
import java.nio.file.Path;
public class Queue263Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong original");
  String[][] notes={
   {"6f67abe0","Payoff263 ORDER-02.3: incoming order20.80 clears before subtype notification. Target order with unresolved58/5c sends POINT, not TARGET, with saved48/50. Native Shift Attack after RemoveUnit activates Attack Move; queued Repair after removed building sends point852024 then synchronously activates following Move851986 at the same scene tick87 (nested dispatch depth2). New head is visible to issued callbacks before any task. Unit_GetInternalTaskHead is sampled again after callbacks; do not discard outer work merely because an instant Stop retired its public head."},
   {"6f693490","Payoff263: genuine Shift Attack/Repair and subsequent Move append with order20.4, preserving executing head/task while count1->2->3 and tail advances. No issued notification during either append. Empty input queue and idle completion retain invalid head/tail and count0. Failed Repair head and canceled pending packets receive deferred release; canonical identities become unresolved after clock drain. Larger control-bit producer inventory remains ORDER-02.2."},
   {"6f673e80","Payoff263: public Stop at scene tick80 cancels two genuine Shift successors before activation; count3->1, tail becomes preserved current head, and both pending canonical identities receive deferred release. Their issued callbacks never fire. Stop replacement subsequently retires old head and its own transient head, leaving count0 and invalid head/tail."},
   {"6f67c230","Payoff263: unresolved queued target emits point event with retained cursor1031.130/511.368 at activation, for both Attack and Repair. Removed Attack constructs point locomotion; removed Repair cannot construct work and activates the next point Move synchronously. Notification shape is independent of subsequent execution success."},
   {"6f67d2b0","Payoff263: recovered Unit_FireIssuedTargetOrderEvent, thiscall ECX Unit, stack4 COrderTarget, RET4. Samples unit8024d/player80228 subscriptions before delivery, creates retained target-order event payload with resolved target and point, fires player before unit, balances local event/order references. Valid queued Attack target emits target event only at activation; unresolved target is classified by67abe0 into point producer67c230 instead. Descriptive name, not an original symbol."},
   {"6f0557b0","Payoff263: watched Shift packets in all five queue scenarios reach deferred release, including failed Repair and canceled unactivated packets. Release scheduling is not an immediate destructor: queued UI request ownership can retain a payload reference after canonical-wrapper retirement."},
   {"6f04c2c0","Payoff263: complete watched queue scenarios observe canonical retirement and one reference decrement for each original admitted user packet. Invalid registry resolution after return proves wrapper release; residual UI request payload references are recorded explicitly, not mistaken for a zero-reference factory reclaim."}
  };
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(row[0].equals("6f67d2b0")){if(!f.getName().equals("FUN_6f67d2b0")&&!f.getName().equals("Unit_FireIssuedTargetOrderEvent"))throw new Exception("name conflict");f.setName("Unit_FireIssuedTargetOrderEvent",SourceType.USER_DEFINED);}
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[1]))f.setComment(old+"\n"+row[1]);
  }
  var target=getFunctionAt(toAddr("6f67d2b0"));
  var manager=currentProgram.getDataTypeManager();
  DataType unit=new PointerDataType(manager.getDataType("/WarcraftIII/Pathfinding127/WC3UnitOrdersPrefix"),4);
  DataType order=new PointerDataType(manager.getDataType("/WarcraftIII/Pathfinding127/WC3OrderTargetPrefix"),4);
  target.setCallingConvention("__thiscall");target.setReturnType(VoidDataType.dataType,SourceType.USER_DEFINED);
  target.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,
   new ParameterImpl("unit",unit,new VariableStorage(currentProgram,currentProgram.getRegister("ECX")),currentProgram),
   new ParameterImpl("order",order,new VariableStorage(currentProgram,4,4),currentProgram));
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f67abe0","6f693490","6f673e80","6f67c230","6f67d2b0","6f0557b0","6f04c2c0"}){
   var f=getFunctionAt(toAddr(a));out.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
