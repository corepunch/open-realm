// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Path;
public class Work251Evidence extends GhidraScript {
 public void run()throws Exception {
  String[][] notes={
   {"6f66fdd0","Payoff251 TARGET-03.1: independent flags1 skip fog and flags2 skip detection; TLS13.200 ORs flag1. Complete original composed visibility query covers6912 controlled cases; engine uses named policies with576 mode4 fixture rows. World3e0 is a lifecycle boolean, not a fog-enable flag."},
   {"6f1dd920","Payoff251: original1dd920+1ddff0+1ddee0+1e0b80 execute unmodified in6912 policy cases; flags4 only forces owner/detection evaluation and never changes its Boolean result. Reveal fallback remains outside this world query."},
   {"6f1ddff0","Payoff251: original detection predicates distinguish observer shared-owner mask from detector mask. Detection-only Attack policy uses flag1; ignore-detection policy2 still requires the vision cell unless TLS/global fog state independently bypasses it."},
   {"6f5fbad0","Payoff251: ability validation is not public native acceptance. New repeated public scenes give DD for self/dead and AA for hidden, yet Move flags6 normalizes them to NULL-target point orders. Smart rejects all three and preserves active task. Captured validation type/query branch table retained without rewriting its earlier witnesses."},
   {"6f207160","Payoff251: fallback is not limited to BA invisibility/fog. Two read-only eight-scene repeats and an observer-free control agree on39 public markers: self DD, hidden AA, dead DD all become NULL-target Move packets; visible control keeps identity. Smart counterparts reject and preserve original route. Removed invalid handles remain rejected upstream."},
   {"6f5fd270","Payoff251: optional target filtering includes self, dead and world-hidden before flags1 detection query; subsequent flags0 query selects physical target versus retained point. Engine normalizes rejected public Move before immediate/Shift FIFO mutation and prevents queued optional self binding."},
   {"6f1dfaf0","Payoff251 global visibility producer: clears world+3e0 at1dfaf9 before teardown/reset operations. This is separate from Vision_SetFogEnabled and TLS show-map. No lifecycle timing claim is inferred from the isolated query oracle."},
   {"6f1e05a0","Payoff251 global visibility producer: clears world3dc/3e0/3e4 together at1e05bc..1e05d0 during world/map transition preparation. The66fdd0 early gate reads this3e0 Boolean."},
   {"6f1e3730","Payoff251 global visibility producer: load-game path allocates d687a8 if absent, then sets world+3e0=1 at1e37e6 before war3map state loading and final pathing initialization. Static producer evidence, not a fresh load-game runtime capture."},
   {"6f1e5a10","Payoff251 global visibility producer: fresh-map start sets world+3e0=1 at1e5a47 before configured player/map setup; later terrain/fog enabling is independent. Static complete-body inspection."},
   {"6f1eab40","Payoff251 global visibility producer: sets world+3e0=1 at1eab56 before installing the fixed target-hidden callback23a760 at1eab6f. Together with reset/load/start writers, resolves the handoff Q1 gate producer."},
   {"6f1e9930","Payoff251: thiscallECX world, plainRET; exact two-instruction setter writes3e0=1. Kept separately from map lifecycle callers; no invented gameplay producer."}
  };
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null && row[0].equals("6f1e9930"))f=createFunction(toAddr(row[0]),"World_EnableWidgetVisibilityQueries");if(f==null)throw new Exception(row[0]);String old=f.getComment();if(old==null)old="";if(!old.contains(row[1]))f.setComment(old+"\n"+row[1]);}
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f66fdd0","6f1dd920","6f1ddff0","6f5fbad0","6f207160","6f5fd270","6f1dfaf0","6f1e05a0","6f1e3730","6f1e5a10","6f1eab40","6f1e9930","6f1ddee0","6f1e0b80","6f699b20","6f5fb940","6f66be80","6f37a4e0"}){
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);out.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
