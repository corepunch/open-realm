CompatLegacyResult = "unset"

function CompatLegacyVararg(...)
    CompatLegacyResult = tostring(arg.n) .. ":" .. tostring(arg[1]) .. ":" .. tostring(arg[2])
end

CompatLegacyVararg("alpha", "beta")
