import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
public class Group340Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong binary");
  String[][] rows={
        {"6f49e130","Attack_HandleTargetAvailable","Payoff340: minimum-range availability guard calls23a740 on CANDIDATE (ECX ESI), unlike explicit task validation. Require active-flight flag Unit5c.20000000 with forced-ground200<1, Attack20.8000, minimum24c less than candidate effective range by at least Math_PublicNearZeroThreshold, then predicted selector1 configured range. Rejection precedes49bc40. Public24-scene matrix crosses min0/120/600, ground/air, near/far and Amov removal: only scenes3/9 reject. Two1065-event repeats and146-marker uninstrumented control match. Engine tests first32 failures of640; saved/minimum-removal inverses now pass."},
        {"6f493380","Attack_MinimumIsBelowRange","Payoff340: fastcall ECX=&Attack248 scalar object, EDX=&effectiveRange. Returns AL: minimum at ECX+4 must be strictly below range, then abs(softwareSubtract(minimum,range))>=Math_PublicNearZeroThreshold (3a83126f). Equality and minimum above maximum disable the dead-zone check. Public min600/max500 bypasses; min120/max500 enters."},
        {"6f23a740","Unit_IsActivelyFlying","Payoff340: ECX Unit, return1 iff flags5c.20000000 and signed forced-ground count200<1. Availability49e130 applies minimum range when this is TRUE; explicit4968e0 and scan493cb0 apply their ground/deactivated-flight dead zone when FALSE. These opposite callers are not interchangeable."},
        {"6f49cab0","Attack_BindWeaponTiming","Payoff340: authored minimum EBX+2c is assigned through scalar object Attack248 virtual0, storing value24c. Unequal-to-zero comparison sets Attack20.8000, otherwise clears it. Public uamn0/120/600 survive creation and owner-change capture."},
        {"6f4968e0","Attack_ValidateTargetForTask","Payoff340 diagnostic public explicit Attack: near ground minimum120 returns83 (0x53), flying target bypasses the minimum; minimum0/600 and far cases return0. Whole public stream retained, but this chunk implements the availability guard only. Initial observer hook at496930 was inside this function and invalid; corrected entry4968e0 plus nullable-target handling yields complete repeat/control evidence."},
  };
  for(String[] row:rows){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String prior=f.getComment();if(prior==null)prior="";
   if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);println(row[0]+" "+f.getName());
  }
 }
}
