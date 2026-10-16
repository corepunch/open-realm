// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Path;
public class Work249Evidence extends GhidraScript {
 public void run()throws Exception {
  String[][] notes={
   {"6f5faaf0","Payoff249: complete callback attaches source pending, then ALL resolved inherited rows before marking peers ready in a second loop. Each peer readiness attempts publication with source pending. Original48-case matrix plus48 observer-free controls freezes12 exclusion boundaries per case; two real queued-input repeats each retain pending/success scopes. Engine reconstructs a bounded canonical request, retaining inherited physical ownership until source readiness."},
   {"6f170fa0","Payoff249: binding writes only the mover physical identity; no old-owner release or row removal. Original queued kernel keeps both old rows through successful rebinding; next real16d1c0 preparation removes them. Engine cold queued callback no longer retires the old owner synchronously."},
   {"6f169c50","Payoff249: queued callback readiness includes pending source and all inherited attachments in every fine exclusion scope.48 complete callback cases retain all three counters at0/1/1/0, first ready mask2 then6 then7. Independent observer-free controls retain exact physical partitions."},
   {"6f169d60","Payoff249: queued pending/success release re-resolves current fine objects and restores all counters. Old physical owners survive pending attempts; only successful source readiness rebinds peers. Engine uses canonical attachment storage through the complete scope, independent of physical cohort rows."},
   {"6f16d1c0","Payoff249: queued rebinding leaves old rows intact until normal reverse owner preparation. Native48-case callback matrix explicitly calls this original preparation after successful source readiness: old count2 becomes0. No immediate old-owner release or stale generation promotion is permitted by engine reconstruction."},
   {"6f16bcf0","Payoff249: delayed selected-queue reconstruction now shares canonical pending/readiness publication with immediate selected packets. Previous request history remains independent of the fresh physical identity. Frozen Work236 partitions and prior movement trajectories remain unchanged; full captain/target-region scopes remain open."}
  };
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);String old=f.getComment();if(old==null)old="";if(!old.contains(row[1]))f.setComment(old+"\n"+row[1]);}
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f5fa950","6f5faaf0","6f169620","6f16d850","6f89cd10","6f169c50","6f169d60","6f16bcf0","6f16b7b0","6f170fa0","6f16d1c0"}){
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);out.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
