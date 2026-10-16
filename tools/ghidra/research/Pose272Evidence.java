// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Pose272Evidence extends GhidraScript {
 public static final String[][] NOTES={
  {"6f749a30","Terrain_InterpolateGroundCorners","Payoff272: stdcall pointXYZ*,four heights*,normal flag,normal output*,corner normals*;RET20. Normalize localXY by128; strict minimum in00,10,01,11 order. Accumulate ((a-min)*(1-y))*(1-x)+min, ((b-min)*(1-y))*x, ((c-min)*y)*(1-x), ((d-min)*y)*x, prior pointZ. Original512 kernels with normal flag0 run unchanged. Engine nested lerps differed by1-4ULP at27 live physical crossing heights; ordered minimum-relative interpolation restores exact words without touching older fixtures. Normal interpolation branch remains outside the new kernel lane."},
  {"6f66d990","Unit_ComputeSupportOrientation","Payoff272 MOVE-03.3: ECX Unit, stack4 float matrix3 output*, stack8 world point3*, RET8/EAX output. Three radius taps at heading+pi, heading-pi/4, heading+pi/4; native78d1e0 layer0 with deck flag1 for ordinary ground units. Flatten each height where max-height >=2.5*radius. Cross/normalize plane, project heading, extract1c7ba0, clamp pitch/roll to authored UI radians, reconstruct1c6c40. Zero limits below.001 bypass taps unless Unit5c.100. Native684480 passes a STACK output then copies into Unit294; direct-output filtering would miss it. Repeated stock10/10, custom3/5, zero0/0 bridge-water crossings verified read-only. Flying bank, zero-radius cached triangle normal and Unit5c.100 remain outside this lane."},
  {"6f1c7ba0","Matrix3_ExtractYawPitchRoll","Payoff272: ECX float column-major matrix3*, stack yaw*,pitch*,roll*;RET12,EAX regular1/singular0. Ordinary yaw atan2(m1,m0), pitch asin(-m2), roll atan2(m5,m8). Original shipped CRT math; ground live inputs and outputs captured, before clamping. Native pitch sign is opposite canonical engine nose-up pitch."},
  {"6f1c6c40","Matrix3_FromYawPitchRoll","Payoff272: ECX output float column-major matrix3*, stack yaw,pitch,roll floats;RET12. Precise double sin/cos stored asfloat, matrix products then transpose. Native published basis compared through generic game-authored pose-offset snapshot; presentation tolerance2e-6, not a new authoritative movement approximation."},
  {"6f748470","Terrain_BuildPresentationHeightGrids","Payoff272: ECX terrain, stack rebuild_flyer,levels;RET8. Mode0 Terrain79c grid is b4*b8 tiles, cell128, four corner flags3 height sum in00,10,01,11 order, multiply.25. Higher modes average2x2; ordinary ground slope taps use mode0, not packed physical corners or maximized flyer field. Native256-word base grid captured on accepted LT06 terrain."},
  {"6f744bb0","Terrain_GetPresentationHeight","Payoff272: ECX terrain, stack pointXY*,mode;RET8/ST0. Pass Terrain79c+mode*32 and point to743810; this is presentation sampling, distinct from physical mode-1 terrain height."},
  {"6f34d3f0","UnitUI_GetMaximumPitch","Payoff272: rawcode ECX;ST0 authored maxPitch radians. Native10deg3e32b8c3/custom3deg3d567750 verified live before slope taps."},
  {"6f34d410","UnitUI_GetMaximumRoll","Payoff272: rawcode ECX;ST0 authored maxRoll radians. Native10deg3e32b8c3/custom5deg3db2b8c3 verified live before slope taps."},
  {"6f33f950","UnitUI_GetElevationRadius","Payoff272: rawcode ECX;ST0 authored elevRad. Native stock Footman/custom radius20; abs below2^-22 selects78da50 cached triangle-normal branch, excluded from this crossing lane."}
 };
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong original");
  if(getScriptArgs().length!=1)throw new Exception("fresh output required");Path p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  var dm=currentProgram.getDataTypeManager();var cat=new CategoryPath("/WarcraftIII/Pathfinding127");
  var vec=new StructureDataType(cat,"WC3FloatVector3",12);String[] names={"x","y","z"};
  for(int i=0;i<3;i++)vec.replaceAtOffset(i*4,FloatDataType.dataType,4,names[i],"Payoff272 native presentation float, not software path scalar.");
  var old=dm.getDataType(cat,vec.getName());if(old!=null&&!old.isEquivalent(vec))throw new Exception("preserve vector");
  var vector=dm.resolve(vec,DataTypeConflictHandler.KEEP_HANDLER);
  var mat=new StructureDataType(cat,"WC3FloatMatrix3",36);String[] columns={"forward","left","normal"};
  for(int i=0;i<3;i++)mat.replaceAtOffset(i*12,vector,12,columns[i],"Payoff272 66d990/1c7ba0 column-major support basis.");
  old=dm.getDataType(cat,mat.getName());if(old!=null&&!old.isEquivalent(mat))throw new Exception("preserve matrix");
  var matrix=dm.resolve(mat,DataTypeConflictHandler.KEEP_HANDLER);
  var unit=(Structure)dm.getDataType(cat,"WC3UnitOrdersPrefix");
  for(int i=0;i<2;i++) {
   int off=i==0?0x290:0x294;DataType t=i==0?FloatDataType.dataType:matrix;
   var component=unit.getComponentAt(off);
   if(component!=null&&!Undefined.isUndefined(component.getDataType())&&!component.getDataType().isEquivalent(t))throw new Exception("preserve unit pose");
   unit.replaceAtOffset(off,t,t.getLength(),i==0?"support_heading":"support_orientation",i==0?"Payoff272 684480 cached heading; abs delta>=.001 rebuilds orientation independently of support queries.":"Payoff272 6845f0 stack basis from virtual1b8 copied at6845f6..68460f.");
  }
  var up=new PointerDataType(unit,4);var mp=new PointerDataType(matrix,4);var vp=new PointerDataType(vector,4);var fp=new PointerDataType(FloatDataType.dataType,4);
  for(String[] row:NOTES) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);else if(!f.getName().equals(row[1]))throw new Exception("preserve "+f.getName());
   String c=f.getComment();if(c==null)f.setComment(row[2]);else if(!c.contains(row[2]))f.setComment(c+"\n"+row[2]);
  }
  for(String a:new String[]{"6f66d990","6f1c7ba0","6f1c6c40"}) {
   var f=getFunctionAt(toAddr(a));f.setCustomVariableStorage(false);f.setCallingConvention("__thiscall");
   boolean pose=a.equals("6f66d990"),extract=a.equals("6f1c7ba0");
   f.setReturnType(pose?mp:extract?UnsignedIntegerDataType.dataType:VoidDataType.dataType,SourceType.USER_DEFINED);
   var params=new java.util.ArrayList<Parameter>();
   params.add(new ParameterImpl("self",pose?up:mp,new VariableStorage(currentProgram,currentProgram.getRegister("ECX")),currentProgram));
   String[] n=pose?new String[]{"output","point"}:new String[]{"yaw","pitch","roll"};
   for(int i=0;i<n.length;i++)params.add(new ParameterImpl(n[i],pose?(i==0?mp:vp):extract?fp:FloatDataType.dataType,new VariableStorage(currentProgram,4+i*4,4),currentProgram));
   f.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,params.toArray(new Parameter[0]));
  }
  for(String[] row:new String[][]{{"6f66da38","SupportOrientation_ReadHeadingAndUILimits"},{"6f66dd5e","SupportOrientation_StoreSampleHeight"},{"6f66dd83","SupportOrientation_ApplyHeightDiscontinuity"},{"6f66e1ac","SupportOrientation_ClampAuthoredAngles"},{"6f6845f0","SupportPosition_CopyStackOrientation"}})createLabel(toAddr(row[0]),row[1],true,SourceType.USER_DEFINED);
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES) {
   var f=getFunctionAt(toAddr(row[0]));out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');out.append("PROTOTYPE ").append(f.getPrototypeString(true,true)).append('\n');out.append("COMMENT ").append(f.getComment()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
