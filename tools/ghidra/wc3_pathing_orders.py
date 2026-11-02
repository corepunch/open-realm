"""Execute the registered current-order native on supplied existing VM backing."""
import struct


def current_order(machine, context):
    """Preserve observer state; verify the full native's actual cdecl boundary."""
    from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX,
        UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESI,
        UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_EFLAGS)
    registers = (UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX,
        UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI,
        UC_X86_REG_EBP, UC_X86_REG_EFLAGS)
    saved = {register: machine.reg_read(register) for register in registers}
    references = bytes(machine.mem_read(context['unit'] + 4, 4))
    exception_chain = bytes(machine.mem_read(0, 4))
    machine.mem_write(context['stack'], struct.pack('<II', context['stop'], context['handle']))
    machine.reg_write(UC_X86_REG_ESP, context['stack'])
    machine.emu_start(0x6f2039d0, context['stop'], count=2000000)
    result = machine.reg_read(UC_X86_REG_EAX)
    assert machine.reg_read(UC_X86_REG_EIP) == context['stop'], 'current-order instruction limit'
    assert machine.reg_read(UC_X86_REG_ESP) == context['stack'] + 4, 'current-order cdecl stack'
    for register in (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP):
        assert machine.reg_read(register) == saved[register], 'current-order callee-saved register'
    assert bytes(machine.mem_read(context['unit'] + 4, 4)) == references, 'current-order references'
    assert bytes(machine.mem_read(0, 4)) == exception_chain, 'current-order exception chain'
    for register, value in saved.items():
        machine.reg_write(register, value)
    return result
