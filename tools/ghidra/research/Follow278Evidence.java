// Read-only Move target admission, native point normalization and task evidence.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Reference;
import java.nio.file.Files;
import java.nio.file.Path;
public class Follow278Evidence extends GhidraScript {
    public void run() throws Exception {
        StringBuilder out=new StringBuilder();
        for(String address:new String[]{"6f5fd270","6f05a5c0","6f6926b0","6f68c190"}) {
            var fn=getFunctionAt(toAddr(address));
            if(fn==null)throw new Exception("Missing function "+address);
            if(address.equals("6f5fd270")) {
                String note="Payoff278: original owned JASS target Move/Smart repeats on mobile Footman, static Barracks/Keep, rooted and uprooted Tree of Life; all797 public markers match observer-free control. Barracks/Keep have no Move ability; rooted Ancient retains Move but sets08000000. All select exactly d0174 authored range0 at5fd81c and retire public order to0. Uprooted Ancient clears08000000 and uses d0173 FollowRange300 at5fd7b1, remaining active. Engine removes legacy footprint-margin Follow for current structures, captures one-shot versus Follow continuation in saved physical group, and uses05a5c0 radius addition for zero-range approach. This is not a claim that all nonstructure Move-ability-absence or special056a40 branches are implemented; those broader target producers remain open.";
                if(fn.getComment()==null || !fn.getComment().contains("Payoff278:"))
                    fn.setComment((fn.getComment()==null ? "" : fn.getComment()+"\n")+note);
            }
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
