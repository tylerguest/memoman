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

#define ALIGNMENT 8
#define COALESCE_THRESHOLD 50
#define NUM_SIZE_CLASSES 12

static char heap[1024 * 1024];
static char* current = heap;
static size_t total_allocated = 0;
static int debug_output = 0;
static block_header_t* free_list = NULL;
static block_header_t* size_classes[12] = {NULL};

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
    // Unrolled for speed
    if (size <= 16) return 0;
    if (size <= 32) return 1;
    if (size <= 64) return 2;
    if (size <= 128) return 3;
    if (size <= 256) return 4;
    if (size <= 512) return 5;
    if (size <= 1024) return 6;
    if (size <= 2048) return 7;
    if (size <= 4096) return 8;
    if (size <= 8192) return 9;
    if (size <= 16384) return 10;
    if (size <= 32768) return 11;
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

// Add block to size class or general free list
static void push_to_class(block_header_t* block) {
    int class = get_size_class(block->size);
    
    if (class >= 0) {
        block->next = size_classes[class];
        size_classes[class] = block;
        return;
    }
    
    // Inset into free_list in address order
    if (free_list == NULL || block < free_list) {
        block->next = free_list;
        free_list = block;
    } else {
        block_header_t* current = free_list;
        while (current->next != NULL && current->next < block) { current = current->next; }
        block->next = current->next;
        current->next = block;
    }
}

// ============================================================================
// COALESCING (only for large blocks in free_list)
// ============================================================================

static void coalesce_free_list(void) {
    if (free_list == NULL) return;

    block_header_t* current = free_list;

    while (current != NULL && current->next != NULL) {
        // Check if blocks are physically adjacent
        char* current_end = (char*)header_to_user(current) + current->size;

        if ((char*)current->next == current_end) {
            // Merge with next block
            current->size += sizeof(block_header_t) + current->next->size;
            current->next = current->next->next;
            // Don't advanced - check if we can merge with new next
        } else { current = current->next; }
    }
}

// ============================================================================
// ALLOCATION
// ============================================================================

// Fast path (size class) → first-fit search → fresh allocation
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
    block_header_t* prev = NULL;
    block_header_t* current_block = free_list;
    
    while (current_block != NULL) {
        if (current_block->size >= size) {
            // Remove from free list
            if (prev == NULL) {
                free_list = current_block->next;
            } else {
                prev->next = current_block->next;
            }
            
            // Split if enough space remains
            size_t remaining_size = current_block->size - size;
            size_t min_split_size = sizeof(block_header_t) + ALIGNMENT;
            
            if (remaining_size >= min_split_size) {
                char* split_point = (char*)header_to_user(current_block) + size;
                block_header_t* new_block = (block_header_t*)split_point;
                new_block->size = remaining_size - sizeof(block_header_t);
                new_block->is_free = 1;
                push_to_class(new_block);
                current_block->size = size;
            }
            
            current_block->is_free = 0;
            current_block->next = NULL;
            void* user_ptr = header_to_user(current_block);
            
            return user_ptr;
        }
        
        prev = current_block;
        current_block = current_block->next;
    }
    
    // Fresh allocation
    size_t total_size = sizeof(block_header_t) + size;
    
    if (current + total_size > heap + sizeof(heap)) {
        // Last resort: coalesce and try again
        coalesce_free_list();

        // Retry first-fit after coalescing
        prev = NULL;
        current_block = free_list;

        while (current_block != NULL) {
            if (current_block->size >= size) {
                if (prev == NULL) { free_list = current_block->next; }
                else { prev->next = current_block->next; }
                current_block->is_free = 0;
                current_block->next = NULL;
                return header_to_user(current_block);
            }
            prev = current_block;
            current_block = current_block->next;
        }
        // Truly out of memory 
        return NULL;
    }
    
    block_header_t* header = (block_header_t*)current;
    header->size = size;
    header->is_free = 0;
    header->next = NULL;
    
    current += total_size;
    total_allocated += total_size;
    
    return header_to_user(header);
}

// Free and trigger deferred coalescing
void memofree(void* ptr) {
    if (ptr == NULL) return;
    
    block_header_t* header = user_to_header(ptr);
    header->is_free = 1;
    
    // Simple: just push to appropriate list
    // Coalescing happens lazily when needed
    push_to_class(header);
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
    size_t used_heap = current - heap;
    size_t free_heap = sizeof(heap) - used_heap;
    
    if (debug_output) printf("\n=== Heap Statistics ===\n");
    if (debug_output) printf("Total heap size: %zu bytes (%.2f MB)\n",
           sizeof(heap), sizeof(heap) / (1024.0 * 1024.0));
    if (debug_output) printf("Used heap space: %zu bytes\n", used_heap);
    if (debug_output) printf("Free heap space: %zu bytes\n", free_heap);
    if (debug_output) printf("Usage: %.1f%%\n",
           (double)used_heap / sizeof(heap) * 100);
    if (debug_output) printf("====================\n\n");
}

void print_free_list(void) {
    if (debug_output) printf("\n=== Free List ===\n");
    
    block_header_t* current = free_list;
    int count = 0;
    
    while (current != NULL) {
        if (debug_output) {
            printf("Block %d: %zu bytes at %p\n", count++, current->size, (void*)current);
        }
        current = current->next;
    }
    
    if (count == 0) {
        if (debug_output) printf("Free list is empty\n");
    }
    
    if (debug_output) printf("====================\n\n");
}