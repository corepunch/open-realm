// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Work229Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f125aa0","Model_RayTriangleFloat","Payoff229 MAP-02.2: fastcall ECX ray origin,EDX normalized direction; stack A*,B*,C*,distance*;RET10,EAX hit. Sets distance+INF first. Abs determinant threshold2^-22 (equality accepted). Moller-Trumbore ordinary SSEfloat, u and distance dot order Y,X,Z; determinant and v X,Y,Z. Reject u/v outside0..1 and rounded v+u>1; no nonnegative-distance or finite-segment check.1837 original-instruction cases retain exact hit/distance bits; do not substitute generic segment intersection."},
   {"6f125520","Model_TraceIndexedPrimitiveGroup","Payoff229: ECX origin,EDX direction; stack matrixTable*,vertexCount,vertices*,vertexStride,groupBytes*,groupStride,primitive,indexCount,ushortIndices*,distance*,triangleIndex*;RET2c. Per-query transformed vertices in global d4addc; groupByte selects 12-float matrix. Native primitive3 triangle triplets,4 strip,5 fan (MDX authored4/5/6). Strict closest-distance comparison keeps first triangle on ties. Read-only bridge trace: selectable geoset128 vertices,192 indices,64 triangles. Engine prepares immutable per-model triangles and reuses per-instance transformed geometry for rigid poses."},
   {"6f18bbe0","Model_TracePreparedMeshes","Payoff229: ECX prepared model,EDX ray start; stack rayEnd*,distance*,includeChildren,includeUnselectable;RET10. Normalizes ray direction; visits active geoset instances, material visibility byte, and skips GEOS+124 bit4 unless includeUnselectable. Strict closest-distance across groups. GEOS+c vertices,+10 count,+4c vertex groups,+c8 primitive-group count,+cc groups,+e0 indices,+104 matrix mode,+11c material,+124 selectable. Animated bone/material support remains open in engine."},
   {"6f1c8230","FloatMatrix3x4_TransformPoint","Payoff229: ECX output,EDX vertex;stack12-float matrix*;RET4,EAX output. x=(m3*y+x*m0)+m6*z+m9; y=(m1*x+m4*y)+m7*z+m10; z=(m2*x+m5*y)+m8*z+m11, each SSEfloat rounding retained.512 original-instruction cases; live rigid LT06 matrix uses cos(1.57079625)=7.54979e-8 and translation1024,672,-192."},
   {"6f1a3880","Model_TraceInstanceMesh","Payoff229: thiscall instance; EDX rayStart*,stack rayEnd*,includeChildren,distance*;RETc. Reject missing model+20; prepare via vt+c if flags28 lacks200000. vt+44 supplies sphere scale;18c9e0 sphere and actual mesh query. Walkable world passes includeChildren0."},
   {"6f18c9e0","Model_TraceWithBoundingSphere","Payoff229: fastcall ECX prepared model,EDX rayStart; stack sphereScale,rayEnd*,unused,distance*,includeChildren;RET14.18b5c0 selects current sequence sphere,18e390 transforms center and scales radius,18a670 computes segment distance squared; <=radius squared admits18bbe0 with includeUnselectable0. Live Stand LT06 sphere1024.9260,671.7625,-35.6725,r646.21399 matches authored sequence extent midpoint transformed to world."},
   {"6f18a670","FloatRay_SegmentDistanceSquared","Payoff229: fastcall ECX point,EDX segmentStart;stack segmentEnd*,outSquared*,outFraction*;RETc. Dot/length squared and result sum order Y,X,Z; projection clamped0..1 but retains rounded residual.518 unchanged-original cases. This finite-segment sphere gate does not change the triangle kernel into a finite ray."},
   {"6f785d20","WalkableDeck_TraceRegisteredModels","Payoff229: thiscall registered walkable list, stack rayStart*,rayEnd*,outDistance*;RETc. Records stride2c;6be470 resolves enabled model,1a3880 with children0. Smallest distance wins, equal accepted (<=); boolean hit retained independently of output pointer.782a80 fixed vertical ray2560 to-2560; final height2560-distance."},
  };
  for(String[] row:rows) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(!f.getName().startsWith("FUN_")&&!f.getName().equals(row[1]))throw new Exception("Preserve "+f.getName());
  }
  var category=new CategoryPath("/WarcraftIII/Pathfinding127");
  var geo=new StructureDataType(category,"WC3PreparedModelGeoset",0x128);
  int[] offsets={0xc,0x10,0x4c,0xc8,0xcc,0xe0,0x104,0x11c,0x124};
  String[] names={"vertices","vertex_count","vertex_groups","primitive_group_count","primitive_groups","indices","matrix_mode","material_id","selectable_flags"};
  for(int i=0;i<offsets.length;i++)geo.replaceAtOffset(offsets[i],UnsignedIntegerDataType.dataType,4,names[i],"18bbe0 original operands; Work229");
  var manager=currentProgram.getDataTypeManager();var old=manager.getDataType(category,"WC3PreparedModelGeoset");
  if(old!=null&&!old.isEquivalent(geo))throw new Exception("Preserve geoset layout");
  if(old==null)manager.addDataType(geo,DataTypeConflictHandler.KEEP_HANDLER);
  for(String[] row:rows) {
   var f=getFunctionAt(toAddr(row[0]));f.setName(row[1],SourceType.USER_DEFINED);
   var prior=f.getComment();if(prior==null)f.setComment(row[2]);else if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);
  }
  createLabel(toAddr("6f125678"),"Model_MeshVerticesTransformed",true,SourceType.USER_DEFINED);
  createLabel(toAddr("6f18ca19"),"Model_TraceWorldSphereReady",true,SourceType.USER_DEFINED);
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f125aa0","6f125520","6f18bbe0","6f1c8230","6f1a3880","6f18c9e0","6f18a670","6f785d20","6f782a80","6f18b5c0","6f18e390"}) {
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
