// Point spell channel boundary and subsequent cast phases; complete instruction/xref export.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Path;
public class Cast281Evidence extends GhidraScript {
    public void run() throws Exception {
        if (!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()) ||
            currentProgram.getImageBase().getOffset()!=0x6f000000L)throw new Exception("Wrong original DLL");
        String[][] notes={
            {"6f40a7a0","Payoff281: Spell approach task chain includes ability-tagged d0160 and d015f around point d016b; physical arrival does not replace the public spell command."},
            {"6f3fbb80","Payoff281: Begin channel: user head40, animation setup, Unit_FireSpellChannelEvent, v384 then v380(1). Stop/Move/RemoveUnit from player CHANNEL cancel before any effect while unit CHANNEL still sees original point context. Delay84 schedules d01bb, otherwise proceeds3fb5d0."},
            {"6f3fb5d0","Payoff281: CAST phase, not EFFECT. Optional v380(2), animation48ba80, producer67cb30, delay8c schedules d01bc then40b380. Captured observer event named cast-effect hooks this CAST function; do not treat that historical label as EFFECT."},
            {"6f40b380","Payoff281: EFFECT phase: optional v380(3), producer67ce30 then3f7b00. Two observed success scenes have EFFECT10 owner counters after CHANNEL (.3 game seconds). Public effect callback precedes mana deduction. Engine Payoff281 ports CHANNEL cancellation only; full phase delay/resources remain open."},
            {"6f67ccb0","Payoff281: Player CHANNEL then unit CHANNEL; subscriber families frozen before dispatch. All8 fresh ground/flying point scenes preserve A281 point1536,288 and no target unit across callback replacement/removal. Two repeats and unhooked control match715 markers after handle-identity-only normalization."},
            {"6f67cb30","Payoff281: Public SPELL_CAST producer; unit event80322 and player80311. Distinct from67ce30 EFFECT."},
            {"6f67ce30","Payoff281: Public SPELL_EFFECT producer; unit event80323 and player80312. Called before3f7b00 in40b380. No claim of complete phase implementation in Payoff281."},
            {"6f3f7b00","Payoff281: After EFFECT producer: v394 gate precedes3fb7b0/4277e0/418750 and v3c0/69a070; full resource mutation attribution remains unproved. Original public markers directly establish unspent mana inside EFFECT, then cost13 on next tick."}
        };
        // Preserve existing mapped names and accumulate the independently verified boundary.
        for(String[] note:notes) {
            var fn=getFunctionAt(toAddr(note[0]));if(fn==null)throw new Exception("Missing function "+note[0]);
            String prior=fn.getComment();
            if(prior==null || prior.isEmpty())fn.setComment(note[1]);
            else if(!prior.contains(note[1]))fn.setComment(prior+"\n"+note[1]);
        }
        String[][] names={{"6f3fbb80","Spell_BeginChannelPhase"},{"6f3fb5d0","Spell_BeginCastPhase"},
            {"6f40b380","Spell_BeginEffectPhase"},{"6f67cb30","Unit_FireSpellCastEvent"},
            {"6f67ce30","Unit_FireSpellEffectEvent"}};
        for(String[] name:names) {
            var fn=getFunctionAt(toAddr(name[0]));
            if(fn.getName().startsWith("FUN_"))fn.setName(name[1],SourceType.USER_DEFINED);
        }
        StringBuilder out=new StringBuilder();
        for(String address:new String[]{"6f40a7a0","6f3fbb80","6f3fb5d0","6f40b380","6f67ccb0","6f67cb30","6f67ce30","6f3f7b00"}) {
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
