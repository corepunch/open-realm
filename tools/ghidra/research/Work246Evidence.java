// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Path;
public class Work246Evidence extends GhidraScript {
 public void run()throws Exception {
  String[][] notes={
   {"6f16cd30",null,"Payoff246: target refresh retains Mover_PredictFinePosition plus fine target_offset directly; no world projection occurs. Two negative-origin public Smart captures are identical:13 sampled refresh vectors lose words if projected through origin -8192 and cell32. Engine physical group goal now remains fine through creation, representative selection, sampling and group routing."},
   {"6f16db00",null,"Payoff246: point setter copies supplied fine vector into target_offset; group construction owns fine coordinates independently of world presentation. Engine save162 stores canonical fine group goal; older saves reject instead of reinterpreting world values."},
   {"6f16de50",null,"Payoff246: mean/source and destination deltas are all fine coordinates. Canonical200 seeds formation point from exact destination, without a target-only/world-coordinate exception."},
   {"6f16ce10",null,"Payoff246: receives exact sampled fine vector and passes it to destination gate/168b80. Engine explicit fine_target overrides world request reuse: distinct fine points can have identical rounded world projections."},
   {"6f168b80",null,"Payoff246: destination publication copies supplied fine words. Negative-origin public capture preserves the sampler words through group route input and accepted path destination; retained cadence and admission gates remain independent."}
  };
  for(String[] row:notes) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(row[1]!=null)f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";
   if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
  }
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f16cd30","6f16db00","6f16de50","6f16ce10","6f168b80","6f167120"}) {
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);
   out.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
