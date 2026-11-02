// Read-only BASE-01.1 caller ABI and field-copy evidence. Run after MapPathfinding.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Reference;
import java.nio.file.Files;
import java.nio.file.Path;
public class Point214Evidence extends GhidraScript {
    public void run() throws Exception {
        StringBuilder out=new StringBuilder();
        for(String address:new String[]{"6f3cbd70","6f6bd5f0","6f6b7930","6f331b10","6f331c30",
                "6f331b80","6f331850","6f32d650","6f6bdc20","6f6bdd00","6f6b93a0","6f680320"}) {
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
        if(args.length!=1)throw new Exception("Expected output path");
        Files.writeString(Path.of(args[0]),out.toString());
        println("Saved instruction/xref evidence: "+args[0]);
    }
}
