// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
public class EntanglePlacementEvidence extends GhidraScript {
    static final String HASH="d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236";
    static final String[][] ROWS={
        {"6f54fd60","Entangle_FindRetainedRootSite","TARGET01.2: captured target support level; source authored radius plus Aent range context; try current point, else55a440 projection then6515f0 filtered32-ring search. Suppress source mover and all regions through virtualD0. Recheck snapped point through544970 before returning approach flag. V2/V3/V4 repeated native admission witnesses."},
        {"6f55a440","Entangle_ProjectRootCandidate","TARGET01.2: requested target range first; if already in it, runtime minimum250 governs fallback. Projection adds source and target authored radii with software scalar rescaling. Authored333 differs from stock500; failed64/zero constructors create no physical task."},
        {"6f566eb0","Entangle_AdmitRootCandidate","TARGET01.2: point in captured mine range,66f050 build result0/45, second range check after snapping, captured target support level. Stores admitted XY at context1c/20. Native red-only placement margins prevent1152,1216 and retain1088,1216 in hostile-occupant witness."},
        {"6f437aa0","Ability_DeferInternalTaskQueue","TARGET01.2 Root placement45: set ability20 bit200, clear unit5c bit1, primary request d019d with runtime bc34=.5 (00b000 initializes50/100). Ten half-second deferrals in repeated V7 slow-friendly witness; request is not a new user order."},
        {"6f406bc0","Ability_ResumeInternalTaskQueue","TARGET01.2: d019d clears ability20 bit200, pops waiting internal task and dispatches retained successor. Root d018f retries placement after each half-second defer; terminal failure leaves occupant ordinary Move alive."},
        {"6f438ac0","Root_BeginMorphAfterFacing","Payoff211: d018f event point38/40; bridge05b440 predicted source/collision gate, placement validation, virtual180 position publication BEFORE morph virtual338. TARGET01.2 V7 proves placement45 retries up to10 at .5-second primary deadlines, then clears counter and fails; occupant Move survives. Position virtual180 receives admission0 after placement. Root211b turns at1536.012,1008.634 then relocates1536,1024 before completion; final visual heading239.015 is not RootAngle250."},

    };
    public void run() throws Exception {
        if(!HASH.equalsIgnoreCase(currentProgram.getExecutableSHA256()))throw new Exception("Wrong original binary");
        for(String[] row:ROWS) {
            Function f=getFunctionAt(toAddr(row[0]));
            if(f==null)throw new Exception("Missing verified function "+row[0]);
            if(f.getSymbol().getSource()!=SourceType.DEFAULT && !f.getName().startsWith("FUN_") && !f.getName().equals(row[1]))
                throw new Exception("Preserve existing annotation "+f.getName());
        }
        for(String[] row:ROWS) {
            Function f=getFunctionAt(toAddr(row[0]));f.setName(row[1],SourceType.USER_DEFINED);
            String evidence=row[2]+"\nRecovered descriptive name, not an original debug symbol. SHA256 "+HASH;
            String prior=f.getComment();
            if(prior==null || prior.isEmpty())f.setComment(evidence);
            else if(!prior.contains(row[2]))f.setComment(prior+"\n"+evidence);
        }
        println("Mapped six verified Entangle/Root functions; prototypes unchanged.");
    }
}
