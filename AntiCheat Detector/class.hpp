#pragma once

#include <cstdint>
#include <vector>
#include <string>

// Represents a generic attribute in a class file.
struct attribute_info {
    uint16_t name_index = 0;
    std::vector<unsigned char> info;
};

// Represents a field in a class.
struct field_info {
    uint16_t access_flags = 0;
    uint16_t name_index = 0;
    uint16_t descriptor_index = 0;
    std::vector<attribute_info> attributes;
};

// Represents an entry in a method's exception table.
struct exception_table_entry {
    uint16_t start_pc, end_pc, handler_pc, catch_type;
};

// Represents the parsed data of a "Code" attribute.
struct code_attribute_data {
    uint16_t max_stack = 0;
    uint16_t max_locals = 0;
    std::vector<unsigned char> code;
    std::vector<exception_table_entry> exception_table;
    std::vector<attribute_info> attributes;
};

// Represents a method in a class.
struct method_info {
    uint16_t access_flags = 0;
    uint16_t name_index = 0;
    uint16_t descriptor_index = 0;
    std::vector<attribute_info> attributes;
    bool has_code_parsed = false;
    code_attribute_data parsed_code;
};