// Read-only retained user head, current task and target-loss reissue evidence.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Reference;
import java.nio.file.Files;
import java.nio.file.Path;
public class Target216Evidence extends GhidraScript {
    public void run() throws Exception {
        StringBuilder out=new StringBuilder();
        for(String address:new String[]{"6f5ff490","6f69c6a0","6f69bd80","6f689a60",
                "6f689820","6f691c70","6f5fd270","6f061320"}) {
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
