// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Nonfinite313Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f6945a0","CUnit_RefreshMoverPathingProfile","Payoff313 NUM-03.2: COMISS6945be and CMOVBE6945ca choose RuntimeOne for unordered authored radius, not merely radius<=1. Actual W3U qNaN7fc00001, sNaN7fa00001 and negative qNaNffc00001 each produce physical fine3d000000 in public CreateUnit. Authored getter retains all three exact words. Preserve separate authored radius and Move profile."},
   {"6f05c2e0","MoveBridge_SetCollisionRadius","Payoff313 NUM-03.2: Original scalar exponent conversion preserves positive infinity as finite fine7d000000; authored7f7fffff becomes7cffffff. The caller6945a0 first selects1 for unordered and negative inputs. Do not replace this conversion with hardware division or globally sanitize authored scalar data."},
   {"6f15fef0","Mover_SetCollisionRadius","Payoff313 NUM-03.2: Physical setter writes3d000000 for all three NaNs, negative infinity, negative FLT_MAX and zero. Positive infinity writes7d000000; positive FLT_MAX writes7cffffff. Repeated public producer, no synthetic helper invocation."},
   {"6f6742f0","CUnit_GetCollisionRadius","Payoff313 NUM-03.2: Public W3U exceptional collision cells retain raw7fc00001/7fa00001/ffc00001/7f800000/ff800000/7f7fffff/ff7fffff/00000000 through actual virtual168 getter. This authored value is distinct from the minimum physical radius."},
   {"6f203a60","Jass_GetUnitDefaultFlyHeight","Payoff313 NUM-03.2: Native registration20956a references stringa98314 GetUnitDefaultFlyHeight; MOV ECX203a60 at20956f. Resolve handle1eef90 then UnitProfile_GetMoveHeight678a80. W3U umvh must be Unreal(type2), not Real(type1). Authored exceptional words become actual JASS coordinate producer inputs without Frida memory writes or game calls. Mutable GetUnitFlyHeight203bf0 is a different getter."},
   {"6f04c330","Widget_ClampWorldPoint","Payoff313 NUM-03.2: COMISS lower,value plus JA and COMISS value,upper plus CMOVA preserve unordered NaN word on both axes. Decompiled lower<=value expression loses unordered behavior; use original instructions. Infinity clamps to bounds in this helper; axis natives bypass it."},
   {"6f058cd0","UnitMover_AdmitWorldPoint","Payoff313 NUM-03.2: Authored radius conversion at058eef..058f09 reads the raw scalar and subtracts exponent02800000 with underflow masking before16ecc0. Positive qNaN7fc00001 and sNaN7fa00001 become finite7d400001/7d200001 and select class3. Public placement at0,0 returns80,80 for these radii, including queryzero. Hardware division preserves NaN and incorrectly selects class0; engine now uses the existing software scalar conversion."},
   {"6f1dda80","Vision_ReadCellPlayerMasks","Payoff313 NUM-03.2 diagnostic1: public SetUnitPosition with GetUnitDefaultFlyHeight-produced sNaN7fa00001 reaches pose commit before access violation at1ddac1. This function indexes two ushort vision maps from supplied cell coordinates, with no local bounds check. Fault is in the synchronous visibility consumer, not proof of setter rejection or producer unreachability. Completed ground matrix: sNaN7fa00001 and negative qNaNffc00001 fault at1ddac1 after both-coordinate placement and constructor pose commits in two observed and two unhooked runs each. Positive qNaN7fc00001 completes. Engine matches committed pose but does not emulate the visibility access violation; NUM-03.2 remains open."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);String c=f.getComment();if(c!=null)c=c.replace("conversion at058f11", "conversion at058eef..058f09");f.setComment(c==null||!c.contains(row[2]) ? (c==null?"":c+"\n")+row[2] : c);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   out.append("BODY ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
