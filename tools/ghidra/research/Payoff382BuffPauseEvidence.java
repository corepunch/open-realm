// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Payoff382BuffPauseEvidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  if(getInt(toAddr("6fb6fac4"))!=0x6f540e30 || getInt(toAddr("6fb6fe3c"))!=0x6f54c640 || getInt(toAddr("6fb6fa1c"))!=0x6fc38250 || getInt(toAddr("6fc3825c"))!=0x6fd2d820)throw new IllegalStateException("Metamorphosis concrete RTTI/detach/policy slots differ");
  String typeName=".?AVCAbilityMetamorphosis@@";if(!new String(getBytes(toAddr("6fd2d828"),typeName.length()),java.nio.charset.StandardCharsets.US_ASCII).equals(typeName))throw new IllegalStateException("Metamorphosis literal RTTI differs");
  String[][] notes={
   {"6f62dff0","Metamorphosis_CompleteForward","Payoff382 static routing: concrete CAbilityMetamorphosis RTTI6fd2d820/COL6fc38250/primary6fb6fa20 +40c6fb6fe2c calls this concrete completion callback; it must be reviewed independently from ANcr. Common initializer55d380, configured v41c6fb6fe3c->54c640, and common detach+a4 are guarded. Concrete callback reads authored HeroDur, creates source-linked BEme timed buff via612070/63d1b0, then applies configured availability and authored health scalar via414cd0 rank+30. BEme removal647a00 can request reverse; do not infer ANcr lifetime equivalence. Payoff382 ordinary/detach two observations plus observer-free controls each match exactly for Misc0/1. Typed BEme initializes independent2.51 embedded expiry via530360/52fe50/532b50 while common2.5 form timer remains; natural removal at40/110/166 is observed before normal .375 inverse. Public removal preliminary records show647a00 temporary80000 requests immediate inverse through569e20; its separate repeats/control match455/503 events for Misc0/1. The additional AM82(codeAEme) BuffIDBblo family also matches467/515 complete events and224literal public markers per setting across two observations/control.3eb340->414350(rank,index0) supplies first authoredBuffID to532a80, which overwrites nonzero rawcode+34 independently of the concreteBEme class. CanonicalBEme publiclevel stayszero whileBblo becomesone; publicBblo removal still invokes647a00 immediate inverse. Twelve additional6d81b8 d01c4 rows are effect-death requests from6d8100 and are retained literally, not treated as Morph timers. ConcreteBEme +e8/6fb6f798->531420 freezes embedded84 request on first positive Ability48; +ec/6fb6f79c->532fa0 resumes remaining on final release.427610/43b0e0 change48 independently of execution3c/order40.382 Pause family is prepared to test public Pause crossing lifetime and public BEme removal while held; no pause runtime result or engine parity claimed yet. No engine parity claim yet. No health bonus engine parity or other subtype claim."},
   {"6f4c8650","Morph_EmptyConcreteReverse","Payoff382: concrete CAbilityMetamorphosis +4106fb6fe30 points to this same one-byte RET. It is guarded statically and deliberately not intercepted. Shared address does not establish other AEme/ANcr lifecycle equivalence."},
   {"6f54c640","Morph_ReadAlternateDisablePolicy","Payoff382 static routing: CAbilityMetamorphosis table6fb6fa20 v41c6fb6fe3c reads the configured Misc policy. No AEme runtime timing/availability behavior is inferred before fresh Edem/Edmm plus Apiv captures."}
  };
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f62dff0","6f647a00","6f4c8650","6f54c640","6f55d380","6f552120","6f551f70","6f540e30","6f569f40","6f5698a0","6f562c50","6f54eb80","6f54cb50","6f612070","6f639470","6f63d1b0","6f52cf60","6f63ba00","6f532a80","6f414cd0","6f631700","6f530360","6f52fe50","6f532b50","6f52d570","6f52cf10","6f52dfa0","6f569e20","6f3eb340","6f414350","6f6d8100","6f531420","6f532fa0","6f427610","6f43b0e0"}) {
   var f=getFunctionAt(toAddr(entry));if(f==null)throw new IllegalStateException(entry);
   if(args[1].equals("apply"))for(String[] note:notes)if(note[0].equals(entry)) {
    if(f.getName().startsWith("FUN_"))f.setName(note[1],ghidra.program.model.symbol.SourceType.USER_DEFINED);
    String prior=f.getComment();if(prior==null)prior="";prior=prior.replace("DataA health scalar","authored health scalar via414cd0 rank+30");f.setComment(prior);
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
  JsonObject slots=new JsonObject();String[] addresses={"6fb6fac4","6fb6fe3c","6fb6fe2c","6fb6fe30","6fb6fa1c","6fc38250","6fd2d828","6fb6f6cc","6fb6f754","6fb6f9d4","6fb6fa08","6fb6f9d0","6fb6f9ec","6fb6f9fc","6fcd53a4","6fb6f798","6fb6f79c"};int[] lengths={4,4,4,4,4,20,typeName.length()+1,4,4,4,4,4,4,4,4,4,4};for(int i=0;i<addresses.length;i++){StringBuilder bytes=new StringBuilder();for(byte x:getBytes(toAddr(addresses[i]),lengths[i]))bytes.append(String.format("%02x",x&255));slots.addProperty(addresses[i],bytes.toString());}out.add("data_guards",slots);
  out.addProperty("task","SEP-01.2");out.addProperty("phase","payoff382-metamorphosis-static-only");out.addProperty("binary_sha256",currentProgram.getExecutableSHA256());
  Files.writeString(path,new GsonBuilder().setPrettyPrinting().create().toJson(out)+"\n");println(instructions.size()+" guards in "+functions.size()+" complete functions");
 }
}
