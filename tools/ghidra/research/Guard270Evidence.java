// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Guard270Evidence extends GhidraScript {
 public static final String[][] NOTES={{"6f49e130", "Attack_HandleTargetAvailable", "Payoff270 GROUP-03.2:49e256 first checks committed configured acquisition range.49e277 then queries Unit66f030(1,0); when false49e2b7 requires predicted configured selected-weapon range max(runtime32,effective selected range). Both guards precede49bc40 exemption. A two-enabled-slot authored Footman with ground90/air500 and Amov removed distinguishes the max-enabled first guard from selected-weapon second guard. Zero authored speed does not remove the Move eligibility predicate. Wider notification/minimum-range/AI policies remain separately open."}, {"6f498e50", "Attack_GetAvailabilityRange", "Payoff270: ECX Attack, stack4 scalar output*, RET4, EAX output*. Ordinary invalid retained2bc/2c0 branch reads acquisition244;4985c0 returns maximum enabled effective slot range; if maximum is less and Unit66f030(1,0) rejects, clamp acquisition to that maximum. Public removed-Amov Footman yields90 instead of authored400. Dual-slot ground90/air500 retains400 but later selected ground guard rejects300-distance target. Retained-adjusted branch remains subject to prior setter reachability exclusion."}, {"6f498520", "Attack_GetCandidateEffectiveRange", "Payoff270: ECX Attack, stack4 scalar output*, stack8 candidate Unit*, RET8, EAX output*.496b80 selects compatible slot, then4984a0/4984b0 returns its effective range; unresolved slot writes runtime zero. Do not confuse maximum enabled range4985c0 with this target-specific range. Public authored ground90/air500 selects90 for ground candidate."}, {"6f66f030", "Unit_CanMoveWithQueryFlags", "Payoff270 ABI from all10 instructions: ECX Unit, stack4 check_disabled, stack8 check_sleep, RET8. Null Unit1ec Move returns0; otherwise tailcalls5fb040 preserving both arguments. No movement-speed read. Public UnitRemoveAbility Amov returns0; SetUnitMoveSpeed0 retains1. Prediction guard49e277 supplies1,0."}, {"6f5fb040", "Move_IsEnabledForQuery", "Payoff270: ECX Move, stack4 check_disabled, stack8 check_sleep, RET8. Positive7c rejects; nonzero first flag tests20.200.3c prevention is reduced for sleep and unit suspension flags/depth. No speed check: the public zero-speed setter retains Move eligibility. Full prevention/sleep compositions are not certified by that lane."}, {"6f4985c0", "Attack_GetMaximumEnabledRange", "Payoff270: maximum-enabled range contributes to acquisition clamp498e50 before notification49e130 predicted selected-weapon guard. Public two-slot authored ground90/air500 proves these are distinct queries; existing retained-adjusted branch reachability evidence is unchanged."}};
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong original");
  if(getScriptArgs().length!=1)throw new Exception("fresh export required");
  Path p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  var dm=currentProgram.getDataTypeManager();
  var unit=new ghidra.program.model.data.PointerDataType(dm.getDataType("/WarcraftIII/Pathfinding127/WC3UnitOrdersPrefix"),4);
  var attack=new ghidra.program.model.data.PointerDataType(dm.getDataType("/WarcraftIII/Pathfinding127/WC3AttackRangePrefix"),4);
  var move=new ghidra.program.model.data.PointerDataType(dm.getDataType("/WarcraftIII/Pathfinding127/WC3MoveSpeedPrefix"),4);
  var scalar=new ghidra.program.model.data.PointerDataType(dm.getDataType("/WarcraftIII/Pathfinding127/WC3PathScalar"),4);
  for(String address:new String[]{"6f66f030","6f5fb040","6f498520","6f498e50"}) {
   var f=getFunctionAt(toAddr(address));f.setCustomVariableStorage(false);f.setCallingConvention("__thiscall");
   boolean predicate=address.equals("6f66f030")||address.equals("6f5fb040");
   f.setReturnType(predicate ? ghidra.program.model.data.UnsignedIntegerDataType.dataType : scalar,SourceType.USER_DEFINED);
   var first=address.equals("6f66f030")?unit:address.equals("6f5fb040")?move:attack;
   var params=new java.util.ArrayList<ghidra.program.model.listing.Parameter>();
   params.add(new ghidra.program.model.listing.ParameterImpl("self",first,new ghidra.program.model.listing.VariableStorage(currentProgram,currentProgram.getRegister("ECX")),currentProgram));
   params.add(new ghidra.program.model.listing.ParameterImpl(predicate?"check_disabled":"output",predicate?ghidra.program.model.data.UnsignedIntegerDataType.dataType:scalar,new ghidra.program.model.listing.VariableStorage(currentProgram,4,4),currentProgram));
   if(!address.equals("6f498e50"))params.add(new ghidra.program.model.listing.ParameterImpl(predicate?"check_sleep":"candidate",predicate?ghidra.program.model.data.UnsignedIntegerDataType.dataType:unit,new ghidra.program.model.listing.VariableStorage(currentProgram,8,4),currentProgram));
   f.replaceParameters(ghidra.program.model.listing.Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,params.toArray(new ghidra.program.model.listing.Parameter[0]));
  }
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String original=f.getComment();String old=original==null ? "" : original;old=old.replaceAll("(?m)^Payoff270[^\\n]*\\n?", "").stripTrailing();if(!old.contains(row[2]))old+="\n"+row[2];if(!old.equals(original))f.setComment(old);
   if(java.util.Set.of("6f66f030","6f5fb040","6f498520","6f498e50").contains(row[0])) {
    var params=f.getParameters();int count=row[0].equals("6f498e50")?2:3;
    if(params.length!=count)throw new Exception("ABI parameter count "+row[0]);
    String[] locations={"ECX:","Stack[0x4]:","Stack[0x8]:"};
    for(int k=0;k<count;k++)if(!params[k].getVariableStorage().toString().startsWith(locations[k])||params[k].getDataType().getLength()!=4)throw new Exception("ABI storage "+row[0]);
    if(f.getReturnType().getLength()!=4)throw new Exception("ABI return "+row[0]);
   }
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   out.append("PROTOTYPE ").append(f.getPrototypeString(true,true)).append('\n');
   for(var param:f.getParameters())out.append("PARAM ").append(param.getName()).append(' ').append(param.getDataType().getName()).append(' ').append(param.getVariableStorage()).append('\n');
   out.append("COMMENT ").append(f.getComment()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
