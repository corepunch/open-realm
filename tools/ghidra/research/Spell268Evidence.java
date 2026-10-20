// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Spell268Evidence extends GhidraScript {
 public static final String[][] NOTES={
  {"6f40a650","Spell_DispatchOrderShape","Payoff268 ORDER-01.13: ECX spell prefix, stack4 ScriptEventPacket*, RET4. Spell flags4 choose target/point packet resolution21b890 vs21b7f0. RAII overwrites input stack slot with temporary order reference; decompiler falsely treats the packet variable as the order. Retained order48/50 are copied at40a74f/755 before dropping temporary reference. Virtual400 then receives unit target, other widget identity, x*, y*. Holy Bolt vtable6fb61628+404 points here. This is static ownership evidence; current268 live hooks certify679cc0/user getter, not this function."},
  {"6f40a7a0","Spell_CreateApproachAndCastTasks","Payoff268 ORDER-01.13: ECX spell prefix, four stack args, RET10. Ordinary unit target selects captured virtual408 range and internal d0174; point selects virtual410 range and internal d016b; other widget branch appends d0161 and d016e. Internal cast tasks d0160/d015f/d0144 are ability-tagged; initial action0 and final action3 do not replace public command with Move. Important correction to earlier prose: existing185 repeats record range queries but ZERO spell-approach438680 entries. Initial spell production is40a7a0, while438680 is the d0161 continuation. Preserve old captures/expected words; do not claim that live185 observed438680. New268 point/flying public ownership witnesses do not certify full task timing or trajectory."},
  {"6f679cc0","Unit_DispatchAbilityOwnedOrder","Payoff268: two read-only original repeats and unhooked control agree on507 markers each across point Flame Strike, flying Druid Holy Light and pre-effect Stop controls. Each replaces an existing public Move with concrete spell head852488/852092. All494 native current-head results per observed run match their public marker values; Stop samples clear to0. Initial ordinary task creation is40a7a0, not438680 d0161 continuation. Engine legacy approach admission publishes the registry-resolved spell command only after accepted locomotion; rejected no-mana replacement preserves ownership. Full cast/channel and numerical lifetimes remain open."},
  {"6f2039d0","Jass_GetUnitCurrentOrder","Payoff268:1521 identical complete public markers across two observed repeats/unhooked control;1482 public head queries total. Point Flame Strike retains852488 through scene0 sample31, clears from32; flying Druid Holy Light retains852092 through scene2 sample20, clears from21. Both interrupted scenes clear at Stop sample10. Engine verifies bounded pre-effect ownership/inverse/save for both legacy shapes; no full cast-phase or trajectory match claimed."},
  {"6f438680","CAbilitySimpleSpell_BeginTargetApproach","Payoff268 correction: this function handles d0161 continuation, NOT every initial public spell. Original185 second/third repeats contain spell-range and spell-range-result rows but zero spell-approach rows; no live438680 entry was observed. Initial Holy Bolt virtual404=40a650 delegates virtual400=40a7a0, which directly constructs d0174 for ordinary unit targets and d016b for points. Preserve unchanged185 physical/public fixtures; static continuation evidence remains valid independently."}
 };
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong original");
  if(getScriptArgs().length!=1)throw new Exception("fresh export required");
  Path p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String original=f.getComment();String old=original==null ? "" : original;if(!old.contains(row[2]))old+="\n"+row[2];if(!old.equals(original))f.setComment(old);
   if(row[0].equals("6f40a650") || row[0].equals("6f40a7a0")) {
    String[] storage=row[0].equals("6f40a650") ? new String[]{"ECX:","Stack[0x4]:"} : new String[]{"ECX:","Stack[0x4]:","Stack[0x8]:","Stack[0xc]:","Stack[0x10]:"};
    var params=f.getParameters();if(params.length!=storage.length)throw new Exception("parameter count differs "+row[0]);
    for(int k=0;k<params.length;k++)if(!params[k].getVariableStorage().toString().startsWith(storage[k]) || params[k].getDataType().getLength()!=4)throw new Exception("ABI differs "+row[0]);
   }
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   out.append("PROTOTYPE ").append(f.getPrototypeString(true,true)).append('\n');
   for(var param:f.getParameters())out.append("PARAM ").append(param.getName()).append(' ').append(param.getDataType().getName()).append(' ').append(param.getVariableStorage()).append('\n');
   out.append("COMMENT ").append(f.getComment()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
