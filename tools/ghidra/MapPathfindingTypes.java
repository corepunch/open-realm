// Persist instruction/oracle-backed layouts and explicit x86 ABI storage.
// @category WarcraftIII
import java.io.FileReader;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

public class MapPathfindingTypes extends GhidraScript {
    private static final CategoryPath CATEGORY = new CategoryPath("/WarcraftIII/Pathfinding127");
    private final Map<String, DataType> types = new LinkedHashMap<>();

    private DataType type(String name) throws Exception {
        if (name.startsWith("ptr:")) return new PointerDataType(type(name.substring(4)), 4);
        DataType result = types.get(name);
        if (result == null) throw new Exception("Unknown schema type " + name);
        return result;
    }

    // A larger body of evidence may name previously undefined bytes. Preserve
    // every existing field, type, offset and comment before allowing refinement.
    private boolean preservesType(DataType existing, DataType desired) {
        if (existing.isEquivalent(desired)) return true;
        if (existing instanceof Structure && desired instanceof Structure &&
            existing.getPathName().equals(desired.getPathName()))
            return preservesFields(existing,desired);
        if (existing.getLength() != desired.getLength()) return false;
        if (existing instanceof Pointer && desired instanceof Pointer) {
            DataType old = ((Pointer)existing).getDataType(), next = ((Pointer)desired).getDataType();
            //147af0/1489a0 access the derived spatial map, beyond the shared
            //108-byte header. This named refinement retains that entire base.
            if(old != null && next instanceof Structure && old.getCategoryPath().equals(CATEGORY) &&
                old.getName().equals("WC3PathMapHeader") && next.getName().equals("WC3SpatialMapPrefix")) {
                DataTypeComponent base=((Structure)next).getComponentAt(0);
                if(base != null && base.getOffset()==0 && base.getDataType().isEquivalent(old))return true;
            }
            // The referenced canonical layout is preflighted separately. Refining its
            // undefined bytes must not invalidate every unchanged pointer to that layout.
            // An opaque pointer can be refined to a newly proven canonical
            // layout without changing its storage or erasing field evidence.
            if (old instanceof VoidDataType && next != null && next.getCategoryPath().equals(CATEGORY) &&
                types.containsKey(next.getName())) return true;
            return old != null && next != null && old.getPathName().equals(next.getPathName()) &&
                old.getCategoryPath().equals(CATEGORY) && types.containsKey(old.getName());
        }
        return false;
    }

    private boolean preservesFields(DataType existing, DataType desired) {
        if (!(existing instanceof Structure) || !(desired instanceof Structure) ||
            existing.getLength() > desired.getLength()) return false;
        Structure old = (Structure)existing, next = (Structure)desired;
        for (DataTypeComponent field : old.getDefinedComponents()) {
            if (Undefined.isUndefined(field.getDataType())) continue;
            DataTypeComponent replacement = next.getComponentAt(field.getOffset());
            if (replacement == null || replacement.getOffset() != field.getOffset() ||
                !preservesType(field.getDataType(),replacement.getDataType()) ||
                !java.util.Objects.equals(field.getFieldName(), replacement.getFieldName()) ||
                !preservesComment(field.getComment(), replacement.getComment())) return false;
        }
        return true;
    }

    // New evidence may append a comment while retaining every prior annotation.
    private boolean preservesComment(String old, String next) {
        return java.util.Objects.equals(old,next) ||
            (old != null && next != null && next.startsWith(old + "\n"));
    }

