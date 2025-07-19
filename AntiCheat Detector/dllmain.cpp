#include <windows.h>
#include <vector>
#include <string>
#include <thread>

#include "jni/jni.h"
#include "jni/jvmti.h"

#include "console.hpp"
#include "instrumenter.hpp"
#include "transaction.hpp"
#include "shared.hh" // need to do this shit because for some reasons the cl's linked cant find extern variables I HATE MICROSOFT

#pragma comment(lib, "ws2_32.lib")

// global Instances & handles
static JavaVM* g_vm = nullptr;
static jvmtiEnv* g_jvmti = nullptr;
static TransactionAnalyzer g_analyzer;

HANDLE g_hIpcEvent = NULL;
HANDLE g_hIpcMapFile = NULL;

// handles server-to-client packets
void JNICALL logServerTransactionIdCpp(JNIEnv* env, jclass clazz, jint transaction_id) {
    (clazz); (env);
    g_analyzer.analyzeServer(static_cast<int16_t>(transaction_id));
}

// native function for client-to-server packets
void JNICALL logClientTransactionIdCpp(JNIEnv* env, jclass clazz, jint transaction_id) {
    (clazz); (env);
    g_analyzer.analyzeClient(static_cast<int16_t>(transaction_id));
}

void JNICALL onClassFileLoadHook(jvmtiEnv* /*jvmti*/, JNIEnv* /*env*/, jclass /*class_being_redefined*/, jobject /*loader*/, const char* name, jobject /*protection_domain*/, jint class_data_len, const unsigned char* class_data, jint* new_class_data_len, unsigned char** new_class_data) {
    if (!name) return;

    try {
        if (strcmp(name, "net/minecraft/network/play/server/S32PacketConfirmTransaction") == 0) {
            Log(INFO, "Hooking class: S32PacketConfirmTransaction");
            ClassInstrumenter instrumenter(class_data, class_data_len);
            std::vector<unsigned char> new_bytes;
            // Instrument S32, telling the instrumenter to create a native method named 'logServerTransactionId'
            instrumenter.instrument_and_get_bytes(new_bytes, "actionNumber", "readPacketData", "(Lnet/minecraft/network/PacketBuffer;)V", "logServerTransactionId");

            if (new_bytes.size() > 0x7FFFFFFF) {
                throw std::runtime_error("Instrumented class size exceeds jint limit.");
            }

            g_jvmti->Allocate(new_bytes.size(), new_class_data);
            memcpy(*new_class_data, new_bytes.data(), new_bytes.size());
            *new_class_data_len = static_cast<jint>(new_bytes.size());

            Log(SUCCESS, "S32PacketConfirmTransaction instrumented successfully.");
        }
        else if (strcmp(name, "net/minecraft/network/play/client/C0FPacketConfirmTransaction") == 0) {
            Log(INFO, "Hooking class: C0FPacketConfirmTransaction");
            ClassInstrumenter instrumenter(class_data, class_data_len);
            std::vector<unsigned char> new_bytes;
            // Instrument C0F, targeting the 'uid' field in 'writePacketData', and creating 'logClientTransactionId'
            instrumenter.instrument_and_get_bytes(new_bytes, "uid", "writePacketData", "(Lnet/minecraft/network/PacketBuffer;)V", "logClientTransactionId");

            if (new_bytes.size() > 0x7FFFFFFF) {
                throw std::runtime_error("Instrumented class size exceeds jint limit.");
            }

            g_jvmti->Allocate(new_bytes.size(), new_class_data);
            memcpy(*new_class_data, new_bytes.data(), new_bytes.size());
            *new_class_data_len = static_cast<jint>(new_bytes.size());

            Log(SUCCESS, "C0FPacketConfirmTransaction instrumented successfully.");
        }
    }
    catch (const std::exception& e) {
        Log(FATAL, "Failed to instrument %s: %s", name, e.what());
    }
}

