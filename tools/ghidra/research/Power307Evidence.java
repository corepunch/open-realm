// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Power307Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f071180","Math_IntegerPower","Payoff307 NUM-01.14 discovery: two bounded actual public Pow(2,I2R(1073741824)*2) calls enter with signed exponent80000000, never return over12 wall seconds and retain the same path-owner counter1090. Read-only thread snapshots remain in this signed-SAR loop or Math_Multiply. The helper instruction budget from older oracles is not a VM watchdog. No numeric result is claimed."},
   {"6f20f990","Jass_Pow","Payoff307 NUM-01.14 discovery: runtime exponent4f000000 reaches integer power with signed80000000. Both fresh observed processes retain counter1090 and no independent public timer heartbeat or after marker for12 bounded seconds. Positive exponent4e800000 control returns and completes its public timer script. Engine currently raises a recoverable runtime error intentionally; that is NOT retail lifetime parity, so NUM-01.14 remains open."},
   {"6f91b460","JassVM_ExecuteBytecode","Payoff307 NUM-01.14: opcode15 calls native invoker918fd0 synchronously; instruction-limit loop is checked only after the native returns. No preemption or watchdog check exists inside this dispatch or original071180 signed exponent loop. Actual public Pow nonreturn stalls path-owner counter and independent timer callbacks in both bounded12-second runs. The observed lifetime is a simulation stall, not an instruction-limit VM status. Engine recovery remains an explicit deviation."},
   {"6f918fd0","JassVM_InvokeNative","Payoff307 NUM-01.14: caller91ba8a opcode15 invokes this inline. Resolves native callback from descriptor1c; real operands are copied into stack locals and passed by pointer. Direct indirect CALL [EBP-80] at919120 returns919123 before argument-pop bookkeeping. Actual public Pow entry stack includes919123 and91ba8a; there is no asynchronous continuation or native timeout in this dispatcher. Native071180 negative exponent never returns; independent timer progress stalls in two bounded processes."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);String c=f.getComment();if(c!=null && c.contains("Direct CALL ESI/descriptor callback at919121")){c=c.replace("Direct CALL ESI/descriptor callback at919121","Direct indirect CALL [EBP-80] at919120");f.setComment(c);}if(c==null||!c.contains(row[2]))f.setComment((c==null?"":c+"\n")+row[2]);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   out.append("BODY ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
