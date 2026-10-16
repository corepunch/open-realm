// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Teleport290Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f9c0490","Jass_TeleportCaptain","Payoff290 BASE-01.4/GROUP-03.4.7.3: cdecl stack4/8 scalar pointers, plain RET. Resolve current AI context Town via identity2c/30, select attack Captain through9c64c0, then9d7ac0. No synthetic logical-position override. Valid owned AI context/CreateCaptains are producer prerequisites; missing context is not a demonstrated public null-safe path. Twelve composed original wrapper cases validate identity selection, raw pointer inputs, private local copies, exact placement arguments and caller stack ownership; terminal placement/identity payloads are controlled."},
 {"6f9c64c0","TownAI_ResolveAttackCaptain","Payoff290: ECX Town, no stack arguments; identity2ac/2b0 selects attack Captain through061320. TeleportCaptain uses this owner, not the defense slot. Original wrapper composes this body. Null identity returns null; native then assumes valid Captain."},
 {"6f9d7ac0","CaptainAI_TeleportActor","Payoff290: ECX Captain, stack4/8 raw scalar pointers, RET8. Copy dereferenced inputs to local scalars, call653510 with admit1/placement1/sentinel-1/zero flags. Distinct9d6ed0 takes FloatMini objects at+4 and follows placement with9d7600 Stop recovery. Two live repeats match14 boundaries;250 public markers match unhooked control. All seven teleports preserve actor velocity, physical group identity, Captain state/home/request/range/roster/targets. No05ca50 recovery during the native call. Covered in-flight/fractional/blocked/empty-roster relocations; no full trajectory or retail-save claim."},
 {"6f653510","Widget_WritePositionAfterAdmission","Payoff290: TeleportCaptain reaches this ordinary virtualdc admission/clamp then bridge world-position write. New actor pose is published while existing velocity and route/group ownership survive. This is not Unit public SetUnitPosition Stop admission. Full virtual dispatch and placement are observed live; the12-case wrapper oracle controls this terminal, so does not certify its geometry algorithm."},
 {"6f9d6ed0","CaptainAI_PlaceActorAtPoint","Payoff290 caller distinction: GoHome placement dereferences FloatMini+4 and invokes9d7600 after653510; explicit TeleportCaptain9d7ac0 dereferences raw script scalar pointers and does not stop. Keep those two operations separate in Move."},
 {"6f9c9c10","TownAI_ResetCaptainLocations","Payoff290 audit only: ResetCaptainLocs9bee60 calls this Town method. It obtains default AI locations through9bb750, updates selected homes and invokes9d7ac0 separately for attack and defense. It is not a demonstrated copy of current authored Captain homes. Default-location selection and full Reset lifetime remain existingBASE-01.4/GROUP-03.4.7.3; no implementation or public closure claimed here."},
 {"6f9bee60","Jass_ResetCaptainLocs","Payoff290 audit only: no stack args, context identity2c/30 resolves Town, tail-call9c9c10. AI native registration9be2fd references ResetCaptainLocs stringb85124 and this entry. Default-location producer remains open."}
 };
 public void run()throws Exception {
  if(!currentProgram.getName().equals("game.dll")||getScriptArgs().length!=1)throw new Exception("game.dll and fresh export required");
  var output=Path.of(getScriptArgs()[0]);if(Files.exists(output))throw new Exception("output exists");
  StringBuilder s=new StringBuilder();
  for(var row:NOTES){var a=toAddr(row[0]);var f=getFunctionAt(a);
   if(f==null){disassemble(a);f=createFunction(a,row[1]);}if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   s.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');s.append("COMMENT ").append(f.getComment()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);while(it.hasNext()){var i=it.next();s.append(i.getAddress()).append('|');for(byte b:i.getBytes())s.append(String.format("%02x",b&255));s.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(a))s.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(output,s);println("Saved "+output);
 }
}
