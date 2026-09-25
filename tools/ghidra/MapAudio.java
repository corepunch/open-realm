// Apply only evidence-backed names and partial layouts to the matching retail binary.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;
public class MapAudio extends GhidraScript {
 void name(String addr,String name,String comment) throws Exception {
  Function f=getFunctionAt(toAddr(addr));
  if(f==null) { disassemble(toAddr(addr));f=createFunction(toAddr(addr),name); }
  if(f!=null) {f.setName(name,SourceType.USER_DEFINED);f.setComment(comment+"\nRecovered name; not an original debug symbol. Image SHA256 d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236.");}
 }
 void field(StructureDataType s,int off,DataType t,String n,String c) {s.replaceAtOffset(off,t,t.getLength(),n,c);}
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("Wrong binary: offsets are version-specific");
  name("6f3593e0","Audio_PlayUnitResponse","ECX=CUnit*, EDX=response kind 0..5. Checks enabled, ownership, fog, unit sound template; forwards to Audio_PlayLabel.");
  name("6f34b150","Audio_PlayLabel","Resolves row; checks mode, distance cutoff, zoom. Chooses variant; converts flags; dispatches to Audio_CreateAndPlay. Restores previous variant on admission failure.");
  name("6f34fee0","Audio_ChooseVariant","Row +0x20=count, +0x24=filename array, +0x70=last index. Sequential/indexed/random modes; random avoids previous variant for bounded retries.");
  name("6f340140","Audio_ConvertSLKFlags","Converts authored flags into backend admission flags. They are distinct bit layouts.");
  name("6f0823d0","Audio_CreateAndPlay","Allocates HSOUND, initializes request, runs admission/playback. Return code is converted by Audio_MapResult.");
  name("6f0afe00","Audio_StartInstance","Dispatches admitted sound to MIDI/2D/3D backend; calls Audio_AdmitInstance.");
  name("6f0af5e0","Audio_AdmitInstance","Confirmed scheduler: duplicate usernames/files; per-channel limits; global count limit 24. Runtime flags control strict-priority, oldest/equal-priority and unconditional oldest preemption. +0x140 priority, +0x178 channel, +0x17c timestamp.");
  name("6f0abd50","Audio_FindLowestPriorityChannelHead","Walks per-channel list heads and selects lowest priority. Returns null if none.");
  name("6f0af9b0","Audio_Start2DSample","Miles set_named_sample_file, playback rate, loop count, EOS callback and start/resume.");
  name("6f07cf80","Audio_ConstructInstance","HSOUND constructor. Runtime layout is partial; unknown fields remain undefined.");
  name("6f35b0b0","Audio_UnitResponseCooldownExpired","ECX=unit pointer, hash lookup keyed by this pointer; deadline entry+0x18 compared against GetTickCount. Missing entry permits response.");
  name("6f353220","Audio_SetUnitResponseCooldown","Unit-keyed entry +0x18 = GetTickCount()+250; gated by global 6fd6a72c. Called by completion/preemption notification.");
  name("6f35b500","Audio_ResponseEnded","Notification callback registered in slots 1 and 3 at 6fab7714; invokes Audio_SetUnitResponseCooldown.");
  name("6f0ae4b0","Audio_InsertChannelByPriority","Ascending unsigned priority; inserts before equal priorities, so newest equal-priority sound becomes head.");
  name("6f0abcf0","Audio_DispatchCallback","Dispatches callback slot at instance +0x244+slot*4; admission preemption passes slot 3.");
  name("6f6955c0","Audio_ResetSelectionResponseCount","Clears global repeated-selection counters and stored unit identity pair.");
  name("6f688b70","Audio_IncrementSelectionResponseCount","Increments global selection response count at 0x6fd707f0.");
  name("6f690dd0","Audio_PlayPissedResponse","Response kind 1, explicit index selectionCount-3; resets on exhausted variants/failure, advances accepted responses.");
  name("6f690ea0","Audio_PlayWhatResponse","Response kind 0; checks per-unit wall-clock cooldown, increments counter unless mapped result is 2.");
  name("6f690cf0","Audio_PlayYesAttackResponse","Response kind 3; resets selection count then checks per-unit cooldown.");
  name("6f690d70","Audio_PlayYesResponse","Response kind 2; resets selection count then checks per-unit cooldown.");
  name("6f690ef0","Audio_PlayWarcryResponse","Response kind 5, global randomized request countdown; cooldown checked first.");
  name("6f35b5d0","Audio_MapResult","Backend result 0=>0, 1=>1, 3=>2, other=>4.");
  CategoryPath cat=new CategoryPath("/WC3AudioRecovered");
  StructureDataType inst=new StructureDataType(cat,"WC3SoundInstancePartial",0x264);
  field(inst,0x34,new ArrayDataType(CharDataType.dataType,260,1),"filename","Compared during duplicate-file admission");
  field(inst,0x13c,UnsignedIntegerDataType.dataType,"runtimeFlags","Audio_AdmitInstance; different from SLK flag bits");
  field(inst,0x140,UnsignedIntegerDataType.dataType,"priority","Larger values win; equality depends on preempt policy");
  field(inst,0x148,UnsignedIntegerDataType.dataType,"username","Duplicate user identifier; not a text username");
  field(inst,0x160,new PointerDataType(null,4),"sampleHandle","Miles 2D sample handle in Audio_Start2DSample");
  field(inst,0x178,UnsignedIntegerDataType.dataType,"channelIndex","Indexes per-channel list and 0x14-byte configuration row");
  field(inst,0x17c,UnsignedIntegerDataType.dataType,"startTicks","GetTickCount timestamp; oldest preemption candidate comparison");
  field(inst,0x1ac,UnsignedIntegerDataType.dataType,"state","3 denotes active in examined paths; full enum not recovered");
  field(inst,0x244,new ArrayDataType(new PointerDataType(null,4),4,4),"callbacks","Dispatch slots: 0 start, 1 end, 3 preempt; Frida verifies slot/context correlation");
  field(inst,0x254,new ArrayDataType(new PointerDataType(null,4),4,4),"callbackContexts","ECX passed to corresponding callback by Audio_DispatchCallback");
  currentProgram.getDataTypeManager().addDataType(inst,DataTypeConflictHandler.REPLACE_HANDLER);
  StructureDataType row=new StructureDataType(cat,"WC3SoundLabelPartial",0x74);
  field(row,0x20,UnsignedIntegerDataType.dataType,"fileCount","Audio_ChooseVariant");
  field(row,0x24,new PointerDataType(new PointerDataType(CharDataType.dataType,4),4),"filenames","Array of filename pointers");
  field(row,0x38,UnsignedIntegerDataType.dataType,"priority","SLK Priority; copied by Audio_PlayLabel, variant added for SCALEPRIORITY");
  field(row,0x3c,UnsignedIntegerDataType.dataType,"channelIndex","SLK Channel; confirmed at 6f34b51e and live Frida alias probe");
  field(row,0x40,UnsignedIntegerDataType.dataType,"authoredFlags","SLK flag table at 0x6fab7c50");
  field(row,0x44,FloatDataType.dataType,"minDistance","Copied into distance parameters by 0x6f3482d0");
  field(row,0x48,FloatDataType.dataType,"maxDistance","Copied into distance parameters by 0x6f3482d0");
  field(row,0x50,FloatDataType.dataType,"distanceCutoffSquared","Compared to sum of squared XYZ deltas");
  field(row,0x70,IntegerDataType.dataType,"lastVariant","-1 sentinel; restored if admission fails");
  currentProgram.getDataTypeManager().addDataType(row,DataTypeConflictHandler.REPLACE_HANDLER);
  StructureDataType channel=new StructureDataType(cat,"WC3SoundChannelConfig",0x14);
  field(channel,0,UnsignedIntegerDataType.dataType,"maxSounds","Admission cap");
  field(channel,4,UnsignedIntegerDataType.dataType,"minPriority","Request priority clamp");
  field(channel,8,UnsignedIntegerDataType.dataType,"maxPriority","Request priority clamp");
  field(channel,12,FloatDataType.dataType,"volumeScale","0x6f0b1f10 multiplies channel volume");
  field(channel,16,UnsignedIntegerDataType.dataType,"flags","Bit 0 disables requested 3D mode in examined constructor");
  DataType ct=currentProgram.getDataTypeManager().addDataType(channel,DataTypeConflictHandler.REPLACE_HANDLER);
  clearListing(toAddr("6fab5ec8"),toAddr("6fab6007"));
  createData(toAddr("6fab5ec8"),new ArrayDataType(ct,16,ct.getLength()));
  createLabel(toAddr("6fab5ec8"),"Audio_DefaultChannelConfigs",true);
  DataType it=currentProgram.getDataTypeManager().getDataType(cat,"WC3SoundInstancePartial");
  Function admit=getFunctionAt(toAddr("6f0af5e0"));
  admit.updateFunction("__thiscall",new ReturnParameterImpl(UnsignedIntegerDataType.dataType,currentProgram),
   Arrays.asList(new ParameterImpl("sound",new PointerDataType(it,4),currentProgram.getRegister("ECX"),currentProgram),
    new ParameterImpl("logger",new PointerDataType(null,4),4,currentProgram)),
   Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED);
  try(PrintWriter w=new PrintWriter(getScriptArgs()[0])) {
   w.println("// Partial, version-specific layouts; undefined bytes are intentional.");
   for(StructureDataType st:new StructureDataType[]{inst,row,channel}) {w.println(st.getName()+" minimum size="+st.getLength());for(DataTypeComponent c:st.getDefinedComponents())w.println(String.format("  +0x%x %s %s // %s",c.getOffset(),c.getDataType().getDisplayName(),c.getFieldName(),c.getComment()));}
  }
 }
}
