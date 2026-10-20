// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Morph374LifetimeEvidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={
   {"6f3e3c00","FloatModifier_CancelAndClearHandle","Morph374 owned-handle lifetime proof: ECX points at Unit128 from66977d,694bd8 and the explicit Unit128 LEA in68b780. If the handle is nonzero, invoke pointee virtual8 cancellation, decrement pointee4 reference count, destroy at zero, then store literalzero to the handle. A later058120 allocation can reuse that address after the prior ownership epoch. Preserve aliases among concurrently observed live holders; do not require allocator reuse across dead epochs to repeat."},
   {"6f3e3bd0","FloatListener_CancelAndClearHandle","Morph374 owned-handle lifetime proof: ECX points at Unit12c through669788/66978e,694be3/694be9 and the explicit Unit12c LEA in68b780. Cancels through pointee virtual8, releases pointee4 refcount with destructor atzero, and clears the owning handle. Only observed last-holder literalzero retires a canonical pointer token. Other scalar bridge words, stable identities, primary clocks and sequence stay literal."},
   {"6f54c640","Morph_UsesAlternateAvailabilityPolicy","Morph374 prepared evidence: map war3mapMisc MorphAlternateDisable0/1 produces26 complete common-policy returns per observed run; ordinary Bear/Raven alternate availability is enabled only with1. Bear acquires at deferred forwardcompletion; Raven at immediate takeoff rebind. Reverse completion releases and restarts Apiv fade0.625; Raven landinghold1.375 means age50 is before the later age55 positive enemyvisibility sample. Engine implementation is not established by these annotations."}
  };
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f3e3c00","6f3e3bd0","6f6696e0","6f694b20","6f68b780","6f058120","6f54c640"}) {
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
