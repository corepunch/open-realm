#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

/* These production-path journeys are compiled into the same test game module.
 * Keep their original literal expectations and observers; repeat each complete
 * fresh/save journey as one cross-feature acceptance category. */
static void wc3_movement_formation169_selected_public_passage_matches_complete_retail_journey_fn(void);
static void wc3_movement_formation170_public_selection_and_independent_orders_match_retail_fn(void);
static void wc3_repulsion_crowds_mixed_owner_radius_rank_blocked_endpoint_matches_capture_fn(void);
static void wc3_repulsion_crowds_disabled_ground_control_matches_retry_and_stop_capture_fn(void);
static void wc3_repulsion_crowds_mixed_crowd_saved_mid_order_matches_complete_suffix_fn(void);
static void wc3_repulsion_crowds_ground_control_saved_mid_order_matches_complete_suffix_fn(void);
static void wc3_movement_retail_group_gate_traversal_skip_and_only_edge_failure_match_all_motion_fn(void);
static void wc3_movement_retail_chained_gates_all_activation_combinations_match_all_motion_fn(void);

static void reset_test_state(void);

/* A category cannot silently pass when its underlying journey is empty.
 * Nested journeys need the same VM/trigger reset as registered test cases. */
static void e2e_journey(void (*run)(void)) {
    int asserts=test_asserts,failures=test_failures;
    reset_test_state();
    run();
    T_ASSERT(test_asserts>asserts); T_EQ(test_failures,failures);
}

TEST(wc3_e2e205, selected_independent_and_passage_formations) {
    FOR_LOOP(repeat,2) {
        e2e_journey(wc3_movement_formation170_public_selection_and_independent_orders_match_retail_fn);
        e2e_journey(wc3_movement_formation169_selected_public_passage_matches_complete_retail_journey_fn);
    }
}

TEST(wc3_e2e205, mixed_and_disabled_crowds_with_saved_continuation) {
    FOR_LOOP(repeat,2) {
        e2e_journey(wc3_repulsion_crowds_mixed_owner_radius_rank_blocked_endpoint_matches_capture_fn);
        e2e_journey(wc3_repulsion_crowds_disabled_ground_control_matches_retry_and_stop_capture_fn);
        e2e_journey(wc3_repulsion_crowds_mixed_crowd_saved_mid_order_matches_complete_suffix_fn);
        e2e_journey(wc3_repulsion_crowds_ground_control_saved_mid_order_matches_complete_suffix_fn);
    }
}

TEST(wc3_e2e205, group_gate_skip_disconnection_and_chained_activation) {
    FOR_LOOP(repeat,2) {
        e2e_journey(wc3_movement_retail_group_gate_traversal_skip_and_only_edge_failure_match_all_motion_fn);
        e2e_journey(wc3_movement_retail_chained_gates_all_activation_combinations_match_all_motion_fn);
    }
}

static void wc3_movement_public_composed_yield_matches_retail_owner_streams_fn(void);
static void wc3_movement_public_dynamic_blockers_match_retail_owner_streams_fn(void);
static void wc3_movement_delayed164_public_walled_smart_approach_matches_retail_fn(void);
static void wc3_movement_delayed164_denied_group_commits_stop_and_refresh_before_recovery_fn(void);
static void wc3_movement_delayed164_group_discards_premature_sample_and_saves_cadence_fn(void);
static void wc3_movement_target166_public_fog_follow_matches_retail_and_cold_save_fn(void);
static void wc3_movement_target166_hidden_approach_ends_at_cached_arrival_fn(void);
static void wc3_movement_target166_hidden_persistent_follow_ends_at_cached_arrival_fn(void);

TEST(wc3_e2e206, dynamic_edits_yielding_and_saved_route_handoffs) {
    FOR_LOOP(repeat,2) {
        e2e_journey(wc3_movement_public_composed_yield_matches_retail_owner_streams_fn);
        e2e_journey(wc3_movement_public_dynamic_blockers_match_retail_owner_streams_fn);
    }
}

