#include "../include/allocator_global_heap.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

namespace
{
    struct demo_record
    {
        int id;
        double score;
        char mark;
    };
}

int main()
{
    allocator_global_heap allocator;

    std::cout << "allocator_global_heap demo\n";

    int* int_value = static_cast<int*>(allocator.allocate(sizeof(int)));
    *int_value = 42;

    double* double_value = static_cast<double*>(allocator.allocate(sizeof(double)));
    *double_value = 2.71828;

    demo_record* record = static_cast<demo_record*>(allocator.allocate(sizeof(demo_record)));
    *record = demo_record{10, 97.25, 'B'};

    char* text = static_cast<char*>(allocator.allocate(32));
    std::string sample = "global heap allocator";
    std::memcpy(text, sample.c_str(), sample.size() + 1);

    std::cout << "\nAllocated objects:\n";
    std::cout << "  int       @" << static_cast<void*>(int_value) << " = " << *int_value << '\n';
    std::cout << "  double    @" << static_cast<void*>(double_value) << " = " << *double_value << '\n';
    std::cout << "  record    @" << static_cast<void*>(record)
              << " = { id = " << record->id
              << ", score = " << record->score
              << ", mark = " << record->mark << " }\n";
    std::cout << "  text      @" << static_cast<void*>(text) << " = " << text << '\n';

    allocator.deallocate(text, 1);
    allocator.deallocate(record, 1);
    allocator.deallocate(double_value, 1);
    allocator.deallocate(int_value, 1);

    std::cout << "\nAll allocated blocks were returned to the global heap.\n";

    return 0;
}
