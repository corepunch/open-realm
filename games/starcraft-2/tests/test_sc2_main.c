/*
 * test_sc2_main.c - StarCraft II fixture test runner.
 */

#include <stdio.h>

#include "test_framework.h"

int _tests_run    = 0;
int _tests_failed = 0;

void run_sc2_map_tests(void);
void run_sc2_layout_tests(void);

int main(void) {
    printf("=== OpenWarcraft3 StarCraft II Tests ===\n\n");

    printf("[SC2 map archives / parser]\n");
    run_sc2_map_tests();
    printf("\n");

    printf("[SC2 layout XML parser]\n");
    run_sc2_layout_tests();
    printf("\n");

    TEST_RESULTS();
}
