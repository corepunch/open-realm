// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Build297Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f543f00","UndeadBuild_OnStructureCompleted","Payoff297 ORDER-01.16 corrects Payoff296: d0032 is Harvest852018, NOT Smart851971. Virtual344 of Undead vtable6fb18a18; ECX Build ability, stack4 completed structure, RET4. Resolve retainedb0/b4, find Abgm on structure, require matching identity, reject Unit5c AI/Town bit4, create target Harvest69bd80 then admit680320. Three owned original twelve-scene public streams agree1938 markers each; two read-only repeats prove finish notification BEFORE this callback. Normal and Stop retain mine and issue Harvest; Move before/after one-second wait, Pause/resume, replacement Build and finish-callback Move clear retention. Removed Abgm leaves identity retained but prevents Harvest. Computer controller alone did NOT acquire5c.4, so that branch is static evidence and engine regression only. Exact routing/harvest slot timing excluded."},
   {"6f543e00","UndeadBuild_OnStructureRetired","Payoff297: virtual348, ECX Build ability, stack4 structure, RET4. Resolve retainedb0/b4; clear only if identity matches. Live KillUnit of Haunted Mine calls this, leaving one-second worker wait intact. RemoveUnit has no entry in the original capture; unresolved canonical identity cannot target a reused entity."},
   {"6f544350","UndeadBuild_BeginUnitWorkWait","Payoff297 adds Haunted Mine runtime evidence to Payoff296: third stack argument is non-null overlay parent. Bind parent through Abgm then retain NEW structure canonical identity at abilityb0/b4. Null overlay leaves retained words untouched in this body. Target IssueTargetOrderById custom Haunted Mine succeeds; point IssueBuildOrderById rejected in the discarded setup and is not a completion witness. One-second worker wait is separate from three-second structure completion. Thirteen begin calls per repeat include one replacement ordinary Ziggurat; retained identity already cleared by accepted replacement before that second begin."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);f.setComment(row[2]);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
  }
  for(String a:new String[]{"6f543f00","6f543e00","6f544350"}){
   var f=getFunctionAt(toAddr(a));out.append("BODY ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
