// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Scope300Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f697770","Unit_BeginScriptedPause","Payoff300 MAP-04.2: public200000 precedes688d30 admission. Stop284 original repeats and new read-only spatial repeats3/4 show nested Pause contributes three distinct Move recovery scopes: old-task cancellation, suspension construction, then first counted Move disable. Engine preserves the retained user packet across physical cleanup, with no rawcode recovery-count loop. Repeated already-suspended Pause does not recreate these stages."},
   {"6f690a50","Unit_CreateSuspensionTasks","Payoff300: d0005 producer tests positive Unit54, builds action0 then d0148/d014a/d0146/d0145/d014c/d014e/d0144 then action8. Internal prepend execution places d0144 cleanup before d0145 physical suspension and scripted ability-disable contributions. This second cleanup is separate from680320 old-task cancellation and5fc120 counted Move cancellation. Existing literal Stop284 header is unchanged."},
   {"6f69c490","Unit_OnSuspendBeginEvent","Payoff300: constructor cleanup precedes this d0145 stage. Set physical100000, acquire separation suppression, then6977a0 scripted ability disabling. New engine Pause cleanup preserves the suspended user packet and any release deadline; its intrinsic Move owner alone subscribes to counted disabling, avoiding duplicate engine-event delivery."},
   {"6f4076e0","Ability_AddDisableContribution","Payoff300: first nonpositive-to-positive3c transition invokes Move virtuale0 at5fc120 before40 orders count is stored. Engine uses existing counted disable owner rather than repeating cancellation on every Pause. New scope regressions assert every public recovery boundary against unchanged Stop284 scenes5/13, fresh and saved with caller fine-exclusion depths0/3."},
   {"6f5fc120","CAbilityMove_CancelOwner","Payoff300: Move virtuale0 cancels related targets/timers/subscriptions then69a840 at5fc18a. This is the third nested Pause recovery after the two d0144 task scopes. New read-only Stop284 repeats keep the same active fine object identity and rectangle through all four recoveries including outer Stop, returning its exclusion counter to0. Death/removal fine lifetime remains separate unfinished engine ownership."},
   {"6f600340","Move_HandleCleanupTask","Payoff300: frozen Stop284 Pause callback scenes5/13 contain two nested cleanup-task recoveries returning600433 and a third counted Move cancel returning5fc18a. Engine old-task cancellation and new suspension-task construction own distinct physical cleanup scopes on opposite sides of suspension notification. No retail fixture bytes or expected counts are regenerated."}
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
