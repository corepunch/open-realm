"""Bind forced wraps and live identity reuse to unchanged original contracts."""
from .e2e205_contract import ROOT, validate_bindings

CATEGORIES = {
    'active-gates': ('wc3_e2e212.active_gate_routes_cross_stamps_and_counter_then_reuse_saved_owners',
                     ['live-gate-lifetime-same-captures-261004', 'live-gate-lifetime-reuse-captures-261004']),
    'storage': ('wc3_e2e212.retained_search_storage_crosses_native_stamps_without_stale_nodes',
                ['oracle-grid-stamp-wrap-engine', 'oracle-adaptive-stamp-wrap-engine', 'oracle-spatial-storage']),
    'requests': ('wc3_e2e212.wrapped_requests_and_reused_members_keep_live_successors',
                 ['oracle-scheduler', 'oracle-pending-request-clocks', 'live-pending-request-clocks']),
    'gate-pool': ('wc3_e2e212.exhausted_gate_ids_reuse_only_after_release_and_survive_cold_load',
                  ['oracle-waygate-pool-engine', 'live-waygate-pool-captures-261004']),
}
# Forced original execution is O; observed gameplay is L. Never promote one
# into the other just because both are used by the same acceptance category.
EVIDENCE = {identity: ('O' if identity.startswith('oracle-') and identity != 'oracle-spatial-storage' else 'L')
            for _, identities in CATEGORIES.values() for identity in identities}
SOURCES = [
    'tools/ghidra/fixtures/retail-e2e-baseline203-1.27.json',
    'tools/ghidra/fixtures/retail-gate-lifetime-same-live-1.27.json',
    'tools/ghidra/fixtures/retail-gate-lifetime-reuse-live-1.27.json',
    'games/warcraft-3/game/tests/retail_gate_lifetime.h',
    'tools/ghidra/fixtures/retail-fine-stamp-wrap-1.27.json',
    'games/warcraft-3/game/tests/retail_fine_queue.h',
    'tools/ghidra/fixtures/retail-adaptive-stamp-wrap-1.27.json',
    'games/warcraft-3/game/tests/retail_adaptive_wrap.h',
    'tools/ghidra/fixtures/retail-spatial-storage-inputs-1.27.json.gz',
    'tools/ghidra/fixtures/retail-spatial-storage-ghidra-1.27.json',
    'tools/ghidra/fixtures/retail-fine-records-ghidra-1.27.json',
    'games/warcraft-3/game/tests/retail_scheduler_work.h',
    'tools/ghidra/fixtures/retail-waygate-pool-1.27.json',
    'tools/ghidra/fixtures/retail-waygate-pool-live-1.27.json',
]
BOUNDARIES = {
    'injection_msec': 1000,
    'owner_counter': 0xfffffffe,
    'fine_search_stamp': 0xffff,
    'proximity_query': 0x80000000,
    'fine_query': 0x80000000,
    'group_allocator': 0xffffffff,
    'injected_passes_per_repeat': 6,
    'gate_same_native_commits': 514,
    'gate_same_saved_suffix_commits': 2692,
    'gate_reuse_native_commits': 299,
    'gate_reuse_saved_suffix_commits': 757,
}


def validate(spec, manifest, root=ROOT):
    entries = validate_bindings(spec, manifest, CATEGORIES, SOURCES, 'E2E-04.1', root, EVIDENCE)
    if spec['boundaries'] != BOUNDARIES:
        raise ValueError('forced boundary inventory differs')
    if spec['evidence_by_id'] != EVIDENCE:
        raise ValueError('forced and live evidence inventory differs')
    return entries
