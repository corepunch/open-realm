// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Refresh274Evidence extends GhidraScript {
 public static final String[][] NOTES={
  {"6f169680","PathGroup_UpdateTargetRefresh","Payoff274 TARGET-04.1: complete original routine4418 distance/adjacent-threshold inputs,180 first-member prediction/elapsed/velocity inputs and298 countdown inputs. Coefficient supplied as initialized3ea8f5c3, independently read live. Zero sticky; -1 reloads16..132 plus165 for400; other nonzero values decrement. Reload runs after movement or before denied-route stop. Owner samples before decrement: next permitted sample is reload+1 owner visits. Public axis setters hold one far roster member, preserving two shared target cohorts834 visits each: reloads1425,1607,1789,1971,2153, eight exact182-counter intervals. Earlier natural-arrival diagnostic retired after68 visits and is retained separately. Engine tests exercise17/18/133/182/183/298 visits and cold saves. Reload now consumes a fine-only prediction without world projection/cache-key construction."},
  {"6f9d1040","CaptainAI_PrepareMoveRequestMember","Payoff274 TARGET-04.1: read-only public refill repeats plus unhooked control retain caller9d1136 into89cca0(1). Point request flags900 -> d00; target policy800 -> c00, physical activation21c01. Existing ordinary private target cohorts21801 do not carry400. Preserve producer distinctions; no flag removal from shared target requests."},
  {"6f89cca0","CMoveReq_SetExtraTargetRefreshDelay","Payoff274: direct callsite9d1131 from9d1040; live returnaddress9d1136. Resolve wrapper8/c then16dc00(1); live point and target preparation proven. No implied extra delay for every Captain private request."},
  {"6f16dc00","MoveRequest_SetExtraTargetRefreshDelay","Payoff274: sole direct wrapper caller89ccb3, canonical100 bit400 set/clear only. Original setter/copy inventory retained; public refill proves target producer800 -> c00. No flag translation at physical handoff."},
  {"6f16bdb0","MoveRequest_BindPhysicalCohort","Payoff274: canonical100 copied whole to physical80 at16be35..16be43. Public target c00 activates physical21c01 (persistent1 and refresh1000 coexist with400); private target1801 lacks400. Frozen public countdown evidence contains195 ordinary17-update intervals and eight extra182-update intervals."},
  {"6f16cd30","PathGroup_SampleDestination","Payoff274: a zero visible target countdown samples BEFORE169680 reload; positive values keep cached path destination. Repeated public ordinary target reload16 samples exactly17 counter updates apart. Public-held Captain shared target reload181 is followed by another sample182 updates later, eight times across two cohorts. Complete original countdown and engine owner/save regressions retain the same cadence."}
 };
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong original");
  if(getScriptArgs().length!=1)throw new Exception("fresh output required");Path path=Path.of(getScriptArgs()[0]);if(Files.exists(path))throw new Exception("exists");
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES){
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   var old=f.getComment();if(old!=null)old=old.replaceAll("(?m)^Payoff274[^\\n]*\\n?", "").stripTrailing();if(old==null)f.setComment(row[2]);else if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append(' ').append(f.getPrototypeString(false,true)).append('\n').append(f.getComment()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
