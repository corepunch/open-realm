import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;

/** Verified guard ownership subset of ORDER-01.2; broader gates remain open. */
public class Order344Evidence extends GhidraScript {
    public void run() throws Exception {
        if (!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))
            throw new IllegalStateException("Wrong retail executable");
        String[][] rows = {
        {"6f9cd390","TownAI_DispatchIdleUnit","Audit328: internal d0006 owns Unit198 in9bd6e0 around this dispatch. Selects resource work from widget flags, then retained Captain9d8a90, then Town policy. Scope also occurs at configured computer CreateUnit before script startup; enabled323 has fifteen pairs, including subsequent Captain completions. ORDER344: resource widget20 masks8000/10000/40000/80000 resolve Ahar/Aaha/Ahrl/Awha respectively;10000 and40000 also require AIrecord28 bits1/2. After Captain priority, resolve guard AIrecord3c/40 and invoke9c0b00."},
            {"6f210970", "Jass_RemoveGuardPosition", "ORDER344d: public RemoveGuardPosition delegates to9be910. Computer hfoo Move at545 becomes idle immediately at550, with no issued Stop event; stays at363.222,391.854. This is Town withdrawal plus Stop, not only post-array removal."},
            {"6f9be910", "TownAI_RemoveUnitGuardPosition", "ORDER344d: require player Town and unit AI record. Invoke9c9b30, then detach retained Captain if any. Recycle9bde20 calls this before resetting ability AI mode and refreshing Town membership."},
            {"6f9c9b30", "TownAI_RetireAssignedGuard", "ORDER344d:9cd170 withdraws Town membership then ReplaceOrdersForStop(1). Search assigned guard; unsubscribe update and owner-change callbacks, invalidate38/3c, set assignment44=99999. KEEP post in linked list. Engine deletion was incorrect."},
            {"6f9bde20", "TownAI_RecycleUnitGuardPosition", "ORDER344 static: remove9be910, Unit_SetAbilityAIMode(unit,0), then Unit_RefreshAIOwnedPoolMembership. Reenrollment observes current Town policy and predicted pose; not merely making the prior position vacant."},
            {"6f9c9a60", "TownAI_RemoveAllGuardPositions", "ORDER344 static: detach Town guard-list head first, walk saved next identities. Assigned units withdraw via9cd170; unregister callbacks, invalidate unit identity, decrement assignment44, destroy guard record. Unlike single RemoveGuardPosition, no explicit ReplaceOrdersForStop call here."},
            {"6f9cd170", "TownAI_WithdrawOwnedUnit", "ORDER344: remove unit from Town roles and owned linked list, update spot references, clear Unit5c bit4 and release AI record699960(0). Used by single/all guard removal before callback subscription teardown. Wider worker-role branches remain separately mapped."},
            {"6f9c0b00", "GuardAI_ReturnAssignedUnit", "ORDER344 static plus read-only public computer Footman trace: ECX guard record, stack4 unit. Saved XY at28/30; MoveBridge_IsPointInCollisionRange with runtime50 and source radius. Outside range and Move ability3c<1 admits Attack point d000f (1,1), then appends AIImmediate. Caller9cd600 follows failed Move recovery. Player-owned control never enters. ORDER344c adds public issued-point trigger: Move events185/365, AI Attack events224/404; all545 prior markers unchanged. ORDER344d expands to six scenes and1103 public markers, two1227-record native repeats and unhooked control. Format196 implements default/explicit guard dispatch, owner inverse, cold saves and callback-safe membership. Complete ORDER-01.2 gates remain open."},
            {"6f9c72d0", "TownAI_EnrollEligibleGuard", "ORDER344: guard admission excludes widget20 maskd8000, structures, Hero-not-illusion and unit248 bit10. Called from9c3660 only with Town2d0 bit0 SET, without illusion order-admission40000000 or BTLF. Town default80181 sets bit0 except NeutralAggressive; StopGathering clears it. Campaign/melee4000 is independent."},
            {"6f9c3270", "TownAI_AssignGuardAtUnitPosition", "ORDER344: query predicted bridge world position, find/create guard by rawcode and exact XY through9c2ee0, then bind unit9bfc90. No committed-position substitution."},
            {"6f9c2ee0", "TownAI_FindOrCreateGuardPost", "ORDER344: search Town1c8/1cc linked guard records, reuse only vacant identity38/3c plus exact rawcode34 and scalar XY28/30 match. Otherwise create and prepend, initialize assignment invalid and counter44 zero. Next guard identity48/4c."},
            {"6f9bfc90", "GuardAI_AssignUnit", "ORDER344: subscribe owner changes, assign guard38/3c canonical unit identity and increment counter44. Detach existing Captain, update AIrecord guard identity3c/40. Engine pointer lifetime must mirror removal/owner-change subscriptions before slot reuse."},
            {"6f603110", "CAbilityMove_OnCantPath", "ORDER344: six public scenes distinguish human flags2-clear from configured computer before VM and after owner round-trip. Failure223/403/943 selects transport-null then6802f0 and guard Attack9c0b00 synchronously. Public removal/transfer550/730 stop before failure. Retained attack-target and successful ferry alternatives remain open."},
            {"6f9ccdb0", "TownAI_AssignTemporaryUnitCaptain", "ORDER344: prior guard3c/40 invokes9c9b30 before policy, resets ability AI mode and refreshes Town membership. Engine suppresses another initial guard while the temporary owner exists before BTLF publication; disabled-policy regression failed two assertions before correction."}
        };
        for (String[] row : rows) {
            var f = getFunctionAt(toAddr(row[0]));
            if(f==null)throw new IllegalStateException("Missing function "+row[0]);
            f.setName(row[1],SourceType.USER_DEFINED);
            String prior=f.getComment();
            if(prior==null || prior.isEmpty())f.setComment(row[2]);
            else if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);
            println(row[0]+" "+f.getName());
        }
    }
}
