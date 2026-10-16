// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Capscope273Evidence extends GhidraScript {
 public static final String[][] NOTES={
  {"6f16aff0","MoveRequest_ResolveTargetRegion","Payoff273 MAP-04.2: ECX canonical request, no stack args, EAX spatial object*. Prefer cached f4 only while region38!=-1; clear retired cache then resolve dc/e0 target through16be70 and read mover98.24 original fixture cases cover no target,distinct,member alias,invalid generation,valid cache,retired cache at outer depths0/3 and ready/drop. Supplied cache/alias state is deliberate, not a public Captain claim. Engine Captain producer currently has no cached-region input; target fallback is implemented without inventing one."},
  {"6f16be70","MoveRequest_ResolveTargetMover","Payoff273 operand ABI: ECX canonical request,no stack args,EAX registered mover* orNULL; dc/e0 identity resolved by054530. Invalid generation returnsNULL. Neither target offset nor cached region refers to a game-unit pointer."},
  {"6f16da60","MoveRequest_ToggleTargetRegionExclusion","Payoff273 operand ABI: ECX canonical request,stack4 acquire,RET4. Resolve16aff0 each boundary; INC/DEC region40, no booleanization or deduplication with member98.169c50 calls target-on BEFORE all attachments;169d60 releases all attachments BEFORE target-off. Two read-only Captain repeats and unhooked public control; valid public target scopes have counter0,1,1,0 while member counter0,0,1,1,0. Engine Captain now uses canonical readiness/publication rather than provisional physical bindings."},
  {"6f169c50","MoveRequest_AcquireCandidateExclusions","Payoff273: Captain/target scopes share the same canonical owner as selected/queued requests.16da60 target-on precedes pending and ready member holds. Native member-target alias adds twice and restores twice. Complete original24-case matrix plus public Captain repeats. Engine physical shared references begin only once canonical publication has no pending members."},
  {"6f169d60","MoveRequest_ReleaseCandidateExclusions","Payoff273: re-resolve all canonical attachments despite consumed ready rows; release target last through16da60. Outer depths0/3 preserved for busy,ready/drop and alias matrix. No callback/reentrancy is introduced into this synchronous scope. Cached-region producer and unresolved Stop notifications remain separately open."},
  {"6f16bcf0","MoveRequest_PublishReadyCohorts","Payoff273: replace the legacy engine Captain prebinding path with bounded canonical attachments/readiness. Partition and seed while target and every attached member fine object are held, restore on both busy and successful exits. Shared parameters,target identity,predicted destination and formation flags copy at physical publication. Existing Captain partition and cold-save literals are retained; no general MAP-04.2 closure claim."}
 };
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong original");
  if(getScriptArgs().length!=1)throw new Exception("fresh output required");Path path=Path.of(getScriptArgs()[0]);if(Files.exists(path))throw new Exception("exists");
  var dm=currentProgram.getDataTypeManager();var cat=new CategoryPath("/WarcraftIII/Pathfinding127");
  var request=(Structure)dm.getDataType(cat,"WC3MoveRequestCanonical");var region=dm.getDataType(cat,"WC3SpatialPrefix");var mover=dm.getDataType(cat,"WC3MoverPrefix");
  if(request==null||region==null||mover==null)throw new Exception("missing existing types");
  var rp=new PointerDataType(request,4);var sp=new PointerDataType(region,4);var mp=new PointerDataType(mover,4);
  var component=request.getComponentAt(0xf4);if(component!=null&&!Undefined.isUndefined(component.getDataType())&&!component.getDataType().isEquivalent(sp))throw new Exception("preserve f4");
  request.replaceAtOffset(0xf4,sp,4,"cached_target_region","Payoff273 16aff0: prefer while region38!=-1; clear retired cache then use resolved target mover98. Supplied original fixture cache cases; public Captain captures haveNULL cache.");
  for(String[] row:NOTES) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   // Preserve existing descriptive names; MapPathfinding already owns the broad names.
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)f.setComment(row[2]);else if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
  }
  for(String address:new String[]{"6f16aff0","6f16be70","6f16da60"}) {
   var f=getFunctionAt(toAddr(address));boolean toggle=address.equals("6f16da60");
   f.setCustomVariableStorage(false);f.setCallingConvention("__thiscall");
   f.setReturnType(toggle?VoidDataType.dataType:address.equals("6f16aff0")?sp:mp,SourceType.USER_DEFINED);
   var params=new java.util.ArrayList<Parameter>();params.add(new ParameterImpl("request",rp,new VariableStorage(currentProgram,currentProgram.getRegister("ECX")),currentProgram));
   if(toggle)params.add(new ParameterImpl("acquire",UnsignedIntegerDataType.dataType,new VariableStorage(currentProgram,4,4),currentProgram));
   f.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,params.toArray(new Parameter[0]));
  }
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES) {
   var f=getFunctionAt(toAddr(row[0]));out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append(' ').append(f.getPrototypeString(false,true)).append('\n').append(f.getComment()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  component=request.getComponentAt(0xf4);out.append("FIELD WC3MoveRequestCanonical ").append(component.getOffset()).append(' ').append(component.getFieldName()).append(' ').append(component.getDataType().getPathName()).append(' ').append(component.getComment()).append('\n');
  Files.writeString(path,out);println("Saved "+path);
 }
}