TEST(wc3_e2e206, pursuit_refresh_visibility_and_saved_owner_state) {
    FOR_LOOP(repeat,2) {
        e2e_journey(wc3_movement_delayed164_public_walled_smart_approach_matches_retail_fn);
        e2e_journey(wc3_movement_delayed164_denied_group_commits_stop_and_refresh_before_recovery_fn);
        e2e_journey(wc3_movement_delayed164_group_discards_premature_sample_and_saves_cadence_fn);
        e2e_journey(wc3_movement_target166_public_fog_follow_matches_retail_and_cold_save_fn);
        e2e_journey(wc3_movement_target166_hidden_approach_ends_at_cached_arrival_fn);
        e2e_journey(wc3_movement_target166_hidden_persistent_follow_ends_at_cached_arrival_fn);
    }
}

/* Keep the literal original assertions inside their production-path journeys.
 * These categories compose SCHED-04 and ORDER-06 with public successors. */
static void wc3_movement_retail_coarse_and_fine_contention_matches_captured_owner_window_fn(void);
static void wc3_movement_public_coarse_contention_saves_pending_two_player_orders_fn(void);
static void wc3_cancel_public_stop_and_replacement_match_turning_and_saved_suffix_fn(void);
static void wc3_cancel_waiting_and_active_replacements_retain_survivor_fifo_and_storage_fn(void);
static void wc3_interrupt_pending_removal_retains_all_order_shapes_and_saved_head_fn(void);
static void wc3_interrupt_actual_spell_completion_replaces_owner_and_releases_removed_peers_once_fn(void);
static void wc3_patrol_orders_queued_move_runs_at_leg_end_then_return_leg_resumes_fn(void);
static void wc3_patrol_orders_saved_rotated_pair_retains_activation_origin_and_fifo_order_fn(void);
static void wc3_patrol_orders_combat_resumes_same_leg_before_starting_queued_move_fn(void);
static void wc3_movement_recovery197_blocked_completion_dispatches_saved_successor_fn(void);

TEST(wc3_e2e210, player_contention_retains_ordered_admission_and_saved_queues) {
    bool responsive=level.move_fine_responsive;
    FOR_LOOP(repeat,2) {
        e2e_journey(wc3_movement_retail_coarse_and_fine_contention_matches_captured_owner_window_fn);
        e2e_journey(wc3_movement_public_coarse_contention_saves_pending_two_player_orders_fn);
    }
    level.move_fine_responsive=responsive;
}

TEST(wc3_e2e210, cancellation_and_completion_release_preserve_successor_ownership) {
    FOR_LOOP(repeat,2) {
        e2e_journey(wc3_cancel_public_stop_and_replacement_match_turning_and_saved_suffix_fn);
        e2e_journey(wc3_cancel_waiting_and_active_replacements_retain_survivor_fifo_and_storage_fn);
        e2e_journey(wc3_interrupt_pending_removal_retains_all_order_shapes_and_saved_head_fn);
        e2e_journey(wc3_interrupt_actual_spell_completion_replaces_owner_and_releases_removed_peers_once_fn);
    }
}

TEST(wc3_e2e210, queued_orders_follow_arrival_combat_and_blocked_completion) {
    FOR_LOOP(repeat,2) {
        e2e_journey(wc3_patrol_orders_queued_move_runs_at_leg_end_then_return_leg_resumes_fn);
        e2e_journey(wc3_patrol_orders_saved_rotated_pair_retains_activation_origin_and_fifo_order_fn);
        e2e_journey(wc3_patrol_orders_combat_resumes_same_leg_before_starting_queued_move_fn);
        e2e_journey(wc3_movement_recovery197_blocked_completion_dispatches_saved_successor_fn);
    }
}
/* E2E-04.1 retains the labelled native wrap aliases: repair does not erase
 * object stamps. Acceptance must not "fix" that historical retail behavior. */
