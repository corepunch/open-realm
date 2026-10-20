// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Morph374RebindEvidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={
   {"6f670950","Unit_RebindTypeRetainingPublicIdentity","Morph374 rebind proof: complete same-unit rebind calls CUnit_RefreshInvisibility68b780 at670d54 after type/profile replacement and before ability replacement completion. That refresh clears owned handles128/12c and forces scalar1 iff the retained invisibility contribution114 is nonzero. It does not preserve or restart an existing fade. Zero-policy Bear/Raven forward completion therefore publishes full invisibility, while configured suspension removes Apiv contribution and reverse completion later resumes its authored fade."},
   {"6f68b780","CUnit_RefreshInvisibility","Morph374 rebind scalar proof: 670950 directly invokes this function at670d54. Clear128/12c via refcounted cancel helpers, then contribution114==0 selects scalar0; otherwise flag01000000 is published before synchronous TargetLost and scalar1. Existing/late configured Apiv must stay absent through reverse type rebind; only counted-owner inverse restarts the authored fade."},
   {"6f1a8950","Config_ParseIntegerOrFourCC","Morph374 common policy query54c640 ->047710(default0) ->1a8d10 ->1a8950. Numeric path uses strtol base0; quoted path packs up to4 bytes. Common acquire54eb80/release54cb50 compare the full policy result against zero, rather than comparing to1 or parsing textual true."}
  };
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f670950","6f68b780","6f544e60","6f047710","6f1a8d10","6f1a8950","6f03f150","6f6ec7d0"}) {
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
