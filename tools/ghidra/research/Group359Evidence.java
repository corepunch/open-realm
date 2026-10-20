// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Group359Evidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={
   {"6f48e2c0","Unit_HasDefendingPriorityOverride","Payoff359 inventory:724 primary CAbility RTTI vtables,719 defaultfalse188 at41d9d0; CBuffStun,Freeze,SleepStun,FreezingBreath use4dc980true andMagicLeashTarget63e680true. Exact class getters identifyBSTN,Bfre,BUst,Bfrz. Public Freeze and FreezingBreath are disabled before ranking, so true188 never bypasses the enabled-Attack gate. SleepStun public reachability and full GROUP-03.2 remain under the existing leaf."},
   {"6f4d96a0","BuffFreeze_GetClassCode","Payoff359: public Orb of Freezing AIzb creates CBuffFreeze primary vtableafa618/classBfre. Initializer530570 flags1,0,0,1 acquires Unit1c4 and per-ability3c/40; repeated public UnitRemoveBuffs releases them through52d050. Durationzero is permanent; removing the source does not remove the buff. Two repeated public lethal-hit scenes attach Freeze during hit resolution and synchronously remove it on death before the next public marker; applying it after committed death would leave an invalid counted owner. Candidate ranking omits armed/mobile bits while disabled despite true188."},
   {"6f517510","BuffSleepStun_GetClassCode","Payoff359 static: CBuffSleepStun primary vtableb0a36c classBUst, distinct from CBuffSleepPause and CBuffSleep. Only this Sleep subtype has true188. Public phase and damage-admission lifetime is not inferred from generic BUsl status."},
   {"6f59c1a0","BuffFreezingBreath_GetClassCode","Payoff359: CBuffFreezingBreath primary vtableb3acac/classBfrz has true188 but shares Stun availability owner. Public Afrz on-hit initializer530570 flags0,0,0,1 acquires Unit1b8 and ability3c, leaving Unit1c4 and ability40 unchanged. This differs from Freeze despite both disabling Attack eligibility; source removal preserves the target buff. Afrz is passive structure-targeted Freezing Breath, not active Frost Nova. Native hit handlers58ff90/590110 first reject positive source ability3c and failed virtual268 requirements."},
   {"6f4197d0","Ability_CheckOwnerRequirements","Payoff359: FreezingBreath hit handlers58ff90/590110 first require nonpositive source ability3c and virtual268. This common checker resolves owner/player state and authored ability requirements/availability; the passive hit must not bypass source disabling or research requirements. Public original probes explicitly researchRufb before the enabled hit."},
  };
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f48e2c0","6f4dc980","6f4d96a0","6f517510","6f59c1a0","6f4d4610","6f4f7150","6f5744d0","6f530570","6f52d050","6f52cf60","6f48c8c0","6f58ff90","6f590110","6f4197d0"}) {
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
