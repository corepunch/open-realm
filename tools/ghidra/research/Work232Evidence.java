// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class Work232Evidence extends GhidraScript {
 private DataType type(String name) throws Exception {
  if(name.equals("u32"))return UnsignedIntegerDataType.dataType;
  if(name.equals("void"))return VoidDataType.dataType;
  if(name.startsWith("ptr:"))return new PointerDataType(type(name.substring(4)),4);
  var t=currentProgram.getDataTypeManager().getDataType(new CategoryPath("/WarcraftIII/Pathfinding127"),name);
  if(t==null)throw new Exception("Missing type "+name);return t;
 }
 private void abi(String address,String returns,String convention,String[][] spec) throws Exception {
  var fn=getFunctionAt(toAddr(address));Parameter[] args=new Parameter[spec.length];
  for(int i=0;i<spec.length;i++) {
   var dt=type(spec[i][1]);var reg=currentProgram.getRegister(spec[i][2]);
   var location=reg!=null ? new VariableStorage(currentProgram,reg):new VariableStorage(currentProgram,Integer.parseInt(spec[i][2]),dt.getLength());
   args[i]=new ParameterImpl(spec[i][0],dt,location,currentProgram);
  }
  fn.setCallingConvention(convention);fn.setReturnType(type(returns),SourceType.USER_DEFINED);
  fn.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,args);
 }
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f013490","Move_InitializePreviousCohortRadius","Payoff232: only write to6fd6fb38 initializes software scalar from integer1000 using070d80. This world radius is read by5fa950; it is not a candidate-count limit."},
   {"6f5fa950","Move_FindPreviousRequestCohort","Payoff232 correction: 67e790 cdecl token sequence is8,radiusWord(6fd6fb38),sourceBridge,24,owner,31. Selector8 means center-only circle around predicted source world position;24 means player-owner filter,31 terminates. Radius initializer013490 supplies1000 world units. There is no eight-candidate limit in this sequence. Two existing queued journeys plus fresh read-only probe retain latest submitted request history independently of active FIFO goal."},
   {"6f5faaf0","Move_TryPreviousRequestCohort","Payoff232: first compatible candidate returns0, ending callback iteration. Never merge subsequent independent cohorts. Eligibility uses exact authored movement-type bits1fc, both previous-identity words, resolved physical owner, exact fine goal and count<12. Copy source first, then nonnull old rows except source. Engine consumes ordered spatial candidates rather than scanning every physical owner."},
   {"6f67e790","WidgetQuery_VisitWithSelectors","Payoff232: cdecl variadic selector parser, first stack callback/context. Token8 reads radius word and mover bridge; local18 remains0 (center-only), local20=1. Token24 restricts owner mask. At terminator31, circle query folds default dead/owner exclusions into spatial mask and clears corresponding Unit predicate flags. Do not describe8/24 as candidate count/radius. Scope dispatches059c10 to05f290 then05d680."},
   {"6f05d680","WidgetQuery_VisitUnitCentersInCircle","Payoff232: fastcall ECX worldX*,EDX worldY*,seven stack words,RET1c. Convert center/radius to fine, collect full rectangle via05eb20/05f010 before callbacks. X outer/Y inner, newest effective links, dedup. Compare retained mover78/7c squared distance<=radiusSquared, no candidate collision radius. Require owner20==0 and owner54 live payload. Callback0 stops; restore private query depth64 and query40. Original complete query oracle covers exact1000/1000.125 boundary and materialization-before-stop."}
  };
  for(String[] row:rows) {
   var a=toAddr(row[0]);var f=getFunctionAt(a);
   if(f==null){disassemble(a);f=createFunction(a,row[1]);}
   if(f==null)throw new Exception("Missing "+row[0]);
   f.setName(row[1],SourceType.USER_DEFINED);
   String prior=f.getComment();if(prior==null)prior="";
   prior=prior.replace("spatial67e790 callback5faaf0 maximum8 candidates within24.","spatial67e790 callback5faaf0 center-only radius1000, owner filter24 (Payoff232 correction).");
   if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);
  }
  abi("6f013490","void","__cdecl",new String[][]{});
  abi("6f67e790","u32","__cdecl",new String[][]{{"callback","ptr:void","4"},{"context","ptr:void","8"}});
  getFunctionAt(toAddr("6f67e790")).setVarArgs(true);
  abi("6f05d680","void","__fastcall",new String[][]{{"world_x","ptr:WC3PathScalar","ECX"},{"world_y","ptr:WC3PathScalar","EDX"},{"radius","ptr:WC3PathScalar","4"},{"mask","u32","8"},{"reserved","u32","12"},{"callback","ptr:void","16"},{"context","ptr:void","20"},{"reserved2","u32","24"},{"reserved3","u32","28"}});
  var global=toAddr("6fd6fb38");createLabel(global,"Move_PreviousCohortRadius",true);setEOLComment(global,"013490 initializes1000 world units;5fa950 passes value to circle selector8.");
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f013490","6f5fa950","6f5faaf0","6f67e790","6f059c10","6f05f290","6f05d680","6f05eb20","6f05f010","6f05ef40","6f05ec40"}) {
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);
   out.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
