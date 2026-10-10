// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class Work241Evidence extends GhidraScript {
 private DataType type(String name) throws Exception {
  if(name.equals("i32"))return IntegerDataType.dataType;
  if(name.equals("u32"))return UnsignedIntegerDataType.dataType;
  if(name.equals("void"))return VoidDataType.dataType;
  if(name.startsWith("ptr:"))return new PointerDataType(type(name.substring(4)),4);
  var t=currentProgram.getDataTypeManager().getDataType(new CategoryPath("/WarcraftIII/Pathfinding127"),name);
  if(t==null)throw new Exception("Missing type "+name);return t;
 }
 private void abi(String address,String returns,String convention,String[][] spec) throws Exception {
  var fn=getFunctionAt(toAddr(address));Parameter[] args=new Parameter[spec.length];
  for(int i=0;i<spec.length;i++) {
   var dt=type(spec[i][1]);var reg=currentProgram.getRegister(spec[i][2]);
   var location=reg!=null ? new VariableStorage(currentProgram,reg):new VariableStorage(currentProgram,Integer.parseInt(spec[i][2]),dt.getLength());
   args[i]=new ParameterImpl(spec[i][0],dt,location,currentProgram);
  }
  fn.setCallingConvention(convention);fn.setReturnType(type(returns),SourceType.USER_DEFINED);
  fn.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,args);
 }
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  var path=new CategoryPath("/WarcraftIII/Pathfinding127");
  var row=new StructureDataType(path,"WC3NetPointCandidateRow",0);
  row.add(new PointerDataType(VoidDataType.dataType,4),"unit",null);
  for(String name:new String[]{"matching_orders","total_orders","point_score","validation_kind","primary_subgroup","fallback","request_wrapper","aux_wrapper"})
   row.add(UnsignedIntegerDataType.dataType,name,null);
  if(row.getLength()!=36)throw new Exception("candidate row layout");
  currentProgram.getDataTypeManager().addDataType(row,DataTypeConflictHandler.REPLACE_HANDLER);
  String[][] rows={
   {"6f687a60","Unit_CountMatchingUserOrders","Payoff241: thiscall Unit, stack orderId,RET4. Walk UserOrder identity chain19c/1a0; orderId0 counts all current/pending user heads, otherwise compare node24. This is not Unit198, which is the separation-suppression depth."},
   {"6f687b30","Unit_QueryPointOrderScore","Payoff241: thiscall Unit, stack orderId/worldX*/worldY*,RETc. Unsigned minimum across authored ability vtable220 queries;ffffffff means none. Fallback queries predicted world pose058900, forces both Z terms zero, computes ((dx*dx+dy*dy)+0)*scalar3dcccccd and wrapped integer. Four live mixed-class captures retain exact score inputs/output words; native comparator oracle198 pairs. Ability-specific overrides remain separately uncertified."},
   {"6f2908c0","Selection_IsPrimarySubgroupMember","Payoff241 ABI correction: thiscall selection manager, stack Unit,RET4. Resolve manager1c0 primary subgroup, then290820 tests membership; it is not a fastcall Unit predicate. Net6ba800 passes context30 (player selection manager)."},
   {"6f6bcc40","NetOrder_ComparePointCandidates","Payoff241:198 original comparator pairs certify all seven priorities and signed32-wrap results. Keys: suppression depth<1 first, validation descending,total user heads ascending,matching heads ascending,primary subgroup descending,point score ascending,Unit canonical+c ascending. UI admission is sorted after attachment; physical member order remains canonical attachment order. Two idle ordinary/Alt witnesses publish0,3,1,2; two mixed stopped/active repeats second packet0,3,2,1. Unit198 is suppression depth, not active-order count. Bounded engine UI sorter uses retained keys before callbacks; independent script/captain producer rows remain ordered."},
   {"6f6ba800","NetOrder_AdmitPointTargetCandidate","Payoff241: builds36-byte WC3NetPointCandidateRow after canonical attachment, all rows beforeqsort6bcc40 and6b93a0 publication. Null-target Move validation kind2, row5 primary subgroup, row1/2 matching/all user heads, row3 ability minimum or predicted software-distance score. Later Alt association checks flight before FLOAT; synthetic FLOAT+fly attachment alone does not prove this later selection."}
  };
  for(String[] item:rows){var f=getFunctionAt(toAddr(item[0]));if(f==null)throw new Exception(item[0]);f.setName(item[1],SourceType.USER_DEFINED);String prior=f.getComment();if(prior==null)prior="";if(!prior.contains(item[2]))f.setComment(prior+"\n"+item[2]);}
  abi("6f687a60","u32","__thiscall",new String[][]{{"unit","ptr:void","ECX"},{"order_id","u32","4"}});
  abi("6f687b30","u32","__thiscall",new String[][]{{"unit","ptr:void","ECX"},{"order_id","u32","4"},{"world_x","ptr:WC3PathScalar","8"},{"world_y","ptr:WC3PathScalar","12"}});
  abi("6f2908c0","u32","__thiscall",new String[][]{{"selection","ptr:void","ECX"},{"unit","ptr:void","4"}});
  abi("6f6bcc40","i32","__cdecl",new String[][]{{"first","ptr:WC3NetPointCandidateRow","4"},{"second","ptr:WC3NetPointCandidateRow","8"}});
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f687a60","6f687b30","6f2908c0","6f290820","6f6bcc40","6f6ba800","6f6b93a0","6f6b9f70","6f6b8c10","6f058900"}) {
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);
   out.append("FUNCTION ").append(a).append(' ').append(f.getName()).append(' ').append(f.getSignature()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
