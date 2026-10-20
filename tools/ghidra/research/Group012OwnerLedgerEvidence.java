// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Group012OwnerLedgerEvidence extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  if(!currentProgram.getName().equals("game.dll") || args.length!=2 ||
     (!args[0].equals("apply")&&!args[0].equals("read")))throw new Exception("game.dll apply/read fresh-output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("original binary differs");
  Path output=Path.of(args[1]);if(Files.exists(output))throw new Exception("output exists");
  String note="GROUP-01.2 existing-behavior acceptance, 2026-10-18: the two already accepted complete selected-independent-Shift captures retain exactly nine logical binding transitions across four physical owner identities with four distinct original path associations. Native U0 and U1 first own independent singletons; U1 departs and publishes a transient singleton; U0 departs and publishes a fresh shared owner; U1 rebinds directly from its transient owner into that fresh owner; both members subsequently leave naturally. The new read-only BZ_TESTS ledger reproduces all nine transitions, four owner generations and retirement only at the actual scheduled empty-owner preparation visit. Both original full motion journeys and warm/pre-merge/post-merge saved continuations pass 206156 assertions per Classic/TFT edition. This verifies existing production behavior and introduces no gameplay implementation change. The comparison is an explicit transition projection of the retained original captures, with raw ordinals and identity/pointer aliases preserved; it is not a new whole-trace equality or newly collected observer-free control claim. Broader moving Captain combat/retreat and arbitrary canonical storage/callback compositions remain in their existing GROUP-03.4/03.4.7.3/04.6 leaves.";
  StringBuilder text=new StringBuilder(note+"\n");
  for(String address:new String[]{"6f16b7b0","6f16d4e0","6f16c150"}) {
   var f=getFunctionContaining(toAddr(address));if(f==null)throw new Exception(address);
   if(args[0].equals("apply")){String old=f.getComment()==null?"":f.getComment();if(!old.contains(note))f.setComment(old+"\n"+note);}
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var reference:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(reference).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
