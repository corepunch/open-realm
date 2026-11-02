// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Numeric259Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f9249d0","JassLexer_ReadToken","Payoff259 NUM-01.15: original numeric DFA has16 reachable prefix states, all256 outgoing bytes recovered; 5139 first-token decisions repeat exactly. Longest numeric prefix: decimal[1-9][0-9]*, octal0[0-7]*, dollar hex,0[xX] hex and digit-dot decimal reals. 078 splits07 then8; 089.5 remains real. Bare0x selects0; bare$ or dot is punctuation. No exponent/hex-float/nan/inf number rule. Original numeric controls finish the parent through conversion; live source compilation independently checks named identifiers and12 invalid expressions. Stops at924bc8 in the bounded DFA harness are not full compiler evidence."},
 {"6f9253e0","JassLexer_ClassifyIdentifier","Payoff259: nan,inf,NaN,Infinity are ordinary DFA action41 identifiers, not host-strtod numbers. This helper resolves declaration/variable/function context to tokens132/130/133; do not require every identifier to return107. Two read-only actual source captures observe nan/inf/Infinity declarations and references, compile successfully twice per map and feed378.5 into a public Move; unhooked control preserves marker values."},
 {"6f922a50","JassParser_CompileSource","Payoff259: thiscall ECX parser, stack4 lexer retained at parser44. Original lexer word24 copied into parser1c at922ba9/922bac and922c2a/922c2d, then shifted through parser20 at923eb5/923eb8 onto the semantic stack at922b42/922b48. Numeric words are consumed by actual source compiler, not inferred from public native arguments. Two valid map repeats each return0/error0 twice;12 invalid-source pairs observe925500 diagnostics. Invalid parser return is deliberately not claimed when error handling destroys observer."},
 {"6f925500","JassLexer_ReportCompileError","Payoff259: cdecl(lexer,error_code), plain RET. Adjusts syntax error2 for semicolon/context, sets lexer8c=1 at92558c and invokes configured callback84 at925596 with context88,lineb8,columnb4 and ECX=a0. Twelve invalid expressions repeat original diagnostics with no probe completion. Read-only observer records entry diagnostic; unavailable RPC after diagnostic is explicitly retained, never represented as a successful parser return."},
 {"6f925210","JassLexer_ParseDecimalInteger","Payoff259: numeric DFA action42 decimal nonzero prefix; complete original parent controls preserve lexer24 token108 words. Grammar choice precedes this converter; host strtol/strtod classification must not admit078 as a real."},
 {"6f925490","JassLexer_ParseOctalInteger","Payoff259: action43 handles0[0-7]* only. Original078 selects07, value7; 08 and09 select0 before the following decimal token. A following point after the full digit sequence instead chooses real action46, including089.5."},
 {"6f925350","JassLexer_ParseHexInteger","Payoff259: action44 dollar-hex uses prefix1; action45 0x/0X-hex uses prefix2. At least one hex digit is required. Bare0xG selects octal0 then identifierxG; bare$G selects punctuation$ then identifierG. Original full parent conversion controls retain token108/value words."},
 {"6f925260","JassLexer_ParseRealLiteral","Payoff259: action46 accepts digits-dot-digits with at least one digit overall; no exponent or hex float grammar. .5,1.,01.2,089.5 are real; nan/inf remain identifiers. Existing scalar conversion expectations are unchanged. The new engine lexer/classifier selects this already verified converter only after whole-token grammar acceptance."},
 };
 public void run()throws Exception {
  if(getScriptArgs().length!=1)throw new Exception("new output required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  var dm=currentProgram.getDataTypeManager();
  var category=new CategoryPath("/WarcraftIII/Pathfinding127");
  var t=(Structure)dm.getDataType(category,"WC3JassParserNumericPrefix");
  if(t==null){t=new StructureDataType(category,"WC3JassParserNumericPrefix",0x48);t=(Structure)dm.addDataType(t,DataTypeConflictHandler.DEFAULT_HANDLER);}
  t.replaceAtOffset(0x18,new PointerDataType(UnsignedIntegerDataType.dataType,4),4,"semantic_stack_cursor","922b3e/922b42/922b48; Payoff259");
  t.replaceAtOffset(0x1c,UnsignedIntegerDataType.dataType,4,"lookahead_value","Copied from lexer24 at922ba9/922bac; Payoff259");
  t.replaceAtOffset(0x20,UnsignedIntegerDataType.dataType,4,"shift_value","923eb5/923eb8 ->922b45/922b48; Payoff259");
  t.replaceAtOffset(0x28,IntegerDataType.dataType,4,"lookahead_token","9249d0 result at922b8f; Payoff259");
  t.replaceAtOffset(0x44,new PointerDataType(VoidDataType.dataType,4),4,"lexer","Stack4 argument retained922a81; Payoff259");
  StringBuilder out=new StringBuilder();
  out.append("TYPE ").append(t).append('\n');
  for(String[] row:NOTES){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";old=old.replace("then shifted onto semantic stack at923eb5.","then shifted through parser20 at923eb5/923eb8 onto the semantic stack at922b42/922b48.");if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("COMMENT ").append(f.getComment()).append("\n");
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
