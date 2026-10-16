import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;

public class Clock275Evidence extends GhidraScript {
 public static final String[][] NOTES={
  {"6f001c40","SimClock_InitConfiguredSpan","Payoff275 SCHED-01.1: registered startup tail MOV EDX,300; ECX d3c7d8; JMP070d80. Full owner157610 passes this initialized scalar into primary14 and presentation68 constructors156ca0; both spans are43960000. The oracle executes this initializer before creating its new owner; the generic spatial rig alone does not initialize the span."},
  {"6f156ca0","SimClock_ConstructAgentRequests","Payoff275: thiscall ECX clock, stack span-pointer/debug-queue/debug-signals/capacity, RET10. Initializes time40=zero, epoch44=0, span48=*span, flags4c=0, serial50=0. Full157610 constructor calls this for owner14 and68 with initialized d3c7d8=300. Empty supplied timer heaps in the clock-wrapper oracle do not claim public heap producers."},
  {"6f213350","Jass_SetGameSpeed","Payoff275: cdecl stack4 input; plain RET. World d687a8 -> setup30, flags38 bit4000 MAP_LOCK_SPEED skips the entire setter. Otherwise2300e0 narrows to unsigned BYTE and clamps above1 to2.1542 full original cases include byte wrap, unrelated flags and locked writes. Public inputs0..4 read back0,1,2,2,2 in repeated unmodified retail map/control. Old engine round-trip FAST3 expectation contradicted this evidence."},
  {"6f2300e0","GameSetup_SetSimulationSpeed","Payoff275: thiscall ECX setup, stack4 narrowed BYTE, RET4. Store canonical byte47=min(byte(input),2), then32ce50 with canonicalECX. Inputs256/511 retain byte conversion; negative public integers pass their low byte. Lock belongs to213350, not this private setter."},
  {"6f2001d0","Jass_GetGameSpeed","Payoff275: plain RET, EAX zero-extended setup byte47 through d687a8->30. ConvertGameSpeed1f91d0 is identity. Public selected3/4 therefore return ConvertGameSpeed(2), as do complete original setter/getter executions."},
  {"6f32ce50","NetScheduler_SetSimulationSpeed","Payoff275: fastcall ECX speed index, plainRET. TLS slotD registry+10 entries[2] supplies network owner. Store canonical index22a8; tablece54dc={3,4,5}; binary32 fraction22ac=table[index]/5. These rate values govern host scheduling; primary event scalar remains .005."},
  {"6f32d670","NetScheduler_AdvanceHostTime","Payoff275: thiscall ECX network owner, stack4 host-time integer, RET4. For each available25-ms host quantum, emit ce54dc[rate] * optional replay factor original40090092 events; each event carries d69c48=.005. Public read-only host entry/exit rows show nonzero primary counts in multiples3/4/5 for rates0/1/2. Speed changes never multiply Move delta. Renderer-facing async event40090094 is separate; no fixed presentation cadence inferred."},
  {"6f15c650","AgentOwner_SelectRequestClock","Payoff275: original identity sign controls owner14 versus68. Five deliberate identity inputs check both returns and300 spans; public map records both selectors. Engine path movers retain primary simulation time. No gameplay presentation-agent producer is invented."},
  {"6f04c1a0","Game_AdvancePrimaryClocks","Payoff275: complete original wrapper preserves primary5-ms input. Config38 nonzero skips all four clocks. Supplied empty timer heaps and independent gate/clock pause-bit matrix retain exact32 ordinary/paused timeline outputs. Public primary words replay original addition for each completed advance, including callback-before-advance markers."},
  {"6f04c0d0","Game_AdvancePresentationClocks","Payoff275: config3c gate; presentation request68 and callback110 independently advance. Both300-second configured spans come from the same startup scalar. Public UI pause retains both clock words over settled two-second interval while renderer callbacks continue. A current host batch may finish after pause entry; the evidence does not discard its final5 primary events."},
  {"6f3719c0","Game_ApplyPause","Payoff275: public PauseGame(true/false) pair and F10/Escape menu use this pause boundary. Repeated settled menu-pause heartbeat samples retain exact primary/presentation words and owner counter. The current host batch finishes before the settled pause; do not assert zero work immediately at the entry hook. A separate Pause-key attempt failed to resume a scripted pause and remains diagnostic only."}
 };
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong original");
  if(getScriptArgs().length!=1)throw new Exception("fresh export required");Path path=Path.of(getScriptArgs()[0]);if(Files.exists(path))throw new Exception("exists");
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES){
   var address=toAddr(row[0]);var f=getFunctionAt(address);if(f==null)f=createFunction(address,row[1]);if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   var old=f.getComment();if(old!=null)old=old.replaceAll("(?m)^Payoff275[^\\n]*\\n?", "").stripTrailing();
   f.setComment(old==null||old.isEmpty()?row[2]:old+"\n"+row[2]);
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append(' ').append(f.getPrototypeString(false,true)).append('\n').append(f.getComment()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(address))out.append("XREF ").append(ref).append('\n');
  }
  createLabel(toAddr("6fce54dc"),"Simulation_PrimaryStepsPerHostQuantum",true);
  Files.writeString(path,out);println("Saved "+path);
 }
}
