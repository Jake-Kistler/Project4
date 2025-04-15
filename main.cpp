#include <fstream>
#include <iostream>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include "MemoryBlock.h"

/*
 * Project 3 asks us to change how jobs are loaded into memory
 * Previously, we loaded directly into the ReadyQueue if memory was available
 * Now, we've been asked to create a NewJobQueue and when there is enough memory
 * we will load them into the readyQueue In the case of not having enough
 * memory to use we have to options: 1) Wait 2) Coalesce memory (more on that
 * below) We then continue like normal
 *
 * Lets say we have 1000 memory cells
 * Process 1 starts at 0 and has a size of 200 so now there are 800 free blocks
 * to work with process 2 starts at 350 and has a size of 300 process 5 starts
 * at 750 and has a size of 250
 *
 * so 0-200 is used there is a gap from 200 - 350 (150 free slots)
 * process 2 starts at 350 and takes 300 cells up so 350-650 is occupied now
 * Then there is another free block from 650 to 750.
 * we then load process 3 from 750 - 1000 and are now out of memory
 *
 * Say we have a new process 4 arrives and needs 180 memory cells to run,
 * We don't have this space in a single cohesive block of memory and will need
 * to Combine the free blocks into one unit to load process 4 and it would need
 * to wait for memory to free up
 *
 *
 * TO COALESCE:
 * Find our unassigned blocks of memory and combine them into one unit
 * so tbe block after process 1 but before process 2 is free and so is
 * the block after process 2 but before process 3
 *
 * 150 [gap after process 1] + 100 [gap after process 2] = 250 units of free
 * space If we make this a cohesive block we can load process 4
 *
 * NEW STRUCTURES:
 * new_job_queue<PCB> // this will store the jobs and load them into the
 * readyQueue only when there is enough memory to do so Dynamic memory
 * allocation handled / monitored by a linked list, each node has the following:
 *   i) int Process_id // the id of the process -1 if free
 *   ii) int start_address // where the block starts
 *   iii) int block_size // size of the block
 *
 */

// ================================
// Function Prototypes
// ================================
struct PCB;
struct MemoryBlock;

int allocate_memory(MemoryBlock *&memory_head, int process_id, int size);
void free_memory(MemoryBlock *&memory_head, int *main_memory, int process_id);
void coalesce_memory(MemoryBlock *&memory_head);
void load_jobs_to_memory(std::queue<PCB> &new_job_queue, std::queue<int> &ready_queue, int *main_memory, MemoryBlock *&memory_head);
void execute_cpu(int start_address, int *main_memory, MemoryBlock *&memory_head, std::queue<PCB> &new_job_queue, std::queue<int> &ready_queue);
void check_io_waiting_queue(std::queue<int> &ready_queue, int *main_memory);
bool allocate_segments(MemoryBlock *&memory_head, int total_memory_needed, int &segment_table_start_address, std::vector<std::pair<int, int>> &segment_table_entries);
int translate_logical_to_physical(int logical_address, const PCB &process, int *main_memory);
void free_memory(MemoryBlock *&memory_head, int start_address, int size);

struct PCB
{
    int process_id;
    std::string state;
    int program_counter;
    int instruction_base;
    int data_base;
    int memory_limit;
    int cpu_cycles_used;
    int register_value;
    int max_memory_needed;
    int main_memory_base;
};

int global_clock = 0;
bool timeout_occurred = false;
bool memory_freed = false;
std::queue<std::tuple<PCB, int, int, int>> io_waiting_queue; // (process, start_address, param_offset, wait_time)
int context_switch_time, cpu_allocated;

std::unordered_map<int, int> opcode_params = {
    {1, 2}, // Compute: iterations, cycles
    {2, 1}, // Print: cycles
    {3, 2}, // Store: value, address
    {4, 1}  // Load: address
};

// Key: state, Value: encoding
std::unordered_map<std::string, int> state_encoding = 
{
    {"NEW", 1},
    {"READY", 2},
    {"RUNNING", 3},
     {"TERMINATED", 4},
     {"IOWAITING", 5}
    };

