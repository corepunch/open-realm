// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Work253Evidence extends GhidraScript {
 static final String[][] NOTES={
  {"6f2069a0","Jass_IsVisibleToPlayer","Payoff253: public mode4 coordinate query. Null resolved player/location returns false. Location wrappers copy coordinates24/28 then delegate to the coordinate native. Read-only repeats and unhooked control match all32 same-turn/timed public markers; no replacement of prior fog expectations."},
  {"6f205460","Jass_IsFoggedToPlayer","Payoff253: public mode2 coordinate query. Null resolved player/location returns false. Location wrappers copy coordinates24/28 then delegate to the coordinate native. Read-only repeats and unhooked control match all32 same-turn/timed public markers; no replacement of prior fog expectations."},
  {"6f205940","Jass_IsMaskedToPlayer","Payoff253: public mode1 coordinate query. Null resolved player/location returns false. Location wrappers copy coordinates24/28 then delegate to the coordinate native. Read-only repeats and unhooked control match all32 same-turn/timed public markers; no replacement of prior fog expectations."},
  {"6f205830","Jass_IsLocationVisibleToPlayer","Payoff253: public mode4 location query. Null resolved player/location returns false. Location wrappers copy coordinates24/28 then delegate to the coordinate native. Read-only repeats and unhooked control match all32 same-turn/timed public markers; no replacement of prior fog expectations."},
  {"6f205770","Jass_IsLocationFoggedToPlayer","Payoff253: public mode2 location query. Null resolved player/location returns false. Location wrappers copy coordinates24/28 then delegate to the coordinate native. Read-only repeats and unhooked control match all32 same-turn/timed public markers; no replacement of prior fog expectations."},
  {"6f2057f0","Jass_IsLocationMaskedToPlayer","Payoff253: public mode1 location query. Null resolved player/location returns false. Location wrappers copy coordinates24/28 then delegate to the coordinate native. Read-only repeats and unhooked control match all32 same-turn/timed public markers; no replacement of prior fog expectations."},
  {"6f206910","Vision_IsPointVisible","Payoff253: clamp through Widget_ClampWorldPoint, convert1ec0c0, read visible30 ORf000 and masked2c AND0fff, classify1e0b80; returns classifier==4. Neutral player12..15 point queries are always visible. No alliance expansion or recomposition on read."},
  {"6f2053d0","Vision_IsPointFogged","Payoff253: clamp through Widget_ClampWorldPoint, convert1ec0c0, read visible30 ORf000 and masked2c AND0fff, classify1e0b80; returns classifier==2. Neutral player12..15 point queries are always visible. No alliance expansion or recomposition on read."},
  {"6f2058c0","Vision_IsPointMasked","Payoff253: clamp through Widget_ClampWorldPoint, convert1ec0c0, read visible30 ORf000 and masked2c AND0fff, classify1e0b80; returns classifier==1. Neutral player12..15 point queries are always visible. No alliance expansion or recomposition on read."},
  {"6f1e0b80","Vision_ClassifyCellState","Payoff253: thiscall map; RET0c; three ushort arguments visible,masked,player mask. Index mask_enabled10+2*fog_enabled14 selects triplets (4,4,4),(4,4,1),(4,2,2),(4,2,1). Visible wins; otherwise explored (~masked) then masked. Live disabled/enabled public markers confirm policy applies to queries; do not infer raw-plane-only classification. Native plane2c is positive MASKED, not explored."},
  {"6f212a60","Vision_WriteCircleState","Payoff253: world-to-fog1ec0c0, truncate radius*map70; zero radius writes nothing, radius1 paints2x2, larger radii use explicit spans. Live radius96 on128-unit grid is a no-op; radius160 gives the accepted public query sequence. Engine64-unit geometry remains a separate uncertified gap."},
 };
 public void run()throws Exception {
  if(getScriptArgs().length!=1)throw new Exception("new output required");
  Path p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  var dm=currentProgram.getDataTypeManager();
  var type=(Structure)dm.getDataType(new CategoryPath("/WC3/Pathfinding"),"WC3FogOfWarMapPrefix");
  if(type==null)throw new Exception("missing fog prefix");
  var member=type.getComponentAt(0x2c);member.setFieldName("masked");
  member.setComment("Positive masked bits; explored is their inverse. Original1e0b80 reads complemented masked; Payoff253.");
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var ins=it.next();out.append(ins.getAddress()).append('|');for(byte b:ins.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(ins).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  for(String address:new String[]{"6fa941e0","6fa941f0","6fa94200"}){out.append("DATA ").append(address).append('|');for(byte b:getBytes(toAddr(address),16))out.append(String.format("%02x",b&255));out.append('\n');}
  Files.writeString(p,out);println("Saved "+p);
 }
}
