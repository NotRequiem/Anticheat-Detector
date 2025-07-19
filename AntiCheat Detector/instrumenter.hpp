#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "jni/jni.h"

#include "endian.hpp"
#include "console.hpp"
#include "class.hpp"

class ClassInstrumenter {
public:
    // Constructor: basically takes the raw class file bytes
    ClassInstrumenter(const unsigned char* data, jint len);

    // Main public method to perform instrumentation and get the modified bytes
    void instrument_and_get_bytes(std::vector<unsigned char>& out_bytes, const std::string& field_to_get, const std::string& method_to_hook, const std::string& method_desc, const std::string& native_callback_name);

private:
    // Member variables
    std::vector<unsigned char> bytes;
    uint32_t magic = 0;
    uint16_t minor_version = 0, major_version = 0;
    uint16_t cp_count_original = 0, cp_count_new = 0;
    std::vector<uint8_t> cp_tags;
    std::vector<size_t> cp_offsets;
    size_t post_cp_offset = 0;
    uint16_t access_flags = 0, this_class = 0, super_class = 0;
    std::vector<uint16_t> interfaces;
    std::vector<field_info> fields;
    std::vector<method_info> methods;
    std::vector<attribute_info> class_attributes;
    std::vector<unsigned char> new_cp_entries;

    // Parsing methods
    void parse_class_file();
    template<typename T> void parse_member(const unsigned char*& p, T& member);
    void parse_attribute(const unsigned char*& p, attribute_info& attr);
    void parse_code_attribute(method_info& m);

    // Rebuilding methods, this one is not used OK but I want it here just in case because it took forever to study the stackmaptable
    void rebuild_stack_map_table(code_attribute_data& code_attr, const std::string& method_desc) {
        const uint8_t ITEM_OBJECT = 7, ITEM_INTEGER = 1, ITEM_UNINITIALIZED_THIS = 6;

        for (auto it = code_attr.attributes.begin(); it != code_attr.attributes.end(); ) {
            if (get_cp_string(it->name_index) == "StackMapTable") it = code_attr.attributes.erase(it); else ++it;
        }

        attribute_info smt_attr;
        smt_attr.name_index = add_utf8("StackMapTable");
        std::vector<unsigned char> frame_data;
        std::vector<std::pair<uint8_t, uint16_t>> locals;

        if (!(access_flags & 0x0008)) { // ACC_STATIC
            locals.push_back({ ITEM_UNINITIALIZED_THIS, static_cast<uint16_t>(0) });
        }
        for (const char* d = method_desc.c_str() + 1; *d != ')'; ++d) {
            if (*d == 'L') {
                const char* end = strchr(d, ';');
                locals.push_back({ ITEM_OBJECT, add_class(std::string(d + 1, end - d - 1)) });
                d = end;
            }
            else if (*d == '[') {
                const char* start = d; while (*d == '[') d++; if (*d == 'L') d = strchr(d, ';');
                locals.push_back({ ITEM_OBJECT, add_class(std::string(start, d - start + 1)) });
            }
            else {
                locals.push_back({ ITEM_INTEGER, static_cast<uint16_t>(0) });
                if (*d == 'J' || *d == 'D') {
                    locals.push_back({ static_cast<uint8_t>(0), static_cast<uint16_t>(0) });
                }
            }
        }

        write_u2(frame_data, 1); // num_entries
        frame_data.push_back(255); // full_frame
        write_u2(frame_data, 0); // offset_delta
        write_u2(frame_data, static_cast<uint16_t>(locals.size())); // still dont know why the fuck i need to cast here to fix UR STUPID WARNINGS IF THIS FUNCTION IS NOT EVEN REFERENCED BRO MICROSOFT
        for (size_t i = 0; i < locals.size(); ++i) {
            frame_data.push_back(locals[i].first);
            if (locals[i].first == ITEM_OBJECT) write_u2(frame_data, locals[i].second);
        }
        write_u2(frame_data, 0);
        smt_attr.info = frame_data;
        code_attr.attributes.push_back(smt_attr);
    }

    void write_class_file(std::vector<unsigned char>& out_bytes);
    template<typename T> void write_member(std::vector<unsigned char>& vec, T& member);
    void write_attribute(std::vector<unsigned char>& vec, const attribute_info& attr);

    // Helper methods
    std::string get_cp_string(uint16_t index);
    uint16_t read_u2_at(size_t offset);
    attribute_info& get_attribute(std::vector<attribute_info>& attrs, const std::string& name);
    code_attribute_data& get_code_attribute(method_info& m);
    uint16_t get_attribute_name_idx(const std::string& name);
    uint16_t add_utf8(const std::string& str);
    uint16_t add_class(const std::string& name);
    uint16_t add_ref(uint8_t tag, uint16_t index);
    uint16_t add_ref(uint8_t tag, uint16_t i1, uint16_t i2);
    uint16_t add_name_and_type(const std::string& name, const std::string& desc);
    uint16_t add_field_ref(uint16_t class_idx, uint16_t nat_idx);
    uint16_t add_method_ref(uint16_t class_idx, uint16_t nat_idx);
    uint16_t add_string(const std::string& str);
};