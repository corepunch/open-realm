// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Target363Evidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={
   {"6f9d4a50", "CaptainAI_SetUnitEventSubscriptions", "Payoff363 typed target-event integration: Captain9d4a50 registers the same Captain callback with target-lost652dd0(d01a4), death652cb0(d01a0), owner6901c0(d01a2), in that order when enabled. All use common0725d0 append FIFO keyed by event/callback; duplicate updates retain position. Dispatcher071e00 freezes the outer tail, nested dispatch sees appended nodes, cancellation suppresses pending delivery. Fresh unchanged ORDER-03.1 oracle627 cases has zero mismatch/fault. Native public RemoveUnit, owner-transfer and KillUnit each have two complete read-only observations plus unhooked control:302 exact public markers per run,2718 total; all137/142/159 synchronous records (438 total) repeat after pointer identity normalization, wall-time removal and explicit FloatMini a9de14 relocation. Typed FIFO is earlyMove, six rosterAttack, Captain, independentAttack, lateMove. KillUnit emits lost before death, Captain unsubscribes on lost. Original engine84/112 assertions (28 real behavior failures) in each edition; common per-event typed registry208 passes22tests/970assertions in each edition, including nested growth, cancellation and saved event ranks. Fullsave224tests/68041assertions passes. Inventory/Missile context remains frozen but its engine ordering, broader Captain policies, trajectories and retail-save remain existing open scope; this does not close GROUP-03.4.7.3."},
   {"6f561680", "Inventory_TargetEventCallback", "Payoff363 bounded producer inventory: primary COL RTTI vtableb28330 is CAbilityInventory. The original mixed owner/death transaction contains this unit-owned callback before typed Move/Attack/Captain listeners. d01a0 routes55bc70; other event arms remain distinct. Typed target-registry208 does not certify Inventory owner/death callback ordering or general authored-listener composition."},
   {"6f6d4e30", "Missile_TargetEventCallback", "Payoff363 bounded producer inventory: primary COL RTTI vtableaddfb8 is CMissile. Complete callback switches d0196/d019c through virtualb4, d0198/d019f through virtual5c and d01a4 through virtualb8=6d6f80. Actual Remove/Kill mixed target rings include this callback after typed Move/Attack/Captain owners; it is retained in frozen transactions, not silently discarded. Wider missile lifetime composition is outside the current typed registry claim."},
   {"6f6d6f80", "Missile_OnTargetLost", "Payoff363: CMissile vtableaddfb8+b8 target-lost inverse. Calls6d6e80, retains packet target and current missile target through widget-class validation, compares their payload identities, then6cde50/6cea20 before releasing both pins. Native callback is present after the observed mixed Move/Attack/Captain target listeners; exact projectile engine parity remains a separate existing inventory/end-to-end scope."},
  };
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f9d4a50","6f9d4aa0","6f9d76c0","6f652cb0","6f652dd0","6f6901c0","6f0725d0","6f0728e0","6f071e00","6f679bb0","6f9d7760","6f6802f0","6f561680","6f6d4e30","6f6d6f80"}) {
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
