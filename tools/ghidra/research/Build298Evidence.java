// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Build298Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f544090","HumanBuild_PrependRepairWorkOrder","Payoff298 ORDER-01.16: ECX Human Build ability; stack4 new structure, stack8 previous task, stackc unused; RETc.418b40 marks the previous task for finish but does not immediately remove its user head. Calls virtual180 committed-position relocation, creates Repair d0038 target order69bd80, then691c70 flags1/1 inserts AFTER the active Build head and BEFORE existing successors. Ten-scene original repeat/control: count1 Build -> count2 Build/Repair -> Build retires -> count1 Repair. Thus there is no resumable Build underneath active Repair in these scenes. Repair target notification precedes409630 work. Nested Move or Stop in that notification prevents work; this differs from instant Stop during earlier construct-start, which permits the later Repair handoff. RemoveUnit structure/worker also prevents work. Pause retains the SAME Repair identity beneath suspend851973, then re-notifies target Repair on resume before409630. Private transient intermediate storage and full geometry/timer trajectories are not an engine-equivalence claim."},
   {"6f67de20","Unit_AdvanceUserOrderHead","Payoff298: ECX Unit; plain RET. Calls694a10 to advance user ownership, resolves head19c, checks canonical wrapper/tag2b61676c/lifetime and69e1c0 eligibility, then67abe0 dispatches with flag1 at67deeb. Human Build completion reaches this with transient Repair now the sole head; pause release reaches the SAME saved Repair identity. See frozen read-only native chronology; no Build rawcode exception exists here."},
   {"6f418b40","Task_ClearTargetsAndFinish","Payoff298: ECX task, plain RET. Writes ffffffff to taskc4/c8 then virtualbc(1). Human544090 calls this for the prior construction task BEFORE inserting Repair, while Unit user head/count and internal task still remain unchanged at5440a2. The subsequent task unwind advances Build and dispatches Repair; do not model this call as synchronous removal of the user head."},
   {"6f691c70","Unit_InsertTransientOrder","Payoff298: Human544090 flags1/1 inserts Repair AFTER the existing Build user head. Entry count1/head=tail Build; return count2/head Build/tail Repair. Later67de20 advances to Repair and dispatches it. Pause mode0 inserts suspend BEFORE Repair; release preserves the exact Repair identity and publishes its target event again. Existing generic head/successor controls and capacity constraints remain separately scoped."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);if(row[0].equals("6f691c70")){String c=f.getComment();if(c==null||!c.contains(row[2]))f.setComment((c==null?"":c+"\n")+row[2]);}else f.setComment(row[2]);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
  }
  for(String a:new String[]{"6f544090","6f67de20","6f418b40","6f691c70","6f409630","6f436e10"}){
   var f=getFunctionAt(toAddr(a));out.append("BODY ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
