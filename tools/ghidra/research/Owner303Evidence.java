// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Owner303Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f679bb0","Unit_BeginDeathTasks","Payoff303 MAP-04.2: two complete read-only eight-scene owner303 repeats and observer-free public control distinguish physical mover existence from attached Move availability. Direct KillUnit on h303 runs two cleanup-task recoveries plus first counted Move cancellation; zero-speed ground i303, zero-speed flyer f303 and htow have no Move class and run zero recoveries. Do not execute death cleanup merely because collision/separation storage exists."},
   {"6f600340","Move_HandleCleanupTask","Payoff303 MAP-04.2: both direct death cleanup calls return600433. These are attached Move task consumers; absent Move means no consumer invocation. Existing Stop284 callback expectations remain frozen."},
   {"6f5fc120","CAbilityMove_CancelOwner","Payoff303 MAP-04.2: direct KillUnit third recovery returns5fc18a after two cleanup-task recoveries. New owner303 window extends beyond RemoveUnit return and observes a separate first-disable recovery during69c510 for an ordinary live mover; earlier Stop284 windows ended at native return and do not certify deferred absence. Broader removal integration remains open; do not rewrite old return-window expectations."},
   {"6f5fcd40","Move_GetAbilityClass","Payoff303 MAP-04.2: complete ability-chain readout identifies this vtable1c class getter on h303, absent on zero-speed i303/f303 and htow. Original getter returns Amov; no game method called by observer."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);String c=f.getComment();if(c==null||!c.contains(row[2]))f.setComment((c==null?"":c+"\n")+row[2]);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   out.append("BODY ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
