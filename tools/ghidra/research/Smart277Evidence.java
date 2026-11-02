// Read-only Move target admission, native point normalization and task evidence.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Reference;
import java.nio.file.Files;
import java.nio.file.Path;
public class Smart277Evidence extends GhidraScript {
    public void run() throws Exception {
        StringBuilder out=new StringBuilder();
        for(String address:new String[]{"6f693490","6f67abe0","6f5ff240","6f5fd270","6f692120","6f6926b0"}) {
            var fn=getFunctionAt(toAddr(address));
            if(fn==null)throw new Exception("Missing function "+address);
            if(address.equals("6f5ff240")) {
                String note="Payoff277: genuine Shift RMB queued behind point Move repeats in six scenes (invisible, removed, dead, fogged, moved then removed, visible control). First five activate exactly one692120 d016b at saved packet48/50, caller5fd95d, preserving public Smart851971; visible allied control retains d0193/d0173/d0193/d0174. Interaction-owner selection in5ff240 remains before ordinary Move filtering; engine now passes the retained packet point through queued Smart and falls back after optional allied-unit validation. Active persistent Follow removal is distinct from a pending queued Smart. Existing Follow fixture was corrected only after two retail repeats and an unhooked control agreed on154 post-removal markers. Wider interaction-owner, enemy and non-unit widget policies remain separately scoped.";
                if(fn.getComment()==null || !fn.getComment().contains("Payoff277:"))
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
