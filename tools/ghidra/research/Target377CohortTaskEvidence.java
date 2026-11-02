// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Target377CohortTaskEvidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={{"6f5faaf0","Move_TryPreviousRequestCohort","Payoff377 verified per-unit point sequences: two observations retain all 3139 ordered records from the fixed public prop-window resume at tick 40 through cleanup at tick 275. An observer-free control matches all 288 public markers of the first observation; the second observation matches all 243 markers inside the declared interval. Full raw startup input timing is retained and is not claimed to repeat. Both units share previous identity 847/906, category 1, and fine goal words 1111549868/1107924485. Native joins at ticks 60 and 137 preserve A entrance task 886/918, then separately consume cached-exit tasks 885/917 and 890/933 at ticks 141 and 143. Public retargeting distinguishes old exit 544/1568 from later exit 1056/1568. The private engine baseline fails five cohort assertions before the proper per-unit port; a separate no-history baseline fails three ownership assertions before the native sentinel guard. The final private engine passes 39 tests and 229803 assertions in each edition, with 258 affected tests and 78603 assertions. Pending point state belongs to each unit, survives physical cohort rebinding and cold saves, and retires on task cancellation. Both entrance and successor use the existing common point-task cohort producer without fabricated issued-point events or a task-type exclusion. Save 215 rejects format 214 before reading the changed layout. Wider TARGET-01.2 remains open."}};
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f5fd270","6f600080","6f414eb0","6f43b8a0","6f04e2b0","6f692120","6f6926b0","6f5ffb60","6f05b970","6f1710a0","6f16e910","6f16fe20","6f170880","6f5fbad0","6f5fa950","6f5faaf0","6f5fcbe0","6f5fa5b0","6f693d20"}) {
   var f=getFunctionContaining(toAddr(entry));if(f==null)throw new IllegalStateException(entry);
   if(args[1].equals("apply"))for(String[] note:notes)if(note[0].equals(entry)) {
    if(f.getName().startsWith("FUN_"))f.setName(note[1],ghidra.program.model.symbol.SourceType.USER_DEFINED);
    String prior=f.getComment();if(prior==null)prior="";
    if(!prior.contains(note[2]))f.setComment(prior+"\n"+note[2]);
   }
   JsonObject row=new JsonObject();row.addProperty("entry",f.getEntryPoint().toString());row.addProperty("observed_entry",entry);row.addProperty("name",f.getName());row.addProperty("end",f.getBody().getMaxAddress().toString());
   var result=decomp.decompileFunction(f,45,monitor);if(!result.decompileCompleted())throw new IllegalStateException(result.getErrorMessage());
   row.addProperty("decompiled",result.getDecompiledFunction().getC());
   JsonArray refs=new JsonArray();for(var ref:getReferencesTo(f.getEntryPoint())){
    JsonObject r=new JsonObject();var parent=getFunctionContaining(ref.getFromAddress());
    r.addProperty("site",ref.getFromAddress().toString());r.addProperty("type",ref.getReferenceType().toString());r.addProperty("entry",parent==null?"":parent.getEntryPoint().toString());refs.add(r);
   }row.add("references",refs);
   StringBuilder assembly=new StringBuilder();int count=0;var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var ins=it.next();StringBuilder bytes=new StringBuilder();for(byte b:ins.getBytes())bytes.append(String.format("%02x",b&255));instructions.addProperty(ins.getAddress().toString(),bytes.toString());assembly.append(ins.getAddress()).append(' ').append(ins).append('\n');count++;}
   row.addProperty("assembly",assembly.toString());row.addProperty("instruction_count",count);functions.add(entry,row);

  }}finally{decomp.dispose();}
  out.add("functions",functions);out.add("instructions",instructions);
  Files.writeString(path,new GsonBuilder().setPrettyPrinting().create().toJson(out)+"\n");println(instructions.size()+" guards in "+functions.size()+" complete functions");
 }
}