    // Preflight all fields/prototypes before mutating the program database.
    private void validate(JsonObject schema) throws Exception {
        for (JsonElement element : schema.getAsJsonArray("layouts")) {
            JsonObject layout = element.getAsJsonObject();
            int length = layout.get("length").getAsInt();
            boolean[] occupied = new boolean[length];
            for (JsonElement item : layout.getAsJsonArray("fields")) {
                JsonObject field = item.getAsJsonObject();
                DataType datatype = type(field.get("type").getAsString());
                int offset = field.get("offset").getAsInt();
                if (offset < 0 || offset + datatype.getLength() > length)
                    throw new Exception("Field outside " + layout.get("name"));
                for (int n = offset; n < offset + datatype.getLength(); n++) {
                    if (occupied[n]) throw new Exception("Overlapping fields " + layout.get("name"));
                    occupied[n] = true;
                }
            }
            DataType existing = currentProgram.getDataTypeManager().getDataType(CATEGORY, layout.get("name").getAsString());
            if (existing != null && !(existing instanceof Structure))
                throw new Exception("Preserve existing non-structure " + existing.getPathName());
        }
        for (JsonElement element : schema.getAsJsonArray("methods")) {
            JsonObject method = element.getAsJsonObject();
            Function function = getFunctionAt(toAddr(method.get("address").getAsString()));
            if (function == null) throw new Exception("Missing function " + method.get("address"));
            String name = method.get("name").getAsString();
            if (!function.getName().equals(name) && !function.getName().startsWith("FUN_"))
                throw new Exception("Preserve existing name " + function.getName());
            type(method.get("returns").getAsString());
            for (JsonElement item : method.getAsJsonArray("parameters")) {
                JsonObject parameter = item.getAsJsonObject();
                type(parameter.get("type").getAsString());
                JsonPrimitive storage = parameter.getAsJsonPrimitive("storage");
                if (storage.isString() && currentProgram.getRegister(storage.getAsString()) == null)
                    throw new Exception("Unknown register " + storage);
            }
        }
        if (schema.has("globals")) for (JsonElement element : schema.getAsJsonArray("globals")) {
            JsonObject global = element.getAsJsonObject();
            ghidra.program.model.address.Address address = toAddr(global.get("address").getAsString());
            if (getInstructionContaining(address) != null) throw new Exception("Global overlaps code " + address);
            type(global.get("type").getAsString());
            Symbol existing = currentProgram.getSymbolTable().getPrimarySymbol(address);
            if (existing != null && existing.getSource() == SourceType.USER_DEFINED &&
                !existing.getName().equals(global.get("name").getAsString()))
                throw new Exception("Preserve existing global " + existing.getName());
        }
    }

    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1 || args.length > 2)
            throw new Exception("Pass the absolute schema path and optional metadata report path");
        JsonObject schema;
        try (FileReader reader = new FileReader(args[0])) {
            schema = JsonParser.parseReader(reader).getAsJsonObject();
        }
        JsonObject target = schema.getAsJsonObject("target");
        if (schema.get("version").getAsInt() != 1 ||
            !target.get("game_sha256").getAsString().equals(currentProgram.getExecutableSHA256()) ||
            currentProgram.getImageBase().getOffset() != Long.parseLong(target.get("image_base").getAsString(), 16))
            throw new Exception("Requires game.dll 1.27.1.7085 at preferred base 6f000000");
        types.put("void", VoidDataType.dataType);
        types.put("u8", UnsignedCharDataType.dataType);
        types.put("u16", UnsignedShortDataType.dataType);
        types.put("u32", UnsignedIntegerDataType.dataType);
        types.put("u64", UnsignedLongLongDataType.dataType);
        types.put("i32", IntegerDataType.dataType);
        types.put("f32", FloatDataType.dataType);
        types.put("WC3PathScalar", new TypedefDataType(CATEGORY, "WC3PathScalar", UnsignedIntegerDataType.dataType));
        for (JsonElement element : schema.getAsJsonArray("layouts")) {
            JsonObject layout = element.getAsJsonObject();
            String name = layout.get("name").getAsString();
            types.put(name, new StructureDataType(CATEGORY, name, layout.get("length").getAsInt()));
        }
        validate(schema);
        // Build detached complete types before resolving them. Pointers can refer
        // forward to a prefix; no undefined field is assigned guessed semantics.
        int fields = 0;
        for (JsonElement element : schema.getAsJsonArray("layouts")) {
            JsonObject layout = element.getAsJsonObject();
            Structure structure = (Structure)types.get(layout.get("name").getAsString());
            structure.setDescription(schema.get("scope").getAsString());
            for (JsonElement item : layout.getAsJsonArray("fields")) {
                JsonObject field = item.getAsJsonObject();
                DataType datatype = type(field.get("type").getAsString());
                structure.replaceAtOffset(field.get("offset").getAsInt(), datatype, datatype.getLength(),
                    field.get("name").getAsString(), field.get("evidence").getAsString());
                fields++;
            }
        }
        DataTypeManager manager = currentProgram.getDataTypeManager();
        // Refuse to discard another analyst's incompatible annotation on rerun.
        for (String name : types.keySet()) {
            if (!name.startsWith("WC3")) continue;
            DataType existing = manager.getDataType(CATEGORY, name);
            if (existing != null && !existing.isEquivalent(types.get(name)) &&
                !preservesFields(existing, types.get(name)))
                throw new Exception("Preserve incompatible existing type " + existing.getPathName());
        }
        for (String name : types.keySet())
            if (name.startsWith("WC3")) types.put(name, manager.resolve(types.get(name), DataTypeConflictHandler.REPLACE_HANDLER));
        for (JsonElement element : schema.getAsJsonArray("methods")) {
            JsonObject method = element.getAsJsonObject();
            Function function = getFunctionAt(toAddr(method.get("address").getAsString()));
            JsonArray parameters = method.getAsJsonArray("parameters");
            Parameter[] argspec = new Parameter[parameters.size()];
            for (int n = 0; n < parameters.size(); n++) {
                JsonObject parameter = parameters.get(n).getAsJsonObject();
                JsonPrimitive storage = parameter.getAsJsonPrimitive("storage");
                DataType parameterType = type(parameter.get("type").getAsString());
                VariableStorage location = storage.isString()
                    ? new VariableStorage(currentProgram, currentProgram.getRegister(storage.getAsString()))
                    : new VariableStorage(currentProgram, storage.getAsInt(), parameterType.getLength());
                argspec[n] = new ParameterImpl(parameter.get("name").getAsString(),
                    parameterType, location, currentProgram);
            }
            function.setName(method.get("name").getAsString(), SourceType.USER_DEFINED);
            function.setCallingConvention(method.has("convention") ? method.get("convention").getAsString() : "__thiscall");
            function.setReturnType(type(method.get("returns").getAsString()), SourceType.USER_DEFINED);
            function.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.USER_DEFINED, argspec);
            println(function.getEntryPoint() + " " + function.getPrototypeString(true, true));
        }
        if (schema.has("globals")) for (JsonElement element : schema.getAsJsonArray("globals")) {
            JsonObject global = element.getAsJsonObject();
            ghidra.program.model.address.Address address = toAddr(global.get("address").getAsString());
            createLabel(address, global.get("name").getAsString(), true, SourceType.USER_DEFINED);
            clearListing(address, address.add(3));
            createData(address, type(global.get("type").getAsString()));
            setEOLComment(address, global.get("evidence").getAsString());
        }
        // Read the installed database back. Export only our layout/ABI metadata,
        // never binary bytes or private decompiled function bodies.
        JsonObject report = new JsonObject();
        report.addProperty("game_sha256", currentProgram.getExecutableSHA256());
        JsonArray layouts = new JsonArray();
        for (JsonElement element : schema.getAsJsonArray("layouts")) {
            JsonObject layout = element.getAsJsonObject();
            Structure structure = (Structure)types.get(layout.get("name").getAsString());
            if (structure.getLength() != layout.get("length").getAsInt())
                throw new Exception("Installed length differs " + structure.getPathName());
            JsonObject installed = new JsonObject();
            installed.addProperty("path", structure.getPathName());
            installed.addProperty("length", structure.getLength());
            JsonArray installedFields = new JsonArray();
            for (JsonElement item : layout.getAsJsonArray("fields")) {
                JsonObject field = item.getAsJsonObject();
                DataTypeComponent component = structure.getComponentAt(field.get("offset").getAsInt());
                if (component == null || !field.get("name").getAsString().equals(component.getFieldName()))
                    throw new Exception("Installed field differs " + field);
                JsonObject value = new JsonObject();
                value.addProperty("offset", component.getOffset());
                value.addProperty("name", component.getFieldName());
                value.addProperty("type", component.getDataType().getPathName());
                value.addProperty("length", component.getLength());
                installedFields.add(value);
            }
            installed.add("fields", installedFields); layouts.add(installed);
        }
        report.add("layouts", layouts);
        JsonArray methods = new JsonArray();
        for (JsonElement element : schema.getAsJsonArray("methods")) {
            JsonObject method = element.getAsJsonObject();
            Function function = getFunctionAt(toAddr(method.get("address").getAsString()));
            JsonObject installed = new JsonObject();
            installed.addProperty("address", function.getEntryPoint().toString());
            installed.addProperty("prototype", function.getPrototypeString(true, true));
            JsonArray parameters = new JsonArray();
            for (Parameter parameter : function.getParameters()) {
                JsonObject value = new JsonObject();
                value.addProperty("name", parameter.getName());
                value.addProperty("type", parameter.getDataType().getPathName());
                value.addProperty("storage", parameter.getVariableStorage().toString());
                parameters.add(value);
            }
            installed.add("parameters", parameters); methods.add(installed);
        }
        report.add("methods", methods);
        if (schema.has("globals")) report.add("globals", schema.getAsJsonArray("globals"));
        report.addProperty("passed", true);
        if (args.length == 2)
            Files.write(Paths.get(args[1]), new GsonBuilder().setPrettyPrinting().create().toJson(report).getBytes(StandardCharsets.UTF_8));
        println("Persisted " + schema.getAsJsonArray("layouts").size() + " partial layouts, " + fields +
            " verified fields and " + schema.getAsJsonArray("methods").size() + " explicit x86 prototypes.");
    }
}
