// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Stop285Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f2155c0","Jass_SetUnitPosition","Payoff285 BASE-01.4/MAP-04.2: public native creates/admit Stop via6803f0(1), delivering player/unit issued callbacks at old pose, then vtable180 writes requested scalar position. A callback-issued active Move survives outer placement; axis-only callback writes precede it. Never replace this owner with silent internal cleanup. Both coordinate and Loc witnesses retain original public chronology."},
 {"6f215620","Jass_SetUnitPositionLoc","Payoff285: complete live Loc producer resolves location and delegates2155c0, preserving identical Stop notification/recovery-before-position order. Location ownership remains script-owned; no implicit RemoveLocation. Invalid location remains no-op."},
 {"6f6803f0","Unit_ReplaceOrdersForStop","Payoff285: creates ordinary d0004 Stop, owner from virtualec, replacement flag1 and dispatch1 to680320. Ordinary request completes old recovery, issued notifications, new recovery BEFORE caller placement. Paused positive-depth admission does not execute these scopes or publish Stop events at this call."},
 {"6f698050","Unit_SetPositionWithAdmission","Payoff285: this placement virtual is independent of preceding public Stop. It executes even when a synchronous issued callback replaced the command; retained active Move head survives, starts from new position. Original2155c0 has no current-head==Stop gate around this write. Structure/cargo/removed/private support cases retain separate limits."},
 {"6f693490","Unit_AppendUserOrder","Payoff285: forced-position6803f0 uses same append/control semantics. Positive suspended depth680320 branch preserves current suspension head and appends Stop without immediate public events; observed paused native placement changes pose while current851973 remains. Wider queue controls remainORDER02.2. Public resume witnesses execute retained Stop after unpause: callbacks see the placed pose, terrain edits recover before final idle, nested completed Stop resumes outer completion. Physical resume timing retains its existing separate clock contract."},
 {"6f680320","Unit_AdmitOrder","Payoff285: public forced-position Stop is a real ordinary replacement and shares673610/690b80 task lifetime. Paused depth branch cancels queued successors and appends new user order rather than performing old/new recovery or issued notification immediately. No position-native specific double-recovery loop."},
 {"6f9d6ed0","CaptainAI_PlaceActorAtPoint","Payoff285 caller audit: virtual Captain relocation is Widget_WritePositionAfterAdmission then9d7600, not public unit2155c0/6803f0 Stop admission. Engine GoHome must retain its internal cleanup/placement entry and emit no player/unit issued Stop event. Read-only intermediate engine capture exposed one spurious public publisher call; focused actual JASS subscription regression and final capture prevent it. Full private Captain placement/recovery chronology remainsGROUP03.4.7.3."},
 {"6f9d7600","CaptainAI_StopActorWithRecovery","Payoff285: after virtual actor placement, virtuald0(1), virtuald4 support query, bridge05ca50 with654060 callback, virtuald0(0). No public unit Stop admission or issued callback in this wrapper. Distinct from2155c0; existing Captain physical/recovery ordering retains its separate task rather than being claimed fully ported here."},
 };
 public void run()throws Exception {
  if(getScriptArgs().length!=1)throw new Exception("new output required");
  if(!currentProgram.getName().equals("game.dll"))throw new Exception("wrong program");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("COMMENT ").append(f.getComment()).append("\n");
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
