// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class Graph266Evidence extends GhidraScript {
 static final String[][] NOTES={
  {"6f145c70","AgentWrapper_Destroy","Payoff266 ORDER-04.2: complete populated original factory graphs. Mark destroying, owner notification and payload reclaim precede borrowed relation detach; owned children destroyed in reverse array order, including null holes and nested descendants.16 graphs x2 lifetimes across COrderPoint/CTaskPoint; four borrowed peers survive unchanged, all owned identities/payloads reclaimed exactly once, zero live counts/pending requests, exact LIFO wrapper and payload factory reuse without new factory allocation. Class and canonical bindings/extra child ref supplied; all constructors, relation producers, dispatch/vtables, clock and reclaim original; only Storm imports use host storage."},
  {"6f1465f0","AgentWrapper_DetachIncomingRelations","Payoff266: ECX owner, stack4 detachBoth, RET4. Drains category3(ac),2(9c),1(8c),0(7c) in that order via146650(link+8,detachBoth). Parent destruction passes0: detach borrowed alpha/receiver binding24/28/30 without destroying peer or its payload. Native insertion plus two peers per list verified in32 populated lifetime compositions."},
  {"6f146690","AgentWrapper_DestroyOwnedChildren","Payoff266: ECX wrapper, RET. Iterate child table64/count74 from count-1 down0; skip null, invoke ORIGINAL child virtual10(0). Nested destruction is depth first. Then146c80(table58,0,count) clears logical count; allocated backing persists for reuse. Native return order differs from destructor-entry order, both frozen. Engine must release owned pending task storage while borrowed unit targets survive."},
  {"6f145dc0","AgentWrapper_BindChildRelation","Payoff266: ECX parent, stack4 child index, stack8 category0..3, RET8. Bounds and null guards; selects list78/88/98/a8, tail15e180(ECX child,stack4 parent,stack8 list). Original producer populates all four relation lists in both point classes; newest child link precedes borrowed peer."},
  {"6f146850","AgentChildren_Append","Payoff266: ECX DynamicTable, stack4 pointer to repeated wrapper value, stack8 count, RET8. Appends values to owned-child table; original growth via Storm external memory contract, backing persists at wrapper-slot reuse. Factory reuse needs no imports, but a recycled borrowed peer becoming a parent may allocate child-array backing. Null entries are legitimate and skipped by146690."},
  {"6f15e180","AgentWrapper_BindReceiverRelation","Payoff266: ECX child wrapper, stack4 receiver owner, stack8 intrusive list anchor, RET8. Unlink prior24/28/30, store owner30, begin binding, then original15e160 inserts child24 at list head. Borrowed references and array ownership are distinct; parent release unlinks references but only child-array members are destroyed."},
  {"6f667400","OrderPoint_DestroyPayload","Payoff266: extra child reference at54 decrements before base observers. Child retained2->1 during root payload reclaim, then real child-wrapper release reaches zero and original factory reclaim exactly once. Separate owned/reference and wrapper return orders frozen; no fabricated child destructor callback."},
  {"6f667a80","TaskPoint_DestroyPayload","Payoff266: same owned-reference lifecycle at4c as COrderPoint54, using original CTaskPoint factory block64 and native destructor. Both final live counters and reference cleanup checked through two complete graph lifetimes."}
 };
 void field(Structure s,int offset,DataType type,String name)throws Exception{s.replaceAtOffset(offset,type,type.getLength(),name,"Payoff266: original populated factory graphs, native ECX/stack operands and RET. See Graph266Evidence.java and graph266_oracle.py.");}
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong original");
  if(getScriptArgs().length!=1)throw new Exception("fresh output required");Path p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  var dm=currentProgram.getDataTypeManager();var cat=new CategoryPath("/WarcraftIII/Pathfinding127");
  DataType vp=new PointerDataType(VoidDataType.dataType,4);
  var base=(Structure)dm.getDataType(cat.getPath()+"/WC3AgentWrapperPrefix");
  field(base,0x24,vp,"receiver_previous");field(base,0x28,vp,"receiver_next");
  field(base,0x30,new PointerDataType(base,4),"receiver_owner");
  var list=new StructureDataType(cat,"WC3AgentRelationList266",16);field(list,0,vp,"anchor");field(list,4,vp,"head");
  DataType savedList=dm.addDataType(list,DataTypeConflictHandler.REPLACE_HANDLER);
  var wrapper=new StructureDataType(cat,"WC3AgentRelations266",0xb8);
  field(wrapper,0,dm.getDataType(cat.getPath()+"/WC3AgentWrapperPrefix"),"base");
  field(wrapper,0x58,dm.getDataType(cat.getPath()+"/WC3DynamicTablePrefix"),"children");
  for(int i=0;i<4;i++)field(wrapper,0x78+16*i,savedList,"relations"+i);
  DataType saved=dm.addDataType(wrapper,DataTypeConflictHandler.REPLACE_HANDLER);
  DataType wp=new PointerDataType(saved,4),lp=new PointerDataType(savedList,4),tp=new PointerDataType(dm.getDataType(cat.getPath()+"/WC3DynamicTablePrefix"),4);
  for(String a:new String[]{"6f145c70","6f1465f0","6f146690","6f145dc0","6f146850","6f15e180"}){
   var f=getFunctionAt(toAddr(a));f.setCallingConvention("__thiscall");f.setReturnType(a.equals("6f146850")?UnsignedIntegerDataType.dataType:VoidDataType.dataType,SourceType.USER_DEFINED);
   java.util.ArrayList<Parameter> ps=new java.util.ArrayList<>();
   ps.add(new ParameterImpl("self",a.equals("6f146850")?tp:wp,new VariableStorage(currentProgram,currentProgram.getRegister("ECX")),currentProgram));
   String[] names=a.equals("6f146690")?new String[]{}:a.equals("6f145dc0")?new String[]{"index","category"}:a.equals("6f146850")?new String[]{"values","count"}:a.equals("6f15e180")?new String[]{"owner","list"}:new String[]{a.equals("6f1465f0")?"detachBoth":"context"};
   for(int i=0;i<names.length;i++){DataType t=a.equals("6f15e180")?(i==0?wp:lp):a.equals("6f146850")&&i==0?vp:UnsignedIntegerDataType.dataType;ps.add(new ParameterImpl(names[i],t,new VariableStorage(currentProgram,4+4*i,4),currentProgram));}
   f.replaceParameters(ps,Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED);
  }
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";old=old.replace("exact LIFO wrapper and payload reuse with no new allocation","exact LIFO wrapper and payload factory reuse without new factory allocation").replace("retained backing reused without imports on second lifetime.","backing persists at wrapper-slot reuse. Factory reuse needs no imports, but a recycled borrowed peer becoming a parent may allocate child-array backing.");if(!old.contains(row[2]))old+="\n"+row[2];f.setComment(old);
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append("PROTOTYPE ").append(f.getPrototypeString(true,true)).append('\n').append("COMMENT ").append(f.getComment()).append('\n');
   for(var param:f.getParameters())out.append("PARAM ").append(param.getName()).append(' ').append(param.getVariableStorage()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  for(DataType t:new DataType[]{base,savedList,saved}){out.append("STRUCT ").append(t.getPathName()).append(" size ").append(t.getLength()).append('\n');for(var c:((Structure)t).getDefinedComponents())out.append(c.getOffset()).append('|').append(c.getDataType().getPathName()).append('|').append(c.getFieldName()).append('\n');}
  Files.writeString(p,out);println("Saved "+p);
 }
}
