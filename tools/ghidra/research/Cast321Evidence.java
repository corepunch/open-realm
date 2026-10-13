// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Cast321Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f525000","Slow_GetBaseOrder","Payoff321: CAbilitySlow vtableb0c410 slot348 returns d006b=852075. Read-only JASS target dispatch207160 accepts this public order. Stock Sorceress UnitAbilities auto=Aslo starts enabled: public slowon852076 returns0; slowoff852077 returns1 before the cast. Repeated Cast321 maps preserve these exact return values and all original Captain320 motion."},
   {"6f40a7a0","Spell_CreateApproachAndCastTasks","Payoff321: ordinary unit target first tests spell range, then41d0a0 with half-angle cd53d4=3e4ccccd. In-range but not facing creates d0174 with FLT_MAX cd5480, rather than starting CHANNEL immediately. Public Captain320 Slow request3.49996090 starts CHANNEL/CAST3.59995842 after four zero-velocity facing commits; authored0.45 completes4.04994488. Initial bypasses are ability v34c bit02000000 or caster+60 bit8; their producer domains are not newly certified."},
   {"6f41d0a0","Unit_IsTargetInSpellFacingWindow","Payoff321: ECX source unit, stack4 target unit, stack8 half-angle pointer. Resolve target bridge through target virtualb8 then source+164 ->05b340; source is a unit, not an ability. Common spell producer40a7a0 passes3e4ccccd (0.2). Read-only Slow request returns0 and selects FLT_MAX target task."},
   {"6f41d070","Unit_IsPointInSpellFacingWindow","Payoff321 static: point-facing wrapper used by40a7a0 when already in range. Copies pointed X/Y to local pair and forwards to05b3a0. This investigation's live Slow capture exercises the unit-target sibling, not this point producer."},
   {"6f3fb5d0","Spell_BeginCastPhase","Payoff321: public Sorceress Slow retains authored8c=3ee66667 (0.45) and94=3f8a3d71 (1.08). After facing completion at3.59995842, CAST schedules effect4.04994488. Engine Slow procedure lacked A_CAST_PROFILE despite using the native common phase owner. The public engine replay matches four facing commits, all phase clocks and the complete retained Captain trajectory; reload tests rebuild Move/cast owners. Private AI coroutine persistence is not certified."},
   {"6f608ed0","Spell_DispatchPreparatoryOrders","Payoff321 static: collects candidate preparatory ability orders from the AUManaPotionAbilData list when caster admission flag4 allows it. Around an accepted transient order only, OR caster+60 bit8, dispatch688790, then clear bit8. This is the scoped bypass consumed by40a7a0/3fbb80, not a structure or immobility classification. Public producer coverage for this mana-item branch remains open."}
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
