// Point spell channel boundary and subsequent cast phases; complete instruction/xref export.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.*;
import java.nio.file.Files;
import java.nio.file.Path;
public class Cast283Evidence extends GhidraScript {
    public void run() throws Exception {
        if (!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()) ||
            currentProgram.getImageBase().getOffset()!=0x6f000000L)throw new Exception("Wrong original DLL");
        String[][] notes={
            {"6f67ce30","Payoff283 correction: frozen subscriber presence retains the event ability/source identity, not a target snapshot. Original202d00/202e30 query current ability fields; repeated player EFFECT interruption returns to unit EFFECT with null target and zero X/Y. The earlier packet wording must not imply unchanged target responses."},
            {"6f425aa0","Payoff283: Ordered spell event dispatcher: d015f activates the owner via438530; d0160 calls437940. d01bb calls3fb5d0(1),d01bc calls40b380(1),d01bd invokes v39c(1);d01be invokes v3a8. Live20 arms and190 dispatcher begin/end rows in each fresh repeat; public control unchanged."},
            {"6f438530","Payoff283: d015f checks the retained task+34 against v348/v368 then3f5930 sets the active ability bit. Separate from d0160 phase start."},
            {"6f437940","Payoff283: d0160 checks task+34 against v348/v368, clears instant flag100, then3fbb80. Sequential public spell ownership persists through FINISH/ENDCAST."},
            {"6f3fbb80","Payoff283: Channel event precedes phase1 validation. Fresh Cast283d Stop/Move/Remove captures clear the target before unit CHANNEL, then emit FINISH immediately after failed phase1 validation (also RemoveUnit). Positive ability84 delay schedules d01bb; zero runs Cast immediately. Read-only Ainf Cast.2 arm3e4ccccd gives CHANNEL1690/CAST1697. AHfs84 stays0 despite Cast.2."},
            {"6f3fb5d0","Payoff283: Cast event returns without revalidation. Positive ability8c delay schedules d01bc even when player CAST issued Stop. Both captured CAST Stop scenes still FINISH10 counters later, without EFFECT or resources. Cancellation during waiting windup removes the pending request entirely."},
            {"6f40b380","Payoff283: EFFECT then concrete body. If still active, ability94 schedules d01bd; Flame Strike concrete effect replaces94 by Cast(.2), Inner Fire retains authored unit backswing2.4. The d01be delay7 is cooldown expiry, not finish timing."},
            {"6f40e030","Payoff283: Normal completion clears retained target identity and scalar X/Y before40e3a0 emits FINISH. Every normal FINISH/ENDCAST public context has target0,X0,Y0; cancellation ENDCAST still has the old target."},
            {"6f40e3a0","Payoff283: Always67d130 FINISH first, even inactive owner after nested CAST Stop. Only active bit1 invokes v388 (ENDCAST), clears ownership405510, then pops/dispatches internal tasks if flag4. Never reject FINISH solely because Stop made the owner inactive."},
            {"6f40c3f0","Payoff283: v388 emits67cfb0 ENDCAST before optional identity release. In player CHANNEL/CAST/EFFECT interruption, ENDCAST sees the old target; subsequent unit CHANNEL/CAST/EFFECT reads cleared target0/X0/Y0. Correction to Payoff282 commentary: subscriber presence/ability packet is frozen; target response fields are live ability state, NOT a frozen target snapshot."},
            {"6f5d9a90","Payoff283: Concrete v388 stops channel work and cancels124/138 controls, then40c3f0. Observed direct and common cleanup entries are independently retained; do not collapse nested base calls."},
            {"6f40d570","Payoff283: v39c clears200000 and optional40000 then40e030. Positive authored Cast is before CAST; unit backswing is after EFFECT. Alias policy belongs to the class, not central rawcode exceptions."},
            {"6f5d9ec0","Payoff283: v39c success creates area effects then40e030; failure skips area creation but still FINISH. Cast.2 defines the post-EFFECT delay, not CHANNEL delay. Engine ordered execution must not apply that delay twice."},
            {"6f436460","Payoff283: v3a4 sets200, cancels/replaces control and schedules d01be with observed7-second cooldown. Earlier post-effect naming was incomplete: d01be is not the completion/backswing event."},
            {"6f40c500","Payoff283: v3a8 cancels control+d0, clears200, refreshes state401fa0. d01be from425aa0 selects this cooldown expiry; it must remain separate from d01bd finish."},
            {"6f67d130","Payoff283: FINISH producer unit80324/player80313, frozen presence with player delivery first. Retains the ability identity; normal target fields have already been cleared by40e030."},
            {"6f67cfb0","Payoff283: ENDCAST producer unit80325/player80314; player first, unit presence frozen. Stop/Move/Remove during player CHANNEL/CAST/EFFECT synchronously nests both ENDCAST families before returning to the unit CAST/EFFECT family."},
            {"6f405510","Payoff283: Generic ability ownership helper: clear bit1 and693d20 notify unit. Used beyond spells; do not assign spell-only semantics."},
            {"6f3f5930","Payoff283: Generic inverse ownership helper: set bit1 only if absent and693d20 notify unit. Used beyond spells."},
            {"6f202e30","Payoff283: Original getter resolves current spell ability2027b0, validates live registry identity, then reads v3cc NOW. A frozen event carries ability identity, not a frozen target coordinate. Stop clears fields before unit CHANNEL/CAST/EFFECT; original public target X becomes0."},
            {"6f202d00","Payoff283: Original getter resolves current spell ability2027b0 then v3c8 and widget class validation. Target identity is read at native query, not copied forever into event packet."},
            {"6f202c10","Payoff283: Original getter resolves current spell ability then v3d0/v3cc and creates a location. Normal completion clears ability target before FINISH/ENDCAST."}
        };
        // Preserve existing mapped names and accumulate the independently verified boundary.
        for(String[] note:notes) {
            var fn=getFunctionAt(toAddr(note[0]));if(fn==null)throw new Exception("Missing function "+note[0]);
            String prior=fn.getComment();
            if(prior==null || prior.isEmpty())fn.setComment(note[1]);
            else if(!prior.contains(note[1]))fn.setComment(prior+"\n"+note[1]);
        }
        String[][] names={{"6f425aa0","Spell_DispatchPhaseEvent"},{"6f438530","Spell_ActivateTaskOwner"},{"6f437940","Spell_StartActivatedTask"},{"6f3fbb80","Spell_BeginChannelPhase"},{"6f3fb5d0","Spell_BeginCastPhase"},{"6f40b380","Spell_BeginEffectPhase"},{"6f40e030","Spell_CompleteTargetPhase"},{"6f40e3a0","Spell_FinishActiveTask"},{"6f40c3f0","Spell_EndCastCleanup"},{"6f5d9a90","FlameStrike_EndCastCleanup"},{"6f40d570","InnerFire_CompleteTargetPhase"},{"6f5d9ec0","FlameStrike_CompleteTargetPhase"},{"6f436460","Spell_ScheduleCooldownRequest"},{"6f40c500","Spell_ClearCooldownRequest"},{"6f67d130","Unit_FireSpellFinishEvent"},{"6f67cfb0","Unit_FireSpellEndCastEvent"},{"6f405510","Ability_ClearActiveOwnership"},{"6f3f5930","Ability_SetActiveOwnership"},{"6f202e30","Jass_GetSpellTargetX"},{"6f202d00","Jass_GetSpellTargetUnit"},{"6f202c10","Jass_GetSpellTargetLoc"}};
        for(String[] name:names) {
            var fn=getFunctionAt(toAddr(name[0]));
            if(fn.getName().startsWith("FUN_") || fn.getName().equals("Spell_SchedulePostEffectTimer"))fn.setName(name[1],SourceType.USER_DEFINED);
        }
        // Refine the existing canonical ability prefix; retain its prior fields.
        DataTypeManager manager=currentProgram.getDataTypeManager();
        String category="/WarcraftIII/Pathfinding127/";
        Structure phases=(Structure)manager.getDataType(category+"WC3SpellAbilityPrefix");
        DataType scalar=manager.getDataType(category+"WC3FloatMini");
        if(phases==null || scalar==null || scalar.getLength()!=8)throw new Exception("Missing canonical spell/scalar type");
        if(phases.getLength()<0x98)phases.growStructure(0x98-phases.getLength());
        int[] offsets={0x20,0x80,0x88,0x90};
        String[] fields={"flags","channel_delay","cast_point","backswing"};
        String[] evidence={"Payoff283:425aa0/3f5930/405510 active bit1, CAST instant100 and cooldown200; repeated original phase words.","Payoff283:3fbb80 reads84; Ainf Cast.2 schedules d01bb while AHfs keeps zero. Wrapper dispatch RVA a9de14 observed.","Payoff283:3fb5d0 reads8c after CAST and schedules d01bc, even after callback Stop. Wrapper dispatch RVA a9de14 observed.","Payoff283:40b380 reads94 after concrete EFFECT body; class may replace it. InnerFire2.4/FlameStrike Cast.2 captured."};
        for(int i=0;i<offsets.length;i++) {
            DataType type=i==0 ? UnsignedIntegerDataType.dataType : scalar;
            DataTypeComponent prior=phases.getComponentAt(offsets[i]);
            if(!Undefined.isUndefined(prior.getDataType()) &&
                (!type.isEquivalent(prior.getDataType()) || !fields[i].equals(prior.getFieldName())))
                throw new Exception("Conflicting canonical field "+fields[i]);
            phases.replaceAtOffset(offsets[i],type,type.getLength(),fields[i],evidence[i]);
        }
        StringBuilder out=new StringBuilder();
        for(String address:new String[]{"6f425aa0","6f438530","6f437940","6f3fbb80","6f3fb5d0","6f40b380","6f40e030","6f40e3a0","6f40c3f0","6f5d9a90","6f40d570","6f5d9ec0","6f436460","6f40c500","6f67d130","6f67cfb0","6f405510","6f3f5930","6f202e30","6f202d00","6f202c10"}) {
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
