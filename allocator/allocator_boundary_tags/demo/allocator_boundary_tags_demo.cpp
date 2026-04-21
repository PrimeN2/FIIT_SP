#include "../include/allocator_boundary_tags.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

namespace
{
    struct student_record
    {
        int id;
        double average_score;
        char grade;
    };
}

int main()
{
    try
    {
        allocator_boundary_tags allocator(1024);

        int* number = static_cast<int*>(allocator.allocate(sizeof(int)));
        *number = 2026;

        double* value = static_cast<double*>(allocator.allocate(sizeof(double)));
        *value = 3.14159;

        student_record* record = static_cast<student_record*>(allocator.allocate(sizeof(student_record)));
        *record = student_record{17, 96.5, 'A'};

        char* text = static_cast<char*>(allocator.allocate(32));
        std::string message = "boundary tags allocator";
        std::memcpy(text, message.c_str(), message.size() + 1);

        std::cout << "allocator_boundary_tags demo\n";
        std::cout << "  int    @" << static_cast<void*>(number) << " = " << *number << '\n';
        std::cout << "  double @" << static_cast<void*>(value) << " = " << std::fixed << std::setprecision(5) << *value << '\n';
        std::cout << "  record @" << static_cast<void*>(record)
                  << " = { id = " << record->id
                  << ", average_score = " << record->average_score
                  << ", grade = " << record->grade << " }\n";
        std::cout << "  text   @" << static_cast<void*>(text) << " = " << text << '\n';

        allocator.deallocate(text, 1);
        allocator.deallocate(record, 1);
        allocator.deallocate(value, 1);
        allocator.deallocate(number, 1);

        std::cout << "All allocated blocks were returned to allocator_boundary_tags.\n";
    }
    catch (const std::exception& ex)
    {
        std::cerr << "allocator_boundary_tags demo failed: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