// Key: processID, Value: instructions
std::unordered_map<int, std::vector<std::vector<int>>> process_instructions;

// Key: processID, Value: value of the global clock the first time the process
// entered running state
std::unordered_map<int, int> process_start_times;

// Helps keep track of each process's current parameter offset
// Key: processID, Value: param_offset
std::unordered_map<int, int> param_offsets;

int main(int argc, char **argv)
{
    int max_memory, num_processes;
    std::queue<PCB> new_job_queue;
    std::queue<int> ready_queue;
    int *main_memory;

    std::cin >> max_memory >> cpu_allocated >> context_switch_time >> num_processes;
    main_memory = new int[max_memory];

    MemoryBlock *memory_head = new MemoryBlock(-1, 0, max_memory);

    // Initialize main_memory to -1
    for (int i = 0; i < max_memory; i++)
    {
        main_memory[i] = -1;
    }

    // Read process data
    for (int i = 0; i < num_processes; i++)
    {
        PCB process;
        int num_instructions;

        std::cin >> process.process_id >> process.max_memory_needed >>
            num_instructions;

        process.state = "NEW";
        process.memory_limit = process.max_memory_needed;
        process.program_counter = 0;
        process.cpu_cycles_used = 0;
        process.register_value = 0;

        std::vector<std::vector<int>> instructions;

        for (int j = 0; j < num_instructions; j++)
        {
            std::vector<int> current_instruction;
            int opcode;
            std::cin >> opcode;
            current_instruction.push_back(opcode);

            int num_params = opcode_params[opcode];

            for (int k = 0; k < num_params; k++)
            {
                int param;
                std::cin >> param;
                current_instruction.push_back(param);
            }
            instructions.push_back(current_instruction);
        }

        // Map the instructions to process_instructions
        process_instructions[process.process_id] = instructions;
        new_job_queue.push(process);
    }

    // Load initial jobs
    load_jobs_to_memory(new_job_queue, ready_queue, main_memory, memory_head);

    // dump main_memory contents
    for (int i = 0; i < max_memory; i++)
    {
        std::cout << i << " : " << main_memory[i] << std::endl;
    }

    //  scheduling loop
    while (!ready_queue.empty() || !io_waiting_queue.empty())
    {
        if (!ready_queue.empty())
        {
            int start_address = ready_queue.front();
            ready_queue.pop();

            execute_cpu(start_address, main_memory, memory_head, new_job_queue, ready_queue);

            // If a timeout occurred, re-add the process
            if (timeout_occurred)
            {
                ready_queue.push(start_address);
                timeout_occurred = false;
            }
        }
        else
        {
            // No jobs in ready_queue, but some in io_waiting_queue
            global_clock += context_switch_time;
        }

        if (memory_freed)
        {
            load_jobs_to_memory(new_job_queue, ready_queue, main_memory,
                                memory_head);
            memory_freed = false;
        }

        check_io_waiting_queue(ready_queue, main_memory);
    }

    global_clock += context_switch_time;
    std::cout << "Total CPU time used: " << global_clock << "." << std::endl;

    delete[] main_memory;
    return 0;
}

// ================================
// Definitions
// ================================

int allocate_memory(MemoryBlock *&memory_head, int process_id, int size)
{
    MemoryBlock *current = memory_head;
    MemoryBlock *prev = nullptr;

    while (current)
    {
        // Found a free block big enough
        if (current->process_id == -1 && current->size >= size)
        {
            int allocated_address = current->start_address;
            if (current->size == size)
            {
                current->process_id = process_id;
            }
            else
            {
                // Split
                MemoryBlock *new_block =
                    new MemoryBlock(process_id, current->start_address, size);
                new_block->next = current;

                if (prev)
                {
                    prev->next = new_block;
                }
                else
                {
                    memory_head = new_block;
                }

                current->start_address += size;
                current->size -= size;
                return allocated_address;
            }
            return allocated_address;
        }
        prev = current;
        current = current->next;
    }
    return -1; // No block big enough
}