// so this callback is triggered when a class is prepared by the JVM.
// we use it to register our native function with the newly loaded class.
void JNICALL onClassPrepare(jvmtiEnv* jvmti, JNIEnv* env, jthread thread, jclass klass) {
    (jvmti); (thread);
    char* signature = nullptr;
    if (g_jvmti->GetClassSignature(klass, &signature, nullptr) != JVMTI_ERROR_NONE) {
        return;
    }

    // Check if this is the class we instrumented
    if (strcmp(signature, "Lnet/minecraft/network/play/server/S32PacketConfirmTransaction;") == 0) {
        Log(INFO, "JIT compilation of S32PacketConfirmTransaction detected. Hooking into virtual machine...");

        // Define the mapping between the Java native method and our C++ function pointer
        JNINativeMethod native_method_def = {
            (char*)"logServerTransactionId",
            (char*)"(I)V",
            (void*)&logServerTransactionIdCpp
        };

        // Perform the registration
        if (env->RegisterNatives(klass, &native_method_def, 1) == JNI_OK) {
            Log(SUCCESS, "Successfully registered native 'logServerTransactionId' method.");
        }
        else {
            Log(FATAL, "Failed to register native 'logServerTransactionId' method.");
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        }
    }
    // Handle the client-side packet
    else if (strcmp(signature, "Lnet/minecraft/network/play/client/C0FPacketConfirmTransaction;") == 0) {
        Log(INFO, "JIT compilation of C0FPacketConfirmTransaction detected. Hooking into virtual machine...");

        // Define the mapping for the client packet's native method
        JNINativeMethod native_method_def = {
            (char*)"logClientTransactionId",
            (char*)"(I)V",
            (void*)&logClientTransactionIdCpp
        };

        // Perform the registration
        if (env->RegisterNatives(klass, &native_method_def, 1) == JNI_OK) {
            Log(SUCCESS, "Successfully registered native 'logClientTransactionId' method.");
        }
        else {
            Log(FATAL, "Failed to register native 'logClientTransactionId' method.");
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        }
    }

    // JVMTI requires us to deallocate the memory for the signature string
    if (signature) {
        g_jvmti->Deallocate((unsigned char*)signature);
    }
}

DWORD WINAPI InitThread(LPVOID) {
    SpawnConsole();
    Log(INFO, "Agent spawned. Attaching to JVM...");

    if (JNI_GetCreatedJavaVMs(&g_vm, 1, nullptr) != JNI_OK) {
        Log(FATAL, "JNI_GetCreatedJavaVMs failed.");
        return 1;
    }

    JNIEnv* env = nullptr;
    if (g_vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) {
        Log(FATAL, "AttachCurrentThread failed.");
        return 1;
    }
    if (g_vm->GetEnv(reinterpret_cast<void**>(&g_jvmti), JVMTI_VERSION_1_2) != JNI_OK) {
        Log(FATAL, "GetEnv for JVMTI failed.");
        return 1;
    }
    Log(INFO, "Successfully attached to JVM and got JVMTI environment.");

    // self-explanatory, we need the capabilities to hook stuff
    jvmtiCapabilities caps = { 0 };
    caps.can_retransform_classes = 1;
    caps.can_generate_all_class_hook_events = 1;
    g_jvmti->AddCapabilities(&caps);

    // set up both callbacks
    jvmtiEventCallbacks callbacks = { 0 };
    callbacks.ClassFileLoadHook = &onClassFileLoadHook;
    callbacks.ClassPrepare = &onClassPrepare;
    g_jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));

    // this is how to enable both events
    g_jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
    g_jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_PREPARE, nullptr);

    Log(SUCCESS, "Agent initialized. Bytecode instrumentation and native registration hooks are active.");

    Log(INFO, "Creating IPC objects for communication...");

    // create the event that will be signaled on detection in case we detect an anticheat in mc
    g_hIpcEvent = CreateEventA(NULL, TRUE, FALSE, IPC_EVENT_NAME);
    if (g_hIpcEvent == NULL) {
        Log(FATAL, "Could not create IPC event object!! Error: %d", GetLastError());
        return 1;
    }

    // create the shared memory block
    g_hIpcMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, SHARED_MEM_SIZE, IPC_SHARED_MEM_NAME);
    if (g_hIpcMapFile == NULL) {
        Log(FATAL, "Could not create file mapping object!!! Error: %d", GetLastError());
        CloseHandle(g_hIpcEvent);
        return 1;
    }

    Log(SUCCESS, "IPC objects created successfully. Waiting for game events...");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        if (HANDLE hThread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr)) {
            CloseHandle(hThread);
        }
        break;

    case DLL_PROCESS_DETACH:
        if (g_jvmti != nullptr) {
            g_jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
            g_jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_PREPARE, nullptr);
            g_jvmti->DisposeEnvironment();
            g_jvmti = nullptr;
        }

        if (g_hIpcEvent) {
            CloseHandle(g_hIpcEvent);
            g_hIpcEvent = NULL;
        }
        if (g_hIpcMapFile) {
            CloseHandle(g_hIpcMapFile);
            g_hIpcMapFile = NULL;
        }

        DetachConsole();
        break;
    }
    return TRUE;
}