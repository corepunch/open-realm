import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
public class Group341Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong binary");
  String[][] rows={
        {"6f49e3a0", "Attack_CompareAvailableTargetPriority", "Payoff341: rejected nonzero priority candidates are stored as weak identity78/7c after stronger-retained or equal-distance rejection; same current target and zero candidate priority do not store. Two complete1065-event public repeats and82-marker control match."},
        {"6f493d20", "Attack_ScanForTarget", "Payoff341: only after ordinary scan finds no target, resolved alternative78/7c may be retried through the same priority filter. Source must be mobile, candidate live and enemy, committed configured range is d6bf48=2000. Alternative is not consumed when promoted: current6c/70 and alternative78/7c may alias. Engine fresh/save death/removal and fallback eligibility regressions exercise this path."},
        {"6f49b160", "Attack_TargetOwnerChanged", "Payoff341: clears alternative78/7c only when event unit equals resolved alternative. Alternative alone is not subscribed; public scene3 owner change leaves it stored. Scenes8/9 promote it to current first, then ownership change clears it. Current target recovery is conditional on candidate-to-source passive alliance."},
        {"6f49d280", "Attack_RecoverTargetTask", "Payoff341: releases current, then retains a resolved alternative only if nonforced recovery and4968e0 visibility-enabled validation succeeds. Unresolved stale identity is not a strong lifetime reference. Public current death/removal leads to outside-acquisition-range fallback."},
        {"6f49ed50", "Attack_CleanupTask", "Payoff341: task cleanup clears alternative78/7c. Public Stop, Move and Pause clear the remembered rejected candidate; removing that candidate alone leaves its weak raw identity unresolved. Engine stores incarnation with pointer and rejects recycled slots."},
  };
  for(String[] row:rows){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String prior=f.getComment();if(prior==null)prior="";
   if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);println(row[0]+" "+f.getName());
  }
 }
}
