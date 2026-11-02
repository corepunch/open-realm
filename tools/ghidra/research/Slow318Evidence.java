// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Slow318Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f5fcd50","Move_AcquireGroupIsolation","Payoff318: Move+80 is a counted exclusion from shared task admission. If zero, resolve Unit+164 bridge and republish the current physical member via058bc0 before incrementing. Direct callers are below-one speed crossing5fb914 and negative Morph641cb4. Public Polymorph then Slow yields counts0,1,2; only first acquisition republishes."},
   {"6f5fc0e0","Move_ReleaseGroupIsolation","Payoff318: decrement Move+80 only, without rejoining prior peers. Direct callers5fb8fb speed-factor crossing back and64c40c negative Morph removal. Public overlapping Polymorph/Slow removal yields2,1,0; the separate physical groups remain."},
   {"6f058bc0","MoveBridge_RepublishGroupMember","Payoff318: resolve canonical mover using bridge identity+8/+c, then16e7c0. This is group-member republication, not a fine-path reset."},
   {"6f16e7c0","Mover_RepublishGroupMember","Payoff318: resolve current physical group, invoke1694f0 if present. First Move group-isolation acquisition uses this chain."},
   {"6f1694f0","PathGroup_RepublishMover","Payoff318: prune invalid members, locate requested resolved mover, dispatch169550 for its row; false if absent."},
   {"6f169550","PathGroup_RepublishMember","Payoff318: member argument is WC3PathMember, not WC3MoveRequestCanonical as stale decompilation typing suggests. Unbind old row, create canonical request058430, retain mover; goal is old group+4c/50 plus old member+c/10 using Math_Add. Retain target identity, wrapper84, cached target region78, shared parameters7c and flags80; publish ready cohort. Public mid-Move Slow leaves peer in old group, old row invalid until pruning, new singleton goal includes old offset. Speed inverse does not rejoin."},
   {"6f5ffb60","Move_HandlePointTask","Payoff318:5ffc7d signed Move+80<1 is required alongside flags20 bit2000 clear and enabled-query success for shared/prepared request admission. Positive count takes independent point bridge path. Read-only public Slow-before-GroupPointOrder and Polymorph-before-GroupPointOrder each bind separate singletons. Test regression reproduces prior engine incorrectly joining the slowed unit."},
   {"6f5ff8b0","Move_HandleTargetTask","Payoff318:5ff9e2 signed Move+80<1 gates shared/current request admission with flags20 bit2000 clear and enabled query. Positive count takes independent target-task path. Static evidence only for this gate; point-scene captures do not certify all target continuations."},
   {"6f6417c0","Morph_ApplyContributions","Payoff318: virtual1e0 reports negative morph; when nonzero and Move exists,641cb4 acquires Move group isolation. Public Polymorph application publishes form base100 then count0->1; later Slow count1->2 does not republish again. Also publishes authored form turn rate via69a690/unit virtualc0 and authored prop window via691aa0/Math_DegreesScale/unit virtualc4 before the speed contribution."},
   {"6f64c090","Morph_RemoveAppliedContributions","Payoff318: negative Morph virtual1e0 release at64c40c follows authored speed inverse. Public removal of overlapping Slow/Polymorph sees speed-factor release2->1 then restored base270 and Morph release1->0. Also resets original authored turn rate and prop window before the base-speed inverse; this is direct publication, not an entry-time snapshot restore."},
   {"6f5fb7c0","Move_AdjustSpeedFactor","Payoff318: significant below-one crossing acquires Move group isolation5fcd50; inverse crossing releases5fc0e0. These counted transitions precede parameter publication. NaN itself is not a below-one factor. Physical singleton goal retains former member offset; public moving pair and pre-slow pair establish split and admission behavior."},
   {"6f69a690","UnitProfile_GetTurnRate","Payoff318: ECX scalar output, EDX rawcode; profile+1e0, fallback0.5. Read-only public Morph capture: constructor hTAA/hsor publishes0.6,641aaf selects nshe0.5,64c2d0 restores hTAA0.6. Morph-turn engine regression fails at first physical commit when original Footman turn rate is retained."},
   {"6f691aa0","UnitProfile_GetPropWindow","Payoff318: ECX scalar output, EDX rawcode; profile+1e4, fallback0.5 authored degrees. Morph641ac3 and inverse64c2e4 query form/original60 degrees in read-only captures, then caller software-multiplies Math_DegreesScale before unit virtualc4 publication."}
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
