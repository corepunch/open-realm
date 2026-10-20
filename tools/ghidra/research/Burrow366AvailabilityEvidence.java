// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Burrow366AvailabilityEvidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={
   {"6f54eb80","Morph_AcquireAlternateAvailability","Burrow366 live availability proof: v400 predecrements own Morph, current Move/Attack and Hero/Inventory exceptions, then Unit_AdjustAbilityDisableContributions(1,1,0,0); sets held bit100000. Initial-alternate construction acquires before the Morph is linked, so its own counters transiently remain minus1 until attachment inherits the aggregate. Existing or late Apiv is suspended and its fade removed."},
   {"6f54cb50","Morph_ReleaseAlternateAvailability","Burrow366 inverse at reverse completion: clears held100000, releases all-owner execution/order contribution before restoring CURRENT Morph/Move/Attack/Hero/Inventory exceptions. Apiv resumes only after the reverse hold, restarting its authored fade.40bb00 decrements orders before execution refresh."},
   {"6f54c640","Morph_UsesAlternateAvailabilityPolicy","SEP producer audit: common v41c queries Misc/MorphAlternateDisable with default zero. Burrow/Submerge override v41c with constanttrue4c8b60. This is not a DataB flag."},
   {"6f4076e0","Ability_AddDisableContribution","Burrow366 direct read-only helper proof: increment3c and invoke refresh on nonpositive-to-positive before optional40 increment. Counted ownership must survive nested callbacks."},
   {"6f40bb00","Ability_RemoveDisableContribution","Burrow366 direct read-only helper proof: optional40 decremented before3c, positive-to-nonpositive resumes concrete availability. Preserve negative transient constructor/exemption counters."}
  };
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f54eb80","6f54cb50","6f54c640","6f4076e0","6f40bb00","6f4c8b60","6f544a90","6f4bcff0","6f54f4d0","6f5152b0"}) {
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
