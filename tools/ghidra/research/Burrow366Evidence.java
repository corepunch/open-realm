// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Burrow366Evidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={
   {"6f058190","FUN_6f058190","Burrow366 decoder: ECX FloatProp bridge; direct reads8/c pass canonical slot/generation to PathRegistry_ResolveCanonical. Unit118 is this bridge;120/124 are registry identity words, not numerical fade values. Normalize only stable identity relationships. Unit128/12c are refcounted modifier/listener pointers retained and released by6696e0/694b20; all other bridge words and counters remain literal."},
   {"6f4c8480","AbilityBurrow_CompleteForward","Burrow366: original public Abur completion acquires Unit114 invisibility through6696e0 with zero fade, after deferred alternate type rebind and before category publication. Initial alternate attachment reaches the same subtype without198 acquisition."},
   {"6f4c85b0","AbilityBurrow_BeginReverse","Burrow366: original public unburrow removes its Unit114 contribution through694b20 with zero fade, clears Unit20 bit26, and publishes category before the timed reverse198 acquisition or base-type rebind. Preserve independently attached invisibility owners."},
   {"6f6696e0","Unit_AddInvisibilityContribution","Burrow366: seven public Abur scenes witness zero-fade 0-to1 acquisition, inverse and initial alternate. A concurrent Apiv contribution changes114 from1to2 then backto1 without replacing the existing scalar/fade. Unit114 is distinct from Ethereal158 and suppression198."},
   {"6f694b20","Unit_RemoveInvisibilityContribution","Burrow366: zero-fade inverse decrements114; another live source retains the invisibility flag. Last contribution clears5c01000000 and refreshes visibility. Public Stop and Pause do not cancel the authored Burrow transition."}
  };
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f058120","6f058190","6f4ba8d0","6f4c8470","6f4c8480","6f4c85b0","6f4d8880","6f5698a0","6f569f40","6f569e20","6f551f70","6f6696e0","6f694b20","6f68b780","6f62e600","6f643f60","6f631320"}) {
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
