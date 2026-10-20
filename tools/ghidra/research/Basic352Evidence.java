// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import com.google.gson.*;
import java.nio.file.*;
import java.util.*;
/** Export every scalar caller and complete bodies; include reciprocal-sqrt/integer helpers and parser roots. */
public class Basic352Evidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  JsonObject out=new JsonObject(),calls=new JsonObject(),functions=new JsonObject(),guards=new JsonObject();
  TreeSet<String> entries=new TreeSet<>();
  for(String a:new String[]{"6f06f9c0","6f06fa90","6f06fbb0","6f06fcd0","6f06fd50","6f06fe10","6f06ff20","6f06ffa0","6f070120","6f070170","6f0703a0","6f070530","6f0705b0","6f070700","6f070790","6f070c20","6f070c80","6f070d20","6f070d80","6f070de0","6f070f70","6f070fe0","6f0710e0","6f071180","6f0711e0","6f071250","6f071280","6f071340","6f071480","6f071590","6f0715c0","6f1d4c80","6f1d4d20","6f16e6c0","6f071570","6f071530","6f06fd30","6f0701d0","6f070f20","6f2103b0","6f2103f0"}){
   entries.add(a);JsonArray refs=new JsonArray();for(var ref:getReferencesTo(toAddr(a))){
    JsonObject row=new JsonObject();var f=getFunctionContaining(ref.getFromAddress());row.addProperty("site",ref.getFromAddress().toString());row.addProperty("type",ref.getReferenceType().toString());row.addProperty("entry",f==null?"":f.getEntryPoint().toString());refs.add(row);if(f!=null)entries.add(f.getEntryPoint().toString());
   }calls.add(a,refs);
  }
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String a:entries){
   Function f=getFunctionAt(toAddr(a));if(f==null)throw new IllegalStateException(a);JsonObject row=new JsonObject();row.addProperty("name",f.getName());row.addProperty("end",f.getBody().getMaxAddress().toString());
   if(calls.has(a)){var result=decomp.decompileFunction(f,45,monitor);if(!result.decompileCompleted())throw new IllegalStateException(a+" "+result.getErrorMessage());row.addProperty("decompiled",result.getDecompiledFunction().getC());}
   JsonArray refs=new JsonArray();for(var ref:getReferencesTo(toAddr(a))){JsonObject r=new JsonObject();var parent=getFunctionContaining(ref.getFromAddress());r.addProperty("site",ref.getFromAddress().toString());r.addProperty("type",ref.getReferenceType().toString());r.addProperty("entry",parent==null?"":parent.getEntryPoint().toString());refs.add(r);}row.add("references",refs);
   StringBuilder assembly=new StringBuilder();var it=currentProgram.getListing().getInstructions(f.getBody(),true);int count=0;
   while(it.hasNext()){var ins=it.next();StringBuilder bytes=new StringBuilder();for(byte b:ins.getBytes())bytes.append(String.format("%02x",b&255));guards.addProperty(ins.getAddress().toString(),bytes.toString());assembly.append(ins.getAddress()).append(" ").append(ins).append("\n");count++;}
   row.addProperty("assembly",assembly.toString());row.addProperty("instruction_count",count);functions.add(a,row);
  }}finally{decomp.dispose();}
  for(String a:calls.keySet()){var f=getFunctionAt(toAddr(a));String old=f.getComment();if(old!=null){int at=old.indexOf("BASIC352");if(at>=0)old=old.substring(0,at).stripTrailing();}f.setComment((old==null||old.isEmpty()?"":old+"\n")+"BASIC352 NUM-01.17: complete41-root inventory7097 references/2008 functions/179722 guarded instructions; CFG basic operand/destination ledger6912 sites includes every direct reference, no name filtering.199 exact data-table startup thunks execute from poisoned storage (183 integer-value,16 decimal-text). Basic helpers preserve all input/output byte-overlap relations with operand snapshots and original composed store order;34038 original/model/C contexts include16470 partial overlaps. Original formatter0701d0 ECX scalar input, EDX char output; public R2S/R2SW use separate256-byte stack buffers. Full5124 original/C formatting contexts delegate only integer sprintf layouts, poison caller-save registers and preserve inputs/guards/RET8. Two read-only public format traces repeat9553 records,423 results,3714 scalar calls;426 markers match unhooked control. Broad public callback repeats2272 records at counter1030; broad frame-query streams remain diagnostic/nondeterministic. R2SW(1152.125,8,3) emits leading-space 1152.125; S2R and actual Move X are0. Host printf deviation first failed516 assertions; shared software formatter passes2231 assertions including actual VM, Move direction and cold-save target. Width subtracts displayed fraction places with signed32 wrap; negative precision6 trimmed to1; normal cap9; dot always; large reduction ignores width and uses max(requested precision,1) zeroes. Negative zero has no sign. Oversized original buffer/fault behavior remains NUM-01.2; engine bounds are transactional safety behavior, not retail fault parity. Broader numeric domains/trajectories remain E2E-06.1; extra indirect roots BASE-03.1.");}
  out.add("calls",calls);out.add("functions",functions);out.add("instructions",guards);
  String[] args=getScriptArgs();if(args.length!=1)throw new IllegalStateException("fresh output required");Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("existing output");Files.writeString(path,new GsonBuilder().setPrettyPrinting().create().toJson(out)+"\n");println(functions.size()+" complete functions; "+guards.size()+" instruction guards");
 }
}
