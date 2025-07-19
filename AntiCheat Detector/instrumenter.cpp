#include "instrumenter.hpp"

ClassInstrumenter::ClassInstrumenter(const unsigned char* data, jint len) : bytes(data, data + len) {}

void __cdecl ClassInstrumenter::instrument_and_get_bytes(std::vector<unsigned char>& out_bytes, const std::string& field_to_get, const std::string& method_to_hook, const std::string& method_desc, const std::string& native_callback_name) {
    Log(DETAIL, "Parsing class file to inject native logger...");
    parse_class_file();

    // STEP ONE TO DEFEAT SUKUNA: Add a new native method declaration to the class
    // This method will be public, static, and native. Its implementation is in our C++ DLL (yeah look at it in the dllmain.cpp source kid)
    Log(DETAIL, "Adding new native method '%s' to the class...", native_callback_name.c_str());
    uint16_t method_name_idx = add_utf8(native_callback_name);
    uint16_t method_desc_idx = add_utf8("(I)V"); // Descriptor for a method that takes an int and returns void
    uint16_t native_method_nat_idx = add_name_and_type(native_callback_name, "(I)V");

    // The method reference needs to point to the current class. 'this_class' is the CP index for it
    uint16_t native_method_ref_idx = add_method_ref(this_class, native_method_nat_idx);

    // Create the method_info structure for our new native method
    method_info native_method_struct;
    native_method_struct.access_flags = 0x0109; // ACC_PUBLIC | ACC_STATIC | ACC_NATIVE
    native_method_struct.name_index = method_name_idx;
    native_method_struct.descriptor_index = method_desc_idx;
    native_method_struct.attributes.clear(); // A native method has no body (i learnt this on my DAM classes :) (FP > Uni), so no attributes
    methods.push_back(native_method_struct);
    Log(SUCCESS, "Native method '%s' added to class structure.", native_callback_name.c_str());

    // STEP 2 TO DEFEAT SUKUNA find the target method we want to inject our call into
    method_info* target_method = nullptr;
    Log(DETAIL, "Searching for method to hook: %s%s", method_to_hook.c_str(), method_desc.c_str());
    for (auto& method : methods) {
        if (get_cp_string(method.name_index) == method_to_hook && get_cp_string(method.descriptor_index) == method_desc) {
            target_method = &method;
            break;
        }
    }
    if (target_method == nullptr) throw std::runtime_error("Target method not found");
    Log(DETAIL, "Target method found.");

    // STEP 3!! Find the reference to the field containing the transaction ID
    uint16_t target_field_ref_idx = 0;
    Log(DETAIL, "Searching for field reference: %s", field_to_get.c_str());
    for (uint16_t i = 1; i < cp_count_original; ++i) {
        if (cp_tags[i] == 9) { // this is called CONSTANT_Fieldref
            uint16_t name_and_type_idx = read_u2_at(cp_offsets[i] + 3);
            uint16_t name_idx = read_u2_at(cp_offsets[name_and_type_idx] + 1);
            if (get_cp_string(name_idx) == field_to_get) {
                target_field_ref_idx = i;
                break;
            }
        }
    }
    if (target_field_ref_idx == 0) throw std::runtime_error("Target field ref not found");
    Log(DETAIL, "Target field reference found at CP index %d.", target_field_ref_idx);

    // The Malevolent Shrine. We writing binary with this one (before it was far, FAAAR more complex)
    std::vector<unsigned char> injection_code = {
        // Opcode to load 'this' onto the stack to access the instance field
        0x2a,
        // Opcode to get the value of the actionNumber field from the object
        0xb4, (unsigned char)((target_field_ref_idx >> 8) & 0xFF), (unsigned char)(target_field_ref_idx & 0xFF),
        // Opcode to invoke our static native method with the integer value now on the stack
        0xb8, (unsigned char)((native_method_ref_idx >> 8) & 0xFF), (unsigned char)(native_method_ref_idx & 0xFF)
    };

    // STEP 5 DEFEAT MAHORAGA: Inject the bytecode before the'return instruction of the target method so that the jvm doesnt cry
    code_attribute_data& code_attr = get_code_attribute(*target_method);
    size_t injection_pos = 0; // Default to start of method

    // For readPacketData (S32), we inject before return to make sure the field is populated
    // For writePacketData (C0F), we inject at the start to read the field before it's written
    if (method_to_hook == "readPacketData") {
        bool found_return = false;
        for (size_t i = 0; i < code_attr.code.size(); ++i) {
            if (code_attr.code[i] == 0xb1 /* return */) {
                injection_pos = i;
                found_return = true;
                break;
            }
        }
        if (!found_return) {
            Log(DETAIL, "No return opcode found in 'readPacketData', injecting at start anyway.");
        }
    }

    Log(DETAIL, "Injecting %zu bytes of bytecode at position %zu.", injection_code.size(), injection_pos);
    code_attr.code.insert(code_attr.code.begin() + injection_pos, injection_code.begin(), injection_code.end());

    // Our injection requires a stack size of 2 (for 'this' and the int field value)
    if (code_attr.max_stack < 2) {
        code_attr.max_stack = 2;
        Log(DETAIL, "Updated max_stack to 2.");
    }

    // STEP 6 DODGE WORLD CUT Update Exception table offsets to account for the new bytecode so that the JVM doesnt fuck up our method
    if (!code_attr.exception_table.empty()) {
        Log(DETAIL, "Updating %zu exception table entries...", code_attr.exception_table.size());
        if (injection_code.size() > 0xFFFF) {
            throw std::runtime_error("Injection code size exceeds uint16_t limit.");
        }
        const auto injection_size = static_cast<uint16_t>(injection_code.size());
        for (auto& entry : code_attr.exception_table) {
            if (entry.start_pc >= injection_pos) entry.start_pc += injection_size;
            if (entry.end_pc >= injection_pos) entry.end_pc += injection_size;
            if (entry.handler_pc >= injection_pos) entry.handler_pc += injection_size;
        }
    }

    // STEP 7 Remove the StackMapTable attribute to force the JVM to recalculate it and dont kick his ugly fucky VerifyError
    auto& attrs = code_attr.attributes;
    bool removed_smt = false;
    for (auto it = attrs.begin(); it != attrs.end(); ) {
        if (get_cp_string(it->name_index) == "StackMapTable") {
            it = attrs.erase(it);
            removed_smt = true;
        }
        else {
            ++it;
        }
    }
    if (removed_smt) Log(DETAIL, "Removed existing StackMapTable.");
    else Log(DETAIL, "No existing StackMapTable found.");

    // FINAL STEP FINALLY - > serialize the entire modified class back into a byte vector
    Log(DETAIL, "Re-serializing the class file...");
    write_class_file(out_bytes);
    Log(SUCCESS, "Class instrumentation for native logging is complete.");
}

