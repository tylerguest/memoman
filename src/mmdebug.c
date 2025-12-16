#include "memoman.h"
#include <stdio.h>

extern char* heap;
extern char* current;
extern size_t heap_capacity;
extern tlsf_control_t* tlsf_ctrl;

void print_heap_stats(void) {
#ifdef DEBUG_OUTPUT
  if (heap == NULL) return;
  
  printf("\n=== Heap Statistics ===\n");
  printf("Total heap capacity: %zu bytes (%.2f MB)\n", heap_capacity, heap_capacity / (1024.0 * 1024.0));
  
  if (tlsf_ctrl) {
    printf("TLSF control: %p\n", (void*)tlsf_ctrl);
    printf("Heap start: %p\n", (void*)tlsf_ctrl->heap_start);
    printf("Heap end: %p\n", (void*)tlsf_ctrl->heap_end);
    printf("TLSF capacity: %zu bytes\n", tlsf_ctrl->heap_capacity);
  } else {
    size_t used_heap = current - heap;
    size_t free_heap = heap_capacity - used_heap;
    printf("Used heap space: %zu bytes\n", used_heap);
    printf("Free heap space: %zu bytes\n", free_heap);
    printf("Usage: %.1f%%\n", (double)used_heap / heap_capacity * 100);
  }
  
  printf("====================\n\n");
#endif
}

void print_free_list(void) {
#ifdef DEBUG_OUTPUT
  printf("\n=== TLSF Free Lists ===\n");
  
  if (tlsf_ctrl == NULL) {
    printf("TLSF not initialized\n");
    printf("====================\n\n");
    return;
  }
  
  int total_blocks = 0;
  size_t total_free_size = 0;
  
  printf("FL bitmap: 0x%08X\n", tlsf_ctrl->fl_bitmap);
  
  for (int fl = 0; fl < 30; fl++) {  // TLSF_FLI_MAX
    if (!(tlsf_ctrl->fl_bitmap & (1U << fl))) continue;
    
    printf("\nFirst Level %d (SL bitmap: 0x%08X):\n", fl, tlsf_ctrl->sl_bitmap[fl]);
    
    for (int sl = 0; sl < 32; sl++) {  // TLSF_SLI_COUNT
      tlsf_block_t* current_block = tlsf_ctrl->blocks[fl][sl];
      if (current_block == NULL) continue;
      
      printf("  Second Level %d:\n", sl);
      int count = 0;
      
      while (current_block != NULL) {
        size_t size = current_block->size & (~(size_t)3);  // TLSF_SIZE_MASK
        printf("    Block %d: %zu bytes at %p (flags: %s%s)\n", 
               count++, 
               size, 
               (void*)current_block,
               (current_block->size & 1) ? "FREE " : "USED ",
               (current_block->size & 2) ? "PREV_FREE" : "PREV_USED");
        
        total_blocks++;
        total_free_size += size;
        current_block = current_block->next_free;
      }
    }
  }
  
  printf("\nTotal free blocks: %d\n", total_blocks);
  printf("Total free size: %zu bytes (%.2f MB)\n", total_free_size, total_free_size / (1024.0 * 1024.0));
  printf("====================\n\n");
#endif
}