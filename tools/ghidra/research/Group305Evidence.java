// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Group305Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f170fa0","Mover_BindGroup","Payoff305 GROUP-04.6: original binding copies canonical group slot/generation into mover9c/a0; it does not own physical row removal or gameplay order. Engine restores a derived unit-to-group pointer after validating unique logical identities. This removes cold registry scans without serializing pointers or changing saved group encounter order. This is an engine complexity improvement, not a new retail save-layout claim."},
   {"6f16d1c0","PathGroup_PruneInvalidMembers","Payoff305 GROUP-04.6: original reverse preparation resolves each mover and compares its one canonical group identity, retaining stale rows until this observation boundary. Unique physical IDs make a cross-group active duplicate impossible: one mover cannot equal two distinct group identities. Engine save validation sorts temporary identity/sequence keys; it never sorts authoritative groups or member rows. Existing retired/tombstone validation remains intact."},
   {"6f16c060","PathGroup_AppendMemberRows","Payoff305 GROUP-04.6: dynamic physical storage and bounded canonical12 request storage are different contracts. This chunk changes only engine save validation and reconstructed lookup complexity; it does not widen native request or formation bounds. Full persistent physical-storage task remains open."}
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