static void pathfinding_fine_lookup_epoch_wrap_and_reuse_fn(void);
static void pathfinding_retained_fine_storage_matches_native_stamp_wrap_nodes_and_routes_fn(void);
static void pathfinding_owned_adaptive_requests_reuse_all_lanes_sizes_and_partial_node_states_fn(void);
static void wc3_fine_spatial_real_search_publishes_head_metadata_and_reuses_it_across_stamp_wrap_fn(void);
static void wc3_fine_spatial_labelled_stamp_repair_retains_native_unlink_alias_fn(void);
static void wc3_proximity_labelled_forced_stamp_repair_retains_native_aliases_fn(void);
static void wc3_movement_scheduler_work_wrap_matches_original_charge_and_clean_admission_fn(void);
static void wc3_movement_group_id_allocation_scales_with_requests_and_restores_reserved_ids_fn(void);
static void wc3_movement_group_move_identity_survives_counter_wrap_and_unit_reuse_fn(void);
static void wc3_movement_group_survivor_reorder_after_member_reuse_reaches_new_goal_fn(void);
static void wc3_unit_releases_different_deadlines_and_unsigned_serial_wrap_choose_the_next_receiver_fn(void);
static void wc3_unit_releases_cold_restore_after_wrap_keeps_rebased_release_and_borrowed_clock_fn(void);
static void wc3_unit_releases_cancellation_preserves_heap_and_does_not_release_reused_slot_fn(void);
static void wc3_waygate_allocation_and_exhaustion_survive_save_and_reject_duplicate_ownership_fn(void);
static void wc3_waygate_exhausted_gate_stays_unallocated_until_ability_recreation_fn(void);

TEST(wc3_e2e212, retained_search_storage_crosses_native_stamps_without_stale_nodes) {
    FOR_LOOP(repeat,2) {
        e2e_journey(pathfinding_fine_lookup_epoch_wrap_and_reuse_fn);
        e2e_journey(pathfinding_retained_fine_storage_matches_native_stamp_wrap_nodes_and_routes_fn);
        e2e_journey(pathfinding_owned_adaptive_requests_reuse_all_lanes_sizes_and_partial_node_states_fn);
        e2e_journey(wc3_fine_spatial_real_search_publishes_head_metadata_and_reuses_it_across_stamp_wrap_fn);
        e2e_journey(wc3_fine_spatial_labelled_stamp_repair_retains_native_unlink_alias_fn);
        e2e_journey(wc3_proximity_labelled_forced_stamp_repair_retains_native_aliases_fn);
    }
}

TEST(wc3_e2e212, wrapped_requests_and_reused_members_keep_live_successors) {
    FOR_LOOP(repeat,2) {
        e2e_journey(wc3_movement_scheduler_work_wrap_matches_original_charge_and_clean_admission_fn);
        e2e_journey(wc3_movement_group_id_allocation_scales_with_requests_and_restores_reserved_ids_fn);
        e2e_journey(wc3_movement_group_move_identity_survives_counter_wrap_and_unit_reuse_fn);
        e2e_journey(wc3_movement_group_survivor_reorder_after_member_reuse_reaches_new_goal_fn);
        e2e_journey(wc3_unit_releases_different_deadlines_and_unsigned_serial_wrap_choose_the_next_receiver_fn);
        e2e_journey(wc3_unit_releases_cold_restore_after_wrap_keeps_rebased_release_and_borrowed_clock_fn);
        e2e_journey(wc3_unit_releases_cancellation_preserves_heap_and_does_not_release_reused_slot_fn);
    }
}

TEST(wc3_e2e212, exhausted_gate_ids_reuse_only_after_release_and_survive_cold_load) {
    FOR_LOOP(repeat,2) {
        e2e_journey(wc3_waygate_allocation_and_exhaustion_survive_save_and_reject_duplicate_ownership_fn);
        e2e_journey(wc3_waygate_exhausted_gate_stays_unallocated_until_ability_recreation_fn);
    }
}
#endif
