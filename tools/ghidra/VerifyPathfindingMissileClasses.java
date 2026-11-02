// Verify the original MSVC RTTI behind the non-unit path producers, without mutation.
// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class VerifyPathfindingMissileClasses extends GhidraScript {
    static final String[][] ROWS = {
        {"6fb7dcc8","CArtilleryLine","6f6cf5e0","CArtilleryLine_StartPointPath"},
        {"6fb7de08","CMissileLine","6f6cfe00","CMissileLine_StartPointPath"},
        {"6fb06c54","CBulletPath","6f6d30c0","CMissile_StartPointPath"},
        {"6fb06d20","CMissilePath","6f6d3190","CMissile_StartTargetPath"},
        {"6fb0fc4c","CMissileSpiderAttack","6f6d3190","CMissile_StartTargetPath"}
    };
    public void run() throws Exception {
        if (!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equalsIgnoreCase(currentProgram.getExecutableSHA256()))
            throw new IllegalStateException("Unexpected binary generation");
        for (String[] row : ROWS) {
            Address vtable=toAddr(row[0]);
            Address locator=toAddr(Integer.toUnsignedLong(getInt(vtable.subtract(4))));
            Address descriptor=toAddr(Integer.toUnsignedLong(getInt(locator.add(12))));
            StringBuilder name=new StringBuilder();
            for(int i=0;i<128;i++) {
                byte value=getByte(descriptor.add(8+i));
                if(value==0) break;
                name.append((char)(value&255));
            }
            if(!name.toString().equals(".?AV"+row[1]+"@@"))
                throw new IllegalStateException("Unexpected RTTI: "+name);
            Function function=getFunctionAt(toAddr(row[2]));
            if(function==null || !function.getName().equals(row[3]))
                throw new IllegalStateException("Producer map missing: "+row[2]);
            String comment=function.getComment();
            if(comment==null || !comment.contains("Payoff104"))
                throw new IllegalStateException("Evidence annotation missing: "+row[2]);
            println(row[0]+" -> "+locator+" -> "+descriptor+" "+name+"; "+row[3]);
        }
        println("Verified five RTTI identities and four persisted producer mappings; no mutation.");
    }
}
