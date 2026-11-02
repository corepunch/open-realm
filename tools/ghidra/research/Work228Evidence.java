// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Work228Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f7474d0","Terrain_InitializeFlyerSupportField","Payoff228 MAP-02.2: thiscall terrain. Terrain+7a4 float grid, dimensions4*b4/4*b8, cells32. Fine cell -> nearest vertex750100. Ground height uses flags7; vertex water flag10 raises raw value to water when water support active. Native4096 words captured before widgets."},
   {"6f7307b0","Terrain_RaiseFlyerSupportRectangle","Payoff228 MAP-02.2: thiscall terrain; stack rectangle pointer [minY,minX,maxY,maxX], float lift; RET8. Inclusive clipped fine rectangle, nearest ground vertex plus lift (no water substitution), strict maximum against existing field. Native LT06 rectangle384,512,960,1536 and lift256 captured read-only."},
   {"6f73fc80","Terrain_FinalizeFlyerSupportField","Payoff228 MAP-02.2: thiscall terrain. Read FlyerMap.MaximizeRadius and SmoothLevels, both default0. Horizontal74d4b0 transposed maximum then vertical; repeated2x2 SSE float average in order a+0+b+c+d, multiply.25, dimensions/2 and cell sizes*2. Stock6/3; native64-word completed grid repeated exactly."},
   {"6f74d4b0","FloatGrid_MaximizeRowsAndTranspose","Payoff228 MAP-02.2: fastcall ECX input floats,EDX output floats; stack width,height,radius;RET12. Compare left endpoint, increasing/plateau/falling peaks strictly inside window, right endpoint, strict greater only. Candidate order affects signed-zero bits.32 unchanged-original instruction cases, including width1024 and radius1027. Engine monotone deque over these same peaks preserves order in linear time."},
   {"6f743810","Terrain_InterpolateFloatHeightGrid","Payoff228 MAP-02.2: thiscall ECX terrain; stack floatGrid*,pointXY*;RET8,ST0. Subtract origin c8/c4, divide cells, subtract.5. Truncate towardzero before subtracting integer; clamp indices to0..size-2 and zero fraction ONLY when clamped. Near-origin negative fractions survive. SSE order rightTop*x*(1-y)+(1-x)*leftTop*(1-y)+(1-x)*leftBottom*y+rightBottom*x*y.80 original instruction cases plus627 live samples."},
   {"6f7434b0","Terrain_GetFlyerSupportHeight","Payoff228 MAP-02.2: thiscall terrain,stack pointXY*,RET4/ST0. Delegate743810 with terrain+7a4 grid. Live observer restricts sampler to return7434c2; other terrain grids use the same interpolation function."},
   {"6f24f380","FlyerMap_RaiseInitialWidget","Payoff228 MAP-02.2: initial-widget enumerator callback. Resolve widget, query world point and shape/region rectangle22f1d0, virtual178 flyHeight, wrapper78ab20 ->7307b0. Native LT06 snaps authored1024,640 to1024,672 before yielding32x18 texture bounds. No claim that textureless widget bounds are completely implemented."},
   {"6f24ffc0","FlyerMap_FinalizeInitialWidgets","Payoff228 MAP-02.2: enumerate initial widgets04c5d0 using24f380;78b290 ->73fc80 finalize once; owner+c=1. Direct map-start call1eabe6. Completed field retains initial map history; do not rebuild from live edicts on save/load."},
};
  for(String[] row:rows) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(!f.getName().startsWith("FUN_")&&!f.getName().equals(row[1]))throw new Exception("Preserve "+f.getName());
  }
  var category=new CategoryPath("/WarcraftIII/Pathfinding127");
  var grid=new StructureDataType(category,"WC3FlyerHeightGrid",32);
  String[] names={"width","height","cell_x","cell_y","capacity","count","values","allocation_quantum"};
  for(int i=0;i<8;i++) {
   DataType t=(i==2||i==3)?FloatDataType.dataType:(i==6?new PointerDataType(FloatDataType.dataType,4):UnsignedIntegerDataType.dataType);
   grid.replaceAtOffset(i*4,t,4,names[i],"7474d0/743810 original operand; Payoff228");
  }
  var manager=currentProgram.getDataTypeManager();var old=manager.getDataType(category,"WC3FlyerHeightGrid");
  if(old!=null&&!old.isEquivalent(grid))throw new Exception("Preserve grid layout");
  if(old==null)manager.addDataType(grid,DataTypeConflictHandler.KEEP_HANDLER);
  for(String[] row:rows) {
   var f=getFunctionAt(toAddr(row[0]));f.setName(row[1],SourceType.USER_DEFINED);
   var prior=f.getComment();if(prior==null)f.setComment(row[2]);else if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);
  }
  createLabel(toAddr("6f73fce3"),"FlyerMap_ReadMaximumRadius",true,SourceType.USER_DEFINED);
  createLabel(toAddr("6f73fd75"),"FlyerMap_ReadSmoothingLevels",true,SourceType.USER_DEFINED);
  createLabel(toAddr("6f743902"),"FloatHeightGrid_ReturnSSEWord",true,SourceType.USER_DEFINED);
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f7474d0","6f7307b0","6f73fc80","6f74d4b0","6f743810","6f7434b0","6f24f380","6f24ffc0","6f66d780","6f750100"}) {
   var fn=getFunctionAt(toAddr(a));if(fn==null)throw new Exception(a);
   out.append("FUNCTION ").append(a).append(' ').append(fn.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(fn.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');
    for(byte b:i.getBytes())out.append(String.format("%02x",b&255));
    out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(fn.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
