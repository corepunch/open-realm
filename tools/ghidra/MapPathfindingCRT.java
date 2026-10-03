// Evidence-backed sibling CRT byte/locale map; run with game.dll active.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.framework.model.DomainFile;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import com.google.gson.*;
import java.nio.file.*;

public class MapPathfindingCRT extends GhidraScript {
    static final String HASH = "86e39b5995af0e042fcdaa85fe2aefd7c9ddc7ad65e6327bd5e7058bc3ab615f";
    static final CategoryPath CATEGORY = new CategoryPath("/WarcraftIII/PathfindingCRT");
    static final String[][] FUNCTIONS = {
        {"1000f1d5", "isdigit", "cdecl C; df7c4==0 returns pctype[C]&4, default pctype RVA1158. S2R070de0 promotes bytes SIGNED (-128..127). Only ASCII48..57 have bit4. Nonzero ever-changed flag calls _isdigit_l(C,NULL), which requires original TLS; do not extend captured default proof to other locales."},
        {"10012652", "_isdigit_l", "Constructor0f764; mb_cur_max<2 directly reads pctype[C]&4 INCLUDING signed-char prefix; otherwise _isctype_l(C,4,context). Restore TLS ownlocale bit2 only when context+c was set. Explicit default C-locale proved without TLS replacement."},
        {"1008957d", "_isctype", "Zero ever-changed flag returns pctype[C]&mask, same C-table branch used by isdigit."},
        {"100895ac", "_isctype_l", "Direct table domain unsigned C+1<257, i.e. -1..255. Other values use multibyte/GetStringType. This differs from _isdigit_l single-byte signed-prefix lookup; Windows character services are excluded from the byte oracle."},
        {"1000f764", "WC3CRT_LocaleContext_Init", "ECX context, stack4 explicit locale pair, RET4. Non-null copies two pointers, clears flag+c, avoids TLS. NULL calls _getptd; reconciles shared locinfo dfa84/mbcinfo dfca8 using TLS ownlocale+70 and mode dfdd4; sets/restores bit2. Only13-byte context prefix recovered."},
        {"100132b8", "_wsetlocale", "Non-null requested locale differing from C sets df7c4=1 at1335c. Returning to C does NOT reset this ever-changed flag. Shared mode updates locinfo dfa84, pctype df858 and mb_cur_max. No direct Game.dll import/symbol for setlocale/_wsetlocale/_configthreadlocale/_setmbcp found; dynamic reachability not asserted."},
        {"100a55eb", "_configthreadlocale", "TLS ownlocale+70 bit2 controls thread locale. -1 sets global dfdd4=-1;1 enables,2 disables,0 queries. Alternative locale creation outside captured default byte parity."},
        {"10022b47", "__pctype_func", "Return TLS/reconciled locinfo pctype; exported7bc61 returns address of same field."}
    };
    static final String[][] GLOBALS = {
        {"100df7c4", "WC3CRT_LocaleEverChanged", "u32 captured0; _wsetlocale1335c writes1 for non-C and never clears on return to C"},
        {"100df858", "WC3CRT_ActivePctype", "Original global pctype pointer, captured native RVA1158"},
        {"100dfa84", "WC3CRT_SharedLocaleInfo", "Global default locale info: verified mb_cur_max+74=1,pctype+90=RVA1158"},
        {"100dfca8", "WC3CRT_SharedMbcInfo", "Original shared multibyte locale pointer used by context constructor0f764"},
        {"100dfdd4", "WC3CRT_ThreadLocaleMode", "Global mode mask tested against TLS ownlocale; _configthreadlocale writes"},
        {"10001058", "WC3CRT_DefaultCtypeSignedPrefix", "128 u16 words for signed indices-128..-1; all digit masks0"},
        {"10001158", "WC3CRT_DefaultCtypeByteTable", "256 u16 entries0..255; bit4 only48..57. Whole384-word table SHA e7304be1d56c85907c3c409a5252fba8b5b51fa74aeb8fa4703b67e8427d7d3f"}
    };

    Address address(Program program, String value) throws Exception {
        return program.getAddressFactory().getDefaultAddressSpace().getAddress(value);
    }

    DataType persist(DataTypeManager manager, Structure structure) throws Exception {
        DataType old = manager.getDataType(CATEGORY, structure.getName());
        if (old != null && !old.isEquivalent(structure)) {
            if (!(old instanceof Structure) || old.getLength() > structure.getLength())
                throw new Exception("Preserve incompatible existing CRT annotation " + old.getPathName());
            for (DataTypeComponent component : ((Structure)old).getDefinedComponents()) {
                DataTypeComponent proposed = structure.getComponentAt(component.getOffset());
                if (proposed == null || proposed.getOffset() != component.getOffset() ||
                    proposed.getLength() != component.getLength() ||
                    !component.getFieldName().equals(proposed.getFieldName()) ||
                    !component.getDataType().isEquivalent(proposed.getDataType()))
                    throw new Exception("Preserve incompatible existing CRT field " + old.getPathName() + "+" + component.getOffset());
            }
        }
        return manager.resolve(structure, DataTypeConflictHandler.REPLACE_HANDLER);
    }