void free_memory(MemoryBlock *&memory_head, int *main_memory, int process_id)
{
    MemoryBlock *current = memory_head;
    while (current)
    {
        if (current->process_id == process_id)
        {
            // Free the memory block
            for (int i = current->start_address;
                 i < current->start_address + current->size; i++)
            {
                main_memory[i] = -1;
            }

            current->process_id = -1;
            return; // Done
        }
        current = current->next;
    }
}

void coalesce_memory(MemoryBlock *&memory_head)
{
    MemoryBlock *current = memory_head;
    while (current && current->next)
    {
        MemoryBlock *next = current->next;

        // Merge only if both and free AND adjacent in memory
        if (current->process_id == -1 && next->process_id == -1 && current->start_address + current->size == next->start_address)
        {
            current->size += next->size;
            current->next = next->next;
            delete next;
            continue;
        }

        current = current->next;
    }
}

void load_jobs_to_memory(std::queue<PCB> &new_job_queue, std::queue<int> &ready_queue,int *main_memory,MemoryBlock *&memory_head)
{
    int new_job_queue_size = static_cast<int>(new_job_queue.size());
    std::queue<PCB> temp_queue;

    for (int i = 0; i < new_job_queue_size; i++)
    {
        PCB process = new_job_queue.front();
        new_job_queue.pop();

        int segment_table_start_address;
        std::vector<std::pair<int, int>> segment_table_entries;

        // needed to move this before I try anything 
        coalesce_memory(memory_head);

        bool success = allocate_segments(memory_head,process.max_memory_needed,segment_table_start_address,segment_table_entries);

        if (!success)
        {
            std::cout << "Insufficient memory for Process "<< process.process_id << ". Attempting memory coalescing." << std::endl;

            

            success = allocate_segments(memory_head,process.max_memory_needed,segment_table_start_address,segment_table_entries);

            if (!success)
            {
                std::cout << "Process " << process.process_id << " waiting in NewJobQueue due to insufficient memory." << std::endl;

                temp_queue.push(process);

                for (int j = i + 1; j < new_job_queue_size; j++)
                {
                    temp_queue.push(new_job_queue.front());
                    new_job_queue.pop();
                }

                break;
            }
        }

        // Print expected message
        std::cout << "Process " << process.process_id << " loaded with segment table stored at physical address " << segment_table_start_address << std::endl;

        // === Update PCB fields ===
        process.main_memory_base = segment_table_start_address;
        process.instruction_base = segment_table_entries[0].first;
        process.data_base = segment_table_entries[1].first;

        // === Store PCB metadata at the segment table start address ===
        main_memory[segment_table_start_address + 0] = process.process_id;
        main_memory[segment_table_start_address + 1] = state_encoding[process.state];
        main_memory[segment_table_start_address + 2] = process.program_counter;
        main_memory[segment_table_start_address + 3] = process.instruction_base;
        main_memory[segment_table_start_address + 4] = process.data_base;
        main_memory[segment_table_start_address + 5] = process.memory_limit;
        main_memory[segment_table_start_address + 6] = process.cpu_cycles_used;
        main_memory[segment_table_start_address + 7] = process.register_value;
        main_memory[segment_table_start_address + 8] = process.max_memory_needed;
        main_memory[segment_table_start_address + 9] = process.main_memory_base;

        // === Store segment table size + entries ===
        int segment_table_size = static_cast<int>(segment_table_entries.size());
        main_memory[segment_table_start_address + 10] = segment_table_size;

        int table_write_index = segment_table_start_address + 11;
        for (const auto &entry : segment_table_entries)
        {
            main_memory[table_write_index++] = entry.first;
            main_memory[table_write_index++] = entry.second;
        }

        // === Store instructions ===
        std::vector<std::vector<int>> instrs = process_instructions[process.process_id];
        int write_index = process.instruction_base;

        // First store opcodes
        for (const auto &instr : instrs)
        {
            main_memory[write_index++] = instr[0];
        }

        // Then store parameters
        for (const auto &instr : instrs)
        {
            for (int k = 1; k < static_cast<int>(instr.size()); k++)
            {
                main_memory[write_index++] = instr[k];
            }
        }

        // Add to ready queue
        ready_queue.push(process.main_memory_base);
    }

    // Return any deferred jobs back to the new job queue
    while (!temp_queue.empty())
    {
        new_job_queue.push(temp_queue.front());
        temp_queue.pop();
    }
}

