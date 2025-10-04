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

static char heap[1024 * 1024];
static char* current = heap;
static size_t total_allocated = 0;
static int debug_output = 0;
static int free_count = 0;
static block_header_t* free_list = NULL;
static block_header_t* size_classes[8] = {NULL};
static const size_t class_sizes[8] = {16, 32, 64, 128, 256, 512, 1024, 2048};
static void coalesce_free_blocks(void);
static void sort_free_list_by_address(void);

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
    for (int i = 0; i < sizeof(class_sizes) / sizeof(class_sizes[0]); i++) {
        if (size <= class_sizes[i]) {
            return i;
        }
    }
    return -1;
}

// O(1) allocation from size class
static void* pop_from_class(int class) {
    if (class < 0 || class >= 8 || !size_classes[class]) {
        return NULL;
    }
    
    block_header_t* block = size_classes[class];
    size_classes[class] = block->next;
    block->is_free = 0;
    block->next = NULL;
    
    if (debug_output) {
        printf("Fast allocation from class %d: %zu bytes at %p\n",
               class, block->size, header_to_user(block));
    }
    
    return header_to_user(block);
}

// Add block to size class or general free list
static void push_to_class(block_header_t* block) {
    int class = get_size_class(block->size);
    
    if (class >= 0) {
        block->next = size_classes[class];
        size_classes[class] = block;
        
        if (debug_output) {
            printf("Added block to size class %d: %zu bytes\n", class, block->size);
        }
        return;
    }
    
    block->next = free_list;
    free_list = block;
}

// ============================================================================
// ALLOCATION
// ============================================================================

// Fast path (size class) → first-fit search → fresh allocation
void* memomall(size_t size) {
    if (size == 0) return NULL;
    
    size = align_size(size);
    
    // Try size class
    int class = get_size_class(size);
    if (class >= 0) {
        void* ptr = pop_from_class(class);
        if (ptr) {
            return ptr;
        }
    }
    
    // First-fit search
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
                
                if (debug_output) {
                    printf("Split block: used %zu bytes, created free block of %zu bytes\n",
                           size, new_block->size);
                }
            }
            
            current_block->is_free = 0;
            current_block->next = NULL;
            void* user_ptr = header_to_user(current_block);
            
            if (debug_output) {
                printf("Reused block of %zu bytes (requested %zu) at %p\n",
                       current_block->size, size, user_ptr);
            }
            
            return user_ptr;
        }
        
        prev = current_block;
        current_block = current_block->next;
    }
    
    // Fresh allocation
    size_t total_size = sizeof(block_header_t) + size;
    
    if (current + total_size > heap + sizeof(heap)) {
        if (debug_output) printf("Out of memory!\n");
        return NULL;
    }
    
    block_header_t* header = (block_header_t*)current;
    header->size = size;
    header->is_free = 0;
    header->next = NULL;
    
    current += total_size;
    total_allocated += total_size;
    
    void* user_ptr = header_to_user(header);
    
    if (debug_output) {
        printf("Allocated %zu bytes (+ %zu header) at %p\n",
               size, sizeof(block_header_t), user_ptr);
    }
    
    return user_ptr;
}

// Free and trigger deferred coalescing
void memofree(void* ptr) {
    if (ptr == NULL) return;
    
    block_header_t* header = user_to_header(ptr);
    header->is_free = 1;
    
    if (debug_output) {
        printf("Freed block of %zu bytes at %p\n", header->size, ptr);
    }
    
    // Add to appropriate free list
    int class = get_size_class(header->size);
    if (class >= 0) {
        push_to_class(header);
    } else {
        header->next = free_list;
        free_list = header;
    }
    
    // Coalesce periodically
    if (++free_count >= COALESCE_THRESHOLD) {
        coalesce_free_blocks();
        free_count = 0;
    }
}

// ============================================================================
// DEFRAGMENTATION
// ============================================================================

// Merge adjacent free blocks in general free list
static void coalesce_free_blocks(void) {
    if (free_list == NULL) return;
    
    if (debug_output) {
        printf("Attempting to coalesce adjacent blocks...\n");
    }
    
    sort_free_list_by_address();
    
    block_header_t* current = free_list;
    
    while (current != NULL && current->next != NULL) {
        block_header_t* next = current->next;
        
        // Check if physically adjacent
        char* current_end = (char*)header_to_user(current) + current->size;
        char* next_start = (char*)next;
        
        if (current_end == next_start) {
            if (debug_output) {
                printf("Coalescing blocks: %zu + %zu = %zu bytes\n",
                       current->size,
                       sizeof(block_header_t) + next->size,
                       current->size + sizeof(block_header_t) + next->size);
            }
            
            current->size += sizeof(block_header_t) + next->size;
            current->next = next->next;
            continue;
        }
        
        current = current->next;
    }
}

// Bubble sort free list by address (for coalescing)
static void sort_free_list_by_address(void) {
    if (free_list == NULL || free_list->next == NULL) return;
    
    int swapped;
    do {
        swapped = 0;
        block_header_t* prev = NULL;
        block_header_t* current = free_list;
        
        while (current != NULL && current->next != NULL) {
            if (current > current->next) {
                block_header_t* next = current->next;
                
                if (prev == NULL) {
                    free_list = next;
                } else {
                    prev->next = next;
                }
                
                current->next = next->next;
                next->next = current;
                
                swapped = 1;
                prev = next;
            } else {
                prev = current;
                current = current->next;
            }
        }
    } while (swapped);
}

// ============================================================================
// MANAGEMENT
// ============================================================================

void reset_allocator(void) {
    current = heap;
    total_allocated = 0;
    free_list = NULL;
    
    for (int i = 0; i < 8; i++) {
        size_classes[i] = NULL;
    }
    
    if (debug_output) {
        printf("Allocator reset - all memory freed\n");
    }
}

size_t get_total_allocated(void) {
    return total_allocated;
}

size_t get_free_space(void) {
    return sizeof(heap) - (current - heap);
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
            printf("Block %d: %zu bytes at %p\n",
                   count++, current->size, (void*)current);
        }
        current = current->next;
    }
    
    if (count == 0) {
        if (debug_output) printf("Free list is empty\n");
    }
    
    if (debug_output) printf("====================\n\n");
}