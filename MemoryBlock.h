#ifndef MEMORYBLOCK_H
#define MEMORYBLOCK_H
#include <iostream>

struct MemoryBlock
{
    int process_id;    // -1 if free, otherwise stores the process ID
    int start_address; // Starting memory address of this block
    int size;          // Size of this memory block
    MemoryBlock *next; // Pointer to the next block in the list

    // Constructor for easier initialization
    MemoryBlock(int id, int start, int sz)
    {
        process_id = id;
        start_address = start;
        size = sz;
        next = nullptr;
    }
};

#endif // MEMORYBLOCK_H