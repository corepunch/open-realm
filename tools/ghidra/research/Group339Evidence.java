import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
public class Group339Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong binary");
  String[][] rows={
        {"6f4959f0","Attack_CanAutoAcquireTarget","Payoff339: candidate Unit+ec acquisition-prevention count rejects ordinary acquiring players, but Town-owned attackers bypass this guard. Sleep public scene12 at tick568 has ec1/noninvulnerable candidate and never reaches exemption/ranking; Town scene5 at tick260 does. Two full1372-event repeats and98-marker uninstrumented control agree. Target-side admission is shared by engine notifications and nearest-enemy scans, independently of source hold-fire."},
        {"6f669570","Unit_AddAcquisitionPrevention","Payoff339 direct producers: natural-sleep524e00, Possession4dace0, Sleep/DarkConversion518290 and MechanicalCritter57f410 increment Unit+ec. Corresponding inverse sites694770 callers526940/4cc500/50da20/58dfa0. This counter is not generic inability to attack or move."},
        {"6f48e260","Unit_HasMobilePriorityOverride","Payoff339 active ACen public cast: Move query0, virtual18c1 and current speed0. RTTI confirms CBuffWeb derives CBuffEnsnareGround derives CBuffEnsnare, and WebAir derives CBuffEnsnareAir; all share5ea560. Engine preserves mobile priority only with retained Amov; removing binding restores authored speed. Runtime ACen evidence does not certify every Web public lifecycle."},
        {"6f49d680","Attack_GetAcquisitionPriority","Payoff339: Bens lookup enables derived-class matching, including Bena/Beng and Web. Binding removes Town high1000 without removing mobile priority. Frozen public in-range ACen key Townfbe04000:66000000, ordinary0be00000:64000000; engine out-of-range regression removes the already established range contributions04000000 and Town4000. Sleep prevention bit differs from mobility."},
        {"6f5fc890","CAbilityMove_GetCurrentSpeed","Payoff339 active ACen publishes zero current speed while mobile-priority override remains true. Engine applies zero to Ensnare/Web family as well as Roots; save/removal regressions retain priority and restore authored speed."},
  };
  for(String[] row:rows){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String prior=f.getComment();if(prior==null)prior="";
   if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);println(row[0]+" "+f.getName());
  }
 }
}
