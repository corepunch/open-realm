import ghidra.app.script.GhidraScript;
import com.google.gson.*;
import java.nio.file.*;
/** Reproduce the radius producer inventory and preserve original instruction guards. */
public class Foot347Evidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong retail binary");
  String[] entries={"6f16c220","6f16e1f0","6f16c940","6f05c2e0","6f15fef0","6f05a5c0","6f5fc640","6f9d3bc0","6f9d4600","6f6d3190","6f6d0630"};
  JsonObject instructions=new JsonObject(),functions=new JsonObject();
  for(String a:entries){var f=getFunctionAt(toAddr(a));if(f==null)throw new IllegalStateException(a);JsonObject row=new JsonObject();row.addProperty("name",f.getName());row.addProperty("entry",a);row.addProperty("instructions",f.getBody().getNumAddresses());functions.add(a,row);
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);int count=0;
   while(it.hasNext()){var ins=it.next();StringBuilder b=new StringBuilder();for(byte v:ins.getBytes())b.append(String.format("%02x",v&255));instructions.addProperty(ins.getAddress().toString(),b.toString());count++;}row.addProperty("instruction_count",count);
  }
  JsonArray callers=new JsonArray();for(var ref:getReferencesTo(toAddr("6f05a5c0"))){var f=getFunctionContaining(ref.getFromAddress());JsonObject row=new JsonObject();row.addProperty("site",ref.getFromAddress().toString());row.addProperty("type",ref.getReferenceType().toString());row.addProperty("entry",f==null?"":f.getEntryPoint().toString());callers.add(row);}
  String note="FOOT347 public Chaos and removal: observed/repeated/control match1304 markers; two read-only native streams match4944 records. Shared current radius95 ->63 ->31 while path+b4 remains95; type replacement detaches before the radius pass. Engine fresh/save regression compares1209 raw public getter states and253 shared/cached states. Existing shared189/190 and Follow50 implementations match; no artificial failing engine baseline is asserted. Five original05a5c0 callers are exported; source/target radii are captured on admission, not recomputed each tick. FOOT-01.3 closed after28 focused tests434088 assertions per edition; larger pathfinding goal remains active.";
  for(String a:new String[]{"6f16c220","6f16e1f0","6f16c940","6f05a5c0"}){var f=getFunctionAt(toAddr(a));if(!f.getComment().contains(note))f.setComment(f.getComment()+"\n"+note);}
  JsonObject output=new JsonObject();output.add("instructions",instructions);output.add("functions",functions);output.add("target_callers",callers);
  String[] args=getScriptArgs();if(args.length!=1)throw new IllegalStateException("output path required");Files.writeString(Path.of(args[0]),new GsonBuilder().setPrettyPrinting().create().toJson(output)+"\n");println("Exported "+instructions.size()+" instructions; "+callers.size()+" target callers");
 }
}
