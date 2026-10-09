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

/* A category cannot silently pass when its underlying journey is empty. */
static void e2e205_journey(void (*run)(void)) {
    int asserts=test_asserts,failures=test_failures;
    run();
    T_ASSERT(test_asserts>asserts); T_EQ(test_failures,failures);
}

TEST(wc3_e2e205, selected_independent_and_passage_formations) {
    FOR_LOOP(repeat,2) {
        e2e205_journey(wc3_movement_formation170_public_selection_and_independent_orders_match_retail_fn);
        e2e205_journey(wc3_movement_formation169_selected_public_passage_matches_complete_retail_journey_fn);
    }
}

TEST(wc3_e2e205, mixed_and_disabled_crowds_with_saved_continuation) {
    FOR_LOOP(repeat,2) {
        e2e205_journey(wc3_repulsion_crowds_mixed_owner_radius_rank_blocked_endpoint_matches_capture_fn);
        e2e205_journey(wc3_repulsion_crowds_disabled_ground_control_matches_retry_and_stop_capture_fn);
        e2e205_journey(wc3_repulsion_crowds_mixed_crowd_saved_mid_order_matches_complete_suffix_fn);
        e2e205_journey(wc3_repulsion_crowds_ground_control_saved_mid_order_matches_complete_suffix_fn);
    }
}

TEST(wc3_e2e205, group_gate_skip_disconnection_and_chained_activation) {
    FOR_LOOP(repeat,2) {
        e2e205_journey(wc3_movement_retail_group_gate_traversal_skip_and_only_edge_failure_match_all_motion_fn);
        e2e205_journey(wc3_movement_retail_chained_gates_all_activation_combinations_match_all_motion_fn);
    }
}
#endif
