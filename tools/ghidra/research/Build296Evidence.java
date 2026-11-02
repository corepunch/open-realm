// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Build296Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f544350","UndeadBuild_BeginUnitWorkWait","Payoff296 ORDER-01.16: construction work virtual340 at Undead vtable6fb18a18. ECX ability, stack structure/owner/haunted-parent, RET0c. Worker lives at ability30. After optional Haunted Mine parent retention, animation selection and worker flag update, calls69c620 with actual Math_RuntimeOne6fd3c748 pointer and scale0 at544453. Two read-only eight-scene original captures plus uninstrumented control agree794 public markers each,104 native boundaries per observed run. Work completion is the UNIT primary d019d request, independent of structure buildTime, pause, RemoveUnit and KillUnit. Move/Stop/worker Pause cancel the old request. No full construction or Haunted Mine runtime closure claim."},
   {"6f69c620","Unit_ScheduleInternalTaskCompletion","Payoff296: ECX Unit, stack scalar delay pointer and scale, RET8.67e130 cancels existing Unit180 control, then0608d0 queues d019d against this unit. Only nonzero delay+scale modifies animation speed. Undead work passes one/zero; scripted unpause passes .01/zero; removal/death can pass zero. Do not globally hardcode one second into this generic producer. Live one-second request dispatch uses request deadline, not rounded display sample time."},
   {"6f67e130","Unit_CancelInternalTaskCompletion","Payoff296: cancels Unit180 event control, with animation-scale inverse when Unit280 bit40 owned it. Move/Stop/worker Pause leave canceled heap records flagged10000; original captures show they execute no receiver callback. Structure pause or destruction do not cancel this worker-owned request."},
   {"6f543f00","UndeadBuild_CompleteWork","Payoff296 static evidence only: validates retained Haunted Mine identity and Abgm owner, then conditionally sends Smart d0032 when worker flags permit. This follow-up has NOT been exercised by the eight ordinary Ziggurat work-clock scenes; broader ORDER-01.16 remains open."},
   {"6f690490","Unit_DispatchCoreTask","Payoff296: d019d calls Unit_PopInternalTaskHead then Unit_DispatchInternalTaskQueue. Generic task-completion receiver also serves unpause and removal. Read-only original worker wait trace ends at this virtualc receiver; one-second work pops Build, while canceled records never invoke this receiver."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);String c=f.getComment();if(c==null||!c.contains(row[2]))f.setComment((c==null?"":c+"\n")+row[2]);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
  }
  for(String a:new String[]{"6f544350","6f69c620","6f67e130","6f543f00","6f0608d0","6f060ca0","6f690490"}){
   var f=getFunctionAt(toAddr(a));out.append("BODY ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
