// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Build295Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f2609b0","Player_PublishConstructStart","Payoff295 ORDER-01.16: thiscall player, stack4 constructing structure, RET4. Tests HasEventSubscriber8021a, creates event data, binds player/structure and dispatches Player_FireScriptEvent synchronously. Three incoming Build creation paths call this before adding construction work. Two read-only25-scene public captures and unhooked control match1270 markers each exactly. Start sees full structure HP and Build rawcode, inside workers already hidden. Human Move from callback retains current Move and leaves construction paused at10%; Stop is instant then Repair starts. Removal histories are captured, not generalized into other lifecycle claims."},
   {"6f54d0f0","Build_CreateStructureFromPointTask","Payoff295: successful677870 spawn is followed by685fc0 owner then2609b0 construct-start at54d460. Only after synchronous return does533490 construction ability initialize/attach and virtual340 at54d4c5 enter race work. Live Human544090 sees nested Move user head intact; instant Stop can be followed by real Repair. Inside worker depth is1 during start,2 after544140. Earlier decompiler argument recovery loses ECX owner: use saved disassembly."},
   {"6f54a5b0","Build_CreateStructureAtTarget","Payoff295: target-based creation calls player construct-start2609b0 at54a6d0 before construction ability initialization/attachment. Point-task live probe is the runtime witness; this alternate producer is mapped statically, not claimed as separately exercised."},
   {"6f54d9d0","Build_CreateSummonedStructure","Payoff295: summoned structure creation calls2609b0 at54da79 before533490 construction ability initialization/attachment and builder relocation. This alternate producer is mapped statically; autonomous summon clock remains separately open."},
   {"6f544090","HumanBuild_PrependRepairWorkOrder","Payoff295: original point creation invokes this after construct-start. Read-only observed normal, nested Move, nested Stop, RemoveUnit building, RemoveUnit worker scenes enter five times; actual409630 Repair work begins only normal and Stop. Thus a callback's live replacement must not be overwritten by eager engine Repair. No complete pending transient Repair resumption claim."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);String c=f.getComment();if(c==null||!c.contains(row[2]))f.setComment((c==null?"":c+"\n")+row[2]);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
  }
  for(String a:new String[]{"6f2609b0","6f54d0f0","6f54a5b0","6f54d9d0","6f544090","6f544140"}){
   var f=getFunctionAt(toAddr(a));out.append("BODY ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
