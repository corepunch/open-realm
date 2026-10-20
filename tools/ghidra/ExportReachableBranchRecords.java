import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.nio.file.*;
import java.io.*;
public class ExportReachableBranchRecords extends GhidraScript {
 public void run() throws Exception {
  String[] a=getScriptArgs();
  if(a.length!=2 || Files.exists(Path.of(a[1])))throw new Exception("fresh output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("wrong binary");
  int functions=0,branches=0;
  try(BufferedWriter w=Files.newBufferedWriter(Path.of(a[1]))) {
   for(String address:Files.readAllLines(Path.of(a[0]))) {
    monitor.checkCancelled();
    Function f=getFunctionAt(toAddr(address));
    if(f==null)throw new Exception("missing function "+address);
    int count=0;
    for(Instruction i:currentProgram.getListing().getInstructions(f.getBody(),true)) {
     if(!i.getFlowType().isConditional() || !i.getFlowType().isJump())continue;
     StringBuilder bytes=new StringBuilder();
     for(byte b:i.getBytes())bytes.append(String.format("%02x",b&255));
     StringBuilder targets=new StringBuilder();
     for(var target:i.getFlows()) {if(targets.length()>0)targets.append(',');targets.append('"').append(target).append('"');}
     w.write("{\"function\":\""+address+"\",\"site\":\""+i.getAddress()+"\",\"bytes\":\""+bytes+"\",\"mnemonic\":\""+i.getMnemonicString()+"\",\"taken\":["+targets+"],\"fallthrough\":");
     w.write(i.getFallThrough()==null?"null":"\""+i.getFallThrough()+"\"");
     w.write("}\n");count++;branches++;
    }
    w.write("{\"function_summary\":\""+address+"\",\"branches\":"+count+"}\n");functions++;
   }
  }
  println("Complete read-only branch export: "+functions+" functions, "+branches+" conditional jumps; no truncation.");
 }
}
