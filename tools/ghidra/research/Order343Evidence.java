import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;

/** Public decay cancellation witness; ORDER-01.2 remains open. */
public class Order343Evidence extends GhidraScript {
    public void run() throws Exception {
        if (!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))
            throw new IllegalStateException("Wrong retail executable");
        String[][] rows = {
            {"6f219060", "Jass_UnitSuspendDecay", "ORDER343: accepts dead unit5c100 with flesh/bone c00. Cancels active/internal tasks. Resume prepends d015a removal then d0159 bone and dispatches, restarting FULL bone duration even from flesh and without preceding suspension. Nine public Footman controls: ignored living/death-stage calls, repeat suspend/resume, removal while suspended. Two842-record native repeats and unhooked187-marker control agree. Engine native previously empty; five failing assertions fixed by unit-owned persisted suspension and fresh bone phase. Save195. Complete ORDER-01.2 arrival/cant-path gate inventory remains open."},
            {"6f69c000", "Unit_BeginFleshDecayTask", "ORDER343: d0158 selects decay animation and sets unit5c400. Authored DecayTime and nonzero selected clip duration schedule Unit180 d019d and own unit28040 scaling. Public hfoo enters tick31, natural bone51 with two-second flesh; removal40 and suspension40 exercise scale inverse."},
            {"6f69c110", "Unit_BeginBoneDecayTask", "ORDER343: d0159 clears unit5c400, sets800; BoneDecayTime or StructureDecayTime. Nonzero clip duration owns unit28040. Public resume80/100 and unsuspended false40/60 synchronously restart full authored four-second bone. No general animation-frame or structure runtime parity claim."}
        };
        for (String[] row : rows) {
            var f = getFunctionAt(toAddr(row[0]));
            f.setName(row[1], SourceType.USER_DEFINED);
            f.setComment(row[2]);
            println(row[0]+" "+f.getName()+" "+f.getComment());
        }
    }
}