// too lazy to comment this
inline void __stdcall ClassInstrumenter::write_class_file(std::vector<unsigned char>& out_bytes) {
    // without 0xCAFEBABE, the JVM won't let us in the club and took forever to figure this shi' out
    write_u4(out_bytes, magic);
    write_u2(out_bytes, minor_version);

    // We're deliberately downgrading to Java 6. Why? cuz some old verifiers are grumpy
    Log(DETAIL, "Downgrading class version to Java 6 (50.0) for verifier compatibility.");
    write_u2(out_bytes, 50);

    // Announce the new, improved size of our constant pool
    write_u2(out_bytes, cp_count_new);

    // basically this is the programming equivalent of "if it ain't broke, don't fix it."
    out_bytes.insert(out_bytes.end(), bytes.data() + 10, bytes.data() + post_cp_offset);

    // now staple our entries to the end
    out_bytes.insert(out_bytes.end(), new_cp_entries.begin(), new_cp_entries.end());

    // now the class's identity crisis: who it is, what it extends, etc etc.
    write_u2(out_bytes, access_flags);
    write_u2(out_bytes, this_class);
    write_u2(out_bytes, super_class);

    // The following sections are guarded by size checks. A u2 can only hold up to 65535,
    // and if we exceed that, the class file will implode

    // MSVC's macro expansion is broken on my IDE so I do it manually for my own sanity.
    if (interfaces.size() > 0xFFFF) throw std::runtime_error("Too many interfaces.");
    write_u2(out_bytes, static_cast<uint16_t>(interfaces.size()));
    for (uint16_t interface_idx : interfaces) write_u2(out_bytes, interface_idx);

    // fields.
    if (fields.size() > 0xFFFF) throw std::runtime_error("Too many fields.");
    write_u2(out_bytes, static_cast<uint16_t>(fields.size()));
    for (auto& field : fields) write_member(out_bytes, field);

    // methods.
    if (methods.size() > 0xFFFF) throw std::runtime_error("Too many methods.");
    write_u2(out_bytes, static_cast<uint16_t>(methods.size()));
    for (auto& method : methods) write_member(out_bytes, method);

    // class-level attributes.
    if (class_attributes.size() > 0xFFFF) throw std::runtime_error("Too many attributes.");
    write_u2(out_bytes, static_cast<uint16_t>(class_attributes.size()));
    for (const auto& attr : class_attributes) write_attribute(out_bytes, attr);
}

