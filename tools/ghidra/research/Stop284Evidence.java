// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Stop284Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f673fe0","Unit_CancelActiveAndInternalTasks","Payoff284 MAP-04.2: retire active/internal tasks by broadcasting stack CEvent d0144 before constructing/broadcasting action0 d0162 and releasing internal chain174/178. This first d0144 reaches Move600340 even when Move20.4 is clear. Full Stop284 public repeats distinguish this cancellation recovery from the later new Stop task; do not fold both across public callbacks."},
 {"6f690490","Unit_DispatchCoreTask","Payoff284: core dispatcher d0004 invokes690b80 Stop task producer; d0162 invokes697ed0 action handler. Public replacement admission673610/673fe0 executes first recovery BEFORE player/unit issued Stop notifications; new Stop action0 and d0144 execute AFTER both notifications when no active replacement retained the task. Rooted structure has no Move subscriber; public258 unchanged."},
 {"6f690b80","Unit_CreateStopTasks","Payoff284: ECX unit, plainRET; prepend event d0148,d014a,d0178,d0144 then action0, giving action0 ->d0144 ->d0178 ->d014a ->d0148 execution. One new Stop cleanup, independent of673fe0 old-task cleanup. Read-only two16-scene repeats/control: callback terrain edits leave callback pose656,656 then new recovery admits624,592. Nested completed Stop allows outer cleanup too; active nested Move suppresses it. No rawcode or double-loop shortcut."},
 {"6f600340","Move_HandleCleanupTask","Payoff284: two ordinary public Stop69a840 calls both return to600433 inside this d0144 consumer, not arrival or virtual5fc120. First is cancellation673fe0, second new Stop690b80. Public JASS player/unit callbacks occur between scopes. Capture52 unit recoveries across16 scenes, including nested/replacement/removal/pause/death/teleport; only Stop-owned scope integrated by284, other owners retain explicit limits."},
 {"6f69a840","Unit_StopWithRecoveryAndSupport","Payoff284: attributed duplicate public Stop invocations to separate old-task and new-task d0144 dispatches, both600340 ->60042e call. Retains existing258 structure gate and247 bounded-recovery geometry. Late new-task cleanup observes callback terrain edits; no callbacks are repeated. Nested completed Stop may leave user head0 yet outer Stop cleanup still executes; active replacement Move owns its successor."},
 {"6f680320","Unit_AdmitOrder","Payoff284: replacement206b5e calls673610 then appends new user head. First Stop recovery occurs during cancellation before notifications. Reentrant issued callback can replace public head: nested active Move suppresses outer Stop producer690b80; completed nested Stop leaves no internal owner and outer producer still runs. Full read-only repeats and observer-free control, no state writes/game calls by observer."},
 {"6f67df00","Unit_DispatchInternalTaskQueue","Payoff284: execute Stop690b80 flat prepend chain after synchronous action0 notification. Callbacks may retire user head without retaining an active task (nested Stop); do not gate resumed outer cleanup solely on current order ID==stop. Engine scope separates initial retirement from task finish, retaining ordered event families."},
 {"6f5fc120","CAbilityMove_CancelOwner","Payoff284: Move vtable6fb62794+e0 points here. Retires timers/targets/subscriptions and69a840, conditionally pops/dispatches active4 task. Stop284 idle/active ordinary Stop does not enter this virtual; pause/death mutation traces do. Do not attribute ordinary duplicate Stop recoveries to this separate cancellation method."},
 {"6f05ca50","MoverBridge_StopWithRecovery","Payoff284: existing exclusion/recovery protocol unchanged; public Stop two scopes are separated by player/unit callback delivery, allowing new terrain to be admitted only by the second recovery. Complete original247/258 numeric expected rows retained; no generic unconditional duplication inside this bridge."},
 {"6f171340","Mover_StopAndInvalidatePath","Payoff284: physical cancellation helper still runs once per actual d0144 owner, with notify0 from05ca50. Two public Stop scopes belong to external order lifecycle; do not synthesize arrivals/notifications from this helper. Existing247/258 instructions and frozen poses unchanged."},
 };
 public void run()throws Exception {
  if(getScriptArgs().length!=1)throw new Exception("new output required");
  if(!currentProgram.getName().equals("game.dll"))throw new Exception("wrong program");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("COMMENT ").append(f.getComment()).append("\n");
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
