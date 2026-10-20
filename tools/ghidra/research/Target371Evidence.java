// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Target371Evidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={{"6f49a980","Attack_CreateTasksFromOrder","TARGET371 TARGET-01.2: actual point Attack49a980 creates d016b at the uncorrected public click and Math_RuntimeZero range; common Move5ffb60 enters05b970 with flag1,d0196/d0198 and normalizes to3efae148 without source collision. Exact8scene754marker and static/boundary4scene378marker captures each have independent full read-only repeat plus matching unhooked control; all3422 whole records repeat, retaining heading/clocks/force/stop. Static5x5 blocked and Farm goals retain raw1536/1024 synchronously. All672 observed arrival/velocity pairs match production numerical helpers. Original quantized sqrt has no exact .49 output among19196 reachable roots in the bounded interval; actual adjacent public inner/outer words4403eb83/84 distinguish ordinary arrival from force/cantpath. Genuine failing-first engine baseline67/80 each edition exposes13 immediate/cold physical-owner and corrected-click failures; eager raw-click port passes29focusedtests11306assertions,30queue/targettests2679assertions and123affectedferrytests9547assertions perClassic/TFT. The guard replacement regression requires a distinct new physical point owner after oldtarget retirement. AttackGround49af00 separately selects weapon range and substitutes7f7fffff FLT_MAX when initially inside. Full24function inventory3216guards. Existing broader TARGET-01.2 producer inventory remains open."}};
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f49a980","6f49af00","6f49b0f0","6f5fd270","6f5ffb60","6f5ff8b0","6f5fc640","6f05a5c0","6f05b970","6f05c410","6f1710a0","6f16e910","6f171060","6f16f560","6f171340","6f170880","6f16fbd0","6f603110","6f5fa7a0","6f05b440","6f49a240","6f071480","6f071530","6f06f9c0"}) {
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
