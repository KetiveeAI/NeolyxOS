/*
 * REOX State Diff Demo
 * Demonstrates reactive state management with diffing
 */

#include <stdio.h>
#include "reox_state.h"

/* Observer callback - called when state changes */
void on_counter_change(int64_t state_id, void* old_val, void* new_val, void* user_data) {
    (void)state_id; (void)user_data;
    int64_t old = *(int64_t*)old_val;
    int64_t new = *(int64_t*)new_val;
    printf("  [Observer] Counter changed: %ld -> %ld (diff: %+ld)\n", old, new, new - old);
}

void on_name_change(int64_t state_id, void* old_val, void* new_val, void* user_data) {
    (void)state_id; (void)user_data;
    const char* old = ((RxString*)old_val)->data;
    const char* new = ((RxString*)new_val)->data;
    printf("  [Observer] Name changed: '%s' -> '%s'\n", old, new);
}

/* Effect - side effect triggered by state changes */
void update_ui_effect(void* user_data) {
    (void)user_data;
    printf("  [Effect] UI needs update!\n");
}

int main(void) {
    printf("=== REOX State Diff Demo ===\n\n");
    
    /* Create reactive state */
    printf("1. Creating reactive state containers...\n");
    RxStateHandle counter = rx_state_int_create(0);
    RxStateHandle username = rx_state_string_create("Guest");
    RxStateHandle dark_mode = rx_state_bool_create(false);
    RxStateHandle volume = rx_state_float_create(0.75);
    
    printf("   counter = %ld\n", rx_state_int_get(counter));
    printf("   username = '%s'\n", rx_state_string_get(username));
    printf("   dark_mode = %s\n", rx_state_bool_get(dark_mode) ? "true" : "false");
    printf("   volume = %.2f\n\n", rx_state_float_get(volume));
    
    /* Add observers */
    printf("2. Adding observers (subscriptions)...\n");
    rx_state_observe(counter, on_counter_change, NULL);
    rx_state_observe(username, on_name_change, NULL);
    printf("   Observers attached.\n\n");
    
    /* Individual state changes */
    printf("3. Making individual state changes...\n");
    rx_state_int_set(counter, 1);
    rx_state_int_set(counter, 5);
    rx_state_string_set(username, "Alice");
    printf("\n");
    
    /* Batch update (transaction) */
    printf("4. Batch update (transaction mode)...\n");
    rx_batch_begin();
    printf("   [Batch] Setting counter = 10\n");
    rx_state_int_set(counter, 10);
    printf("   [Batch] Setting username = 'Bob'\n");
    rx_state_string_set(username, "Bob");
    printf("   [Batch] Setting dark_mode = true\n");
    rx_state_bool_set(dark_mode, true);
    printf("   [Batch] Setting volume = 0.50\n");
    rx_state_float_set(volume, 0.50);
    printf("   [Batch] Committing changes...\n");
    rx_batch_commit();
    printf("\n");
    
    /* Collect diffs manually */
    printf("5. Making more changes and collecting diffs...\n");
    rx_state_int_set(counter, 15);
    rx_state_string_set(username, "Charlie");
    
    rx_diff_batch diffs = rx_collect_diffs();
    printf("   Collected %d diffs:\n", diffs.count);
    for (int i = 0; i < diffs.count; i++) {
        printf("   ");
        rx_print_diff(&diffs.diffs[i]);
    }
    rx_clear_dirty();
    free(diffs.diffs);
    printf("\n");
    
    /* No change if value is same */
    printf("6. Setting same value (should NOT trigger change)...\n");
    int old_count = 0;
    rx_state* s = rx_state_registry;
    while (s) { if (s->dirty) old_count++; s = s->next; }
    
    rx_state_int_set(counter, 15);  /* Same value */
    
    int new_count = 0;
    s = rx_state_registry;
    while (s) { if (s->dirty) new_count++; s = s->next; }
    
    printf("   Dirty states before: %d, after: %d (should be same)\n\n", old_count, new_count);
    
    /* Final state */
    printf("7. Final state values:\n");
    printf("   counter = %ld\n", rx_state_int_get(counter));
    printf("   username = '%s'\n", rx_state_string_get(username));
    printf("   dark_mode = %s\n", rx_state_bool_get(dark_mode) ? "true" : "false");
    printf("   volume = %.2f\n\n", rx_state_float_get(volume));
    
    printf("=== Demo Complete ===\n");
    return 0;
}
