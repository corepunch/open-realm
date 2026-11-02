// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
import ghidra.program.model.data.*;
public class Work244Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f21e790","PathFootprint_CreateFromImage","Payoff244: thiscall ECX new6c-byte footprint, stack4 image filename,8 owner kind; RET8. Calls70dd90 with width destination footprint+c and height destination+8. Transposes normalized BGRA: output[y*footprintWidth+x] reads source[x*footprintHeight+y]. File red bit0 contributes d2; blue bit0 contributes08; any green contributes04 and flag64 bit0. Repeated read-only LT06 captures: original32x18 CityBridgeLarge0.tga produces18x32 categories, orientation1. Engine now uses this decoded coordinate system and exact categories; full mixed Alt+Shift trajectories and save continuations retain frozen retail expectations."},
   {"6f70d9b0","Image_LoadNormalizedBGRA","Payoff244: fastcall ECX filename,EDX imageWidth*,stack4 imageHeight*,8 image vector; RET8. TGA origin bit5 chooses row order: clear reverses file rows, set preserves rows. Copies BGRA channels without swapping.21e790 separately swaps dimension destinations and transposes this normalized image. Do not conflate renderer image coordinates with path footprint coordinates."},
   {"6f70dd90","Image_LoadTgaOrBlp","Payoff244: fastcall ECX filename,EDX imageWidth*,stack4 imageHeight*,8 image vector; RET8. Removes extension and tries image suffixes through70d9b0; footprint21e790 deliberately supplies reversed dimension destinations."},
   {"6f252b30","PathTexture_FacingQuarterTurn","Payoff244: facing selects raster turn independently of texture aspect ratio. Original tests near0,nearHalfPi,nearPi then returns3. Live LT0618x32 footprint and fixedRot90 use orientation1. Engine removes its extra nonsquare turn because the authored image is now decoded in retail footprint coordinates. Arbitrary-angle caller selection remains outside this cardinal fixture."},
   {"6f22e9c0","PathTexture_RasterizeRotated","Payoff244: repeated read-only pre-main LT06 raster entries expose18x32 decoded categories and orientation1. Same asymmetric bridge and all four final hierarchy levels/lanes underpin850 complete mixed-class Alt+Shift motion commits per run. Asset BGRA normalization/transposition is owned by70d9b0/21e790 before this ordered row-major raster."}
  };
  for(String[] row:rows){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);if(!f.getName().equals(row[1])){if(!f.getName().startsWith("FUN_"))throw new Exception("Preserve "+row[0]);f.setName(row[1],ghidra.program.model.symbol.SourceType.USER_DEFINED);}var prior=f.getComment();if(prior==null)prior="";if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);}
  var dtm=currentProgram.getDataTypeManager();
  var category=new CategoryPath("/WarcraftIII/Pathfinding127");
  var texture=(Structure)dtm.getDataType(category,"WC3PathTexturePrefix");
  if(texture==null)throw new Exception("MapPathfindingTypes prefix required");
  if(texture.getLength()<108)texture.growStructure(108-texture.getLength());
  var scalar=dtm.getDataType(category,"WC3PathScalar");
  if(scalar==null)throw new Exception("scalar required");
  texture.replaceAtOffset(0,new PointerDataType(VoidDataType.dataType,4),4,"vtable","21e790/22e9c0;Payoff244");
  texture.replaceAtOffset(4,UnsignedIntegerDataType.dataType,4,"references","21e790/22e9c0;Payoff244");
  texture.replaceAtOffset(16,scalar,4,"half_width","21e790/22e9c0;Payoff244");
  texture.replaceAtOffset(20,scalar,4,"half_height","21e790/22e9c0;Payoff244");
  texture.replaceAtOffset(24,UnsignedIntegerDataType.dataType,4,"category_capacity","21e790/22e9c0;Payoff244");
  texture.replaceAtOffset(28,UnsignedIntegerDataType.dataType,4,"category_count","21e790/22e9c0;Payoff244");
  texture.replaceAtOffset(32,new PointerDataType(UnsignedCharDataType.dataType,4),4,"categories","21e790/22e9c0;Payoff244");
  texture.replaceAtOffset(100,UnsignedIntegerDataType.dataType,4,"flags","21e790/22e9c0;Payoff244");
  texture.replaceAtOffset(104,UnsignedIntegerDataType.dataType,4,"constructor_argument","21e790/22e9c0;Payoff244");
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f21e790","6f70d9b0","6f70dd90","6f252b30","6f22e9c0"}) {
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);
   out.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
