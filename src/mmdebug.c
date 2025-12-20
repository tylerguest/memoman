#include "memoman.h"
#include <stdio.h>
#include <inttypes.h>

/* Extern globals from memoman.c */
extern char* heap;
extern char* current;
extern size_t heap_capacity;
extern tlsf_control_t* tlsf_ctrl;

/* * Helper: Prints a 32-bit integer as a binary string 
 * formatted for easy reading (groups of 4).
 * Example: ....1010
 */
__attribute__((unused))
static void print_bitmap_visual(uint32_t bitmap) {
    printf(" [");
    // Iterate from MSB (31) to LSB (0)
    for (int i = 31; i >= 0; i--) {
        if (i % 4 == 3 && i != 31) printf(" "); // Spacing every 4 bits
        printf("%c", (bitmap & (1U << i)) ? '1' : '.');
    }
    printf("] (0x%08X)\n", bitmap);
}

/* * Helper: Calculates and prints the size range for a specific FL/SL bin.
 * This helps verify if a block is in the correct list.
 */
__attribute__((unused))
static void print_sl_range(int fl, int sl) {
    // 1. Calculate the base size of this FL (2^fl * offset)
    // Note: fl is the index (0..31), real FL is fl + TLSF_FLI_OFFSET
    size_t min_size = (1 << (fl + TLSF_FLI_OFFSET));
    
    // 2. Calculate the size of each SL slice
    size_t range_size = min_size; // The FL spans from 2^k to 2^(k+1)
    size_t step = range_size / TLSF_SLI_COUNT;
    
    // 3. Calculate lower and upper bounds for this specific SL
    size_t lower = min_size + (sl * step);
    size_t upper = lower + step - 1;
    
    printf("  SL %2d Range: [%4zu - %4zu bytes]:\n", sl, lower, upper);
}

void print_heap_stats(void) {
#ifdef DEBUG_OUTPUT
  if (heap == NULL) return;
  
  printf("\n=== Heap Statistics ===\n");
  printf("Total heap capacity: %zu bytes (%.2f MB)\n", heap_capacity, heap_capacity / (1024.0 * 1024.0));
  
  if (tlsf_ctrl) {
    printf("TLSF control:   %p\n", (void*)tlsf_ctrl);
    printf("Heap start:     %p\n", (void*)tlsf_ctrl->heap_start);
    printf("Heap end:       %p\n", (void*)tlsf_ctrl->heap_end);
    printf("TLSF capacity:  %zu bytes\n", tlsf_ctrl->heap_capacity);
  } else {
    size_t used_heap = current - heap;
    size_t free_heap = heap_capacity - used_heap;
    printf("Used heap space: %zu bytes\n", used_heap);
    printf("Free heap space: %zu bytes\n", free_heap);
    printf("Usage: %.1f%%\n", (double)used_heap / heap_capacity * 100);
  }
  
  printf("=======================\n");
#endif
}

void print_free_list(void) {
#ifdef DEBUG_OUTPUT
    if (!tlsf_ctrl) {
        printf("[TLSF] Not Initialized.\n");
        return;
    }

    printf("\n=== TLSF Heap Visualizer ===\n");
    printf("FL Bitmap:");
    print_bitmap_visual(tlsf_ctrl->fl_bitmap);

    int total_blocks = 0;
    size_t total_free_bytes = 0;

    // Iterate First Level (FL)
    for (int fl = 0; fl < TLSF_FLI_MAX; fl++) {
        // Skip empty FL lists
        if (!(tlsf_ctrl->fl_bitmap & (1U << fl))) continue;

        printf("\nFL %d (Block size base: %d bytes)\n", 
               fl, 1 << (fl + TLSF_FLI_OFFSET));
        printf("  SL Bitmap:");
        print_bitmap_visual(tlsf_ctrl->sl_bitmap[fl]);

        // Iterate Second Level (SL)
        for (int sl = 0; sl < TLSF_SLI_COUNT; sl++) {
            // Skip empty SL lists (optimization)
            if (!(tlsf_ctrl->sl_bitmap[fl] & (1U << sl))) continue;
            
            tlsf_block_t* curr = tlsf_ctrl->blocks[fl][sl];
            
            if (curr) {
                print_sl_range(fl, sl);
                
                int list_depth = 0;
                while (curr) {
                    // SAFETY: Infinite loop detection (Linked list corruption check)
                    if (list_depth++ > 100) {
                        printf("    [CRITICAL] Infinite loop detected in free list!\n");
                        break;
                    }

                    size_t size = curr->size & TLSF_SIZE_MASK;
                    int is_free = curr->size & TLSF_BLOCK_FREE;
                    int prev_free = curr->size & TLSF_PREV_FREE;

                    printf("    -> [%p] Size: %-5zu | %s | %s\n", 
                           (void*)curr, size, 
                           is_free ? "FREE" : "USED(BUG!)", 
                           prev_free ? "PREV_FREE" : "PREV_USED");
                    
                    total_blocks++;
                    total_free_bytes += size;
                    curr = curr->next_free;
                }
            }
        }
    }

    printf("\n[Stats] Total Free Blocks: %d | Total Free Bytes: %zu (%.2f MB)\n", 
           total_blocks, total_free_bytes, total_free_bytes / (1024.0 * 1024.0));
    printf("============================\n\n");
#endif
}