// Structure-target spell producer and Move admission, complete instruction/xref export.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Path;
public class Structure279Evidence extends GhidraScript {
    public void run() throws Exception {
        if (!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()) ||
            currentProgram.getImageBase().getOffset()!=0x6f000000L)throw new Exception("Wrong original DLL");
        String[][] notes={
            {"6f40a7a0","Payoff279: ordinary unit-target branch includes static Human buildings and rooted Ancient targets; there is no structure rejection. Two read-only repeats and unhooked control match633 public markers across House, Barracks, rooted Ancient, Stop, RemoveUnit and ordinary-unit control. Custom Ainf range333 feeds one nonpersistent d0174 without replacing public innerfire852066. Full cast/effect/backswing timing remains excluded."},
            {"6f5ff8b0","Payoff279: structure spell approach d0174 follows the same captured-world-range dispatch as ordinary unit targets; all six original scenes enter this target owner. Do not route structure spells through a presentation-frame range poll."},
            {"6f5fc640","Payoff279: six live spell targets, including static Human buildings and rooted Ancient, reach this request owner with authored range333 and persistent0. Structure classification is not an exclusion gate."},
            {"6f05a5c0","Payoff279: structure spell arrival captures333 plus caster32 plus target72/144 in world before divide32, giving13.65625/15.90625 fine. Two repeats match471 physical visits; House exact approach requires authored4x4SimpleSolid footprint. Do not substitute footprint margins for physical radii. Stop/removal cancel the approach. Complete cast phase timing is separate."},
            {"6f5fc640","Payoff279 region identity: query virtual158 chooses mask2, except (query&0x12)==0x10 chooses0x10. With target resource34 present,063d60 selects the first matching widget region in collection order and passes it as05a5c0 final argument20. This is task-owned captured identity, not a permanent building mover98 alias."},
            {"6f063d60","Payoff279: iterate widget region collection in stored order; return first region whose category34 intersects supplied mask. Target task5fc640 selects ground mask2 or water mask10 before capture. Farm final-leg route uses blue category010000c2, distinct from its ordinary mover98 object."},
            {"6f16daf0","Payoff279: store supplied region pointer at canonical request f4. BeginTargetRequest05a5c0 invokes this setter for nonnull final argument20; geometry and exclusion must resolve that captured identity."},
            {"6f16aff0","Payoff279 live spell integration: prefer captured f4 while stamp38!=-1; retirement clears it permanently before fallback to target mover98. Never replace it implicitly with a new widget region. This reference supplies both coarse and fine target exclusion."},
            {"6f16ce10","Payoff279: supplied target-region argument is retained at path a4 for coarse routing. Farm approach must use the captured blue widget rectangle, rather than the separate collision rectangle."},
            {"6f166e90","Payoff279: fine search holds actual path a4 target object; original Farm refill1139 uses category010000c2 flags10000001 and stored YX rectangle6,46,11,51. Cells46,7..11 are allowed with targetSeen1 and obstruction0. Original partial25-point route starts index0, matching all103 physical visits and saved continuation. No building-specific alias or perimeter exception."}
        };
        // Preserve existing mapped names and accumulate the independently verified boundary.
        for(String[] note:notes) {
            var fn=getFunctionAt(toAddr(note[0]));if(fn==null)throw new Exception("Missing function "+note[0]);
            String prior=fn.getComment();
            if(prior==null || prior.isEmpty())fn.setComment(note[1]);
            else if(!prior.contains(note[1]))fn.setComment(prior+"\n"+note[1]);
        }
        for(String[] name:new String[][]{{"6f063d60","RegionCollection_FindFirstMatchingCategory"},{"6f16daf0","MoveRequest_SetCachedTargetRegion"}}) {
            var fn=getFunctionAt(toAddr(name[0]));
            if(fn.getName().startsWith("FUN_"))fn.setName(name[1],SourceType.USER_DEFINED);
        }
        StringBuilder out=new StringBuilder();
        for(String address:new String[]{"6f40a7a0","6f5ff8b0","6f5fc640","6f05a5c0","6f063d60","6f16daf0","6f16aff0","6f16ce10","6f166e90"}) {
            var fn=getFunctionAt(toAddr(address));
            if(fn==null)throw new Exception("Missing function "+address);
            out.append("FUNCTION ").append(address).append(' ').append(fn.getName()).append('\n');
            var ins=currentProgram.getListing().getInstructions(fn.getBody(),true);
            while(ins.hasNext()) {
                var i=ins.next();out.append(i.getAddress()).append('|');
                for(byte b:i.getBytes())out.append(String.format("%02x",b&255));
                out.append('|').append(i).append('\n');
            }
            for(Reference r:getReferencesTo(fn.getEntryPoint()))out.append("XREF ").append(r).append('\n');
        }
        String[] args=getScriptArgs();
        if(args.length!=1)throw new Exception("Expected new output path");
        Path path=Path.of(args[0]);if(Files.exists(path))throw new Exception("Output exists");
        Files.writeString(path,out.toString());println("Saved "+path);
    }
}
