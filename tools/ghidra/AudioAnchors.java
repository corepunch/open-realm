// Export sound-related strings, RTTI symbols and their callers.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.decompiler.*;
import java.io.*;
import java.util.*;
public class AudioAnchors extends GhidraScript {
 public void run() throws Exception {
  String out = getScriptArgs()[0];
  Set<Function> funcs = new LinkedHashSet<>();
  try(PrintWriter w = new PrintWriter(out + "/anchors.tsv")) {
   DataIterator it = currentProgram.getListing().getDefinedData(true);
   while(it.hasNext()) {
    Data d=it.next(); Object v=d.getValue();
    if(!(v instanceof String)) continue;
    String s=(String)v;
    if(!s.matches("(?is).*(CSound|SoundDBChannel|SoundManager|UnitAckSounds|SoundChannels|SoundPriority|SetSoundChannel|UnitResponse|Pissed|unitSoundType|unit fogged|sound def template|sound cutoff|ZOOMEDINONLY|MASTER|SndCreate failed).*")) continue;
    w.println("STRING\t"+d.getAddress()+"\t"+s.replace('\n',' '));
    for(Reference ref:getReferencesTo(d.getAddress())) {
     Function f=getFunctionContaining(ref.getFromAddress());
     w.println("XREF\t"+ref.getFromAddress()+"\t"+(f==null?"data":f.getName()+"@"+f.getEntryPoint()));
     if(f!=null) funcs.add(f);
    }
   }
   SymbolIterator syms=currentProgram.getSymbolTable().getAllSymbols(true);
   while(syms.hasNext()) { Symbol s=syms.next(); if(s.getName(true).matches("(?i).*(CSound|SoundDBChannel|SoundManager|CSoundListener).*")) w.println("SYMBOL\t"+s.getAddress()+"\t"+s.getName(true)); }
  }
  DecompInterface decomp = new DecompInterface(); decomp.openProgram(currentProgram);
  try(PrintWriter w=new PrintWriter(out+"/anchor-functions.c")) {
   for(Function f:funcs) { monitor.checkCancelled(); w.println("\n/* "+f.getEntryPoint()+" "+f.getName()+" */"); DecompileResults r=decomp.decompileFunction(f,60,monitor); if(r.decompileCompleted()) w.println(r.getDecompiledFunction().getC()); else w.println(r.getErrorMessage()); }
  } finally { decomp.dispose(); }
 }
}
