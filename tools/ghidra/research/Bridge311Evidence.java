// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
import ghidra.program.model.data.*;
public class Bridge311Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f78bc90","Terrain_GetPlacementLevelWithDeck","Payoff311: Effective integer placement level: nearest packed W3E vertex, then signed maximum with every containing retained registry rectangle. Bridge flag reports containment even when the terrain maximum wins.178 public cases across20 profiles reproduce the exact admission/perimeter destinations."},
   {"6f782bf0","WalkableDeck_GetPlacementLevel","Payoff311: Owner+4 count, +8 stride2c entries. Rectangle limits are inclusive xmin10/xmax18/ymin0c/ymax14. Level array24 indexes row-major; maximum begins at-1. The full rotated texture rectangle contributes, including base-only black pixels."},
   {"6f77fe80","WalkableDeck_RegisterPlacementSurface","Payoff311: Identity-deduplicated registration. Entry holds a strong widget reference and independently retained level pixels. Repeated bridge311 captures observe three insertions and no disable on Kill/Restore/Remove: registered levels4,6,9 survive removal. Public inactivity retires live pathing later; do not equate that lifetime with this registry."},
   {"6f784820","WalkableDeck_BuildPlacementSurface","Payoff311: Capture rotated texture world bounds; convert with software32-unit Math and wrapped truncation. Inclusive maxima=min+dimensions-1. Base cliff level is nearest terrain at signed integer rectangle midpoint.6c1ab0 computes additive level delta;22e9c0 invokes78e260 for every category/black pixel."},
   {"6f78e260","WalkableDeck_WritePlacementPixel","Payoff311: Store captured base level for category0, otherwise base+delta. Convert callback point with software scalar truncation. Raw LT06 entries all576 values equal4/6/9. Integer level data is distinct from mesh geometry and live blocking regions."},
   {"6f6c1ab0","Destructable_GetPlacementLevelDelta","Payoff311: Read authored integer70, virtuale4 support at current nativeXY with layer-1, and6c3370 sampling radius.6c0040 selects baseline support. Store both x87 results to float, then SSE subtract, multiply1/128, add.5, signed truncation, plus cliffHeight. CreationZ80 is NOT the queried height: new elevated bridges see prior retained model geometry."},
   {"6f6c0040","Destructable_GetSamplingSupportHeight","Payoff311: Actual assembly has stack radius and unused facing pointer; RET8. Strict abs(radius-0)<float.001 selects layer-2. Otherwise min(CVTTSS2SI(radius/128),3), passed to78d1e0 with flag0. Nonnegative0..3 read prepared presentation mips and exclude deck geometry. Existing decompiler prototype omitted both arguments; instruction listing is authoritative."},
   {"6f6c3370","Destructable_GetAuthoredSamplingRadius","Payoff311: Hash rawcode lookup returns float at row50, zero if missing. Read-only LT06 row snapshot confirms200; ordinary authored radius is200, flyH is256.6c1ab0 and6c3120 use this sampling setting independently of physical creationZ."},
   {"6f6c09b0","Destructable_GetAuthoredCliffHeight","Payoff311: Hash rawcode lookup returns signed word row70; original LT06 value2.6c1ab0 adds it after host SSE rounding/truncation."},
   {"6f6505e0","Widget_GetSupportHeight","Payoff311: Virtuale4 on destructable vtableb7d708: query78d1e0(point,layer,out,flag0), store float, add virtuale8 extra height.652bc0 returns0 for the sampled destructable. Ignores its own creationZ; previous registered bridges can supply higher support after public removal."},
   {"6f744bb0","Terrain_GetPresentationHeight","Payoff311: Layer0..3 indexes Terrain79c grid headers at stride20h, then743810 cell-centred interpolation. Repeated read-only bridge311 grid snapshots have16x16,8x8,4x4,2x2 values, cells128/256/512/1024. Do not substitute physical terrain for sampling-radius-selected baselines."},
   {"6f748470","Terrain_BuildPresentationHeightGrids","Payoff311: Build mode0 from four packed vertex heights in00,10,01,11 order. Higher layers halve dimensions/double cells, average previous four cells in the same explicit SSE add/add/add/add/multiply. All340 values across four grids are frozen from repeated read-only retail observations."},
   {"6f6c3630","Destructable_RequestRemoval","Payoff311: Public210950 dispatch. If alive, virtual14c(1,1) retires alive regions then148(0,1,0) constructs death texture.6c0890 notifies target loss/inactivity; final virtual5c0557b0 requests deferred canonical-wrapper release. Same-callback placement observes death footprint; later primary release retires it. Do not immediately free the game edict."},
   {"6f6c0890","Destructable_CommitRemovalVisibility","Payoff311: Remove path notifies targets before generic world removal event and render/player membership teardown.653190 clears widget map membership. This synchronous stage is separate from0557b0 scheduled wrapper release and retained walkable registry strong reference."},
   {"6f0557b0","Agent_RequestDeferredRelease","Payoff311: Shared destructable/unit/order canonical-wrapper release producer. bridge311 removal resolves this slot5c after the synchronous death-footprint stage. Existing primary-clock/minimum-delay contract remains authoritative; the renderer or wall time must not select release timing."}
  };
  StringBuilder out=new StringBuilder();
  var manager=currentProgram.getDataTypeManager();var category=new CategoryPath("/WarcraftIII/Pathfinding127");
  String[][] fields={{"widget","base_level","delta_level","minimum_y","minimum_x","maximum_y","maximum_x","level_capacity","level_count","levels","level_growth"},{"capacity","count","entries","growth"}};
  String[] names={"WC3PlacementSurfaceEntry311","WC3PlacementSurfaceRegistry311"};
  for(int n=0;n<2;n++){
   StructureDataType desired=new StructureDataType(category,names[n],fields[n].length*4);
   for(int i=0;i<fields[n].length;i++){
    DataType t=(n==0&&i>=1&&i<=6)?IntegerDataType.dataType:UnsignedIntegerDataType.dataType;
    if(n==0&&i==0)t=new PointerDataType(manager.getDataType(category,"WC3UnitOrdersPrefix"),4);
    if(n==0&&i==9)t=new PointerDataType(IntegerDataType.dataType,4);
    if(n==1&&i==2)t=new PointerDataType(manager.getDataType(category,names[0]),4);
    desired.replaceAtOffset(i*4,t,4,fields[n][i],"Payoff311: original77fe80/784820/782bf0 and repeated raw registry captures.");
   }
   DataType existing=manager.getDataType(category,names[n]);
   if(args[1].equals("apply")){if(existing!=null&&!existing.isEquivalent(desired))throw new Exception("conflicting existing layout "+names[n]);manager.resolve(desired,DataTypeConflictHandler.DEFAULT_HANDLER);}
   else if(existing==null||!existing.isEquivalent(desired))throw new Exception("missing saved layout "+names[n]);
   out.append("LAYOUT ").append(names[n]).append(' ').append(desired.getLength()).append('\n');
   for(var c:desired.getDefinedComponents())out.append(c.getOffset()).append('|').append(c.getFieldName()).append('|').append(c.getDataType()).append('\n');
  }

  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);String c=f.getComment();if(c==null||!c.contains(row[2]))f.setComment((c==null?"":c+"\n")+row[2]);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   out.append("BODY ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
