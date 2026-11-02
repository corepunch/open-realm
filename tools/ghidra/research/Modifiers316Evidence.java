// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Modifiers316Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f5fb7c0","Move_AdjustSpeedFactor","Payoff316 NUM-03.2: retained Move78 changes by software ADD of signed contributions. Three complete public scenes, each two read-only repeats and unhooked control, verify stock Slow-.6/Bloodlust+.25 in both initial orders and authored-.37/+.17. Stock factor3f266666 returns432f7fff; custom factor3f4ccccc returns4357ffff. Removing custom haste leaves3f2147ad, not initial3f2147ae; inverse rounding history matters. Near-one normalization restores runtime1. Engine retains factor and captured status deltas in Save190. Below-one transition invokes group republication through5fcd50; that broader group lane remains open."},
   {"6f52d490","MoveSpeedBuff_ApplyContribution","Payoff316 NUM-03.2: cancel embedded eventdc; require applied20.400000 clear; resolve owner30 and Move1ec; set applied flag, copy retained buffd0 signed delta, then5fb7c0. Actual Slow and Bloodlust share this producer. The game status-refresh hook applies once and publishes once; a duplicated publication was caught by prior modifier88 exact motion at commit115."},
   {"6f5324c0","MoveSpeedBuff_RemoveContribution","Payoff316 NUM-03.2: cancel embedded eventdc; require applied20.400000; resolve owner30 and Move1ec; clear applied flag; XOR buffd0 with80000000 before5fb7c0. Captured contribution owns the inverse. Custom-.37/+.17 public removal gives speed432a1998 rather than initial Slow432a1999, then removes Slow back to43870000. Cold-save regression retains exact factor, deltas and application flags; broader buff/mover lifetime compositions remain open."}
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
