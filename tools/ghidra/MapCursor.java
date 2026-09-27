// Evidence-backed cursor/model-renderer map for WC3 1.27.1.7085.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;

public class MapCursor extends GhidraScript {
    static final String HASH = "d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236";
    void name(String address, String name, String evidence) throws Exception {
        Function f = getFunctionAt(toAddr(address));
        if (f == null) {
            disassemble(toAddr(address));
            f = createFunction(toAddr(address), name);
        }
        if (f == null) throw new Exception("Cannot create function at " + address);
        if (f.getSymbol().getSource() != SourceType.DEFAULT && !f.getName().equals(name)
                && !f.getName().startsWith("FUN_"))
            throw new Exception("Preserve existing annotation: " + f.getName());
        f.setName(name, SourceType.USER_DEFINED);
        f.setComment(evidence + "\nRecovered descriptive name, not an original debug symbol. "
            + "See open-realm/docs/games/warcraft-3/cursor-rendering.md. SHA256 " + HASH);
    }
    void field(StructureDataType s, int offset, DataType type, String name) {
        s.replaceAtOffset(offset, type, type.getLength(), name, "Partial recovered layout");
    }
    public void run() throws Exception {
        if (!HASH.equals(currentProgram.getExecutableSHA256())
                || currentProgram.getImageBase().getOffset() != 0x6f000000L)
            throw new Exception("Requires mapped game.dll at image base 6f000000");
        name("6f11dd70", "Cursor_GetOrCreateFrame", "CScreenFrame +16c; allocates 1b0 bytes. Sole direct caller of cursor constructor.");
        name("6f11d380", "Cursor_ConstructFrame", "CSpriteFrame subclass, layer 10000. Main vtable 6fa8da14; layout subobject +b4 vtable 6fa8db04.");
        name("6f11d520", "Cursor_UpdateFrame", "Vtable +2c; checks cursor visibility and first sprite, positions it then calls SpriteFrame_Update.");
        name("6f11d580", "Cursor_BindFrameSprite", "Calls SpriteFrame_SetFirstSprite(sprite,-1,0); -1 bypasses model-camera extraction.");
        name("6f11d5a0", "Cursor_UpdatePosition", "Mouse x/y -> vec3 with z bits 43f9ffdf; CSpriteUber track 1 via Sprite_SetVectorTrack, retainMask 0.");
        name("6f072e80", "Cursor_GetUIPosition", "Thread event-context lookup and locking around pixel-to-UI conversion.");
        name("6f075540", "Cursor_ConvertClientPosition", "091150 client pixels -> 0753a0 normalized -> 8b38a0 UI units.");
        name("6f0753a0", "Cursor_NormalizeClientPosition", "Optional clip rectangle clamps to interior and warps pointer. x/width, 1-y/height.");
        name("6f8b38a0", "UI_ConvertNormalizedPosition", "Multiplies normalized coordinates by .8 and .6 respectively.");
        name("6f0f0a70", "Cursor_SetLayerSprite", "Retains new owner +2c sprite, releases old; vtable +84 propagates change.");
        name("6f0f0ab0", "Cursor_RefreshScreenSprite", "Gets screen cursor frame then binds sprite.");
        name("6f0f6980", "SpriteFrame_Construct", "Sprite slots +170..17c, draw buffer +180, scale +19c; optional delta clamp +1a4/+1a8.");
        name("6f0f7060", "SpriteFrame_GetFirstSprite", "First entry in sprite pointer array, or null.");
        name("6f0f7770", "SpriteFrame_SetFirstSprite", "Reference-counted first slot replacement. Optional model camera extraction; cursor passes -1.");
        name("6f0f73e0", "SpriteFrame_Update", "Base frame setup, projection, clear old draw records, collect/update each sprite tree.");
        name("6f0f72f0", "SpriteFrame_Draw", "Cursor vtable +30; reverse buffer traversal then shared model queue flush.");
        name("6f1add30", "UI_SetOrthographicModelProjection", "Default frame camera path; orthographic near/far -500/+500, Z inversion and frame-center transform.");
        name("6f11e7e0", "SpriteBuffer_CollectTree", "Builds update context; traverses sprite tree using callback 11f1f0.");
        name("6f11f1f0", "SpriteBuffer_UpdateAndCollect", "Indirect callback ECX=sprite, EDX=context. Updates sprite then appends eligible root/attachments.");
        name("6f11ee60", "SpriteBuffer_Append", "Retains sprite in 18-byte record. Buffer count +14, allocation capacity +8/data +c.");
        name("6f11e8e0", "SpriteBuffer_Clear", "Releases previous records, resets count, manages allocation.");
        name("6f11f1d0", "SpriteBuffer_DrawReverse", "Walks collected records back to front through Sprite_Draw.");
        name("6f1a31c0", "Sprite_TraverseAttachments", "Negative-order children, current sprite callback, remaining children; vtable +64 enumerates attachments.");
        name("6f1a2de0", "Sprite_Create", "ECX=kind: 0 Mini, nonzero Uber. Cursor callers pass 1. Leaf Uber vtable 6fa92924.");
        name("6f19e510", "SpriteUber_Construct", "Nine managed tracks. Translation track 1 value +c0; uniform scale +e8; color +148; alpha +1b0.");
        name("6f1a0010", "SpriteUber_Update", "Uber vtable +c. Resolves pending sequence -2, updates tracks and model animation, transforms and attachments.");
        name("6f1a54f0", "SpriteUber_ApplyColorAndAlpha", "Uber vtable +14. Propagates dirty color/alpha tracks to model instance.");
        name("6f1a2bf0", "Sprite_Draw", "Model nonnull: vtable +14 then Model_QueueDraw. +14 is tint preparation, not whole geometry draw.");
        name("6f1a42b0", "Sprite_SelectAnimationTokens", "ECX=sprite EDX=token array; stack count,flags. Cursor uses flags=0. Return uses x87; do not trust inferred integer prototype.");
        name("6f1a2f70", "Sprite_CloneModelInstance", "Clones Mini/Uber model state, animation queue and resource reference; installs completion callback at data xref 1a30e5.");
        name("6f1a4bb0", "Sprite_BindModelInstance", "Clears prior sprite model, retains supplied instance and installs completion callback at data xref 1a4bc7.");
        name("6f1a1680", "Sprite_CompleteQueuedSequence", "Model end callback installed by Sprite_LoadModel. Looping queue head marks sequence -2; non-looping single head sets flag 1000000 unless queue flag 400 forces repeat. Advances head when another entry exists.");
        name("6f1a1740", "Sprite_ResolvePendingAnimation", "Selects queued sequence, resets/starts model sequence via 183150 -> 8a3a70 -> 8a4eb0.");
        name("6f1ac940", "Sprite_SetVectorTrack", "ECX=manager EDX=track index; stack vector pointer, retain-axis mask. Push 3 means vector width, NOT track index.");
        name("6f8a4eb0", "Model_StartSequence", "ECX=animation state EDX=sequence data; stack sequence index,flags. Sequence clock table +8 with 10-byte entries.");
        name("6f8a3b80", "Model_TransferSequencePhase", "Transfers proportional phase only when BOTH runtime sequence records have bit 2 at +c; otherwise returns 0.");
        name("6f8a5000", "Model_AdvanceSequenceClock", "Updates interval position; loop/clamp and end callbacks. Caller converts elapsed milliseconds with animation speed.");
        name("6f18c810", "Model_QueueDraw", "Queues geosets/effects and recursive children. Called with CSprite model pointer at sprite +20.");
        name("6f18ce80", "Model_FlushDrawQueues", "Flushes both shared queues and clears counts; reached from SpriteFrame_Draw.");
        name("6f18d760", "Model_DrawQueuedGeoset", "Queue kind 0; skips invisible geoset and applies model/material state.");
        name("6f18d7c0", "Model_DrawGeosetLayers", "Chooses shared-alpha/UV optimized path or per-layer path.");
        name("6f18da10", "Model_DrawGeosetSharedStreams", "Observed HumanCursor path. Sets streams once, begins indexed primitive, draws material layers, ends primitive.");
        name("6f18d800", "Model_DrawGeosetSeparateLayers", "Alternative path when visible layers do not share alpha and texture coordinate streams.");
        name("6f18c5b0", "Model_BindGeosetStreams", "Positions/normals and up to two authored texture coordinate streams -> graphics backend.");
        name("6f1424e0", "GxD3D_BeginIndexedPrimitive", "Observed device vtable +70. Copies 16-bit indices, unlocks index buffer, SetIndices.");
        name("6f1422b0", "GxD3D_DrawIndexedPrimitive", "Device vtable +74. Flushes cached states at 1431d0, calls IDirect3DDevice9 +148 at 142303.");
        name("6f3528e0", "Cursor_RegisterAnimationTokens", "Normal0 Select1 Target2 TargetSelect3 InvalidTarget4 HoldItem5 Scroll6 Left7 Right8 Up9 Down10.");
        name("6f38a9c0", "Cursor_SelectMode", "Owner +1c0 locks changes; modes0..17 map to animation tokens, then owner +1ac and tint. Mode9 binds held item texture slot15h.");
        name("6f38a930", "Cursor_ApplyModeTint", "RGB table 6fabb554 with 18 rows, alpha FF. Mode8 uses dynamic relationship/player-color lookup 343e00.");
        name("6f2e0340", "Cursor_LoadSkinSprite", "Creates Uber, resolves skin key Cursor, loads MDL/MDX, selects Normal and binds to layer; releases temporary reference.");

        name("6f386fa0", "Cursor_PushMode", "WorldFrame thiscall(force,explicitMode,mode). Force bypasses lock/duplicate checks; stack +1b0 capacity,+1b4 count,+1b8 data,+1bc growth.");
        name("6f386f50", "Cursor_PopMode", "Unlocked and nonempty only: reselect top with restore flag, then remove it.");
        name("6f3931f0", "SelectMode_UpdateWidgetCursor", "CSelectMode widget hover event: modes0..3 by visibility and relationship.");
        name("6f394d50", "SelectMode_UpdateUnitCursor", "CSelectMode unit hover event: modes0..3 by visibility and relationship.");
        name("6f3bed10", "TargetMode_UpdateWidgetCursor", "CTargetMode widget validation: Target mode4 on invalid; modes5..7 on valid relationships. Not InvalidTarget token4.");
        name("6f3c3550", "TargetMode_UpdateUnitCursor", "CTargetMode unit validation: Target mode4 on invalid; modes5..7 on valid relationships.");
        name("6f3a9300", "DragScrollMode_Enter", "CDragScrollMode vtable6fab9914 +24: unlock, force push current, Normal, lock.");
        name("6f3a9370", "EscMenu_Enter", "CEscMenu vtable6fabdf34 +24: force push current, Normal, cursor visibility and pause.");
        name("6f3a94d0", "QuestMode_Enter", "CQuestMode vtable6fac12f0 +24: push current, Normal, pause.");
        name("6f3a9680", "ScriptDialogMode_Enter", "CScriptDialogMode vtable6fac19b8 +24: push current, Normal, pause.");
        name("6f3535d0", "Cursor_LoadTeamColorPalette", "Resolves TeamColor skin prefix; 6f3537b4..6f353804 decode each image and retain first texel at table6fd6a6fc for Signal tint.");
        name("6f3be010", "SignalMode_DispatchEvent", "CSignalMode vtable6fab9944 +0c: routes world, widget and minimap click events.");
        name("6f3c4de0", "SignalMode_SendPoint", "Signal click completion reaches 6f26ff00 CNetCommandAllyPing.");
        name("6f3a97a0", "SignalMode_Enter", "CSignalMode vtable6fab9944 +24: push current, mode8 TargetSelect with dynamic color, lock.");
        name("6f3ae020", "DragScrollMode_Leave", "Unlock/pop and clear interaction state.");
        name("6f3ae0a0", "EscMenu_Leave", "Pop cursor mode, refresh, restore visibility, release pause.");
        name("6f3ae210", "QuestMode_Leave", "Pop cursor mode, release pause.");
        name("6f3ae2c0", "ScriptDialogMode_Leave", "Pop cursor mode, release pause.");
        name("6f3ae380", "SignalMode_Leave", "Unlock and pop cursor mode.");
        name("6f3a97f0", "TargetMode_Enter", "CTargetMode vtable6fab98b4 +24: establish command target; select Target4 or force push explicit4 if locked; refresh unit/widget hover events.");

        CategoryPath category = new CategoryPath("/WC3CursorRecovered");
        DataType u32 = UnsignedIntegerDataType.dataType;
        DataType pointer = new PointerDataType(null, 4);
        StructureDataType frame = new StructureDataType(category, "CursorFramePartial", 0x1b0);
        field(frame, 0, pointer, "vtable");
        field(frame, 0xa8, IntegerDataType.dataType, "layer");
        field(frame, 0xac, u32, "alpha");
        field(frame, 0x140, pointer, "modelCamera");
        field(frame, 0x170, u32, "spriteCapacity");
        field(frame, 0x174, u32, "spriteCount");
        field(frame, 0x178, pointer, "sprites");
        field(frame, 0x19c, FloatDataType.dataType, "scale");
        field(frame, 0x1a4, u32, "clampDelta");
        field(frame, 0x1a8, FloatDataType.dataType, "maxDelta");
        currentProgram.getDataTypeManager().addDataType(frame, DataTypeConflictHandler.REPLACE_HANDLER);
        StructureDataType sprite = new StructureDataType(category, "CursorSpriteUberPartial", 0x1d4);
        field(sprite, 0, pointer, "vtable");
        field(sprite, 0x20, pointer, "model");
        field(sprite, 0x28, u32, "flags");
        field(sprite, 0x2c, ShortDataType.dataType, "sequence");
        field(sprite, 0x30, pointer, "animationSet");
        field(sprite, 0x34, ByteDataType.dataType, "queueCount");
        field(sprite, 0x35, ByteDataType.dataType, "queueHead");
        field(sprite, 0x36, ByteDataType.dataType, "queueTail");
        field(sprite, 0x40, pointer, "queueRecords");
        field(sprite, 0xc0, new ArrayDataType(FloatDataType.dataType, 3, 4), "position");
        field(sprite, 0xe8, FloatDataType.dataType, "uniformScale");
        field(sprite, 0x148, u32, "colorARGB");
        field(sprite, 0x1b0, u32, "alpha");
        currentProgram.getDataTypeManager().addDataType(sprite, DataTypeConflictHandler.REPLACE_HANDLER);
        // These are reference types only: do not impose guessed full prototypes.
        setEOLComment(toAddr("6fa8da40"), "CCursorFrame update virtual slot +2c -> Cursor_UpdateFrame");
        setEOLComment(toAddr("6fa8da44"), "CCursorFrame draw virtual slot +30 -> SpriteFrame_Draw");
        setEOLComment(toAddr("6fa92930"), "CSpriteUber leaf update virtual slot +c -> SpriteUber_Update");
        setEOLComment(toAddr("6fa92938"), "CSpriteUber leaf virtual slot +14 -> SpriteUber_ApplyColorAndAlpha");
        println("Mapped cursor lifecycle, animation, projection and D3D submission; partial layouts only.");
    }
}
