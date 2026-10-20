// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Captain369SelectorEvidence extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  if(!currentProgram.getName().equals("game.dll")||args.length!=2||(!args[0].equals("apply")&&!args[0].equals("read")))throw new Exception("game.dll apply/read fresh output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("binary differs");
  Path output=Path.of(args[1]);if(Files.exists(output))throw new Exception("output exists");
  var manager=currentProgram.getDataTypeManager();CategoryPath path=new CategoryPath("/WarcraftIII/Pathfinding127");
  DataType prefix=manager.getDataType(path,"WC3CaptainAIPrefix"),identity=manager.getDataType(path,"WC3PathIdentity");
  if(prefix==null||identity==null||prefix.getLength()!=0x128||identity.getLength()!=8)throw new Exception("primary prefix differs");
  String note="Payoff369 selector distinction: Begin9d7240 raw4clear passes ownplayer68 to selector17; raw4set passes requestedplayer130 to selector24. 67e790 case17 complements outgoing passive mask1e0e80(ownplayer); 1e0e80 resolves ownPlayer then diplomacy38 via25ba40 and canonical payload78 via056a40. 1e2b60 tests this same source-to-other mask, preserving asymmetric alliance direction. Case24 restricts to1<<requestedowner. These are not units-versus-structures filters. Native seven-candidate repeated observations retain331 identical publicmarkers and enqueue hrif,Hpal,ugol,hbar,htow,hfoo,hsor, reverse creation order; ticks1360/1364/1367/1370/1374/1377/1380 each process one eligible candidate. Actual stream13 die1(1d3) at1380 selects hfoo1609(score2). Unit248 values: htow48,hbar8,ugola; ugol explicit category0 branch. Full owner-transfer class-query ordering and unhooked candidate control remain pending; no209 engine claim.";
  if(args[0].equals("apply")) {
   StructureDataType bucket=new StructureDataType(path,"WC3CaptainSelectionBucket",12);
   bucket.replaceAtOffset(0,identity,8,"identity","Native weak slot/generation; +8 remains untyped.");
   DataType savedBucket=manager.addDataType(bucket,DataTypeConflictHandler.REPLACE_HANDLER);
   StructureDataType selection=new StructureDataType(path,"WC3CaptainSelectionPrefix",0x208);
   selection.replaceAtOffset(0,prefix,0x128,"captain","Original Captain prefix remains unchanged.");
   selection.replaceAtOffset(0x130,UnsignedIntegerDataType.dataType,4,"requested_player",note);
   selection.replaceAtOffset(0x134,UnsignedIntegerDataType.dataType,4,"pending_header_word","Intrusive sentinel/header word; no fabricated node semantics.");
   selection.replaceAtOffset(0x138,new PointerDataType(VoidDataType.dataType,4),4,"pending_head","Queue appends here; strong Unit references.");
   selection.replaceAtOffset(0x13c,new PointerDataType(VoidDataType.dataType,4),4,"pending_tail","Consumer pops oldest here; FIFO preserves enumeration order.");
   selection.replaceAtOffset(0x144,new ArrayDataType(savedBucket,12,12),144,"best_candidates","Requested-owner0..5, other-owner6..11; unreachable adds3.");
   selection.replaceAtOffset(0x1d8,new ArrayDataType(UnsignedIntegerDataType.dataType,12,4),48,"best_scores","Unsigned strict lower-than; Begin resets999999.");
   manager.addDataType(selection,DataTypeConflictHandler.REPLACE_HANDLER);
   for(String va:new String[]{"6f9d7240","6f67e790","6f1e0e80"}) {var f=getFunctionAt(toAddr(va));String old=f.getComment()==null?"":f.getComment();if(!old.contains(note))f.setComment(old+"\n"+note);}
  }
  StringBuilder text=new StringBuilder(note+"\n");
  for(String name:new String[]{"WC3CaptainSelectionBucket","WC3CaptainSelectionPrefix"}) {var d=manager.getDataType(path,name);if(d==null)throw new Exception(name);text.append(d).append('\n');}
  for(String va:new String[]{"6f9d7240","6f67e790","6f1e0e80","6f25ba40","6f056a40","6f1e2b60"}) {
   var f=getFunctionAt(toAddr(va));if(f==null)throw new Exception(va);text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
  }
  Files.writeString(output,text);println(output.toString());
 }
}
