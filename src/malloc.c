/**
 * Custom Memory Allocator
 * - Segregated free lists (size classes: 16-2048 bytes) for O(1) allocation
 * - First-fit search for larger sizes
 * - Block splitting when oversized blocks are reused
 * - Deferred coalescing (every 50 frees) to reduce fragmentation
 * - 1MB static heap, 8-byte alignment
 */

#include "malloc.h"
#include <stdio.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define ALIGNMENT 32
#define COALESCE_THRESHOLD 50
#define NUM_SIZE_CLASSES 18

static char heap[1024 * 1024];
static char* current = heap;
static size_t total_allocated = 0;
static block_header_t* free_list = NULL;
static block_header_t* size_classes[18] = {NULL};

// ============================================================================
// UTILITIES
// ============================================================================

// Round up to nearest multiple of ALIGNMENT
static size_t align_size(size_t size) {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

// Convert between header and user pointers
static void* header_to_user(block_header_t* header) {
    return (char*)header + sizeof(block_header_t);
}

static block_header_t* user_to_header(void* ptr) {
    return (block_header_t*)((char*)ptr - sizeof(block_header_t));
}

// ============================================================================
// SIZE CLASSES
// ============================================================================

// Find size class index, or -1 if too large
static int get_size_class(size_t size) {
    if (size <= 16) return 0;
    if (size <= 24) return 1;    
    if (size <= 32) return 2;
    if (size <= 48) return 3;    
    if (size <= 64) return 4;
    if (size <= 96) return 5;    
    if (size <= 128) return 6;
    if (size <= 192) return 7;   
    if (size <= 256) return 8;
    if (size <= 512) return 9;
    if (size <= 1024) return 10;
    if (size <= 2048) return 11;
    if (size <= 4096) return 12;
    if (size <= 8192) return 13;
    if (size <= 16384) return 14;
    if (size <= 32768) return 15;
    if (size <= 65536) return 16;
    if (size <= 131072) return 17;
    return -1;
}

// O(1) allocation from size class
static void* pop_from_class(int class) {
    if (class < 0 || class >= NUM_SIZE_CLASSES || !size_classes[class]) {
        return NULL;
    }
    
    block_header_t* block = size_classes[class];
    size_classes[class] = block->next;
    block->is_free = 0;
    block->next = NULL;
    
    return header_to_user(block);
}

// ============================================================================
// ALLOCATION
// ============================================================================

void* memomall(size_t size) {
    if (size == 0) return NULL;
    
    size = align_size(size);
    
    // Fast path: try size class (O(1))
    int class = get_size_class(size);
    if (class >= 0) {
        void* ptr = pop_from_class(class);
        if (ptr) return ptr;
    }
    
    // Slow path: first-fit search
    block_header_t** prev_ptr = &free_list;
    block_header_t* current_block = free_list;
    
    while (current_block != NULL) {
        if (current_block->size >= size) {
            *prev_ptr = current_block->next;

            // Split if enough space remains
            size_t remaining_size = current_block->size - size;
            size_t min_split_size = sizeof(block_header_t) + ALIGNMENT;
            
            if (remaining_size >= min_split_size) {
                char* split_point = (char*)header_to_user(current_block) + size;
                block_header_t* new_block = (block_header_t*)split_point;
                new_block->size = remaining_size - sizeof(block_header_t);
                new_block->is_free = 1;
                new_block->next = free_list;
                free_list = new_block;
                current_block->size = size;
            }
            
            current_block->is_free = 0;
            current_block->next = NULL;
            return header_to_user(current_block);  // ← ADD THIS RETURN!
        }
        
        prev_ptr = &current_block->next;
        current_block = current_block->next;
    }
    
    // Fresh allocation
    size_t total_size = sizeof(block_header_t) + size;
    
    if (current + total_size > heap + sizeof(heap)) {
        return NULL;  // Out of memory
    }
    
    block_header_t* header = (block_header_t*)current;
    header->size = size;
    header->is_free = 0;
    header->next = NULL;
    
    current += total_size;
    total_allocated += total_size;
    
    return header_to_user(header);
}

void memofree(void* ptr) {
    if (ptr == NULL) return;
    
    block_header_t* header = user_to_header(ptr);
    header->is_free = 1;
    
    int class = get_size_class(header->size);

    if (class >= 0) {
        // Small blocks : fast path, no coalescing needed
        header->next = size_classes[class];
        size_classes[class] = header;
        return;
    }

    // Large blocks: check for adjacent free blocks
    char* block_end = (char*)header + sizeof(block_header_t) + header->size;

    // Try to merge with next block if it's free and adjacent
    block_header_t* next = (block_header_t*)block_end;
    if ((char*)next < heap + sizeof(heap) && next->is_free) {
        // Remove next from free list first
        block_header_t* curr = free_list;
        block_header_t* prev = NULL;

        while (curr != NULL && curr != next) {
            prev = curr;
            curr = curr->next;
        }

        if (curr == next) {
            if (prev) prev->next = next->next;
            else free_list = next->next;
            
            header->size += sizeof(block_header_t) + next->size;
        }
    }

    // Insert into free list (LIFO)
    header->next = free_list;
    free_list = header;
}

// ============================================================================
// MANAGEMENT
// ============================================================================

size_t get_total_allocated(void) { return total_allocated; }
size_t get_free_space(void) { return sizeof(heap) - (current - heap); }
block_header_t* get_free_list(void) { return free_list; }

void reset_allocator(void) {
    current = heap;
    total_allocated = 0;
    free_list = NULL;
    
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        size_classes[i] = NULL;
    }
}

void print_heap_stats(void) {
#ifdef DEBUG_OUTPUT
    size_t used_heap = current - heap;
    size_t free_heap = sizeof(heap) - used_heap;
    printf("\n=== Heap Statistics ===\n");
    printf("Total heap size: %zu bytes (%.2f MB)\n",
           sizeof(heap), sizeof(heap) / (1024.0 * 1024.0));
    printf("Used heap space: %zu bytes\n", used_heap);
    printf("Free heap space: %zu bytes\n", free_heap);
    printf("Usage: %.1f%%\n",
           (double)used_heap / sizeof(heap) * 100);
    printf("====================\n\n");
#endif
}

void print_free_list(void) {
#ifdef DEBUG_OUTPUT
    printf("\n=== Free List ===\n");
    block_header_t* current_block = free_list;
    int count = 0;
    while (current_block != NULL) {
        printf("Block %d: %zu bytes at %p\n", count++, current_block->size, (void*)current_block);
        current_block = current_block->next;
    }
    if (count == 0) {
        printf("Free list is empty\n");
    }
    printf("====================\n\n");
#endif
}
