// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Target356Producers extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={
   {"6f167e40","Path_CheckDestinationChange","Payoff356 TARGET-02.1 audit: compare native uint32 coordinate words directly. The original evidence analyzer rounded self pose to six decimal places; 23 pose words across the nine-scene archived stream reconstruct adjacent floats. Counter2452 native member Y4133fef2 was mislabeled4133fef3 by the compact display round-trip. Keep historical expected fixtures immutable; new evidence retains raw pose words. The destination-change predicate and original timestamps are unchanged."},
   {"6f16cd30","PathGroup_SampleDestination","Payoff356: group owner observation begins before membership pruning; gtick without a subsequent sample is a retiring owner, not a movement decision. Original repeated Move-on-unit scene2 has618 gtick snapshots but617 sampler/gate decisions (2441..3057); last3058 prunes after explicit Stop. Smart-step scene1 has618 snapshots but616 sampler decisions across approach/persistent ownership. New engine comparisons retain the actual decision frontier and independently verify Stop teardown."},
   {"6f16ce10","PathGroup_RequestRoute","Payoff356: wider target-family audit reads original raw group/member snapshots rather than reconstructing fine coordinates from displayed decimal values. Move-on-unit is persistent from its first request; a completed prior cohort's final preprune gtick must not be replayed as a live request. Group cached destination, coarse timestamp and member fine/coarse timestamps are separate owners."},
  };
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f5fc640","6f05a5c0","6f5ff8b0","6f49a240","6f16c150","6f23a760","6f166c30","6f166e90"}) {
   var f=getFunctionAt(toAddr(entry));if(f==null)throw new IllegalStateException(entry);
   if(args[1].equals("apply"))for(String[] note:notes)if(note[0].equals(entry)) {
    if(f.getName().startsWith("FUN_"))f.setName(note[1],ghidra.program.model.symbol.SourceType.USER_DEFINED);
    String prior=f.getComment();if(prior==null)prior="";
    if(!prior.contains(note[2]))f.setComment(prior+"\n"+note[2]);
   }
   JsonObject row=new JsonObject();row.addProperty("name",f.getName());row.addProperty("end",f.getBody().getMaxAddress().toString());
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
