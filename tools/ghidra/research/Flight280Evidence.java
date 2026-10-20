// Flying spell producers and physical Move ownership; complete instruction/xref export.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Path;
public class Flight280Evidence extends GhidraScript {
    public void run() throws Exception {
        if (!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()) ||
            currentProgram.getImageBase().getOffset()!=0x6f000000L)throw new Exception("Wrong original DLL");
        String[][] notes={
            {"6f40a7a0","Payoff280 flying caster: no flight exclusion in unit d0174 or point d016b selection. Fresh edtm Ainf/AHfs approaches, Stop, target removal and Farm target retain public innerfire/flamestrike while using physical Move owners. Two complete read-only repeats and unhooked control match634 public markers;357 physical input states repeat word-for-word. Full cast/effect/channel/backswing timing remains separate."},
            {"6f5ff8b0","Payoff280: flying unit-target spells reach the same nonpersistent target task as ground casters. Physical receiver owns completion/cancellation, not a presentation-frame range poll."},
            {"6f5ffb60","Payoff280: flying point-target spells submit captured authored333 through the ordinary point-task producer. Prior user Move is replaced without exposing its head; point approach captures333/32, excluding both collision radii."},
            {"6f05a5c0","Payoff280: edtm radius8 to Footman31 captures(333+8+31)/32=11.625, to Farm72 captures12.90625. Unit target approach remains adaptive for a freshly created flying caster; original fine search executes25-point first route, not an adaptive-disabled shortcut."},
            {"6f05b970","Payoff280: flying point spell retains10.40625 fine arrival range and physical completion. Two point spell scenes produce90 and33 owned visits, distinct from their immediately superseded range-zero user Move. No game-memory writes in the observer."},
            {"6f68a060","Payoff280: freshly created edtm retains enabled adaptive routing despite flight. Read-only original scene0 executes group coarse4 points and member fine25/3 points. Do not apply the separate670950 type-change disable policy during initial construction."}
        };
        // Preserve existing mapped names and accumulate the independently verified boundary.
        for(String[] note:notes) {
            var fn=getFunctionAt(toAddr(note[0]));if(fn==null)throw new Exception("Missing function "+note[0]);
            String prior=fn.getComment();
            if(prior==null || prior.isEmpty())fn.setComment(note[1]);
            else if(!prior.contains(note[1]))fn.setComment(prior+"\n"+note[1]);
        }
        StringBuilder out=new StringBuilder();
        for(String address:new String[]{"6f40a7a0","6f5ff8b0","6f5ffb60","6f05a5c0","6f05b970","6f68a060"}) {
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
