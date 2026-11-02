// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Target371GroundQueueEvidence extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  if(!currentProgram.getName().equals("game.dll")||args.length!=2||(!args[0].equals("apply")&&!args[0].equals("read")))throw new Exception("game.dll apply/read fresh-output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("original binary differs");
  Path output=Path.of(args[1]);if(Files.exists(output))throw new Exception("output exists");
  String note="TARGET371 final typed AIImmediate queue parity: both complete Ground queue observations match all1815 semantic records and unhooked control370 public markers. Fresh fully flushed birth-proven observations/control match all102 public markers and639 whole semantic records, including literal callers and returnProof code bytes/operands; this supersedes the earlier99-marker prefix limitation without discarding those captures. Signed Unit198 scope is acquired before d0194 construction, popped before release, and returns before common user FIFO activation. Initial Town Stop precedes guard enrollment; AIImmediate851974 follows enrollment; its public callback can relocate the unit before Guard851983 activation at depth0 with a following851974 retained. Common replacement6802f0 and append693450 now share a typed saved packet, immediate callback frontier, incarnation/replacement validation, and post-body FIFO advance. Internal no-Town Stop preserves the FIFO and existing Attack Stand policy without a public Stop notification or legacy guard-anchor mutation. Proper preport birth regression failed20 behavior assertions per edition with setup guards green; final compiled engine passes birth1test38assertions and full Ground43tests11838assertions in both Classic/TFT. New queue capacity/save regression is postport protection, not a claimed preport red. No new persisted fields are required; the static movement boundary uses existing F_MMOVE module-relative relocation. Full-map journey parity and broader ordinary Attack anchor policy remain outside this queue extension.";
  String[] addresses={"6f9bd6e0","6f680320","6f693490","6f691260","6f688d90","6f6785c0","6f67df00","6f67de20","6f694a10","6f67abe0"};
  StringBuilder text=new StringBuilder(note+"\n");
  for(String address:addresses){var f=getFunctionAt(toAddr(address));if(f==null)throw new Exception(address);
   if(args[0].equals("apply")){String old=f.getComment()==null?"":f.getComment();if(!old.contains(note))f.setComment(old+"\n"+note);}
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var reference:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(reference).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
