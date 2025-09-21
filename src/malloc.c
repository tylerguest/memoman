#include "malloc.h"
#include <stdio.h>

#define ALIGNMENT 8 // for performance

// bump allocator
static char heap[1024 * 1024]; // 1MB heap
static char* current = heap;
static size_t total_allocated = 0;

static block_header_t* free_list = NULL;

static void coalesce_free_blocks(void);
static void sort_free_list_by_address(void);

static size_t align_size(size_t size) {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

static void* header_to_user(block_header_t* header) {
    return (char*)header + sizeof(block_header_t);
}

static block_header_t* user_to_header(void* ptr) {
    return (block_header_t*)((char*)ptr - sizeof(block_header_t));
}

void* my_malloc(size_t size) {
    if (size == 0) return NULL;

    size = align_size(size);

    block_header_t* prev = NULL;
    block_header_t* current_block = free_list;

    while (current_block != NULL) {
        if (current_block->size >= size) {
            if (prev == NULL) {
                free_list = current_block->next;
            } else {
                prev->next = current_block->next;
            }

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

                printf("Split block: used %zu bytes, created free block of %zu bytes\n",
                       size, new_block->size);
            }

            current_block->is_free = 0;
            current_block->next = NULL;

            void* user_ptr = header_to_user(current_block);
            printf("Reused block of %zu bytes (requested %zu) at %p\n",
                   current_block->size, size, user_ptr);

            return user_ptr;
        }

        prev = current_block;
        current_block = current_block->next;
    }

    size_t total_size = sizeof(block_header_t) + size;

    // check for space
    if (current + total_size > heap + sizeof(heap)) {
        printf("Out of memory!\n");
        return NULL;
    }

    block_header_t* header = (block_header_t*)current;
    header->size = size;
    header->is_free = 0;
    header->next = NULL;

    current += total_size;
    total_allocated += total_size;

    void* user_ptr = header_to_user(header);
    printf("Allocated %zu bytes (+ %zu header) at %p\n",
           size, sizeof(block_header_t), user_ptr);

    return user_ptr;
}

void my_free(void* ptr) {
    if (ptr == NULL) return;

    block_header_t* header = user_to_header(ptr);
    header->is_free = 1;

    printf("Freed block of %zu bytes at %p\n", header->size, ptr);

    header->next = free_list;
    free_list = header;

    coalesce_free_blocks();
}

static void coalesce_free_blocks(void) {
    if (free_list == NULL) return;

    printf("Attempting to coalesce adjacent blocks...\n");

    sort_free_list_by_address();

    block_header_t* current = free_list;

    while (current != NULL && current->next != NULL) {
        block_header_t* next = current->next;
        
        char* current_end = (char*)header_to_user(current) + current->size;
        char* next_start = (char*)next;

        if (current_end == next_start) {
            printf("Coalescing blocks: %zu + %zu = %zu bytes\n",
                    current->size,
                    sizeof(block_header_t) + next->size,
                    current->size + sizeof(block_header_t) + next->size);
            current->size += sizeof(block_header_t) + next->size;
            current->next = next->next;
            continue;
        }
        current = current->next;
    }
}

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
                } else { prev->next = next; }
                
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

void print_heap_stats(void) {
    printf("\n=== Heap Statistics ===\n");
    printf("Total heap size: %zu bytes (%.2f MB)\n",
           sizeof(heap), sizeof(heap) / (1024.0 * 1024.0));
    printf("Total allocated: %zu bytes\n", total_allocated);
    printf("Free space: %zu bytes\n", sizeof(heap) - total_allocated);
    printf("Usage: %.1f%%\n",
           (double)total_allocated / sizeof(heap) * 100);
    printf("====================\n\n");
}

void print_free_list(void) {
    printf("\n=== Free List ===\n");
    block_header_t* current = free_list;
    int count = 0;

    while (current != NULL) {
        printf("Block %d: %zu bytes at %p\n",
                count++, current->size, (void*)current);
        current = current->next;
    }

    if (count == 0) { printf("Free list is empty\n"); }
    printf("====================\n\n");
}

size_t get_total_allocated(void) {
    return total_allocated;
}

size_t get_free_space(void) {
    return sizeof(heap) - total_allocated;
}

void reset_allocator(void) {
    current = heap;
    total_allocated = 0;
    
    printf("Allocator reset - all memory freed\n");
}