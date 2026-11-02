import ghidra.app.script.GhidraScript;
import com.google.gson.*;
import java.nio.file.*;
/** Complete direct Pow/log/exp caller relationships, including non-movement wrappers. */
public class Power349Evidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong retail binary");
  String[] entries={"6f0710e0","6f070c20","6f06fe10","6f070f70","6f06ff20","6f06fd50","6f071180","6f1705c0","6f171500","6f20f990","6f502600","6f1d4450","6f1d44a0","6f1d4500","6f1d1de0","6f1d1c10","6f1d1c50","6f1d1c90","6f1d4630","6f1d1cd0","6f4f6f80","6f5181b0","6f520300","6f514d30","6f51d9f0","6f64cf40","6f500a60","6f50bcf0","6f6d3470","6f685170","6f057fd0","6f058190","6f161b70","6f162340","6f001de0","6f001c30","6f004200","6f004210"};
  JsonObject instructions=new JsonObject(),functions=new JsonObject(),callers=new JsonObject();
  for(String a:entries){var f=getFunctionAt(toAddr(a));if(f==null){disassemble(toAddr(a));f=createFunction(toAddr(a),"Power349_Entry_"+a);}if(f==null)throw new IllegalStateException(a);JsonObject row=new JsonObject();row.addProperty("name",f.getName());row.addProperty("entry",a);functions.add(a,row);
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);int count=0;
   while(it.hasNext()){var ins=it.next();StringBuilder b=new StringBuilder();for(byte v:ins.getBytes())b.append(String.format("%02x",v&255));instructions.addProperty(ins.getAddress().toString(),b.toString());count++;}row.addProperty("instruction_count",count);
   JsonArray refs=new JsonArray();for(var ref:getReferencesTo(toAddr(a))){JsonObject r=new JsonObject();var parent=getFunctionContaining(ref.getFromAddress());r.addProperty("site",ref.getFromAddress().toString());r.addProperty("type",ref.getReferenceType().toString());r.addProperty("entry",parent==null?"":parent.getEntryPoint().toString());refs.add(r);}callers.add(a,refs);
  }
  String note="POWER349 NUM-01.19 verified caller relationships: five Pow sites170726,171509,20fa20,502678,5026c2; five Log sites07113d,1d4484,1d44d8,1d45ce,1d1de3; Exp only071152.209 complete caller cases/1111 nested original results match independent integer and compiled engine models. Forwarding thunks exercise all five Pow pointer equality partitions and both Log partitions; actual visual/public/Shadow Strike/distribution producers own separate locals. CBuffShadowStrike timer84 virtual1c is elapsed; formula Pow(abs(elapsed-duration),exponent)/Pow(duration,exponent), retained factor written after one-difference movement contribution. Word12c is interval3, ec is patched periodic damage10; duration4171999b. Two read-only retail streams match3615 records and1351 scalar calls; control matches428 markers. Engine422 raw getters match fresh and cold-save. Health bridge primary-counter prediction is distinct from callback logical deadline; five poison commits are captured in349j. Nonreturning original VM policy remains NUM-01.14; full nonunit trajectories remain E2E-06.1.";
  for(String a:new String[]{"6f0710e0","6f070f70","6f070c20","6f1705c0","6f502600","6f4f6f80","6f5181b0","6f685170","6f057fd0","6f058190","6f161b70","6f162340"}) {
   var f=getFunctionAt(toAddr(a));String old=f.getComment();
   if(old!=null){int at=old.indexOf("POWER349");if(at>=0)old=old.substring(0,at).stripTrailing();}
   f.setComment((old==null||old.isEmpty()?"":old+"\n")+note);
  }
  JsonObject out=new JsonObject();out.add("instructions",instructions);out.add("functions",functions);out.add("callers",callers);
  String[] args=getScriptArgs();if(args.length!=1)throw new IllegalStateException("output path required");Files.writeString(Path.of(args[0]),new GsonBuilder().setPrettyPrinting().create().toJson(out)+"\n");println("Exported "+instructions.size()+" instructions in "+functions.size()+" functions");
 }
}
