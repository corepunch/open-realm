"""Bind dynamic edits and pursuit to the original intermediate-state contracts."""
from .e2e205_contract import ROOT, fingerprint, validate_bindings

CATEGORIES = {
    'dynamic': ('wc3_e2e206.dynamic_edits_yielding_and_saved_route_handoffs',
                ['live-composed-dynamic-blockers-and-yields']),
    'pursuit': ('wc3_e2e206.pursuit_refresh_visibility_and_saved_owner_state',
                ['live-target-destination-delays-and-denied-visits',
                 'live-target-visibility-cached-arrival']),
}
SOURCES = [
    'tools/ghidra/fixtures/retail-e2e-baseline203-1.27.json',
    'tools/ghidra/fixtures/retail-composed-blocker163-1.27.json',
    'games/warcraft-3/game/tests/retail_yield_composed163.h',
    'games/warcraft-3/game/tests/retail_dynamic_blocker163.h',
    'tools/ghidra/fixtures/retail-target-delay164-1.27.json',
    'games/warcraft-3/game/tests/retail_target_approach164.h',
    'tools/ghidra/fixtures/retail-target-visibility166-1.27.json',
    'games/warcraft-3/game/tests/retail_target_fog166.h',
]


def validate(spec, manifest, root=ROOT):
    return validate_bindings(spec, manifest, CATEGORIES, SOURCES, 'E2E-01.2', root)