inline void __cdecl ClassInstrumenter::parse_class_file() {
    const unsigned char* p = bytes.data();

    // First, the magic number. If it's not 0xCAFEBABE, we're probably reading a JPEG or some shit
    magic = read_u4(p);
    minor_version = read_u2(p);
    major_version = read_u2(p); // If this number is high, we're living in the future

    // this is like asking .. _> How many constants are in our pool?
    cp_count_original = read_u2(p);
    cp_count_new = cp_count_original; // We'll add our own entries later

    // Prepare to map out the constant pool.
    cp_tags.resize(cp_count_original);
    cp_offsets.resize(cp_count_original);

    // Let the great constant pool traversal begin
    // it's 1-indexed because the JVM designers were feeling quirky and thats why I HATE JAVA I HATE JAVA I HATE JAVA I HATE JHAVAFAWFAOPWFJAW
    const unsigned char* cp_cursor = p;
    for (uint16_t i = 1; i < cp_count_original; ++i) {
        cp_offsets[i] = cp_cursor - bytes.data();
        cp_tags[i] = *cp_cursor;
        cp_cursor++;

        switch (cp_tags[i]) {
        case 1: { // CONSTANT_Utf8
            uint16_t len = (cp_cursor[0] << 8) | cp_cursor[1]; // Read length, big-endian style
            cp_cursor += 2 + len; // Skip over the length and the string itself
            break;
        }
              // These constants are all a cozy 4 bytes long so easy pezy
        case 3: case 4: case 9: case 10: case 11: case 12: case 18: cp_cursor += 4; break;
            // longs and doubles take up 8 bytes AND the next slot in the pool
        case 5: case 6: cp_cursor += 8; i++; break;
            // these are a tidy 2 bytes
        case 7: case 8: case 16: cp_cursor += 2; break;
            // this one is 3 bytes, just to be different
        case 15: cp_cursor += 3; break;
            // If we get here, the tablet is cursed and our program must now die, good bye Lunar Client or whatever ur running
        default: char msg[64]; sprintf_s(msg, "Unsupported CP tag: %d.", cp_tags[i]); throw std::runtime_error(msg);
        }
    }
    // We survived the constant pool. What's next?
    p = cp_cursor;
    post_cp_offset = p - bytes.data(); // Mark where the real class definition starts.

    access_flags = read_u2(p);  // Is it public? Is it final? etc etc
    this_class = read_u2(p);    // name
    super_class = read_u2(p);   // self-explanatory..

    // Round up the interfaces
    uint16_t interfaces_count = read_u2(p);
    interfaces.resize(interfaces_count);
    for (uint16_t i = 0; i < interfaces_count; ++i) interfaces[i] = read_u2(p);

    // Round up the fields
    uint16_t fields_count = read_u2(p);
    fields.resize(fields_count);
    for (uint16_t i = 0; i < fields_count; ++i) parse_member(p, fields[i]);

    // Round up the methods
    uint16_t methods_count = read_u2(p);
    methods.resize(methods_count);
    for (uint16_t i = 0; i < methods_count; ++i) parse_member(p, methods[i]);

    // and finally the class attributes
    uint16_t attributes_count = read_u2(p);
    class_attributes.resize(attributes_count);
    for (uint16_t i = 0; i < attributes_count; ++i) parse_attribute(p, class_attributes[i]);
}

