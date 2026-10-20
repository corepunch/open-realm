// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Payoff380ChemicalEvidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  if(getInt(toAddr("6fae0bc8"))!=0x6f540e30 || getInt(toAddr("6fae0f40"))!=0x6f54c640 || getInt(toAddr("6fae0b20"))!=0x6fc0a504 || getInt(toAddr("6fc0a510"))!=0x6fd16314)throw new IllegalStateException("ChemicalRage concrete RTTI/detach/policy slots differ");
  String typeName=".?AVCAbilityChemicalRage@@";if(!new String(getBytes(toAddr("6fd1631c"),typeName.length()),java.nio.charset.StandardCharsets.US_ASCII).equals(typeName))throw new IllegalStateException("ChemicalRage literal RTTI differs");
  String[][] notes={
   {"6f4c84c0","ChemicalRage_CompleteForward","Payoff380 static concrete producer: CAbilityChemicalRage RTTI6fd16314/COL6fc0a504/primary6fae0b24 +40c at6fae0f30 reaches this completion callback; it removes a concrete existing buff and, for positive authored lifetime, builds its timed buff using duration plus Chaos_CommitDelay. Configured passive policy is separately inherited through v41c6fae0f40 ->54c640. Runtime Misc0/1 evidence is required; no Metamorphosis equivalence claimed."},
   {"6f4c8650","ChemicalRage_CompleteReverse","Payoff380 static concrete CAbilityChemicalRage +410 at6fae0f34 reaches this empty reverse callback. Common inverse owns availability restoration; no extra concrete inverse side effect is inferred. Requires fresh public/native repeats/control."},
   {"6f54c640","Morph_ReadAlternateDisablePolicy","Payoff380 static routing: CAbilityChemicalRage primary6fae0b24 v41c at6fae0f40 contains this configured Misc/MorphAlternateDisable policy, unlike unconditional Burrow/Submerge overrides. This annotation is static routing only; actual public ANcr availability/timed inverse needs fresh Misc0/1 captures."}
  };
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f4c84c0","6f4c8650","6f54c640","6f55d380","6f552120","6f551f70","6f540e30","6f569f40","6f5698a0","6f562c50","6f54eb80","6f54cb50"}) {
   var f=getFunctionAt(toAddr(entry));if(f==null)throw new IllegalStateException(entry);
   if(args[1].equals("apply"))for(String[] note:notes)if(note[0].equals(entry)) {
    if(f.getName().startsWith("FUN_"))f.setName(note[1],ghidra.program.model.symbol.SourceType.USER_DEFINED);
    String prior=f.getComment();if(prior==null)prior="";
    if(!prior.contains(note[2]))f.setComment(prior+"\n"+note[2]);
   }
   JsonObject row=new JsonObject();row.addProperty("name",f.getName());row.addProperty("comment",f.getComment());row.addProperty("end",f.getBody().getMaxAddress().toString());
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
  JsonObject slots=new JsonObject();String[] addresses={"6fae0bc8","6fae0f40","6fae0f30","6fae0f34","6fae0b20","6fc0a504","6fd1631c"};int[] lengths={4,4,4,4,4,20,27};for(int i=0;i<addresses.length;i++){StringBuilder bytes=new StringBuilder();for(byte x:getBytes(toAddr(addresses[i]),lengths[i]))bytes.append(String.format("%02x",x&255));slots.addProperty(addresses[i],bytes.toString());}out.add("data_guards",slots);
  out.addProperty("task","SEP-01.2");out.addProperty("phase","payoff380-chemical-static-only");out.addProperty("binary_sha256",currentProgram.getExecutableSHA256());
  Files.writeString(path,new GsonBuilder().setPrettyPrinting().create().toJson(out)+"\n");println(instructions.size()+" guards in "+functions.size()+" complete functions");
 }
}
