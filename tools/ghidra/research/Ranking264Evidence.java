// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class Ranking264Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f49d680","Attack_GetAcquisitionPriority","Payoff264 GROUP-03.2: thiscall ECX Attack, stack4 candidate Unit, stack8/12 unsigned64 lower bound, RET12; EDX:EAX unsigned64 priority. Ordinary branch exits49e0fc before TownAI-specific49da60+. High baseline03800000; enabled candidate Attack adds00400000; no task target or target allied to observer adds08200000, enemy00200000, neutral08000000. Low mobility20000000, both active flyers00200000, non-fort defense40000000, attacking observer00c00000, attacking observer ally00400000, candidate can counterattack08000000 except Unit248.10, committed weapon reach04000000 and retained-in-reach01000000. Counterattack selector496b80 succeeds on zero, not nonzero. Range is max(min selected weapon range/acquisition, runtime minimum32 atd6bf64). Bound pruning uses high0be00000 then low60000000/68000000. New public owner-transfer captures prove reach, stickiness, ties and weapon/mobility differences. Full TownAI rawcode/health/type additions, artillery-area495880 and optional ability188/18c overrides remain outside this integration."},
 {"6f49e3a0","Attack_ShouldReplaceAcquiredTarget","Payoff264: unsigned64 comparison of49d680(candidate,0) and49d680(retained,candidateKey). Same pointer or zero candidate rejects. Stronger retained rejects; exact rank ties compare05b1c0 squared distances, rejecting equality or a farther newcomer. Source is observer058900 predicted WORLD position; both candidate distances use committed fine pose selector0. A fine-only predicted source shortcut omits the world roundtrip. Rejected candidate is stored in Attack78/7c via224310, not installed in current6c/70.49e130 already began speed-cap exemption before this predicate, including when ranking rejects."},
 {"6f495b90","Attack_HasEnabledWeaponForPriority","Payoff264: plain RET; ECX Attack. Returns false for signed prevention3c>0, else Boolean(flags20&180000). Flags are enabled weapon slots, not proof of an executing attack. Idle Footmen qualify."},
 {"6f05b1c0","MoveBridge_GetCommittedPointDistanceSquared","Payoff264: thiscall ECX bridge, stack4 scalar output*, stack8 world x*, stack12 world y*, stack16 prediction selector; RET16, EAX output*. Converts WORLD input using map origin and guarded exponent scaling, then source-minus-point square/sum through software scalars. Selector0 reads stored mover position;49e3a0 uses this for both candidates after one predicted-world query of observer. Sixteen unmodified058900->05b1c0 compositions cover negative/nonzero/large origins, fractional fine poses and epoch crossings without modifying either mover."},
 {"6f68c250","Unit_IsOnTargetPerimeter","Payoff264 ABI correction from instructions: ECX source Unit, stack4 target Unit, RET4; reads target34 or target's bridge canonical5c perimeter, then source164 bridge058cb0/16ea30/165fa0.49d680 uses Attack498850 source and candidate target. An absent perimeter returns0; present-perimeter branch not certified by this chunk."}
 };
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong original");
  if(getScriptArgs().length!=1)throw new Exception("fresh output path required");Path p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  var dm=currentProgram.getDataTypeManager();
  var attack=new PointerDataType(dm.getDataType("/WarcraftIII/Pathfinding127/WC3AttackRangePrefix"),4);
  var unit=new PointerDataType(dm.getDataType("/WarcraftIII/Pathfinding127/WC3UnitOrdersPrefix"),4);
  var f=getFunctionAt(toAddr("6f49d680"));f.setCallingConvention("__thiscall");f.setReturnType(UnsignedLongLongDataType.dataType,SourceType.USER_DEFINED);
  f.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,
   new ParameterImpl("attack",attack,new VariableStorage(currentProgram,currentProgram.getRegister("ECX")),currentProgram),
   new ParameterImpl("candidate",unit,new VariableStorage(currentProgram,4,4),currentProgram),
   new ParameterImpl("lower_bound",UnsignedLongLongDataType.dataType,new VariableStorage(currentProgram,8,8),currentProgram));
  f=getFunctionAt(toAddr("6f49e3a0"));f.setCallingConvention("__thiscall");f.setReturnType(UnsignedIntegerDataType.dataType,SourceType.USER_DEFINED);
  f.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,
   new ParameterImpl("attack",attack,new VariableStorage(currentProgram,currentProgram.getRegister("ECX")),currentProgram),
   new ParameterImpl("candidate",unit,new VariableStorage(currentProgram,4,4),currentProgram));
  var scalar=new PointerDataType(dm.getDataType("/WarcraftIII/Pathfinding127/WC3PathScalar"),4);
  var bridge=new PointerDataType(dm.getDataType("/WarcraftIII/Pathfinding127/WC3UnitMoverBridge"),4);
  f=getFunctionAt(toAddr("6f05b1c0"));f.setCallingConvention("__thiscall");f.setReturnType(scalar,SourceType.USER_DEFINED);
  f.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,
   new ParameterImpl("bridge",bridge,new VariableStorage(currentProgram,currentProgram.getRegister("ECX")),currentProgram),
   new ParameterImpl("output",scalar,new VariableStorage(currentProgram,4,4),currentProgram),
   new ParameterImpl("world_x",scalar,new VariableStorage(currentProgram,8,4),currentProgram),
   new ParameterImpl("world_y",scalar,new VariableStorage(currentProgram,12,4),currentProgram),
   new ParameterImpl("prediction",UnsignedIntegerDataType.dataType,new VariableStorage(currentProgram,16,4),currentProgram));
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES){f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("COMMENT ").append(f.getComment()).append('\n').append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append("PROTOTYPE ").append(f.getPrototypeString(true,true)).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
