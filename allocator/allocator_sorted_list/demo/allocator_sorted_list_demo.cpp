#include "../include/allocator_sorted_list.h"

#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

namespace
{
    struct demo_record
    {
        int id;
        double score;
        char tag;
    };

    void print_blocks(const allocator_test_utils& allocator, const std::string& title)
    {
        std::cout << title << '\n';

        const auto blocks = allocator.get_blocks_info();
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            std::cout << "  block " << i
                      << ": size=" << std::setw(4) << blocks[i].block_size
                      << ", state=" << (blocks[i].is_block_occupied ? "occupied" : "free")
                      << '\n';
        }
    }
}

int main()
{
    allocator_sorted_list allocator(512, nullptr, allocator_with_fit_mode::fit_mode::first_fit);
    auto& fit_allocator = static_cast<allocator_with_fit_mode&>(allocator);

    std::cout << "allocator_sorted_list demo\n";

    int* int_value = static_cast<int*>(allocator.allocate(sizeof(int)));
    *int_value = 42;

    double* double_value = static_cast<double*>(allocator.allocate(sizeof(double)));
    *double_value = 3.14159;

    demo_record* record = static_cast<demo_record*>(allocator.allocate(sizeof(demo_record)));
    *record = demo_record{7, 99.5, 'A'};

    print_blocks(allocator, "\nState after allocating int, double and demo_record:");

    allocator.deallocate(double_value, 1);
    print_blocks(allocator, "\nState after freeing the double block:");

    fit_allocator.set_fit_mode(allocator_with_fit_mode::fit_mode::the_best_fit);
    char* text = static_cast<char*>(allocator.allocate(24));
    std::string sample = "sorted-list allocator";
    sample.copy(text, 23);
    text[23] = '\0';

    print_blocks(allocator, "\nState after switching to best-fit and allocating text:");

    std::cout << "\nStored values:\n";
    std::cout << "  int       = " << *int_value << '\n';
    std::cout << "  record.id = " << record->id << ", score = " << record->score << ", tag = " << record->tag << '\n';
    std::cout << "  text      = " << text << '\n';

    allocator.deallocate(text, 1);
    allocator.deallocate(record, 1);
    allocator.deallocate(int_value, 1);

    print_blocks(allocator, "\nState after freeing all blocks:");

    return 0;
}
