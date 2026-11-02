// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.listing.CodeUnit;
import java.nio.file.*;
public class Build293Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] rows={
   {"6f544090","HumanBuild_PrependRepairWorkOrder","Payoff293 ORDER-01.16: thiscall ECX Build ability; stack4 new building, stack8 task, stack12 forwarded unused word; RET12. Retires passed task through418b40, resolves owning unit30, queries committed position and calls virtual180 relocation. Creates target Repair via69bd80 with commandd0038, current player, building target, zero point/association/retained.691c70 prepends the real Repair user head with flags1/1. Two repeated public five-style captures/control retain737 identical markers each. Human construct-start still exposes building rawcode; immediately following sample exposes852024 until finish/interruption. This is not generic Move identity or publisher-history inference. Original pending Build under Repair, construction notification order and wider inverse/refund compositions remain ORDER-01.16; engine293 integrates only accepted head/work ownership and validated replacement queues."},
   {"6f206a20","Jass_IssueBuildOrderById","Payoff293 ORDER-01.16: public native preserves old user chain on failed placement/validation; accepted rawcode is passed unchanged into6910c0 then680320 mode1/dispatch1. New five-style observed repeats and observer-free control preserve737 public markers; nested active Move in Build-issued callback survives outer native return. No numeric fixture words rewritten."},
   {"6f4358a0","Repair_GetTargetOrderID","Payoff293: primary Human construction hands off through544090 to this public Repair852024 identity. Concrete Repair owns work head; inside Orc/NightElf/Naga keep the structure rawcode. Undead retains its structure head only for summon work; exact retail work clock is outside this ownership patch."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:rows){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);String c=f.getComment();if(c==null||!c.contains(row[2]))f.setComment((c==null?"":c+"\n")+row[2]);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
  }
  for(String a:new String[]{"6f206a20","6f544090","6f4358a0","6f680320","6f691c70","6f544140","6f566dd0"}){
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);
   out.append("BODY ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
