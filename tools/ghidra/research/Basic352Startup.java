// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import com.google.gson.*;
import java.nio.file.*;
import ghidra.program.model.symbol.SourceType;
/** Inspect data-rooted scalar initializers without inventing entry addresses. */
public class Basic352Startup extends GhidraScript {
 public void run()throws Exception{
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  JsonArray rows=new JsonArray();
  for(String target:new String[]{"6f070d80","6f070de0"})for(var ref:getReferencesTo(toAddr(target))){
   var containing=getFunctionContaining(ref.getFromAddress());if(containing!=null&&!containing.getName().startsWith("ScalarStartup352_"))continue;
   JsonObject row=new JsonObject();row.addProperty("site",ref.getFromAddress().toString());row.addProperty("target",target);JsonArray context=new JsonArray();
   var ins=getInstructionAt(ref.getFromAddress());
   for(int i=0;i<12&&ins!=null;i++,ins=getInstructionBefore(ins.getAddress())){
    JsonObject item=new JsonObject();item.addProperty("address",ins.getAddress().toString());item.addProperty("asm",ins.toString());JsonArray incoming=new JsonArray();for(var root:getReferencesTo(ins.getAddress())){if(root.getReferenceType().isData())incoming.add(root.getFromAddress().toString());}item.add("data_roots",incoming);context.add(item);
   }
   JsonObject root=null;int index=-1;for(int i=0;i<context.size();i++){var item=context.get(i).getAsJsonObject();if(item.getAsJsonArray("data_roots").size()>0){root=item;index=i;break;}}
   if(root==null||index!=2)throw new IllegalStateException("Expected three-instruction data-rooted thunk");
   String first=root.get("asm").getAsString(),second=context.get(1).getAsJsonObject().get("asm").getAsString(),last=context.get(0).getAsJsonObject().get("asm").getAsString();
   if(!(first.startsWith("MOV EDX,0x")||first.equals("XOR EDX,EDX")||first.equals("OR EDX,0xffffffff"))||!second.startsWith("MOV ECX,0x")||!last.equals("JMP 0x"+target))throw new IllegalStateException("Unexpected scalar thunk");
   var entry=toAddr(root.get("address").getAsString());for(var value:root.getAsJsonArray("data_roots")){var table=toAddr(value.getAsString());if(Integer.toUnsignedLong(getInt(table))!=entry.getOffset())throw new IllegalStateException("Root pointer differs");}
   var f=getFunctionAt(entry);if(f==null)f=createFunction(entry,"ScalarStartup352_"+entry);if(f==null)throw new IllegalStateException("Cannot recover data-rooted thunk");
   f.setComment("BASIC352: three-instruction scalar initializer thunk, reached from the exact exported static initialization table pointers. Loads literal EDX and global output ECX, then tail-jumps to integer/decimal conversion. No input/output pointer alias can be introduced by the integer VALUE. Exact original initializer execution is a separate witness from a gameplay producer claim.");
   row.addProperty("entry",entry.toString());row.add("context",context);rows.add(row);
  }
  Path path=Path.of(getScriptArgs()[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");Files.writeString(path,new GsonBuilder().setPrettyPrinting().create().toJson(rows)+"\n");println(rows.size()+" uncontained sites");
 }
}
