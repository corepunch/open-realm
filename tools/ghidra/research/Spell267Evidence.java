// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Spell267Evidence extends GhidraScript {
 public static final String[][] NOTES={
  {"6f679cc0","Unit_DispatchAbilityOwnedOrder","Payoff267 ORDER-01.13: incoming user order24 is copied into event+8 at679d07..679d18, order pointer into event+c at679d1b, then selected ability virtualc receives the packet679d2b. Public spell command and internal approach task are independent. Existing Frida S184 archive has three complete raw repeats and observer-free control; Holy Bolt accepted852092 and remains852092 through approach (scene0 samples1..36; scene2 samples1..39), later0. Engine ground target spell owner now records the registry-resolved public command on accepted physical approach, including aliases and command-card path; old Move identity cannot leak. Full cast/effect/channel timing and other shapes remain open."},
  {"6f438680","CAbilitySimpleSpell_BeginTargetApproach","Payoff267 ORDER-01.13: range-false path4387b4..4387c1 pops INTERNAL head691260 and submits target taskd0174 via6926b0 at4387fb; it does not substitute public Move d0012 or erase the user head. S184 public order remains852092 until spell lifecycle retires. Engine internal Move continues to own routing, but spell owns current_order_id. Existing physical motion185 fixture unchanged; no new full effect/cast deadline claim."},
  {"6f2039d0","Jass_GetUnitCurrentOrder","Payoff267 ORDER-01.13: S184 live public Holy Bolt reports852092 immediately and at all pre-effect approach samples in both fixed and moving-target scenes, then0. Three complete observer repeats and one observer-free control agree. This registered getter resolves Unit19c/1a0 USER head and reads order24; internal d0174 movement is not public Move. Engine regression uses this native through JASS, command card and alias admission, Stop/replacement/removal/death, saved approach, rejected replacement and queued successor."},
  {"6f6857e0","Unit_ResolveOrderAbility","Payoff267: instruction-checked ability selection for incoming order. ECX Unit, stack4 order, RET4; order24 public command at6857f3. Target and point forms walk Unit1dc abilities and compare order38 identity against ability virtuala0, then validate command/target/point through concrete virtual methods. Returned ability receives679cc0 packet with original public command. No rawcode-specific current-order constant belongs in spell locomotion; registry order mapping supplies engine primary command. Other selection branches are not newly runtime-certified."}
 };
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong original");
  if(getScriptArgs().length!=1)throw new Exception("fresh export required");
  Path p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String original=f.getComment();String old=original==null ? "" : original;old=old.replace("at4387f9","at4387fb");if(!old.contains(row[2]))old+="\n"+row[2];if(!old.equals(original))f.setComment(old);
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   out.append("PROTOTYPE ").append(f.getPrototypeString(true,true)).append('\n');
   out.append("COMMENT ").append(f.getComment()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
