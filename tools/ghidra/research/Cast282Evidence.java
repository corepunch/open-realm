// Point spell channel boundary and subsequent cast phases; complete instruction/xref export.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Path;
public class Cast282Evidence extends GhidraScript {
    public void run() throws Exception {
        if (!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()) ||
            currentProgram.getImageBase().getOffset()!=0x6f000000L)throw new Exception("Wrong original DLL");
        String[][] notes={
            {"6f40b380","Payoff282: Twelve fresh near/far point and unit phase scenes, two read-only repeats plus unhooked control,1093 identity-normalized public markers. EFFECT67ce30 completes before3f7b00. Stop inside player EFFECT emits nested ENDCAST then unit EFFECT still runs with original packet; no mana spend or effect body. Player EFFECT mana0 is visible to unit EFFECT and does not reject the effect; final mana stays0."},
            {"6f67ce30","Payoff282: Freeze unit80323 and player80312 presence before player delivery; retain one event packet. Player EFFECT Stop returns with head0 before unit EFFECT, whose ability/point/target/caster identity remains unchanged. Indexed synchronous dispatch replaces engine queued post-spend effect events."},
            {"6f3f7b00","Payoff282: EFFECT producer precedes this body. v394=3fa400 guards ownership, not a second mana affordability test. Scope: engine ports synchronous precommit EFFECT and callback cancellation; complete Cast/Finish/Endcast phase timing remains open."},
            {"6f3fa400","Payoff282: Observed point/unit v394. Return false if caster5c has100; otherwise allow instant v34c&2000000 or live identity at caster19c/1a0. This gate does not recheck mana. Authored player EFFECT sets mana0: unit EFFECT sees0, finish/endcast still occur."},
            {"6f402bd0","Payoff282: Base validation v370(0), then reject ability20 bit200. Distinct from3fa400 post-EFFECT owner gate."},
            {"6f4363f0","Payoff282: Observed v384 no-op in both Flame Strike and Inner Fire; do not infer other subclasses are no-ops."},
            {"6f436730","Payoff282: Observed v398 sets current user head flag8 through caster19c/1a0 identity. Does not spend mana."},
            {"6f436460","Payoff282: Observed v3a4 sets ability20 bit200 and schedules d01be via0608d0 on ability+b0. The input scalar is copied before scheduling. Do not label this mana spend."},
            {"6f40a2b0","Payoff282: Observed v3c0 dispatches retained widget target or point to concrete v41c/v420/v424/v418/v428 effect owner. Resource/event attribution must follow concrete calls."},
            {"6f40d570","Payoff282: Inner Fire v39c clears200000 and optional40000 then40e030. Scene3 authored Cast.2 delays CHANNEL-to-CAST7 counters, CAST-to-EFFECT10, EFFECT-to-FINISH80; no generic Cast field timing assumption."},
            {"6f64ac10","Payoff282: Inner Fire v3a4 wraps436460 then64cee0; concrete completion differs from Flame Strike."},
            {"6f5d9ec0","Payoff282: Flame Strike v39c conditionally creates concrete area effect then40e030. Authored AHfs Cast.2 does not change initial delay84=0; its finish is7 counters after EFFECT, unlike Ainf Cast.2 whose initial delay84 is.2. Preserve class-specific phase policies."},
            {"6f40e030","Payoff282: Concrete phase completion retains target identities, may rebuild approach tasks d0174/d016b and d015f/d0160, then40e3a0. This is not evidence that all abilities can share one fixed delay policy."}
        };
        // Preserve existing mapped names and accumulate the independently verified boundary.
        for(String[] note:notes) {
            var fn=getFunctionAt(toAddr(note[0]));if(fn==null)throw new Exception("Missing function "+note[0]);
            String prior=fn.getComment();
            if(prior==null || prior.isEmpty())fn.setComment(note[1]);
            else if(!prior.contains(note[1]))fn.setComment(prior+"\n"+note[1]);
        }
        String[][] names={{"6f3f7b00","Spell_ExecuteEffectBody"},{"6f3fa400","Spell_EffectOwnerIsLive"},
            {"6f436460","Spell_SchedulePostEffectTimer"},{"6f40a2b0","Spell_DispatchConcreteEffect"}};
        for(String[] name:names) {
            var fn=getFunctionAt(toAddr(name[0]));
            if(fn.getName().startsWith("FUN_"))fn.setName(name[1],SourceType.USER_DEFINED);
        }
        StringBuilder out=new StringBuilder();
        for(String address:new String[]{"6f40b380","6f67ce30","6f3f7b00","6f3fa400","6f402bd0","6f4363f0","6f436730","6f436460","6f40a2b0","6f40d570","6f64ac10","6f5d9ec0","6f40e030"}) {
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