void execute_cpu(int start_address, int *main_memory, MemoryBlock *&memory_head, std::queue<PCB> &new_job_queue, std::queue<int> &ready_queue)
{
    PCB process;
    int cpu_cycles_this_run = 0;

    process.process_id = main_memory[start_address + 0];
    process.state = "READY";
    main_memory[start_address + 1] = state_encoding[process.state];
    process.program_counter = main_memory[start_address + 2];
    process.instruction_base = main_memory[start_address + 3];
    process.data_base = main_memory[start_address + 4];
    process.memory_limit = main_memory[start_address + 5];
    process.cpu_cycles_used = main_memory[start_address + 6];
    process.register_value = main_memory[start_address + 7];
    process.max_memory_needed = main_memory[start_address + 8];
    process.main_memory_base = main_memory[start_address + 9];

    global_clock += context_switch_time;

    if (process.program_counter == 0)
    {
        process.program_counter = process.instruction_base;
        param_offsets[process.process_id] = 0;
        process_start_times[process.process_id] = global_clock;
    }

    process.state = "RUNNING";
    main_memory[start_address + 1] = state_encoding[process.state];
    main_memory[start_address + 2] = process.program_counter;

    std::cout << "Process " << process.process_id << " has moved to Running." << std::endl;

    int param_offset = param_offsets[process.process_id];

    while (process.program_counter < process.data_base &&
           cpu_cycles_this_run < cpu_allocated)
    {
        int opcode = main_memory[process.program_counter];

        switch (opcode)
        {
        case 1: // Compute
        {
            int iterations = main_memory[process.data_base + param_offset];
            int cycles = main_memory[process.data_base + param_offset + 1];
            std::cout << "compute" << std::endl;

            process.cpu_cycles_used += cycles;
            main_memory[start_address + 6] = process.cpu_cycles_used;
            cpu_cycles_this_run += cycles;
            global_clock += cycles;
            break;
        }
        case 2: // Print (I/O)
        {
            int cycles = main_memory[process.data_base + param_offset];
            std::cout << "Process " << process.process_id << " issued an IOInterrupt and moved to the IOWaitingQueue." << std::endl;

            io_waiting_queue.push({process, start_address, cycles, global_clock});
            process.state = "IOWAITING";
            main_memory[start_address + 1] = state_encoding[process.state];
            return;
        }
        case 3: // Store
        {
            int value = main_memory[process.data_base + param_offset];
            int logical_address = main_memory[process.data_base + param_offset + 1];

            process.register_value = value;
            main_memory[start_address + 7] = value;

            int physical = translate_logical_to_physical(logical_address, process, main_memory);
            if (physical != -1)
            {
                main_memory[physical] = value;
                std::cout << "stored" << std::endl;
            }
            else
            {
                std::cout << "store error!" << std::endl;
                break;
            }

            process.cpu_cycles_used++;
            main_memory[start_address + 6] = process.cpu_cycles_used;
            cpu_cycles_this_run++;
            global_clock++;
            break;
        }
        case 4: // Load
        {
            int logical_address = main_memory[process.data_base + param_offset];
            int physical = translate_logical_to_physical(logical_address, process, main_memory);

            if (physical != -1)
            {
                process.register_value = main_memory[physical];
                main_memory[start_address + 7] = process.register_value;
                std::cout << "loaded" << std::endl;
            }
            else
            {
                std::cout << "load error!" << std::endl;
                break;
            }

            process.cpu_cycles_used++;
            main_memory[start_address + 6] = process.cpu_cycles_used;
            cpu_cycles_this_run++;
            global_clock++;
            break;
        }
        default:
            break;
        }

        process.program_counter++;
        main_memory[start_address + 2] = process.program_counter;
        param_offset += opcode_params[opcode];
        param_offsets[process.process_id] = param_offset;

        if (cpu_cycles_this_run >= cpu_allocated && process.program_counter < process.data_base)
        {
            std::cout << "Process " << process.process_id << " has a TimeOUT interrupt and is moved to the ReadyQueue." << std::endl;
            process.state = "READY";
            main_memory[start_address + 1] = state_encoding[process.state];
            timeout_occurred = true;
            return;
        }
    }

    // === Process terminated ===
    process.program_counter = process.instruction_base - 1;
    process.state = "TERMINATED";
    main_memory[start_address + 2] = process.program_counter;
    main_memory[start_address + 1] = state_encoding[process.state];

    // === Free segment table ===
    free_memory(memory_head, process.main_memory_base, 13);

    // === Free all segments from segment table ===
    int segment_table_start = process.main_memory_base + 10;
    int segment_count = main_memory[process.main_memory_base + 10];

    for (int i = 0; i < segment_count; ++i)
    {
        int seg_start = main_memory[segment_table_start + 2 * i];
        int seg_size = main_memory[segment_table_start + 2 * i + 1];
        free_memory(memory_head, seg_start, seg_size);
    }

    memory_freed = true;

    int total_execution_time = global_clock - process_start_times[process.process_id];

    std::cout << "Process ID: " << process.process_id << std::endl;
    std::cout << "State: " << process.state << std::endl;
    std::cout << "Program Counter: " << process.program_counter << std::endl;
    std::cout << "Instruction Base: " << process.instruction_base << std::endl;
    std::cout << "Data Base: " << process.data_base << std::endl;
    std::cout << "Memory Limit: " << process.memory_limit << std::endl;
    std::cout << "CPU Cycles Used: " << process.cpu_cycles_used << std::endl;
    std::cout << "Register Value: " << process.register_value << std::endl;
    std::cout << "Max Memory Needed: " << process.max_memory_needed << std::endl;
    std::cout << "Main Memory Base: " << process.main_memory_base << std::endl;
    std::cout << "Total CPU Cycles Consumed: " << total_execution_time << std::endl;

    std::cout << "Process " << process.process_id << " terminated. Entered running state at: " << process_start_times[process.process_id] << ". Terminated at: " << global_clock << ". Total Execution Time: " << total_execution_time << "." << std::endl;

    std::cout << "Process " << process.process_id << " terminated and freed memory blocks." << std::endl;
}

