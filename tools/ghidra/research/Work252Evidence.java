// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Path;
public class Work252Evidence extends GhidraScript {
 static final String[][] NOTES={
  {"6f008260","FogTimer_InitializePeriod","Payoff252: original startup constructs software integers4 and10 then divides into d69474. Live original period word3eccccce; do not replace with host literal0.4. This is the authoritative fog event period, independent of the30ms Move owner."},
  {"6f28ba80","UI_SetFogUpdateEnabled","Payoff252: thiscall UI owner, stack4 enabled,RET4. Arm embedded primary timer280 through0608d0 with period d69474,event80269,self,1,0; disable cancels0606c0. Fresh map1e5d77 arms; world teardown1dfb11 cancels."},
  {"6f289260","UI_FireFogUpdate","Payoff252: ECX UI owner, plainRET via tail JMP251ac0. Unless embedded control290 bit2 is set, rearm28ba80(1) BEFORE composing world34 vision. Repeated read-only five-scene observations confirm0.4s event ownership and first short-fog publication at owner counter1304; first visible Follow visit1305."},
  {"6f251ac0","Vision_RebuildAuthoritativePlanes","Payoff252: ECX fog map, plainRET. Clear current-visible30 when global flags permit, rebuild player masks, apply enabled40 pre-unit modifiers, ordered unit sight, then enabled40 after-unit100 modifiers; publish renderer planes afterward. Visibility queries read the last completed plane between event80269 visits. This is not a per-display-frame callback."},
  {"6f1fe600","Jass_FogModifierStart","Payoff252: resolved modifier flags20 OR40. The periodic composer251ac0 tests40 and100 to retain pre/after-unit ordering. Existing engine synchronous first application remains separately covered; no inference here that this simple flag setter alone composes visibility."},
  {"6f1fe620","Jass_FogModifierStop","Payoff252: resolved modifier flags20 ANDffffffbf. No plane write or fog-event cancellation in this body. Removing the contribution becomes visible at the next authoritative fog composition, not the next display frame."},
  {"6f1fd040","Jass_DestroyFogModifier","Payoff252: resolve agent then dispatch virtual5c retirement. Live destroy at owner1300 does not recompose immediately; fog event at1304 publishes reacquisition before next Follow visit1305. No invented per-target delay."},
  {"6f250d00","Vision_SaveCompletedPlanes","Payoff252: serializes fog/mask enable flags, dimensions, stride/shift and all four ushort planes before modifier-related objects and viewer flags. The current visible plane is saved simulation state; re-rasterizing sources during load is not equivalent between fog events."},
  {"6f250120","Vision_LoadCompletedPlanes","Payoff252: reads dimensions/stride, allocates aligned four-plane storage and restores saved ushort contents. Engine Save164 now retains completed current visibility and exploration, next fog deadline and registration serial; derived row/geometry caches are rebuilt without a visibility update."},
  {"6f24f570","Vision_AccumulateChecksum","Payoff252: CRC/checksum helper reached by world1de210, not a rolling fog updater. Row grouping by16 belongs to checksum traversal; do not infer incremental visibility scheduling from this loop."}
 };
 public void run()throws Exception {
  if(getScriptArgs().length!=1)throw new Exception("new output path required");
  var path=Path.of(getScriptArgs()[0]);if(Files.exists(path))throw new Exception("exists");
  var dm=currentProgram.getDataTypeManager();
  var type=new StructureDataType(new CategoryPath("/WC3/Pathfinding"),"WC3FogOfWarMapPrefix",0x78);
  type.replaceAtOffset(0,PointerDataType.dataType,4,"vtable",null);
  type.replaceAtOffset(0x10,UnsignedIntegerDataType.dataType,4,"mask_enabled",null);
  type.replaceAtOffset(0x14,UnsignedIntegerDataType.dataType,4,"fog_enabled",null);
  type.replaceAtOffset(0x24,UnsignedIntegerDataType.dataType,4,"flags",null);
  for(int i=0;i<4;i++)type.replaceAtOffset(0x2c+i*4,new PointerDataType(UnsignedShortDataType.dataType,4),4,
   new String[]{"explored","visible","render_mask","support_height"}[i],"Partial plane contract; authoritative updates through251ac0.");
  for(int i=0;i<4;i++)type.replaceAtOffset(0x60+i*4,UnsignedIntegerDataType.dataType,4,
   new String[]{"width","stride","stride_shift","height"}[i],null);
  dm.addDataType(type,DataTypeConflictHandler.DEFAULT_HANDLER);
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
