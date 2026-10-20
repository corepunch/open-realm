// Export unchanged original strategic-region masks. Args: fresh output path.
import ghidra.app.script.GhidraScript;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
public class Captain369ExportRegionTables extends GhidraScript {
    public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length!=1)throw new IllegalArgumentException("Provide fresh output path");
        Path output=Path.of(args[0]);
        if(Files.exists(output))throw new IllegalArgumentException("Fresh output required");
        StringBuilder text=new StringBuilder();
        long[] addresses={0x6fcfcca8L,0x6fcfcba0L,0x6fcfcdb0L,0x6fcfca98L,0x6fcfceb8L};
        for(int index=0;index<addresses.length;index++) {
            int length=index==4?1024:256;
            byte[] bytes=new byte[length];
            currentProgram.getMemory().getBytes(toAddr(addresses[index]),bytes);
            text.append(Long.toHexString(addresses[index])).append('|');
            for(byte value:bytes)text.append(String.format("%02x",value&255));
            text.append('\n');
        }
        Files.writeString(output,text.toString(),StandardCharsets.UTF_8);
        println("Exported original masks to "+output);
    }
}
