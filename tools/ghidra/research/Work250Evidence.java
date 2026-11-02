// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Path;
public class Work250Evidence extends GhidraScript {
 public void run()throws Exception {
  String[][] notes={
   {"6f693490","Payoff250: Shift append on an existing internal head only notifies it; no issued event until queued head activation. Two read-only Work250 UI repeats show busy source appended with count2 at tick29 and issued only at tick81, while idle selected heads issue immediately."},
   {"6f67abe0","Payoff250: issued callback precedes the post-callback internal-task-head test, task construction and cold cohort lookup. Nested same-point Move creates its own task, so outer dispatch skips. Instant Stop retires the user head but leaves no internal task: outer point construction resumes with public head absent. Two normal and two callback UI repeats independently witness both branches."},
   {"6f67c230","Payoff250: selected Shift point event belongs to head activation, before Move task construction. All player/unit subscribers retain original point payload across nested same-point Move or instant Stop. Public head already reports Move at entry; Stop callback reports zero after returning."},
   {"6f5ffb60","Payoff250: this point-task handler runs after issued callbacks. Work250b same-point nested Move executes once and takes precedence; instant Stop returns with no user head, after which the original point task still runs and cold cohort lookup occurs. Engine distinguishes task replacement from public head retirement."},
   {"6f5fa950","Payoff250: cold queued-cohort search follows issued notification and point-task construction. Busy enqueue invokes neither issued callback nor search. In Stop reentry, search still runs after Stop has retired the public user head; do not infer task cancellation solely from current order ID zero."}
  };
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);String old=f.getComment();if(old==null)old="";if(!old.contains(row[1]))f.setComment(old+"\n"+row[1]);}
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f693490","6f67abe0","6f67c230","6f5fd270","6f5ffb60","6f5fa950"}){
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);out.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
