// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Payoff375RoboPolicyEvidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  if(getInt(toAddr("6fb348e0"))!=0x6f54c640 || getInt(toAddr("6fb344c0"))!=0x6fc24ee8 || getInt(toAddr("6fc24ef4"))!=0x6fd23e1c)throw new IllegalStateException("concrete RoboGoblin RTTI/vtable differs");
  String rtti=new String(getBytes(toAddr("6fd23e24"),24),java.nio.charset.StandardCharsets.US_ASCII);if(!rtti.equals(".?AVCAbilityRoboGoblin@@"))throw new IllegalStateException("RoboGoblin RTTI name differs");
  var f=getFunctionAt(toAddr("6f54c640"));String note="Payoff375 static producer proof: concrete CAbilityRoboGoblin RTTI descriptor6fd23e1c -> COL6fc24ee8 -> primary table6fb344c4, whose v41c at6fb348e0 contains6f54c640. RoboGoblin uses common Misc/MorphAlternateDisable integer policy. This is static inheritance proof only; public ANrg runtime behavior requires fresh observations and controls in existing SEP-01.2.";
  if(args[1].equals("apply")){String prior=f.getComment();if(prior==null)prior="";if(!prior.contains(note))f.setComment(prior+"\n"+note);}
  JsonObject out=new JsonObject(),guards=new JsonObject();out.addProperty("task","SEP-01.2");out.addProperty("phase","payoff375-static-only");out.addProperty("binary_sha256",currentProgram.getExecutableSHA256());out.addProperty("function",f.getEntryPoint().toString());out.addProperty("comment",f.getComment());
  DecompInterface d=new DecompInterface();d.openProgram(currentProgram);try{var r=d.decompileFunction(f,45,monitor);if(!r.decompileCompleted())throw new IllegalStateException(r.getErrorMessage());out.addProperty("decompiled",r.getDecompiledFunction().getC());}finally{d.dispose();}
  var it=currentProgram.getListing().getInstructions(f.getBody(),true);while(it.hasNext()){var ins=it.next();StringBuilder b=new StringBuilder();for(byte x:ins.getBytes())b.append(String.format("%02x",x&255));guards.addProperty(ins.getAddress().toString(),b.toString());}out.add("instructions",guards);
  JsonObject slots=new JsonObject();for(String address:new String[]{"6fb348e0","6fb344c0","6fc24ee8","6fd23e24"}){int length=address.equals("6fc24ee8")?20:address.equals("6fd23e24")?25:4;StringBuilder b=new StringBuilder();for(byte x:getBytes(toAddr(address),length))b.append(String.format("%02x",x&255));slots.addProperty(address,b.toString());}out.add("data_guards",slots);
  Files.writeString(path,new GsonBuilder().setPrettyPrinting().create().toJson(out)+"\n");println("RoboGoblin v41c confirmed; "+guards.size()+" instruction guards and "+slots.size()+" data guards");
 }
}
