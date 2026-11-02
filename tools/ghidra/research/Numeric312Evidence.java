// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Numeric312Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f674280","CUnitType_GetCachedCollisionRadius","Payoff312: Cached row+19c is authored world radius. 320 repeated finite-input scenes retain negative values, signed zero, .0001, .5 and up to65536; no positivity gate or physical minimum here."},
   {"6f6742f0","CUnit_GetCollisionRadius","Payoff312: Virtual168 copies authored profile radius unchanged; repeated1600 getter results distinguish physical radius from public placement radius, including SetUnitPathing(false)."},
   {"6f6945a0","CUnit_RefreshMoverPathingProfile","Payoff312: COMISS radius against RuntimeOne and CMOVBE select minimum1 world unit before05c2e0. Two physical setter publications per actor repeat for all320 scenes. Scale(-2,-2,-2) does not update collision radius."},
   {"6f05c2e0","MoveBridge_SetCollisionRadius","Payoff312: ECX bridge, stack scalarpointer, exponent conversion to fine units then canonical resolution/15fef0. This conversion has no min1 policy; caller6945a0 owns that clamp."},
   {"6f15fef0","Mover_SetCollisionRadius","Payoff312: Repeated finite scenes expose physical fine radius=.03125 for authored values<=1; otherwise authored/32 through65536. Writes geometry and zero-displacement publication. Authored negative placement radius remains separate."},
   {"6f6a8ca0","UnitData_GetAuthoredCollisionRadius","Payoff312: Thiscall typed object getter for field descriptor+3f8 delegates6ad270;66bf40 copies returned scalar into cached profile+19c. No clamp in this wrapper."},
   {"6f6ad270","ObjectData_ReadInheritedScalar","Payoff312: Actual field-key presence chooses current object value6ae800; absent lookup recurses via parent6ac7e0 or default199f00. Presence is not determined by scalar positivity."},
   {"6f204100","Jass_GetUnitX","Payoff312: numeric312 observer captures1920 EAX X returns through actual native calls. Create/SetUnitPosition clamp world0..2016 in authored64-cell map; axis setters expose -16384 and32768 unchanged."},
   {"6f204140","Jass_GetUnitY","Payoff312: numeric312 observer captures1920 EAX Y returns; authored negative/outside points clamp in public placement, while SetUnitY preserves -512/2560/2048.0002 bits via predicted pose query."},
   {"6f215900","Jass_SetUnitX","Payoff312: 320-scene finite probe preserves out-of-range worldX via direct world pose setter; no terrain bounds clamp. Public CreateUnit and SetUnitPosition use distinct admission producer."},
   {"6f215960","Jass_SetUnitY","Payoff312: 320-scene finite probe preserves outside worldY after sequential axis setters, order0/zero velocity unchanged. Signed-zero radius behavior belongs to geometry profile, not coordinate getter."}
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
