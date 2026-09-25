// Decompile requested functions and export incoming/outgoing calls.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.decompiler.*;
import java.io.*;
public class AudioFunctions extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs(); DecompInterface d=new DecompInterface();d.openProgram(currentProgram);
  try(PrintWriter w=new PrintWriter(args[0])) {
   for(int i=1;i<args.length;i++) {
    Function f=getFunctionContaining(toAddr(args[i])); if(f==null) { w.println("MISSING "+args[i]);continue; }
    w.println("\n/* "+f.getEntryPoint()+" "+f.getName()+" */");
    for(Reference r:getReferencesTo(f.getEntryPoint()))w.println("// caller "+r.getFromAddress()+" "+getFunctionContaining(r.getFromAddress()));
    for(Function c:f.getCalledFunctions(monitor))w.println("// callee "+c.getEntryPoint()+" "+c.getName());
    DecompileResults r=d.decompileFunction(f,60,monitor);
    if(r.decompileCompleted())w.println(r.getDecompiledFunction().getC());else w.println(r.getErrorMessage());
   }
  }finally{d.dispose();}
 }
}
