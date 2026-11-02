// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class Work230Evidence extends GhidraScript {
 private DataType type(String name) throws Exception {
  if(name.startsWith("ptr:"))return new PointerDataType(type(name.substring(4)),4);
  var t=currentProgram.getDataTypeManager().getDataType(new CategoryPath("/WarcraftIII/Pathfinding127"),name);
  if(t==null)throw new Exception("Missing type "+name);return t;
 }
 private void abi(String address,String returns,String convention,String[][] spec) throws Exception {
  var fn=getFunctionAt(toAddr(address));Parameter[] args=new Parameter[spec.length];
  for(int i=0;i<spec.length;i++) {
   var dt=spec[i][1].equals("i32")?IntegerDataType.dataType:type(spec[i][1]);
   var location=spec[i][2].equals("ECX")?new VariableStorage(currentProgram,currentProgram.getRegister("ECX")):
    new VariableStorage(currentProgram,Integer.parseInt(spec[i][2]),dt.getLength());
   args[i]=new ParameterImpl(spec[i][0],dt,location,currentProgram);
  }
  fn.setCallingConvention(convention);fn.setReturnType(returns.equals("void")?VoidDataType.dataType:type(returns),SourceType.USER_DEFINED);
  fn.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,args);
 }
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f063d50","WidgetRegions_GetFirstObject","Payoff230 MAP-04.2: fastcall ECX collection, plain RET; count+4 zero returnsNULL, otherwise dereferences array+8 first slot.059590 selects only this region for each widget exclusion; duplicate widget slots remain distinct operations."},
   {"6f22f1d0","PathTexture_GetWorldBounds","Payoff230: thiscall ECX texture,stack4 output scalar rectangle*,8 centerXY*,c quarterTurn;RETc. Authored width+8 and height+c multiplied by software scalar16. Turns0/2 use widthX/heightY,1/3 swap. Output order minY,minX,maxY,maxX. Full texture extent includes empty margins, unlike raster sample centres.96 original-instruction cases; repeated read-only live LT06 bounds verified."},
   {"6f0642f0","WidgetRegions_SetWorldBounds","Payoff230: thiscall ECX collection saved to local14,stack4 scalar rectangle*;RET4. Normalize each axis via COMISS, subtract world origin6fd3c82c+6c/+70, divide32 by scalar exponent adjustment,05ee40 floor min and floor max+1. Iterate ascending collection slots, skipNULL,14e8d0 writes rectangle. No world clamp or raster links.96 unchanged-original cases and two live repeats."},
   {"6f14e8d0","SpatialObject_SetBounds","Payoff230: thiscall ECX spatial object,stack4 integer rectangle*;RET4. Copies four words to+1c..28, no cell-link emission or counters.0642f0 applies after22e9c0 raster; cached construction inputs reconstruct identical bounds on engine load."},
   {"6f059590","PathAcc_QueryDistanceExcludingRectangles","Payoff230 MAP-04.2: after descending unit-bridge clears, descending widget-collection loop calls063d50 then clears its first region rectangle. Query1627e0 runs between clear/restore. Restore descending unit bridges then descending first widget regions, re-reading references. No scope counter writes, deduplication, early return or recoverable failure. Captain0594f0 supplies target,auxiliary source,predicted source then target widget,source widget. Engine integrates missing widget slots; wider group/point scope audit remains open."},
  };
  for(String[] row:rows) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(!f.getName().startsWith("FUN_")&&!f.getName().equals(row[1]))throw new Exception("Preserve "+f.getName());
  }
  for(String[] row:rows) {
   var f=getFunctionAt(toAddr(row[0]));f.setName(row[1],SourceType.USER_DEFINED);
   var prior=f.getComment();if(prior==null)f.setComment(row[2]);else if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);
  }
  var category=new CategoryPath("/WarcraftIII/Pathfinding127");
  var collection=new StructureDataType(category,"WC3WidgetRegionCollection",12);
  collection.replaceAtOffset(0,UnsignedIntegerDataType.dataType,4,"capacity","064460;Payoff230");
  collection.replaceAtOffset(4,UnsignedIntegerDataType.dataType,4,"count","063d50/0642f0;Payoff230");
  collection.replaceAtOffset(8,type("ptr:ptr:WC3SpatialPrefix"),4,"objects","063d50/0642f0;Payoff230");
  var texture=new StructureDataType(category,"WC3PathTexturePrefix",16);
  texture.replaceAtOffset(8,IntegerDataType.dataType,4,"width","22f1d0;Payoff230");
  texture.replaceAtOffset(12,IntegerDataType.dataType,4,"height","22f1d0;Payoff230");
  var manager=currentProgram.getDataTypeManager();
  for(var desired:new StructureDataType[]{collection,texture}) {
   var prior=manager.getDataType(category,desired.getName());
   if(prior!=null&&!prior.isEquivalent(desired))throw new Exception("Preserve "+desired.getName());
   if(prior==null)manager.addDataType(desired,DataTypeConflictHandler.KEEP_HANDLER);
  }
  abi("6f063d50","ptr:WC3SpatialPrefix","__fastcall",new String[][]{{"collection","ptr:WC3WidgetRegionCollection","ECX"}});
  abi("6f22f1d0","ptr:WC3PathScalarRectangle","__thiscall",new String[][]{{"texture","ptr:WC3PathTexturePrefix","ECX"},{"output","ptr:WC3PathScalarRectangle","4"},{"center","ptr:WC3PathVector2","8"},{"turn","i32","12"}});
  abi("6f0642f0","void","__thiscall",new String[][]{{"collection","ptr:WC3WidgetRegionCollection","ECX"},{"bounds","ptr:WC3PathScalarRectangle","4"}});
  abi("6f14e8d0","void","__thiscall",new String[][]{{"object","ptr:WC3SpatialPrefix","ECX"},{"bounds","ptr:WC3PathIntegerRectangle","4"}});
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f063d50","6f22f1d0","6f0642f0","6f14e8d0","6f05ee40","6f6501a0","6f059590","6f0594f0","6f9d8c70"}) {
   var fn=getFunctionAt(toAddr(a));if(fn==null)throw new Exception(a);
   out.append("FUNCTION ").append(a).append(' ').append(fn.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(fn.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');
    for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(fn.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
