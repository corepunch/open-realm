// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Target377WayGateMoveEvidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={{"6f5fd270","Move_CreateTasksFromOrder","Payoff377 final private point-task proposal: the original four-scene and supplemental Smart-retarget families retain four complete observations and two observer-free controls. Both full ordered repeat projections match, totaling 6699 semantic records and 1045 public markers. Literal request identities are Move 851986 and Smart 851971. Active targets create entrance then captured-exit d016b point tasks with zero requested range and null associations; inactive Move creates only the entrance. A mistaken Smart-head review is preserved separately and corrected by literal packet/public-marker attribution. Canonical real nwgt setup precedes 32 genuine failing assertions. The checked private port passes 37 tests and 229597 assertions in each edition, including warm/cold continuation, callback replacement/free/reuse and unchanged numeric retail gate journeys. Final integration must rebase the actual post380 HEAD and its next save version; wider TARGET-01.2 scope remains open."},{"6f414eb0","AbilityWarp_ReadCapturedDestination","Payoff377 final cached-exit lifetime: both explicit Move-retarget and Smart-retarget retain admission exit (1568,288), while ordinary portal traversal reads live exit (1568,800) after mutation. The second physical point task still targets the original cached exit. Complete repeated transactions and observer-free public controls are preserved. Setter coordinate quantization remains outside this engine claim. The target-owned CAbilityWarp dispatcher now delegates to the same ordinary Move point producer in the private proposal; the obsolete synchronous relocation walker is retired with saved fields preserved."},{"6f600080","Move_AdvanceTaskContinuation","Payoff377 engine continuation safety is separately labeled synthetic callback protection: existing production A_MOVE_START callbacks issue nested Move, Stop, free or same-storage reincarnation at first and second point starts, fresh and cold-saved. Correctly subscribed baseline fails 32 assertions before checked startup; the alternate target-owned dispatch route fails 31 before delegation. These are engine regressions rather than new native branch observations. Static continuation evidence remains a complete original function with whole-instruction byte guards."}};
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f5fd270","6f600080","6f414eb0","6f43b8a0","6f04e2b0","6f692120","6f6926b0","6f5ffb60","6f05b970","6f1710a0","6f16e910","6f16fe20","6f170880","6f5fbad0"}) {
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
