// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Group354Evidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={
   {"6f49d680","Attack_GetAcquisitionPriority","Payoff354 GROUP-03.2: no resolved victim plus enabled candidate Attack permits virtual188 defending override; MagicLeashTarget contributes low00400000 while high armed00400000 remains. Five-scene public Aerial Shackles/removed buff/removed Attack/Thunder Bolt controls repeat547 complete normalized native events and37 unhooked markers. Ordinary candidate key0be00000:64000000 becomes0be00000:64400000 only with retained MagicLeashTarget; removal restores it. Engine distinguishes weapon priority eligibility from execution lock, preserves authored custom BuffID policy and cold-save inverse. Other override classes, flag8000 fallback and wider acquisition remain open."},
   {"6f48e2c0","Unit_HasDefendingPriorityOverride","Payoff354: inspect concrete vtable, not a shared stun label. Public BPSE is CBuffPause vtableadcec8 with virtual188=41d9d0 false; both Thunder Bolt and War Stomp use it. CBuffStun vtableadcb60 has188=4dc980 true but is not certified by those public casts. Aerial Shackles Bmlt uses CBuffMagicLeashTarget b6bb70 with188=63e680 true. Enabled Attack and no victim remain caller prerequisites. Diagnostic mid-function observer changed high priority words and is excluded; accepted observer uses complete function hooks only."},
   {"6f495b90","Attack_HasEnabledWeaponForPriority","Payoff354: MagicLeashTarget victim has signed3c zero and enabled weapon bits even while physical execution is held; public availability reaches priority virtual188 and low00400000. Do not reuse a can-execute-Attack gate here. Removing Aatk clears the armed high00400000 and prevents the override."},
   {"6f63e680","MagicLeashTarget_HasDefendingPriorityOverride","Payoff354: CBuffMagicLeashTarget virtual188 returns literal1. Public Aerial Shackles victim retains it through owner transfer; UnitRemoveBuffs clears it before the next availability. Two full547-event repeats and37-marker no-hook control agree. Engine uses an owned status state, including custom BuffID, without granting attacks while bound."},
  };
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f49d680","6f48e2c0","6f495b90","6f4dc980","6f63e680"}) {
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
