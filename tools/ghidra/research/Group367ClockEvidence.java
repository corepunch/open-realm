// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Group367ClockEvidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={{"6f48e460","Unit_AdjustAbilityClockPauseContributions","Payoff367 complete static clock proof: increment Unit1d4 before prefetched ordered ability walk. Add invokes virtualbc; inverse invokes c0(1). ScriptPause6977a0 enters this clock contribution before48c8c0 counted availability; unpause6977f0 removes availability before clock contribution. MainSleep b0aa60 and initialSleepPause b0a6e4 share bc427610,c043b0e0,e8531420,ec532fa0. Their clocks therefore independently obey the same counted pause mechanism."},{"6f531420","TimedBuff_FreezePrivateExpiryClock","Payoff367 static: read embedded+84 virtual18 remaining into scalar+a8/ac, virtual1c elapsed into+a0/a4, then cancel request. Called only on Ability48 nonpositive-to-positive through427610. MainSleep and initialSleepPause both bind this exact e8 entry."},{"6f532fa0","TimedBuff_ResumePrivateExpiryClock","Payoff367 static: if stored remaining+ac positive call virtual34c532b50 with remaining and notify0; then zero remaining scalar+a8/ac. Last positive-to-nonpositive Ability48 contribution reaches this ec through43b0e0. MainSleep and initialSleepPause bind same entry."}};
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f48e460","6f6977a0","6f6977f0","6f427610","6f43b0e0","6f531420","6f532fa0","6f532b50","6f52fe50","6f531a80","6f5203f0","6f530300","6f518290","6f518320","6f5310f0","6f5310b0","6f48e8a0","6f52fe10","6f652d10","6f652da0","6f532a80","6f50c100","6f5008a0","6f50da20","6f500430","6f500460","6f52d000","6f533050","6f060ad0","6f060630","6f0606c0","6f0609b0","6f0602a0","6f3ea470"}) {
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
  byte[] minimum=getBytes(toAddr("6fcd539c"),4); StringBuilder minimumHex=new StringBuilder();for(byte b:minimum)minimumHex.append(String.format("%02x",b&255));instructions.addProperty("6fcd539c",minimumHex.toString());
  out.add("functions",functions);out.add("instructions",instructions);
  Files.writeString(path,new GsonBuilder().setPrettyPrinting().create().toJson(out)+"\n");println(instructions.size()+" guards in "+functions.size()+" complete functions");
 }
}
