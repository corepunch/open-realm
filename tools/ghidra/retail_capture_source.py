"""Resolve historical capture inputs without changing frozen evidence pins.

A captured builder may evolve independently. Shared verifier archives additionally
require the current code to differ only in this source-pin routing: original-code
checks and every other executable statement must remain AST-identical.
"""
import ast
import hashlib
from pathlib import Path
import re

SHARED_BUILDER = 'tools/frida/make_wc3_pathfinding_map.py'

class SourcePinSyntax(ast.NodeTransformer):
    def visit_Module(self,node):
        node.body=[n for n in node.body if not (isinstance(n,ast.ImportFrom) and
            n.module=='retail_capture_source' and len(n.names)==1 and
            n.names[0].name=='matches_capture_source' and n.names[0].asname is None)]
        return self.generic_visit(node)

    def visit_UnaryOp(self,node):
        call=node.operand
        if (isinstance(node.op,ast.Not) and isinstance(call,ast.Call) and
            isinstance(call.func,ast.Name) and call.func.id=='matches_capture_source' and
            len(call.args)==3 and not call.keywords and
            isinstance(call.args[0],ast.Name) and call.args[0].id=='ROOT'):
            return ast.Compare(left=ast.Call(func=ast.Name(id='digest',ctx=ast.Load()),
                args=[ast.BinOp(left=call.args[0],op=ast.Div(),right=call.args[1])],keywords=[]),
                ops=[ast.NotEq()],comparators=[call.args[2]])
        return self.generic_visit(node)

def verifier_code(source):
    return ast.dump(SourcePinSyntax().visit(ast.parse(source)))

def capture_source(root,relative,expected):
    root=Path(root);live=root/relative
    def matches(path):
        return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest()==expected
    if matches(live):return live
    if not re.fullmatch(r'[0-9a-f]{64}',expected):
        raise ValueError('invalid capture source hash')
    builder=relative==SHARED_BUILDER
    verifier=re.fullmatch(r'tools/ghidra/verify_wc3_pathing_[a-z0-9_]+\.py',relative)
    if not builder and not verifier:raise ValueError('changed evidence source: '+relative)
    parent='tools/frida/research' if builder else 'tools/ghidra/research'
    archived=root/parent/'source-archive'/expected/Path(relative).name
    if not matches(archived):raise ValueError('missing or changed historical source: '+relative)
    if verifier:
        if not live.is_file() or verifier_code(live.read_bytes())!=verifier_code(archived.read_bytes()):
            raise ValueError('original-code verifier changed: '+relative)
    return archived

def matches_capture_source(root,relative,expected):
    try:
        capture_source(root,relative,expected)
        return True
    except (ValueError,SyntaxError):return False