void check_io_waiting_queue(std::queue<int> &ready_queue, int *main_memory)
{
    int queue_size = static_cast<int>(io_waiting_queue.size());
    for (int i = 0; i < queue_size; i++)
    {
        std::tuple<PCB, int, int, int> front = io_waiting_queue.front();
        PCB process = std::get<0>(front);
        int start_address = std::get<1>(front);
        int wait_time = std::get<2>(front);
        int time_entered_io = std::get<3>(front);

        io_waiting_queue.pop();

        if (global_clock - time_entered_io >= wait_time)
        {
            int param_offset = param_offsets[process.process_id];
            int cycles = main_memory[process.data_base + param_offset];

            // Execute print operation
            std::cout << "print" << std::endl;
            process.cpu_cycles_used += cycles;
            main_memory[start_address + 6] = process.cpu_cycles_used;

            // Increment program counter and paramOffset for next instruction
            process.program_counter++;
            main_memory[start_address + 2] = process.program_counter;
            param_offset += opcode_params[2];
            param_offsets[process.process_id] = param_offset;

            // Reset state to READY and context switch
            process.state = "READY";
            main_memory[start_address + 1] = state_encoding[process.state];

            std::cout << "Process " << process.process_id<< " completed I/O and is moved to the ReadyQueue."<< std::endl;

            ready_queue.push(start_address);
        }
        else
        {
            io_waiting_queue.push(std::make_tuple(process, start_address, wait_time, time_entered_io));
        }
    }
}