    public void run() throws Exception {
        DomainFile file = currentProgram.getDomainFile().getParent().getFolder("CRT").getFile("msvcr120.dll");
        Program program = (Program)file.getDomainObject(this, true, false, monitor);
        int transaction = program.startTransaction("NUM-01.13 exact shipped CRT byte classification");
        boolean applied = false;
        try {
            if (!HASH.equals(program.getExecutableSHA256())) throw new Exception("Unsupported sibling CRT hash");
            DataTypeManager manager = program.getDataTypeManager();
            Structure info = new StructureDataType(CATEGORY, "WC3CRT_LocaleInfoPrefix", 0x94, manager);
            info.replaceAtOffset(0x74, UnsignedIntegerDataType.dataType, 4, "mb_cur_max", "Verified single-byte versus multibyte branch");
            info.replaceAtOffset(0x90, new PointerDataType(UnsignedShortDataType.dataType, 4, manager), 4, "pctype", "Signed prefix plus byte table");
            DataType infoType = persist(manager, info);
            Structure pair = new StructureDataType(CATEGORY, "WC3CRT_LocalePair", 0, manager);
            pair.add(new PointerDataType(infoType, 4, manager), "locinfo", null);
            pair.add(new PointerDataType(VoidDataType.dataType, 4, manager), "mbcinfo", null);
            DataType pairType = persist(manager, pair);
            Structure context = new StructureDataType(CATEGORY, "WC3CRT_LocaleContextPrefix", 0, manager);
            context.add(new PointerDataType(infoType, 4, manager), "locinfo", null);
            context.add(new PointerDataType(VoidDataType.dataType, 4, manager), "mbcinfo", null);
            context.add(new PointerDataType(VoidDataType.dataType, 4, manager), "thread_data", "Only populated for null explicit locale");
            context.add(ByteDataType.dataType, "toggled_ownlocale", "Caller restores TLS ownlocale bit2 when nonzero");
            DataType contextType = persist(manager, context);
            JsonArray functions = new JsonArray();
            for (String[] row : FUNCTIONS) {
                Function function = program.getFunctionManager().getFunctionAt(address(program, row[0]));
                if (function == null) throw new Exception("Missing analyzed CRT function " + row[0]);
                if (row[1].startsWith("WC3CRT_")) function.setName(row[1], SourceType.USER_DEFINED);
                function.setComment("NUM-01.13: " + row[2] + " Exact sibling CRT SHA " + HASH);
                if (row[0].equals("1000f764")) {
                    Parameter[] parameters = {
                        new ParameterImpl("context", new PointerDataType(contextType, 4, manager), new VariableStorage(program, program.getRegister("ECX")), program),
                        new ParameterImpl("locale", new PointerDataType(pairType, 4, manager), new VariableStorage(program, 4, 4), program)
                    };
                    function.setCallingConvention("__thiscall");
                    function.setReturnType(new PointerDataType(contextType, 4, manager), SourceType.USER_DEFINED);
                    function.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.USER_DEFINED, parameters);
                }
                JsonObject entry = new JsonObject();
                entry.addProperty("address", row[0]);
                entry.addProperty("name", function.getName());
                entry.addProperty("prototype", function.getPrototypeString(true, true));
                entry.addProperty("comment", function.getComment());
                functions.add(entry);
            }
            for (String[] row : GLOBALS) {
                Address location = address(program, row[0]);
                program.getSymbolTable().createLabel(location, row[1], SourceType.USER_DEFINED);
                program.getListing().setComment(location, CommentType.PLATE, "NUM-01.13: " + row[2]);
            }
            program.getListing().setComment(address(program, "1001335c"), CommentType.EOL,
                "NUM-01.13 non-C request sets ever-changed flag1; captured default is0");
            JsonObject result = new JsonObject();
            result.addProperty("crt_sha256", program.getExecutableSHA256());
            result.addProperty("types", 3);
            result.addProperty("global_labels", GLOBALS.length);
            result.add("functions", functions);
            String[] arguments = getScriptArgs();
            if (arguments.length == 1)
                Files.writeString(Path.of(arguments[0]), new GsonBuilder().setPrettyPrinting().create().toJson(result) + "\n");
            println("NUM-01.13 persisted/read back3 CRT types,8 function roles,7 globals and explicit x86 context ABI");
            applied = true;
        } finally {
            program.endTransaction(transaction, applied);
            if (applied) program.save("NUM-01.13 CRT byte/locale ownership", monitor);
            program.release(this);
        }
    }
}
