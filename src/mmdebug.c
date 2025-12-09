#include "memoman.h"
#include <stdio.h>

extern char* heap;
extern char* current;
extern size_t heap_capacity;
extern block_header_t* free_list[];

void print_heap_stats(void) {
#ifdef DEBUG_OUTPUT
  if (heap == NULL) return;
  size_t used_heap = current - heap;
  size_t free_heap = heap_capacity - used_heap;
  printf("\n=== Heap Statistics ===\n");
  printf("Total heap size: %zu bytes (%.2f MB)\n", heap_capacity, heap_capacity / (1024.0 * 1024.0));
  printf("Used heap space: %zu bytes\n", used_heap);
  printf("Free heap space: %zu bytes\n", free_heap);
  printf("Usage: %.1f%%\n", (double)used_heap / heap_capacity * 100);
  printf("====================\n\n");
#endif
}

void print_free_list(void) {
#ifdef DEBUG_OUTPUT
  printf("\n=== Free List ===\n");
  for (int i = 0; i < 8; i++) {  // NUM_FREE_LISTS
    printf("Free List %d:\n", i);
    block_header_t* current_block = free_list[i];
    int count = 0;
    while (current_block != NULL) {
      printf("  Block %d: %zu bytes at %p\n", count++, current_block->size, (void*)current_block);
      current_block = current_block->next;
    }
    if (count == 0) printf("  Empty\n");
  }
  printf("====================\n\n");
#endif
}