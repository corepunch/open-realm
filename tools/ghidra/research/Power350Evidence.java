// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import com.google.gson.*;
import java.nio.file.*;
public class Power350Evidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  for(String entry:new String[]{"6f20f990","6f0710e0","6f071180","6f070c20","6f06fe10","6f918fd0","6f91b460","6f06f9c0"}) {
   var f=getFunctionAt(toAddr(entry));if(f==null)throw new IllegalStateException(entry);
   JsonObject row=new JsonObject();row.addProperty("name",f.getName());row.addProperty("end",f.getBody().getMaxAddress().toString());int count=0;
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var ins=it.next();StringBuilder bytes=new StringBuilder();for(byte v:ins.getBytes())bytes.append(String.format("%02x",v&255));instructions.addProperty(ins.getAddress().toString(),bytes.toString());count++;}
   row.addProperty("instruction_count",count);functions.add(entry,row);
   String note="POWER350 NUM-01.14 closed: eight read-only public-native observations and four observer-free controls cover direct conversion and both Exp signs. Six nonreturning observations retain counter1090/tick20/beat13 for12 seconds, with295 thread samples inside IntegerPower or Multiply; terminating control returns raw0 and completes. Complete8-function/1574-instruction guards prove signed SAR reaches ffffffff without exit; native CALL919120 returns919123 inline before opcode limits. Engine regression reproduced recoverable VM error, then public Pow executes the original offending arithmetic loop synchronously. All8 Classic/TFT bounded workers pass in debug and O2 and match complete retail marker/counter streams. Checked helper budget stops remain analysis results, never public numeric results. Owned process controllers enforce startup/hold deadlines and kill/reap only their workers. Non-VM consumer lifetime/full trajectories remain E2E-06.1; nonfinite producers remain NUM-03.2.";
   if(entry.equals("6f071180")||entry.equals("6f918fd0")){String old=f.getComment();if(old==null||!old.contains(note))f.setComment((old==null?"":old+"\n")+note);}
  }
  out.add("instructions",instructions);out.add("functions",functions);
  String[] args=getScriptArgs();if(args.length!=1)throw new IllegalStateException("output required");Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  Files.writeString(path,new GsonBuilder().setPrettyPrinting().create().toJson(out)+"\n");println(instructions.size()+" guarded instructions in "+functions.size()+" functions");
 }
}
