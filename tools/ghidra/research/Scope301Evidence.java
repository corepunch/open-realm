// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Scope301Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f69c510","Unit_CommitRemovalInactivity","Payoff301 MAP-04.2: removal inactivity/finalization is storage teardown after semantic removal tasks. Unchanged complete Stop284 spatial repeats contain no additional69a840 after RemoveUnit returns: all three total recoveries return600433, unlike death counted5fc18a. Engine retirement context distinguishes storage destruction from death availability; do not recreate movement during free."},
   {"6f5fc120","CAbilityMove_CancelOwner","Payoff301 MAP-04.2: resolve unit from bound owner or canonical fallback and return if unavailable BEFORE69a840. Death counted cancellation reaches this recovery while fine remains active; complete RemoveUnit scenes have no additional deferred-destruction recovery. Engine storage retirement preserves that already-cancelled ownership boundary."},
   {"6f210f90","JassNative_ReviveHero","Payoff301 MAP-04.2: Registered by20987b for ReviveHero. Resolve live canonical Hero identity, reject removal flag40000000; call695ab0 with public point and effect flag."},
   {"6f695ab0","Unit_ReviveHeroAtPoint","Payoff301 MAP-04.2: Requires dead100 Hero. Public revival reaches695620 after position/owner/life preparation. This path establishes the inverse producer statically; no new live revival capture is claimed."},
   {"6f695620","Unit_RestoreLifeState","Payoff301 MAP-04.2: Clear authoritative dead100, restore life, then48c8c0(1,0,0,0) BEFORE constructing Stop tasks. Engine releases only owned retirement contributions, preserving scripted Pause contributions, including saved corpse revival."},
   {"6f210c10","JassNative_RemoveUnit","Payoff301 MAP-04.2: Public removal calls widget virtual84 before694690(1,1). Fine unpublication precedes old-task cancellation; actual destruction is deferred."},
   {"6f694690","Unit_QueueRemovalTasks","Payoff301 MAP-04.2: Cancel old chain, acquire separation suppression, construct removal d0144 and action0. Both cleanup scopes retain the already-inactive fine object; actual owner destruction remains separate."},
   {"6f679bb0","Unit_BeginDeathTasks","Payoff301 MAP-04.2: Commit dead flag and life zero before old-task cancellation; death d0144 constructs a second cleanup, then finalization disables attached Move. All three nested recoveries retain active fine coverage."},
   {"6f6744d0","Unit_FinalizeDeathStateAndPathing","Payoff301 MAP-04.2: Counted48c8c0(1,1,0,0) precedes virtual14c(0,1),650c00,05c660(1), then public death callbacks. Authoritative dead state must not prematurely retire fine identity during these recoveries."},
   {"6f650c00","Widget_RetirePathing","Payoff301 MAP-04.2: 05c660(1) clears fine active bit and updates its rectangle; it does not physically retire the fine object. Region collection retirement is separate."},
   {"6f05c660","UnitMoverBridge_SetFineOccupancyDisabled","Payoff301 MAP-04.2: Mover98 fine category34 bit01000000 owns active coverage. Clear/set then15fb40 refreshes bounds. Removal deactivates early, death after counted cancellation; both retain identity/counter."},
   {"6f160590","Mover_UpdateFineOccupancyBounds","Payoff301 MAP-04.2: Inactive coverage dispatches immutable integer rectangle6fce4588, four signed -128000 coordinates, to14e770. Exact empty coverage encoding, not a world-position clamp. Save184 persists active/inactive separately."},
   {"6f15ee60","Mover_RetireSpatialObjects","Payoff301 MAP-04.2: Physical owner destruction retires proximity and fine objects and clears both pointers. This differs from05c660 logical fine unpublication."},
   {"6f14e770","SpatialObject_UpdateRectangle","Payoff301 MAP-04.2: Update old/new rectangle links lazily while retaining object identity, stamp and exclusion counter. Empty inactive coverage does not free the pooled object."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);String c=f.getComment();if(c==null||!c.contains(row[2]))f.setComment((c==null?"":c+"\n")+row[2]);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   out.append("BODY ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  var address=toAddr("6fce4588");
  var type=currentProgram.getDataTypeManager().getDataType("/WarcraftIII/Pathfinding127/WC3PathIntegerRectangle");
  if(type==null || type.getLength()!=16)throw new Exception("integer rectangle type missing");
  if(args[1].equals("apply")) {
   if(getDataAt(address)==null)createData(address,type);
   if(getSymbolAt(address)==null || !getSymbolAt(address).getName().equals("FineOccupancy_UnpublishedRectangle"))
    createLabel(address,"FineOccupancy_UnpublishedRectangle",true);
  }
  out.append("GLOBAL ").append(address).append(' ').append(getDataAt(address).getDataType().getPathName()).append('\n');
  out.append(address).append('|');for(byte v:getBytes(address,16))out.append(String.format("%02x",v&255));
  out.append("|inactive integer rectangle\n");
  Files.writeString(path,out);println("Saved "+path);
 }
}
