// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Spell269Evidence extends GhidraScript {
 public static final String[][] NOTES={
  {"6f416cc0","Spell_GetPointCastRange","Payoff269 ORDER-01.13: spell virtual410, ECX typed spell prefix; stack4 output scalar*,8 x*,c y*,10 buffer flag, RET10. x/y/buffer unused:417a70 reads rank50 authored Rng and returns output*. Two complete read-only point Flame Strike repeats observe range44480000 at admission buffer0 and later buffer1. Do not infer a single-argument ABI from the decompiler; keep unused stack arguments."},
  {"6f40a7a0","Spell_CreateApproachAndCastTasks","Payoff269: two complete read-only initial producer witnesses per shape and unhooked control retain all507 prior268 public markers exactly. Ground Flame Strike calls virtual410 with buffer0, then692120 d016b with captured world range800 and point1536,288. Move05b970 receives these words unchanged. Arrival is Move-owned; public spell/effect/channel phase timing remains separately open."},
  {"6f692120","Unit_CreatePointTask","Payoff269: actual initial spell producer40a7a0 submits d016b/world800, then Move5ffb60 passes captured range into05b970. Both observed repeats also retain prior Move d016b/world0 before replacement. Point task retains authored range, independently of later metadata reads."},
  {"6f5ffb60","Move_HandlePointTask","Payoff269: actual ground point Flame Strike task d016b enters common05b970 with world800; fine member publishes25 exactly, without adding caster radius1. Two raw repeats yield52 exact primary-clock owner inputs and same frozen motion. Retired previous Move owner still has a stale membership row; only rows whose member group identity equals the visited group are active-owner evidence."},
  {"6f05b970","MoveBridge_StartPoint","Payoff269: point spell uses the ordinary point bridge, arrival/cantpathd0196/d0198, flag1, world range800. Raw fine arrival25; collision radius1 is excluded from physical arrival. Initial40a7a0 already-in-range predicate05b440 instead adds source radius. Engine uses the common fine owner with retained receiver and no per-frame spell distance polling; save/cancel/owned-child cleanup reuse existing mechanisms."},
  {"6f05b440","MoveBridge_IsPointInCollisionRange","Payoff269: initial point-spell producer40a7a0 invokes this predicted-source admission check with authored virtual410 range. Source collision belongs only to this admission predicate; accepted point physical task publishes max(.49,range/32) without it. Engine point command and native spell entries now use this existing exact scalar predicate."}
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
   if(row[0].equals("6f416cc0")) {
    String[] storage=new String[]{"ECX:","Stack[0x4]:","Stack[0x8]:","Stack[0xc]:","Stack[0x10]:"};
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
