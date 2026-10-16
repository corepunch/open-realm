import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
/** Reproducible ORDER345 annotations; full ORDER-01.2 remains open. */
public class Order345Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong retail executable");
  String[][] rows={
   {"6f602ad0","Move_SelectNearestFailureAlternative","ORDER345: two exact1263-record native repeats and759 public markers also matching unhooked control. Nearest failure alternative admits widget20 bit8 invulnerability with ec0. Vulnerable target binds Attack6c/70; invulnerable target emits851983 but49a980 forwards the original event to Move and leaves Attack identity invalid.5fd270 admits no-Amov target as one-shot d0174 range0. Engine now handles failure separately from cancellation: detaching ability-owned target chase does not dispatch cannot-path; point recovery retains cancellation notification. Strict528 tests101339 assertions per Classic/TFT; broader arrival/transport inventory remains open."},
   {"6f49a980","Attack_CreateTasksFromOrder","ORDER345: two exact1263-record native repeats and759 public markers also matching unhooked control. Nearest failure alternative admits widget20 bit8 invulnerability with ec0. Vulnerable target binds Attack6c/70; invulnerable target emits851983 but49a980 forwards the original event to Move and leaves Attack identity invalid.5fd270 admits no-Amov target as one-shot d0174 range0. Engine now handles failure separately from cancellation: detaching ability-owned target chase does not dispatch cannot-path; point recovery retains cancellation notification. Strict528 tests101339 assertions per Classic/TFT; broader arrival/transport inventory remains open."},
   {"6f5fd270","Move_CreateTasksFromOrder","ORDER345: two exact1263-record native repeats and759 public markers also matching unhooked control. Nearest failure alternative admits widget20 bit8 invulnerability with ec0. Vulnerable target binds Attack6c/70; invulnerable target emits851983 but49a980 forwards the original event to Move and leaves Attack identity invalid.5fd270 admits no-Amov target as one-shot d0174 range0. Engine now handles failure separately from cancellation: detaching ability-owned target chase does not dispatch cannot-path; point recovery retains cancellation notification. Strict528 tests101339 assertions per Classic/TFT; broader arrival/transport inventory remains open."}
  };
  for(String[] row:rows){
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new IllegalStateException(row[0]);
   f.setName(row[1],SourceType.USER_DEFINED);String old=f.getComment();
   if(old==null || !old.contains(row[2]))f.setComment((old==null?"":old+"\n")+row[2]);
   println(row[0]+" "+f.getName()+" "+f.getComment().contains(row[2]));
  }
 }
}
