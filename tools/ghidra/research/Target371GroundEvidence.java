// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Target371GroundEvidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={{"6f49af00","Attack_CreateAttackGroundTasks","TARGET371 Ground: six read-only observations and three matching unhooked controls retain1910 whole records and3402 public markers. Footman far90/nearFLT_MAX, Mortar far1150 synchronously enter common Move; normalized40340000/7cffffff/420fc000. Exact/adjacent configured minimum boundaries reject53 beforetasks. Stock etrp selectedrange128 has no physical Move request: nearhead851984, farhead0 beforepublicreturn. Genuine engine baseline288/330 each exposes42 behavior failures; shared physical-owner/minimum port passes477/477 immediate/cold assertions each. Rooted immediatehead clean baseline61/66 exposes5 failures, port66/66 green. Corrected authored-Amov/Aatk blocked human/computer baseline66/80 each exposes14 genuine failures. IdleAI baseline47/66 exposes19 failures; publicbirth14/34 exposes20, allsetup passing. Shared callback/typedAI userqueue port and500pending capacity/coldsave pass43tests11838assertions each Classic/TFT. Twelve observations/six controls preserve5923 whole records and5928 public markers; fullground polls/projectile/stat and wider TARGET01.2 inventory remain open."},{"6f496410","Attack_ValidatePointOrderGeometry","TARGET371 Ground audit: d0010 checks enabled artillery/line499530 before minimum-range and terrain-point guards. When Attack8000 set, select effectiveweaponrange and apply3f1740 comparator to configuredminimum; if active and canonical source point is inside minimum via05b440(flag1), return53. This is admission rejection, not retreat.3f1740 is not493380 strict-below test: near-equal minimum/range passes its nearzero branch. No guessed shared comparator substitution."},{"6f603110","CAbilityMove_OnCantPath","TARGET371 blocked Ground: independent observe2/observe3 and unhooked control1 match370 public markers; both complete1559-record compositions match. Human1167 flags20-to16 preserves identical Ground public head through5fb190 while physical task retires. Computer1767 flags22-to18 preserves head through recovery,603d70 returnsNULL,6802f0 from60330e calls9c0b00 guard272/304 and replaces head synchronously before603110 returns. No VM or carrier is started. Corrected authored-Amov/Aatk public natural-wall warm/cold baseline66/80 each has14 genuine head/goal failures. Callback recovery retains human logicalhead dormant and computer recovery enters typedAI admission; d0194 scope holds guard then commonuseradvance runs only afterpop/release/return. FullGround strict43tests11838assertions eachgreen includesidleAI, publicbirth,500pending privateAppend andcoldsaves. Supplemental1815queue records plusfullyflushed639birth rows102publicmarkerseach matchrepeats/controls, allcallers/returnProofcode literal. Total12observations6controls5923records5928publicmarkers2757guards across36functions. Broader transport/alternative target branches retain existing owners."}};
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f49af00","6f49e730","6f49a4f0","6f496410","6f4967c0","6f3f1740","6f499530","6f498460","6f494350","6f4984a0","6f493380","6f05b440","6f05b970","6f05c410","6f1710a0","6f16e910","6f171060","6f16f560","6f171340","6f170880","6f603110","6f5fa7a0","6f5fb190","6f603d70","6f6802f0","6f9c0b00"}) {
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
