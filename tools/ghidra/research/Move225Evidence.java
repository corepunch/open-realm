// Read-only Hero agility movement contribution evidence.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Reference;
import java.nio.file.Files;
import java.nio.file.Path;
public class Move225Evidence extends GhidraScript {
    public void run() throws Exception {
        StringBuilder out=new StringBuilder();
        for(String address:new String[]{"6f528b00","6f6852f0","6f203a90","6f52aad0","6f5fb740","6f5fcd80","6f5276d0","6f056a40"}) {
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