void free_memory(MemoryBlock *&memory_head, int start_address, int size)
{
    MemoryBlock *new_block = new MemoryBlock(-1, start_address, size);

    // If the list is empty, insert as head
    if (!memory_head)
    {
        memory_head = new_block;
        return;
    }

    MemoryBlock *current = memory_head;
    MemoryBlock *prev = nullptr;

    // Find the correct spot to insert based on start address
    while (current && current->start_address < start_address)
    {
        prev = current;
        current = current->next;
    }

    // Insert new_block into the list
    new_block->next = current;

    if (prev)
    {
        prev->next = new_block;
    }
    else
    {
        memory_head = new_block;
    }

    // Now try to coalesce (merge) with adjacent free blocks
    coalesce_memory(memory_head);
}

bool allocate_segments(MemoryBlock *&memory_head, int total_memory_needed, int &segment_table_start_address, std::vector<std::pair<int, int>> &segment_table_entries)
{
    // Step 1: Coalesce memory before allocation attempt
    coalesce_memory(memory_head);

    // Step 2: Find a block big enough to store the segment table (13 integers)
    MemoryBlock *current = memory_head;
    MemoryBlock *prev = nullptr;
    segment_table_start_address = -1;
    bool found_segment_table_block = false;

    while (current)
    {
        if (current->process_id == -1 && current->size >= 23)
        {
            // Allocate space for segment table at start of block
            segment_table_start_address = current->start_address;

            // Adjust current block to reflect used space
            current->start_address += 23;
            current->size -= 23;

            // If this completely consumed the block
            if (current->size == 0)
            {
                if (prev)
                    prev->next = current->next;
                else
                    memory_head = current->next;
                delete current;
                current = (prev ? prev->next : memory_head);
            }

            // Insert a new block for the segment table (marked as reserved with process_id = -2)
            MemoryBlock *segment_table_block = new MemoryBlock(-2, segment_table_start_address, 13);
            segment_table_block->next = memory_head;
            memory_head = segment_table_block;

            found_segment_table_block = true;
            break;
        }

        prev = current;
        current = current->next;
    }

    if (!found_segment_table_block)
    {
        return false; // Can't allocate space for segment table
    }

    // Step 3: Try to allocate remaining memory for segments
    int remaining = total_memory_needed;
    current = memory_head;
    prev = nullptr;

    while (current && remaining > 0)
    {
        if (current->process_id == -1)
        {
            int alloc_size = std::min(current->size, remaining);
            int alloc_start = current->start_address;

            // Record this segment's allocation
            segment_table_entries.emplace_back(alloc_start, alloc_size);

            // Adjust block
            current->start_address += alloc_size;
            current->size -= alloc_size;
            remaining -= alloc_size;

            // If block is fully used, remove it
            if (current->size == 0)
            {
                if (prev)
                    prev->next = current->next;
                else
                    memory_head = current->next;

                MemoryBlock *to_delete = current;
                current = current->next;
                delete to_delete;
                continue;
            }
        }

        prev = current;
        current = current->next;
    }

    // Not enough memory → clean up and fail
    if (remaining > 0)
    {
        // Free segment table block
        free_memory(memory_head, segment_table_start_address, 13);

        // Free all allocated segment entries
        for (const auto &entry : segment_table_entries)
        {
            free_memory(memory_head, entry.first, entry.second);
        }

        segment_table_entries.clear();
        return false;
    }

    return true;
}

int translate_logical_to_physical(int logical_address, const PCB &process, int *main_memory)
{
    // Assuming logical_address is an index in the segment table
    int segment_table_start = process.main_memory_base + 10;
    int segment_count = main_memory[process.main_memory_base + 10];
    int logical_tracker = 0;

    for (int i = 0; i < segment_count; i++)
    {
        int segment_start = main_memory[segment_table_start + 2 * i];
        int segment_size = main_memory[segment_table_start + 2 * i + 1];

        if (logical_address >= logical_tracker && logical_address < logical_tracker + segment_size)
        {
            int off_set = logical_address - logical_tracker;
            int physical_address = segment_start + off_set;

            std::cout << "Logical address " << logical_address << " translated to physical address " << physical_address << " for process " << process.process_id << std::endl;
            return physical_address;
        }

        logical_tracker += segment_size;
    } // END FOR

    std::cout << "Memory violation: address " << logical_address << " out of bounds for Process " << process.process_id << std::endl;
    return -1; // Memory violation
}