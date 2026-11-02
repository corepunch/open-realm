// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Path;
public class Work245Evidence extends GhidraScript {
 public void run()throws Exception {
  String[][] notes={
   {"6f16d7e0",null,"Payoff245 integrates FORM-01.3 whole-image scan: no rel32 CALL/JMP and no aligned or unaligned absolute entry reference in any stock section. Both initializer spacing sets are word-identical40b00001/40000000/40200000. No mapped stock producer reaches alternate spacing; pointer synthesis or modified binaries are outside this result."},
   {"6f16dc90",null,"Payoff245 policy inventory: canonical10 disables group coarse warp edges; target unit Adro producers retain1010/1811, point CArtilleryLine uses the distinct no-warp producer.05a5c0 sixth argument [ebp+1c] enables persistent1/800; seventh [ebp+20] sets10 when zero. Historical caller-built witnesses and Payoff149 engine routing/save remain frozen."},
   {"6f15f660","Mover_IsPointInFacingWindow","Complete original query predicts own center, compares squared displacement against cd5470=3456bf95, then wrapped heading magnitude against caller half-angle with the same deadzone. Equality passes. No position or velocity mutation. Payoff245 executes complete function/callees without stubs on320 supplied cases."},
   {"6f05b340","MoveBridge_IsTargetInFacingWindow","ECX source bridge,stack4 target bridge,stack8 half-angle pointer,RET8. Resolves both movers, predicts target, delegates15f660. Attack495180 supplies Misc.AttackHalfAngle (observed3f000000). Separate from Move arrival tolerance3e4ccccd."},
   {"6f05a5c0",null,"Payoff245: compare unconverted authored range to runtime FLT_MAX at05a666 before radius addition/divide32; equality sets canonical200. Native public in-range Attack creates nonpersistent1200, retained b0=7cffffff; numeric range alone does not encode bypass policy."},
   {"6f16a790",null,"Payoff245: group200 calls171060 forced arrival before route step and restores temporary turn override afterward. This bypasses range only;16e910 still gates heading<=3e4ccccd. Two read-only public Attack repeats retain6/60/1 initial zero-velocity commits at stock/slow/aligned facing, then weapon windup; no local routing."},
   {"6f49a240",null,"Payoff245: four static caller sites are49529b/495329 and49a4d0/49a4e4; true turn_only selects FLT_MAX. Repeated public scene captures show five later true requests via49a4e9; initial Move approach also uses same range sentinel. Engine captures200 before conversion and delegates facing-only movement to physical owner; whole Attack readiness/task timing remains TARGET/ORDER scope."},
   {"6f495180",null,"Payoff245: ready in-range target fails05b340 AttackHalfAngle predicate ->49a240(target,1), then physical200 owner enforces its narrower independent .2 arrival angle before retry. Public stock/slow/aligned scenes preserve heading and XY words; frame/weapon timer scheduling is independent."}
  };
  for(String[] row:notes) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(row[1]!=null)f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";old=old.replace("at05a669","at05a666");
   if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
  }
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f15f660","6f05b340","6f05a5c0","6f05b970","6f16a790","6f171060","6f49a240","6f495180","6f49a390","6f16d7e0","6f16dc30","6f16dc50","6f16dcb0","6f16dc70","6f16dc90","6f16dde0","6f16dd00","6f16dc00","6f16ddb0"}) {
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
