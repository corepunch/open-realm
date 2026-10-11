// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Build294Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f693490","Payoff294 ORDER-01.16/02.2: queued Build is a COrderPoint, as206a20's6910c0 factory confirms. Busy append has no command-id special case: retains old head and sends only d02a5 internal notification. Twelve complete original append/dispatch cases use hB93 and nonstock zB94 commands, idle/busy and supplied none/nested-task/instant-Stop callbacks. Canonical registry/copy/link helpers run unchanged; script event and ability execution are controlled boundaries. Existing263 genuine Shift Attack/Repair captures establish public shared busy-queue semantics;293 establishes Build factory/current head. This does not claim a new public UI Shift-Build trajectory."},
   {"6f67abe0","Payoff294: COrderPoint publishes point before GetInternalTaskHead, without inspecting command rawcode. Original controlled Build cases: none -> ability then task dispatch; nested live task -> no outer ability/dispatch; instant Stop clears public head but no task -> outer ability/dispatch with absent head. Engine queued Build now follows shared activation notification instead of append. Copied popped command receives owner cancellation on callback discard, releasing its presentation indicator; native packet/preview representation is not identical."},
   {"6f206a20","Payoff294: accepted Build rawcode enters6910c0 point factory. Combining this subtype with complete693490/67abe0 control flow proves shared activation publisher contract without a Build rawcode exception. Twelve supplied original append/dispatch cases complement actual293 Build native captures and263 Shift controls; complete UI Shift-Build placement/work remains separately open."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){String c=f.getComment();if(c==null||!c.contains(row[1]))f.setComment((c==null?"":c+"\n")+row[1]);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
  }
  for(String a:new String[]{"6f693490","6f67abe0","6f206a20","6f684a90"}){
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);
   out.append("BODY ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
