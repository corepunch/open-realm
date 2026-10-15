import ghidra.app.script.GhidraScript;
import com.google.gson.*;
import java.nio.file.*;
/** Export parameter publication, request overwrite and temporary restore ownership. */
public class Move348Evidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong retail binary");
  String[] entries={"6f15ff40","6f170f60","6f170f80","6f171200","6f171170","6f1710a0","6f05c5c0","6f05c560","6f05c8c0","6f05c890","6f05a5c0","6f05b970","6f05c0e0","6f16a790","6f16b5c0","6f16c220","6f16b060","6f16ea70","6f173bd0","6f173450","6f685310","6f698af0","6f2181a0","6f218d40"};
  JsonObject instructions=new JsonObject(),functions=new JsonObject(),callers=new JsonObject();
  for(String a:entries){var f=getFunctionAt(toAddr(a));if(f==null)throw new IllegalStateException(a);JsonObject row=new JsonObject();row.addProperty("name",f.getName());row.addProperty("entry",a);functions.add(a,row);
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);int count=0;
   while(it.hasNext()){var ins=it.next();StringBuilder b=new StringBuilder();for(byte v:ins.getBytes())b.append(String.format("%02x",v&255));instructions.addProperty(ins.getAddress().toString(),b.toString());count++;}row.addProperty("instruction_count",count);
   JsonArray refs=new JsonArray();for(var ref:getReferencesTo(toAddr(a))){JsonObject r=new JsonObject();var parent=getFunctionContaining(ref.getFromAddress());r.addProperty("site",ref.getFromAddress().toString());r.addProperty("type",ref.getReferenceType().toString());r.addProperty("entry",parent==null?"":parent.getEntryPoint().toString());refs.add(r);}callers.add(a,refs);
  }
  String note="MOVE348 / MOVE-01.3: authored333/min173/max389,turn.37,window45deg. Public point/target requests republish maximum88 and incrementb4 independently of angularb8/bc. Timed facing temporarily overrides b8 from group74 and restores it after16a790; authored/public turn survives. Member heading comes from retained velocity length when >=runtime cd5474=34d6bf95, otherwise physical8c. Immediate SetUnitFacing while moving must not redirect the next decision. Explicit Amov removal gives speed0 and public head0; UnitAddAbility(Amov) returnsfalse through authored factory and cannot recreate it. Read-only public scene has488 raw getter states and549 markers; fresh/cold-save engine regression. Original writer callers and all instruction bytes exported by Move348Evidence.java; existing motion/speed/temporary166/facing207/shared189/scheduler190 contracts cover independent parameter domains. Forced173bd0 ballistic owner uses FLTMAX cap; full nonunit trajectories retain E2E-06.1 scope.";
  for(String a:new String[]{"6f16a790","6f05c0e0","6f05b970","6f05a5c0","6f2181a0","6f685310","6f173bd0"}){var f=getFunctionAt(toAddr(a));String old=f.getComment();if(old==null||!old.contains(note))f.setComment((old==null?"":old+"\n")+note);}
  JsonObject out=new JsonObject();out.add("instructions",instructions);out.add("functions",functions);out.add("callers",callers);
  String[] args=getScriptArgs();if(args.length!=1)throw new IllegalStateException("output path required");Files.writeString(Path.of(args[0]),new GsonBuilder().setPrettyPrinting().create().toJson(out)+"\n");println("Exported "+instructions.size()+" instructions in "+functions.size()+" functions");
 }
}
