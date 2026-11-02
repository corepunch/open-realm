// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Place309Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f1fc930","Jass_CreateUnit","Payoff309: repeated public CreateUnit at six outside/boundary requests for12 authored profiles (four radii8/24/40/56, foot/fly/amph) reaches ordinary shared widget placement before return. All72 constructor results and immediate SetUnitPosition/queryzero writes are frozen alongside an unhooked public control. Constructor clamps before the32-ring placement search; do not search from raw out-of-map coordinates."},
   {"6f64e650","Unit_CreatePublic","Payoff309: public constructor profiles retain sequential identity/placement semantics. Each outside creation reaches653510 and6515f0 with world clamp before admission and fresh-mover sentinel scalar writes.72 constructors and144 subsequent public placements are recorded with exact fine/radius/result words. This witness excludes building textures and bridges."},
   {"6f04c330","Widget_ClampWorldPoint","Payoff309: both constructor and public placement clamp to0..2016 in flat0..2048 map before searching. Ground and amphibious radius24/40 west-edge results48,1008; radius56 yields80,976. Flyer geometry differs and must retain its query/mask producer. Queryzero retains bounds and support matching."},
   {"6f653510","Widget_WritePositionAfterAdmission","Payoff309:216 calls across72 profiles: creation plus ordinary and queryzero public repositioning. Each shared wrapper clamps, admits, commits; constructor additionally performs its canonical scalar write before public return. Frozen native delta receiver distinguishes signed zero in final fine state from rounded public strings."},
   {"6f6515f0","Widget_AdmitPosition","Payoff309:216 full original admissions return1 across72 constructor/placement/queryzero scenes. Inner clamp occurs even though shared653510 already clamped. This helper owns radius/mask and support policy; constructor must enter with the same preclamp. Four class shapes and foot/fly/amph profiles covered on flat land; no water/bridge/rejected-support closure."},
   {"6f05c200","UnitMover_WriteWorldPosition","Payoff309: read-only15f7b0 delta receiver records288 before/after mover states across72 scenes. Each creation has a fresh sentinel commit then canonical write; zero coordinates can become negative-zero fine words after the second write. Preserve exact fine state rather than deriving it from public four-decimal strings."},
   {"6f058cd0","UnitMover_AdmitWorldPoint","Payoff309:72 public creation and144 placement writes retain scan2/rings32/filterFFFFFFFF/budget0/lane0. Ground query02000002, flight04000004, amphibious80000080; SetUnitPathing(false)query0. Shared world wrapper derives integer-result from arg14==0; observed flyers forward1, ground/amph0. This affects only relocated ring candidates, not an already-legal scalar request."},
   {"6f16ecc0","PathOwner_FindPlacement","Payoff309:216 complete live calls capture footprint and mask separately, plus explicit integer-result1 for flying units versus0 ground/amph. Engine must carry that producer policy into indexed placement rather than always appending half-cell offsets. Stop recovery retains its independent integer-result0 contract; do not infer this policy from query-maskzero."},
   {"6f171340","Mover_StopAndInvalidatePath","Payoff309: even a never-moving newly constructed unit executes IntegrateThenChangeVelocity before Stop recovery. With fine negative-zero and zero velocity this integration produces positive-zero; the following public05c200 write can publish negative-zero again. Engine cannot skip physical stop integration solely because no valid prior movement clock exists.72 public profiles with216 exact fine states and cold saves witness this boundary."}
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
  Files.writeString(path,out);println("Saved "+path);
 }
}
