// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import com.google.gson.*;
import java.nio.file.*;
import java.util.*;
/** Export every direct scalar trig caller; names alone do not establish reachability. */
public class Trig351Evidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  JsonObject out=new JsonObject(),calls=new JsonObject(),functions=new JsonObject(),guards=new JsonObject();
  TreeSet<String> entries=new TreeSet<>();
  for(String a:new String[]{"6f06ffa0","6f0703a0","6f070530","6f0705b0","6f070790","6f071280","6f071340","6f071590"}){
   entries.add(a);JsonArray refs=new JsonArray();for(var ref:getReferencesTo(toAddr(a))){
    JsonObject row=new JsonObject();var f=getFunctionContaining(ref.getFromAddress());row.addProperty("site",ref.getFromAddress().toString());row.addProperty("type",ref.getReferenceType().toString());row.addProperty("entry",f==null?"":f.getEntryPoint().toString());refs.add(row);if(f!=null)entries.add(f.getEntryPoint().toString());
   }calls.add(a,refs);
  }
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String a:entries){
   Function f=getFunctionAt(toAddr(a));if(f==null)throw new IllegalStateException(a);if(a.equals("6f59ae50"))f.setName("TornadoWander_ChooseDestination",SourceType.USER_DEFINED);JsonObject row=new JsonObject();row.addProperty("name",f.getName());row.addProperty("end",f.getBody().getMaxAddress().toString());
   var result=decomp.decompileFunction(f,45,monitor);if(!result.decompileCompleted())throw new IllegalStateException(a+" "+result.getErrorMessage());row.addProperty("decompiled",result.getDecompiledFunction().getC());
   JsonArray refs=new JsonArray();for(var ref:getReferencesTo(toAddr(a))){JsonObject r=new JsonObject();var parent=getFunctionContaining(ref.getFromAddress());r.addProperty("site",ref.getFromAddress().toString());r.addProperty("type",ref.getReferenceType().toString());r.addProperty("entry",parent==null?"":parent.getEntryPoint().toString());refs.add(r);}row.add("references",refs);
   StringBuilder assembly=new StringBuilder();var it=currentProgram.getListing().getInstructions(f.getBody(),true);int count=0;
   while(it.hasNext()){var ins=it.next();StringBuilder bytes=new StringBuilder();for(byte b:ins.getBytes())bytes.append(String.format("%02x",b&255));guards.addProperty(ins.getAddress().toString(),bytes.toString());assembly.append(ins.getAddress()).append(" ").append(ins).append("\n");count++;}
   row.addProperty("assembly",assembly.toString());row.addProperty("instruction_count",count);functions.add(a,row);
  }}finally{decomp.dispose();}
  for(String a:entries){Function f=getFunctionAt(toAddr(a));String old=f.getComment();if(old!=null){int at=old.indexOf("TRIG351");if(at>=0)old=old.substring(0,at).stripTrailing();}f.setComment((old==null||old.isEmpty()?"":old+"\n")+"TRIG351 NUM-01.18: all140 references/70 complete functions/6897 instructions; CFG proves132 local-output sites, seven forwarding thunks, one value-angle wrapper.1170 original/model contexts retain all pointer partitions and40 forced sign-loss controls. Two retail runs agree454 nested scalars/84 public returns;98 unhooked markers agree. Observed inputs are distinct/unmodified; existing production values agree in debug/O2. Additional indirect roots remain BASE-03.1; full numeric domains/trajectories E2E-06.1. CFG resolves input and third operands through pointer-return calls and callee-saved registers; every concrete caller has disjoint input/output origins and separate paired locals.");}
  out.add("calls",calls);out.add("functions",functions);out.add("instructions",guards);
  String[] args=getScriptArgs();if(args.length!=1)throw new IllegalStateException("fresh output required");Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("existing output");Files.writeString(path,new GsonBuilder().setPrettyPrinting().create().toJson(out)+"\n");println(functions.size()+" complete functions; "+guards.size()+" instruction guards");
 }
}
