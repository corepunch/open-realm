// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Captain369StrategicRegionsEvidence extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  if(!currentProgram.getName().equals("game.dll")||args.length!=2||(!args[0].equals("apply")&&!args[0].equals("read")))throw new Exception("game.dll, apply/read and fresh output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("binary differs");
  Path output=Path.of(args[1]);if(Files.exists(output))throw new Exception("output exists");
  String note="Payoff369 continuation GROUP-03.4.7.3 strategic random structure destination. Two read-only original captures match all65536 grid bytes and67 node records (16words each), plus331 publicmarkers; unhooked control pending. Input adaptive level1 has100x68 cells/scale4 fine units (128world), ground traversal word1>>30 (existing packed class mapping independently established MAP fixtures). Constructor thins ASCII walkable grid using four256-byte tables until stable, marks junctions, traces max4edges per node with costs169cardinal/239diagonal and marks structure-owned regions/neighborhoods. Captain-nearest node6 filters eligible/near lists[3,8]; actual secondRNG13 draw picksnode8 and publishes(-2304,256). Graph lives at EBP-34838 and endsEBP-4 (minimum size34834);34844 is stack allocation, not struct size. Native source CPACell inputs, stream state, full producer regression and engine port remain pending. No saved-state or final policy parity claim.";
  String[][] names={{"6f9b7080","TownRegionGraph_Construct"},{"6f9b79f0","TownRegionGraph_ThinWalkableGrid"},{"6f9b7f30","TownRegionGraph_ApplyThinningPattern"},{"6f9b9ce0","TownRegionGraph_NeighborBlockMask"},{"6f9bcab0","TownRegionGraph_MarkJunctions"},{"6f9bc960","TownRegionGraph_TraceJunctionEdges"},{"6f9b7dd0","TownRegionGraph_CollectJunctions"},{"6f9c08b0","TownRegionGraph_TraceFromJunction"},{"6f9c0d20","TownRegionGraph_WalkSkeleton"},{"6f9b7c90","TownRegionGraph_AddOrImproveEdge"},{"6f9b9a80","TownRegionGraph_ExpandMarkedNeighborhoods"},{"6f9b9a20","TownRegionGraph_ExpandMarkedDistance"},{"6f9b8f50","TownRegionGraph_FindNearestWithin640"},{"6f9bce00","TownRegionGraph_CollectEligibleDestinations"},{"6f9bd7b0","TownRegionGraph_CanReachThroughUnmarkedIntermediates"},{"6f9bdad0","TownRegionGraph_RollDestinationCenter"}};
  if(args[0].equals("apply"))for(String[] row:names) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],ghidra.program.model.symbol.SourceType.USER_DEFINED);
   if(!f.getName().equals(row[1]))throw new Exception("unexpected name "+f.getName());
   String old=f.getComment()==null?"":f.getComment();if(!old.contains(note))f.setComment(old+"\n"+note);
  }
  StringBuilder text=new StringBuilder(note+"\n");
  for(String va:new String[]{"6f9b7080","6f9b79f0","6f9b7f30","6f9b9ce0","6f9bcab0","6f9bc960","6f9b7dd0","6f9c08b0","6f9c0d20","6f9b7c90","6f9b9a80","6f9b9a20","6f9b8f50","6f9bce00","6f9bd7b0","6f9bdad0","6f9bcb80"}) {
   var f=getFunctionAt(toAddr(va));if(f==null)throw new Exception(va);
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(r).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
