native BJassAssert takes boolean condition, string message returns nothing
native UnitId takes string id returns integer
native UnitId2String takes integer id returns string

function run_tests takes nothing returns nothing
    call BJassAssert(UnitId("hfoo") == 1751543663, "native result must use script byte order")
    call BJassAssert(UnitId2String(1751543663) == "hfoo", "native argument must use engine byte order")
endfunction
