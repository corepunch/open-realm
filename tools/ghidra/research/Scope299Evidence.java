// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Scope299Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f673fe0","Unit_CancelActiveAndInternalTasks","Payoff299 MAP-04.2: cleanup d0144 is unconditional old-task cancellation, even for idle Move. In unchanged Stop284 scenes2/10, nested public Move contributes cancellation cleanup and a separate point-task cleanup, yielding three69a840 calls including the outer Stop cancellation. Engine ordinary point Move/Smart now separates these stages, preserving each scoped fine-record exclusion. Original16-scene fixture and all literal expected headers remain untouched. Pause/death/removal compositions are separate unfinished owners."},
   {"6f680320","Unit_AdmitOrder","Payoff299: ordinary point replacement cancels the existing task before publishing the new user head. Do not collapse this cancellation into the later point constructor cleanup.680320 mode1 calls673610, then693490 appends/dispatches;67abe0 notifies before task construction. Frozen Stop284 scenes2/10 independently witness both nested Move recoveries."},
   {"6f67abe0","Unit_DispatchUserOrderHead","Payoff299: ordinary engine Move/Smart point admission now uses the same notification-before-construction rule as queued activation. A callback active replacement wins; an instantaneous completed Stop permits outer construction with absent public head. Pending prepared group admission is isolated during callbacks, preventing a nested independent command borrowing the outer cohort. No repeated callback or rawcode-case recovery count is introduced."},
   {"6f600340","Move_HandleCleanupTask","Payoff299: the old cancellation and new point constructor both dispatch this d0144 consumer; they are independent scopes around the issued-point notification. This closes the previously omitted recovery in engine ordinary Move/Smart admission, not the separate counted ability-disable5fc120 path. Frozen284 Move callback scenes3 recoveries versus ordinary Stop2 are unchanged."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);String c=f.getComment();if(c==null||!c.contains(row[2]))f.setComment((c==null?"":c+"\n")+row[2]);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   out.append("BODY ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
