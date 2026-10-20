// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class Bridge265Evidence extends GhidraScript {
 static final String[][] NOTES={
  {"6f056090","Agent_TranslateSpatialMessage","Payoff265 ORDER-04.3: thiscall ECX receiver Unit, stack4 low-level tag, stack8 packet, RET8. Short-circuit requires globald3c82c and host8, canonical selfc/10, resolved wrapperc==2b61676c and wrapper20==0. Pending release rejects before event construction even while identity still resolves.132 unmodified057590->056090 cases cover both signed-slot domains, ref1/3, arrival and both blocked tags, all guard failures, unknown tags and absent payload; rejected cases change no supplied object/registry/packet byte and dispatch no event. Valid cases execute original071dc0/071e00, pin refs+1 then restore exactly. Guard states supplied explicitly; public ability to fabricate invalid identities not inferred."},
  {"6f057590","AgentPayload_ForwardSpatialMessage","Payoff265: thiscall ECX canonical wrapper, stack4 spatial packet, RET4. Accepted packet tags forward through payload54's ORIGINAL virtual18 to056090. Null payload or unaccepted tag returns before receiver. Complete132-case guard matrix uses unmodified vtables and records full dispatcher entry/pin without replacing calls."},
  {"6f055800","Agent_EmitArrivalEvent","Payoff265: original arrival63702661 builds40190065 with receiver as sender and dispatches via original CUnit virtual14. Both slot-sign domains produce same event. Nonempty supplied event table with empty bucket executes reference pin/unpin, not an early null-table shortcut."},
  {"6f055820","Agent_EmitBlockedMoveEvent","Payoff265: both6370266f and63702670 build40190066, original virtual14 dispatcher balances ref1/3 in both canonical domains. No callback is delivered when056090 host/identity/type/pending-release guard fails."},
  {"6f054530","PathRegistry_ResolveCanonical","Payoff265:132 composed spatial bridge cases assert resolution independently before delivery. Positive slots+c/bound1c and negative slots+2c/bound3c select by high bit; slot1 requires bound2. Stale generation, free marker, outside bound and ffffffff identity return0 without changing either domain. Host/type/release guards are independent of successful resolution."}
 };
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong original");
  if(getScriptArgs().length!=1)throw new Exception("fresh output required");Path p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  var dm=currentProgram.getDataTypeManager();var unit=new PointerDataType(dm.getDataType("/WarcraftIII/Pathfinding127/WC3UnitOrdersPrefix"),4);
  var wrapper=new PointerDataType(dm.getDataType("/WarcraftIII/Pathfinding127/WC3AgentWrapperPrefix"),4);var packet=new PointerDataType(VoidDataType.dataType,4);
  var f=getFunctionAt(toAddr("6f056090"));f.setCallingConvention("__thiscall");f.setReturnType(VoidDataType.dataType,SourceType.USER_DEFINED);
  f.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,
   new ParameterImpl("receiver",unit,new VariableStorage(currentProgram,currentProgram.getRegister("ECX")),currentProgram),
   new ParameterImpl("tag",UnsignedIntegerDataType.dataType,new VariableStorage(currentProgram,4,4),currentProgram),
   new ParameterImpl("packet",packet,new VariableStorage(currentProgram,8,4),currentProgram));
  f=getFunctionAt(toAddr("6f057590"));f.setCallingConvention("__thiscall");f.setReturnType(VoidDataType.dataType,SourceType.USER_DEFINED);
  f.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,
   new ParameterImpl("wrapper",wrapper,new VariableStorage(currentProgram,currentProgram.getRegister("ECX")),currentProgram),
   new ParameterImpl("packet",packet,new VariableStorage(currentProgram,4,4),currentProgram));
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES){f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append("PROTOTYPE ").append(f.getPrototypeString(true,true)).append('\n').append("COMMENT ").append(f.getComment()).append('\n');
   for(var param:f.getParameters())out.append("PARAM ").append(param.getName()).append(' ').append(param.getVariableStorage()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