template<typename T>
__forceinline void __fastcall ClassInstrumenter::parse_member(const unsigned char*& p, T& member) {
    member.access_flags = read_u2(p);
    member.name_index = read_u2(p);
    member.descriptor_index = read_u2(p);
    uint16_t attributes_count = read_u2(p);
    member.attributes.resize(attributes_count);
    for (uint16_t i = 0; i < attributes_count; ++i) parse_attribute(p, member.attributes[i]);

    // Ah, if constexpr. For when you want to write code that only applies to some templates
    // Fields don't have code!!!
    if constexpr (std::is_same_v<T, method_info>) member.has_code_parsed = false;
}

__forceinline void __fastcall ClassInstrumenter::parse_attribute(const unsigned char*& p, attribute_info& attr) {
    attr.name_index = read_u2(p); // What's this attribute called?
    uint32_t len = read_u4(p);   // How big is the surprise inside?
    attr.info.assign(p, p + len);
    p += len;
}

__forceinline void __stdcall ClassInstrumenter::parse_code_attribute(method_info& m) {
    // If we've already done this, don't do it again cuz silly
    if (m.has_code_parsed) return;

    // First, find the "Code" attribute. If it's not there, it's an abstract method or something weird
    attribute_info& code_attr_info = get_attribute(m.attributes, "Code");
    const unsigned char* p = code_attr_info.info.data();

    // Now for the juicy details.....
    m.parsed_code.max_stack = read_u2(p); // How many plates can we stack?
    m.parsed_code.max_locals = read_u2(p); // How many variables can we juggle?
    uint32_t code_len = read_u4(p);
    m.parsed_code.code.assign(p, p + code_len); // the actual bytecode
    p += code_len;

    uint16_t ex_table_len = read_u2(p);
    m.parsed_code.exception_table.resize(ex_table_len);
    for (uint16_t i = 0; i < ex_table_len; ++i) {
        m.parsed_code.exception_table[i] = { read_u2(p), read_u2(p), read_u2(p), read_u2(p) };
    }

    // a Code attribute can have its own attributes
    uint16_t attributes_count = read_u2(p);
    m.parsed_code.attributes.resize(attributes_count);
    for (uint16_t i = 0; i < attributes_count; ++i) parse_attribute(p, m.parsed_code.attributes[i]);

    // mark it as parsed so we never have to do that again
    m.has_code_parsed = true;
}

template<typename T>
inline void __stdcall ClassInstrumenter::write_member(std::vector<unsigned char>& vec, T& member) {
    write_u2(vec, member.access_flags);
    write_u2(vec, member.name_index);
    write_u2(vec, member.descriptor_index);

    // If this is a method and we've tampered with its code, we need to rebuild it from scratch
    if constexpr (std::is_same_v<T, method_info>) {
        if (member.has_code_parsed) {
            // obliterate the old Code attribute
            auto& attrs = member.attributes;
            for (auto it = attrs.begin(); it != attrs.end(); ) {
                if (get_cp_string(it->name_index) == "Code") {
                    it = attrs.erase(it);
                }
                else {
                    ++it;
                }
            }

            // now, let's craft a new, beautiful Code attribute (took me 2 days straight to figure out how lol)
            attribute_info new_code_attr;
            new_code_attr.name_index = get_attribute_name_idx("Code");
            std::vector<unsigned char> code_info; // This will hold the guts of the attribute

            // Write all the pieces back in the correct order. Don't screw this up or i will uninstall minecraft from my pc
            write_u2(code_info, member.parsed_code.max_stack);
            write_u2(code_info, member.parsed_code.max_locals);
            write_u4(code_info, static_cast<uint32_t>(member.parsed_code.code.size()));
            code_info.insert(code_info.end(), member.parsed_code.code.begin(), member.parsed_code.code.end());
            write_u2(code_info, static_cast<uint16_t>(member.parsed_code.exception_table.size()));
            for (const auto& ex : member.parsed_code.exception_table) {
                write_u2(code_info, ex.start_pc); write_u2(code_info, ex.end_pc);
                write_u2(code_info, ex.handler_pc); write_u2(code_info, ex.catch_type);
            }
            write_u2(code_info, static_cast<uint16_t>(member.parsed_code.attributes.size()));
            for (const auto& attr : member.parsed_code.attributes) write_attribute(code_info, attr);

            new_code_attr.info = code_info;
            member.attributes.push_back(new_code_attr); // welcome to the family.
        }
    }

    // write all the member's attributes, including our potentially new Code attribute
    if (member.attributes.size() > 0xFFFF) throw std::runtime_error("Too many attributes.");
    write_u2(vec, static_cast<uint16_t>(member.attributes.size()));
    for (const auto& attr : member.attributes) write_attribute(vec, attr);
}

