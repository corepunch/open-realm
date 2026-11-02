// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class Work231Evidence extends GhidraScript {
 private DataType type(String name) throws Exception {
  if(name.equals("u32"))return UnsignedIntegerDataType.dataType;
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
  fn.setCallingConvention(convention);fn.setReturnType(returns.equals("void")?VoidDataType.dataType:returns.equals("u32")?UnsignedIntegerDataType.dataType:type(returns),SourceType.USER_DEFINED);
  fn.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,args);
 }
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f04df50","PathWorld_TestPointQuery","Payoff231 MAP-04.2: 600 complete unchanged-original executions verify blocked EAX, counted exclusion restoration, endpoint mode restoration, map query counter and object stamps. High-only masks still traverse active raw identities before low-mask miss; duplicate links stamp once. No callback/map edit/recoverable failure in query closure. Engine native now uses shared raw-cell traversal."},
   {"6f04e090","PathWorld_TestTerrainPointMask","Payoff231: high-only mask changes object verdict eligibility, not traversal/stamping. Two read-only public-native captures plus unhooked control cover six positions/lifecycles and all eight pathing types. Native uses NULL excluded bridge."},
   {"6f149320","PathFine_TestScalarPointCell","Payoff231: EAX is the occupied-cell boolean, consumed by04df50; prior void annotation was incorrect. Thiscall ECX fine,stack4 scalarXY*,8 mask*,RET8. Stores a4 then software floor and1489a0. Endpoint mode forced by caller, restored after exclusion release."},
   {"6f05bd30","UnitMoverBridge_ToggleSpatialExclusion","Payoff231: acquire/release resolves bridge canonical identity each time, then updates mover+98 object+40. 600 complete original point-query scopes cover low count bits,20000000/40000000 motion flags,10000000 region flag and alias links. No identity mutation/callback in point query closure."}
  };
  for(String[] row:rows) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null||!f.getName().equals(row[1]))throw new Exception("Preserve "+row[0]);
   var prior=f.getComment();if(prior==null)f.setComment(row[2]);else if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);
  }
  abi("6f149320","u32","__thiscall",new String[][]{{"fine","ptr:WC3FineSearchPrefix","ECX"},{"point","ptr:WC3PathVector2","4"},{"mask","ptr:u32","8"}});
  abi("6f05bd30","ptr:WC3SpatialPrefix","__thiscall",new String[][]{{"bridge","ptr:WC3UnitMoverBridge","ECX"},{"on","i32","4"}});
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f04df50","6f04e090","6f04e060","6f149320","6f05bd30","6f1489a0","6f05ee40","6f6501a0"}) {
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);
   out.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');
    for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
