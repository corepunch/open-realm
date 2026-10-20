// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Group367Evidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={
   {"6f52f300","BuffStun_AllocatePooled","Payoff367 static: concrete CBuffStun primary vtableadcb60 is stored only by pooled constructor52f300 and standalone4aaaa0 in the current reference inventory. Standalone constructor has no direct callers; pooled allocation is reachable through the instance generator, so absence of direct construction does NOT prove public unreachability."},
   {"6f513c40","BuffSleepStun_AllocatePooled","Payoff367 static: CBuffSleepStun primaryb0a36c has both standalone4f7150 and pooled513c40 constructors. The pooled constructor is called by510ed0 through instance-generatorvtableb0a35c. No public producer is inferred merely from factory registration."},
   {"6f50c100","AbilitySleep_ApplySleepAndInitialPause","Payoff367: public Sleep producer first creates/refreshes BUsl via4f1000, then queries existing BUst and BUsp. It refreshes existing BUst, otherwise refreshes/creates BUsp via4f10c0; this function does not directly create a new BUst. Full factory/public producer reachability is still under investigation."},
   {"6f5008a0","BuffSleep_OnDamageWake","Payoff367: nonzero incoming damage wakes BUsl only when neither BUst nor BUsp is currently found. Both initial-owner classes suppress damage wake, although only BUst has true188; generic Sleep cannot stand in for this distinction."},
   {"6f518290","BuffSleep_InitializeAndPushSuspension","Payoff367 repeated/control public Sleep: main BUsl acquires acquisition prevention and pushes Unit suspension with argument0, retaining accepted user orders under order851973 without Unit1b8/1c4 ability-disable contributions. Initial BUsp prevents incoming damage for authored DataA; late damage queues BUsl end and the held Move resumes at the subsequent boundary. Public markers repeat exactly across two observations and control."},
   {"6f50da20","BuffSleep_RemoveAndPopSuspension","Payoff367: ordinary initialized Sleep sets flag100 and releases its suspension on removal through Unit_PopSuspensionOrder; legacy path without flag100 uses separate direct availability inverses. Do not implement Sleep by calling scripted PauseUnit, whose public flag and counted ability disables differ."},
   {"6f518320","BuffSleepPause_InitializeWithDamagePrevention","Payoff367: SleepPause calls530260 then48b850(1); its500430 inverse calls52d000 then48ecd0(1). Repeated/control public damage during BUsp leaves life unchanged, whereas damage after BUsp expiry is applied and wakes BUsl. Source pause does not remove the victim owners."},
  };
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f52f300","6f4aaaa0","6f513c40","6f4f7150","6f510ed0","6f52e900","6f50c100","6f5008a0","6f515490","6f515480","6f515470","6f517510","6f52fc50","6f52f6e0","6f4f10c0","6f4f1000","6f4f2340","6f4fbf00","6f507510","6f4f3f30","6f530260","6f5314b0","6f518290","6f5203f0","6f531a80","6f52fe50","6f518320","6f500430","6f500460","6f50da20","6f414b50","6f4ffe20","6f52d000"}) {
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