inline void __stdcall ClassInstrumenter::write_attribute(std::vector<unsigned char>& vec, const attribute_info& attr) {
    write_u2(vec, attr.name_index);
    if (attr.info.size() > 0xFFFFFFFF) throw std::runtime_error("Info size exceeds 4GB.");
    write_u4(vec, static_cast<uint32_t>(attr.info.size()));
    vec.insert(vec.end(), attr.info.begin(), attr.info.end());
}

_inline std::string ClassInstrumenter::get_cp_string(uint16_t index) {
    // Some pointer magic to find where the string ACTUALLY starts
    const char* string_start = reinterpret_cast<const char*>(bytes.data() + cp_offsets[index] + 3);
    // The length is stored just before the string data. Of course it is....
    uint16_t string_length = read_u2_at(cp_offsets[index] + 1);
    return std::string(string_start, string_length);
}

_inline uint16_t ClassInstrumenter::read_u2_at(size_t offset) {
    return (bytes[offset] << 8) | bytes[offset + 1];
}

_inline attribute_info& __stdcall ClassInstrumenter::get_attribute(std::vector<attribute_info>& attrs, const std::string& name) {
    for (auto& attr : attrs) if (get_cp_string(attr.name_index) == name) return attr;
    // If you can't find it, panic.
    throw std::runtime_error("Attribute not found: " + name + "???'!4'o23q04'9i32");
}

_inline code_attribute_data& __stdcall ClassInstrumenter::get_code_attribute(method_info& m) {
    parse_code_attribute(m); // this does all the heavy lifting
    return m.parsed_code;
}

_inline uint16_t __stdcall ClassInstrumenter::get_attribute_name_idx(const std::string& name) {
    for (uint16_t i = 1; i < cp_count_original; ++i) if (cp_tags[i] == 1 && get_cp_string(i) == name) return i;
    return add_utf8(name); // Not found? ok then add it
}

inline uint16_t __fastcall ClassInstrumenter::add_utf8(const std::string& str) {
    if (str.length() > 0xFFFF) throw std::runtime_error("This string is too long. Please write a shorter novel :).");
    new_cp_entries.push_back(1); // Tag for CONSTANT_Utf8
    write_u2(new_cp_entries, static_cast<uint16_t>(str.length()));
    new_cp_entries.insert(new_cp_entries.end(), str.begin(), str.end());
    return cp_count_new++;
}

inline uint16_t __fastcall ClassInstrumenter::add_class(const std::string& name) {
    return add_ref(7, add_utf8(name)); // Tag 7 is for CONSTANT_Class
}

inline uint16_t __fastcall ClassInstrumenter::add_ref(uint8_t tag, uint16_t index) {
    new_cp_entries.push_back(tag);
    write_u2(new_cp_entries, index);
    return cp_count_new++;
}

inline uint16_t __fastcall ClassInstrumenter::add_ref(uint8_t tag, uint16_t i1, uint16_t i2) {
    new_cp_entries.push_back(tag);
    write_u2(new_cp_entries, i1);
    write_u2(new_cp_entries, i2);
    return cp_count_new++;
}

inline uint16_t __fastcall ClassInstrumenter::add_name_and_type(const std::string& name, const std::string& desc) {
    return add_ref(12, add_utf8(name), add_utf8(desc)); // Tag 12 for NameAndType
}

inline uint16_t __fastcall ClassInstrumenter::add_field_ref(uint16_t class_idx, uint16_t nat_idx) {
    return add_ref(9, class_idx, nat_idx); // Tag 9 for FieldRef
}

inline uint16_t __fastcall ClassInstrumenter::add_method_ref(uint16_t class_idx, uint16_t nat_idx) {
    return add_ref(10, class_idx, nat_idx); // Tag 10 for MethodRef
}

inline uint16_t ClassInstrumenter::add_string(const std::string& str) {
    uint16_t utf8_idx = add_utf8(str); // first, add the raw string data
    return add_ref(8, utf8_idx);       // then, add a CONSTANT_String entry that points to it
}