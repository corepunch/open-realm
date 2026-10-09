"""Map the pinned WC3 sibling CRT and relocate data; do not replace its code."""
import hashlib
import struct

SHA256 = '86e39b5995af0e042fcdaa85fe2aefd7c9ddc7ad65e6327bd5e7058bc3ab615f'
BASE = 0x50000000


def load_crt(machine, path):
    binary = path.read_bytes()
    if hashlib.sha256(binary).hexdigest() != SHA256:
        raise ValueError('requires the exact shipped Warcraft III msvcr120.dll')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    preferred, size = [struct.unpack_from('<I', binary, opt+n)[0] for n in (28, 56)]
    machine.mem_map(BASE, (size+4095) & ~4095)
    machine.mem_write(BASE, binary[:struct.unpack_from('<I', binary, opt+60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe+6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe+20)[0] + 40*i
        va, count, offset = struct.unpack_from('<III', binary, section+12)
        if count:
            machine.mem_write(BASE+va, binary[offset:offset+count])

    def read(address):
        return struct.unpack('<I', machine.mem_read(address, 4))[0]

    reloc, reloc_size = struct.unpack_from('<II', binary, opt+96+5*8)
    cursor = BASE+reloc
    while cursor < BASE+reloc+reloc_size:
        page, block = struct.unpack('<II', machine.mem_read(cursor, 8))
        if not block:
            break
        assert block >= 8 and block % 2 == 0 and cursor+block <= BASE+reloc+reloc_size
        for item in struct.unpack('<'+'H'*((block-8)//2), machine.mem_read(cursor+8, block-8)):
            if item >> 12 == 3:
                address = BASE+page+(item & 4095)
                machine.mem_write(address, struct.pack('<I', (read(address)+BASE-preferred) & 0xffffffff))
            else:
                assert item >> 12 == 0, 'unsupported shipped CRT relocation'
        cursor += block
    export = BASE+struct.unpack_from('<I', binary, opt+96)[0]
    count, functions, names, ordinals = struct.unpack('<4I', machine.mem_read(export+24, 16))
    resolved = {}
    for i in range(count):
        name = bytes(machine.mem_read(BASE+read(BASE+names+i*4), 128)).split(b'\0', 1)[0].decode('ascii')
        ordinal = struct.unpack('<H', machine.mem_read(BASE+ordinals+i*2, 2))[0]
        resolved[name] = BASE+read(BASE+functions+ordinal*4)
    return dict(base=BASE, sha256=SHA256, exports=resolved,
                scope='Original PE sections and HIGHLOW relocations; DLL startup and uncalled imports are not emulated')
