// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Move271Evidence extends GhidraScript {
 public static final String[][] NOTES={{"6f5fc900", "CAbilityMove_ComputeEffectiveSpeed", "Payoff271 MOVE-01.1: BPly class639550 via48cb80 selects form+114 for minimum68f010 and maximum68ed40. Unit original30 still owns default237 and structure predicate. Repeated read-only map hP27(237,173..389)/nP27(220,211..233), plus unhooked control, prove current233/high233/low211 while morphed; original bounds return after removal. Broader special caps and modifier ordering remain open."}, {"6f63a950", "Polymorph_WriteEnabledMovementModifier", "Payoff271: ECX CBuffPolymorph, stack4 output*,8 enabled, RET8. Nonnull output and enabled and self20.8000 write software(form114 authored base minus owner original base) to output28. Live preset300 becomes283, not220."}, {"6f63ac00", "Polymorph_WriteMovementModifier", "Payoff271: ECX CBuffPolymorph, stack4 output*, RET4. Same form-minus-original speed contribution at output28, without enabled argument; flags8000 and nonnull output still gate."}, {"6f64c090", "Morph_RemoveAppliedContributions", "Payoff271: shared morph inverse, ECX buff, plainRET. Applied100 and !retired200 gates; flags8000 branch uses original-base minus retained forme0-base then5fb740 Move additive operation. Live SetUnitMoveSpeed300 while form active removes to317, retains851986 Move. Not entry-time speed restoration. Wider shared morph branches remain unclaimed."}, {"6f631b60", "Polymorph_RemoveAndRestore", "Payoff271: CBuffPolymorph vtable33c invokes shared inverse64c090 after common cleanup, conditional400 ability disable inverse, and118 timer cancellation. Public negative UnitRemoveBuffs reaches removal while preserving current Move."}, {"6f639550", "Polymorph_GetBuffClass", "Payoff271: immediate BPly=42706c79, plainRET. Effective-speed5fc900 uses this class lookup before selecting form profile114."}, {"6f63bae0", "Polymorph_GetClass", "Payoff271: CBuffPolymorph vtable class getter returns BPly=42706c79. RTTI of6fb53244 identifies CBuffPolymorph."}, {"6f5fb740", "Move_AdjustBaseSpeed", "Payoff271: shared additive operation, also used by temporary form inverse64c090. Software add delta; nearzero3a83126f repair; publish speed. Public current setter during form replaces base, then inverse adds17 in the captured authored profile."}, {"6f3fa460", "Buff_IsAutoDispellable", "Payoff271: CBuffPolymorph vtable330 forwards virtual1d8 (magic), RET4. Class vtable1d8/1e0 return1,1dc/1e4/1e8/32c return0: negative magic, not physical/positive/aura/timed-life. Negative native removal verified live."}};
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong original");
  if(getScriptArgs().length!=1)throw new Exception("fresh export required");
  Path p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  var dm=currentProgram.getDataTypeManager();
  var category=new ghidra.program.model.data.CategoryPath("/WarcraftIII/Pathfinding127");
  var buff=dm.getDataType(category,"WC3BuffPrefix");if(buff==null)throw new Exception("buff prefix missing");
  var form=new ghidra.program.model.data.StructureDataType(category,"WC3PolymorphMovementPrefix",0x118);
  form.replaceAtOffset(0,buff,56,"buff","CBuffPolymorph RTTI vtable6fb53244; owner30 and flags20 used by63a950/63ac00.");
  form.replaceAtOffset(0x114,ghidra.program.model.data.UnsignedIntegerDataType.dataType,4,"movement_profile","5fc981 reads BPly+114 for68f010 minimum and68ed40 maximum;63a950/63ac00 read form base66ad20.");
  var modifier=new ghidra.program.model.data.StructureDataType(category,"WC3MovementModifierPrefix",0x2c);
  modifier.replaceAtOffset(0x28,dm.getDataType(category,"WC3PathScalar"),4,"speed_delta","63a950 and63ac00 write form-minus-original base through0702b0.");
  for(var t:new ghidra.program.model.data.StructureDataType[]{form,modifier}) {
   var old=dm.getDataType(category,t.getName());if(old!=null&&!old.isEquivalent(t))throw new Exception("preserve existing type "+t.getName());
   dm.resolve(t,ghidra.program.model.data.DataTypeConflictHandler.KEEP_HANDLER);
  }
  var formptr=new ghidra.program.model.data.PointerDataType(dm.getDataType(category,"WC3PolymorphMovementPrefix"),4);
  var modptr=new ghidra.program.model.data.PointerDataType(dm.getDataType(category,"WC3MovementModifierPrefix"),4);
  var move=new ghidra.program.model.data.PointerDataType(dm.getDataType(category,"WC3MoveSpeedPrefix"),4);
  var scalar=new ghidra.program.model.data.PointerDataType(dm.getDataType(category,"WC3PathScalar"),4);
  for(String address:new String[]{"6f5fc900","6f63a950","6f63ac00","6f64c090"}) {
   var f=getFunctionAt(toAddr(address));f.setCustomVariableStorage(false);f.setCallingConvention("__thiscall");
   boolean speed=address.equals("6f5fc900"),inverse=address.equals("6f64c090");
   f.setReturnType(speed?scalar:ghidra.program.model.data.VoidDataType.dataType,SourceType.USER_DEFINED);
   var params=new java.util.ArrayList<ghidra.program.model.listing.Parameter>();
   params.add(new ghidra.program.model.listing.ParameterImpl("self",speed?move:formptr,new ghidra.program.model.listing.VariableStorage(currentProgram,currentProgram.getRegister("ECX")),currentProgram));
   if(!inverse)params.add(new ghidra.program.model.listing.ParameterImpl("output",speed?scalar:modptr,new ghidra.program.model.listing.VariableStorage(currentProgram,4,4),currentProgram));
   if(address.equals("6f63a950"))params.add(new ghidra.program.model.listing.ParameterImpl("enabled",ghidra.program.model.data.UnsignedIntegerDataType.dataType,new ghidra.program.model.listing.VariableStorage(currentProgram,8,4),currentProgram));
   f.replaceParameters(ghidra.program.model.listing.Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,params.toArray(new ghidra.program.model.listing.Parameter[0]));
  }
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String original=f.getComment();String old=original==null ? "" : original;old=old.replaceAll("(?m)^Payoff271[^\\n]*\\n?", "").stripTrailing();if(!old.contains(row[2]))old+="\n"+row[2];if(!old.equals(original))f.setComment(old);
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   out.append("PROTOTYPE ").append(f.getPrototypeString(true,true)).append('\n');
   for(var param:f.getParameters())out.append("PARAM ").append(param.getName()).append(' ').append(param.getDataType().getName()).append(' ').append(param.getVariableStorage()).append('\n');
   out.append("COMMENT ").append(f.getComment()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  for(long a:new long[]{0x6fb53240L,0x6fb5341cL,0x6fb53420L,0x6fb53424L,0x6fb53428L,0x6fb5342cL,0x6fb53570L,0x6fb53574L}) {
   var at=toAddr(a);out.append("DATA ").append(at).append('|');for(byte v:getBytes(at,4))out.append(String.format("%02x",v&255));out.append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
