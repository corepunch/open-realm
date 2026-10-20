// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Captain320Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f428120","Spell_NotifyUnitTargetBeforeEffect","Payoff320: common unit-target effect preparation calls v38c policy, then checks TARGET player -> CASTER player diplomacy. Suppress only when passive38 and shared-spells78 both set (alliance types0 and4, stride16 verified at2016a0). Builds zero-amount packet flags10 and dispatches target v120 before shield/concrete effect. Repeated Slow completion at4.04994488 reaches4935e0 and49bc40; captain singleton then bypasses shared minimum, keeping270 rather than173."},
   {"6f40a2b0","Spell_DispatchConcreteEffect","Payoff320: unit receiver branch calls428120 before shield414/48b9e0 and concrete424. Attack notification belongs to spell target dispatch, not timed-buff refresh or shared speed accumulation."},
   {"6f3fa740","Spell_ShouldNotifyUnitTarget","Payoff320: default v38c returns1, shared by Spell/SimpleSpell, Slow, InnerFire and Polymorph. This is not a harmful-buff predicate. Projectile subclasses have separate zero-return overrides."},
   {"6f68bfa0","Unit_GrantsPassiveAllianceToSource","Payoff320: target ECX, caster stack argument. Uses target player58 and source player getter to inspect player diplomacy38, type0."},
   {"6f699b80","Unit_GrantsSharedSpellsToSource","Payoff320: target ECX, caster stack argument. Uses target player58 and source player getter to inspect player diplomacy78, type4."},
   {"6f4935e0","Attack_OnDamageNotification","Payoff320: Slow common dispatcher428120 reaches this with packet source=caster, amount0, flags10;49bc40 precedes retaliation checks even when target grants passive alliance. Read-only repeats retain861 commits270 public samples215 shared publications and match unhooked public control."},
   {"6f16b060","PathGroup_SelectSpeedCap","Payoff320: shared Captain Slow split retains shared identity. The slowed singleton is exempt via Attack notification bit01000000 and does not call this minimum owner; other two members retain270. Baseline engine missed spell notification and published173 at counter1161; first189 commits/66 public rows matched before divergence."},
   {"6f171340","Mover_StopAndInvalidatePath","Payoff320: same-point Captain replacement at1130 stops and sets sentinel destination while retaining fine request1128. Native next Path_Advance1131 returns invalid100000 until admission permits refill. Engine had reset these timestamps through its ordinary Move ability setup; preserving the virtual bridge clocks restores the later Captain blocker assignment and all861 member commits/293 actor commits."},
   {"6f9d44d0","CaptainAI_PublishPointRequest","Payoff320: virtual actor replacement has no fresh CAbilityMove activation166060. Stop retains path request clocks; repeated all-entered replacement at1130 retains fine1128. Missing retention moved actor four ticks early and omitted its1191 peer20 wait assigned to the slowed singleton. Preserve clocks around generic engine order initialization, independently of shared parameter ownership."},
   {"6f16ea70","Mover_Activate","Payoff320: initializes physical turn b8 from runtimecd53e8 and propulsion window bc fromcd541c. Two complete read-only captures show virtual Captain b8=3eccccce andbc=3dcccccd. Host0.4 produced3ecccccd and seven one-bit heading mismatches in293 actor commits; the existing retail decimal parser yields3eccccce. Ordinary unit authored overrides remain separate."}
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
