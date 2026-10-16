"""Bind active-order lifetime compositions to unchanged original witnesses."""
from .e2e205_contract import ROOT, validate_bindings

CATEGORIES = {
    'map-lifetime': ('wc3_e2e213.real_map_callback_pool_pressure_matches_uninterrupted_and_cold_continuations',
                     ['oracle-map-lifetime']),
    'spatial-load': ('wc3_e2e213.rebuilt_spatial_order_keeps_native_differences_and_resumed_motion',
                     ['oracle-spatial-save']),
    'callbacks': ('wc3_e2e213.removal_and_spell_callbacks_retire_old_owners_and_keep_successors',
                   ['oracle-interrupt209']),
    'pool-pressure': ('wc3_e2e213.queue_and_shared_pool_pressure_keep_saved_payloads_and_reuse',
                       ['oracle-cold-order-pool-growth', 'live-point-task-pool-lifetimes']),
}
EVIDENCE = {identity: ('O' if identity == 'oracle-cold-order-pool-growth' else 'L')
            for _, identities in CATEGORIES.values() for identity in identities}
SOURCES = [
    'tools/ghidra/fixtures/retail-e2e-baseline203-1.27.json',
    'tools/ghidra/fixtures/retail-map-lifetime-inputs-1.27.json.gz',
    'tools/ghidra/fixtures/retail-map-lifetime-release-1.27.json',
    'tools/ghidra/fixtures/research/MAP-06.1-expected.json',
    'tools/ghidra/fixtures/retail-spatial-save-inputs-1.27.json.gz',
    'tools/ghidra/fixtures/retail-spatial-load-insertion-1.27.json',
    'tools/ghidra/fixtures/research/MAP-06.2-expected.json',
    'tools/ghidra/fixtures/retail-interrupt209-1.27.json',
    'tools/ghidra/fixtures/retail-interrupt209-1.27.jsonl.gz',
    'games/warcraft-3/game/tests/retail_interrupt209_scene.h',
    'tools/ghidra/fixtures/retail-order-pool-growth192-1.27.json',
    'tools/ghidra/fixtures/retail-point-pool-lifetimes192-1.27.json',
    'games/warcraft-3/tests/resources-src/Maps/Test/PathingReload.w3m',
    'games/warcraft-3/tests/resources-src/Maps/Test/PathingChangeLevel.w3m',
]
BOUNDARIES = {
    'map_shapes': 2,
    'actor_collision': 16,
    'cold_hierarchy_collision': 8,
    'variants_per_shape': 4,
    'frames_per_variant': 1400,
    'frame_msec': 10,
    'queued_victim_orders': 129,
    'callback_replacements': 129,
    'callback_msec': 500,
    'cold_before_callback_msec': 250,
    'cold_after_callback_msec': 550,
    'fresh_spatial_request_serials': 2,
    'retired_world_observations_per_repeat': 12,
    'motion_words_per_frame': 23,
    'unchanged_ownership_words_per_frame': 6,
}


def validate(spec, manifest, root=ROOT):
    entries = validate_bindings(spec, manifest, CATEGORIES, SOURCES, 'E2E-04.2', root, EVIDENCE)
    if spec['boundaries'] != BOUNDARIES:
        raise ValueError('lifetime boundary inventory differs')
    if spec['evidence_by_id'] != EVIDENCE:
        raise ValueError('original and live evidence inventory differs')
    return entries
