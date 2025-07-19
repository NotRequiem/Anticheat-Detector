# El Comienzo de la Batalla
El objetivo principal de interceptar una función en Java usando la Interfaz Nativa de Java  y la Interfaz de Herramientas de la Máquina Virtual de Java es poder observar y alterar el comportamiento de una aplicación Java en tiempo de ejecución. Esto abre un mundo de posibilidades, desde la creación de herramientas de profiling y debugging hasta la modificación de juegos como Minecraft para hacer tonterías como esta y detectar anticheats en algún server peruano como SoloLe... o ####

Nuestra historia comienza con un objetivo aparentemente sencillo: deobfuscar, localizar y "hookear" (interceptar) varios métodos específicos dentro del código de Minecraft. La idea era simple: ejecutar nuestro propio código cada vez que el juego llamara a una función determinada pero **AL ESTILO REQUIEM**, sin usar agentes externos de java ni pollas, sin librerías externas, sin usar la API de Forge o Fabric, sin gilipolleces en puro C y C++. 

SIN EMBARGO NO SABÍA LO QUE ME PUTO ESPERABA EN ESTAS 2 SEMANAS DE INVESTIGACIÓN PARA ESTE PROYECTO.

## Los Primeros y Desastrosos Intentos
Primero empecé a deobfuscar las clases que interceptaban transacciones en Minecraft porque necesitamos saber cómo es el 'layout' de un método antes de poder hacer hooking. Esto no fue muy difícil, sabía el nombre de la clase que quería interceptar (S32PacketConfirmTransaction). El layout de la clase se encuentra en fuentes online como https://skmedix.github.io/ForgeJavaDocs/javadoc/forge/1.7.10-10.13.4.1614/net/minecraft/network/play/server/S32PacketConfirmTransaction.html, así que pude estimar directamente cómo se vería un código completo gracias a este link:

```java
package net.minecraft.network.play.server;

import java.io.IOException;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayClient;

public class S32PacketConfirmTransaction implements Packet<INetHandlerPlayClient>
{
    private int windowId;
    private short actionNumber;
    private boolean field_148893_c;

    public S32PacketConfirmTransaction()
    {
    }

    public S32PacketConfirmTransaction(int windowIdIn, short actionNumberIn, boolean p_i45182_3_)
    {
        this.windowId = windowIdIn;
        this.actionNumber = actionNumberIn;
        this.field_148893_c = p_i45182_3_;
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayClient handler)
    {
        handler.handleConfirmTransaction(this);
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer buf) throws IOException
    {
        this.windowId = buf.readUnsignedByte();
        this.actionNumber = buf.readShort();
        this.field_148893_c = buf.readBoolean();
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer buf) throws IOException
    {
        buf.writeByte(this.windowId);
        buf.writeShort(this.actionNumber);
        buf.writeBoolean(this.field_148893_c);
    }

    public int getWindowId()
    {
        return this.windowId;
    }

    public short getActionNumber()
    {
        return this.actionNumber;
    }

    public boolean func_148888_e()
    {
        return this.field_148893_c;
    }
}
```

Este layout fue confirmado al escribir un código usando JNI para comprobar si realmente esos métodos y fields siguen existiendo, apoyándome en estas fuentes: https://stackoverflow.com/questions/77064243/getting-a-list-of-all-fields-in-a-class-using-jni y https://stackoverflow.com/questions/40004522/how-to-get-values-from-jobject-in-c-using-jni

VALE PERFECTO, ya podemos empezar.

Tras leer un poco en https://medium.com/@hugosafilho/how-to-manipulate-byte-code-with-asm-3c9b8fbe0f8c, el primer enfoque fue utilizar los eventos `onMethodEntry` y `onMethodExit`. De forma sencilla, onMethodEntry y onMethodExit son "ganchos" que te permiten ejecutar tu propio código justo cuando la JVM está a punto de entrar a un método y justo antes de que vaya a salir de él.

**Imagina que cada método de Java es una habitación.**

`onMethodEntry` sería como poner un sensor en la puerta de entrada de la habitación. Cada vez que alguien (el flujo de ejecución del programa) va a entrar, el sensor se activa y puedes realizar una acción, como anotar quién entra o a qué hora.
`onMethodExit` es como poner otro sensor, pero esta vez en la puerta de salida. Cada vez que alguien va a salir de la habitación, este sensor se activa. Aquí puedes hacer otras cosas, como ver si la persona se lleva algo (el valor de retorno) o si salió porque se activó una alarma de incendios (una excepción).

El prototipo de la función se ve así en los headers oficiales de la JVMTI:
```c
typedef void (JNICALL *jvmtiEventMethodEntry)
    (jvmtiEnv *jvmti_env,
     JNIEnv* jni_env,
     jthread thread,
     jmethodID method);

typedef void (JNICALL *jvmtiEventMethodExit)
    (jvmtiEnv *jvmti_env,
     JNIEnv* jni_env,
     jthread thread,
     jmethodID method,
     jboolean was_popped_by_exception,
     jvalue return_value);
```

Mi idea era sencilla, cada vez que entraran o salieran del método que me interesaba (en este caso, al querer leer transacciones, sería processPacket), obtendría el valor de `this`, y desde el objeto referenciado sacaría información de cuál es el número de transacción que el cliente está recibiendo (actionNumber es el campo que nos interesa).

```cpp
void JNICALL onMethodEntry(jvmtiEnv *jvmti, JNIEnv* jni, jthread thread, jmethodID method) {
    // 1. OBTENER INFORMACIÓN DEL MÉTODO
    // Necesitamos saber en qué clase y método estamos antes de empezar a hacer nada, ya que esta cosa se llama por cada vez que se entra a un método en java
    jclass methodClass;
    char* className = NULL;
    char* methodName = NULL;
    char* methodSignature = NULL;

    // la clase que declara el método, encontré la función tratando de buscar "get method name" en https://docs.oracle.com/javase/8/docs/platform/jvmti/jvmti.html
    jvmti->GetMethodDeclaringClass(method, &methodClass);
    // firma de la clase ("Lnet/minecraft/network/play/server/S32PacketConfirmTransaction;" en nuestro caso)
    jvmti->GetClassSignature(methodClass, &className, NULL);

    // 2. FILTRAR POR CLASE
    // Comparamos el nombre de la clase con la que nos interesa. Si no es, salimos pitando inmediatamente.
    // Esto es CRUCIAL para el rendimiento
    const char* targetClassName = "Lnet/minecraft/network/play/server/S32PacketConfirmTransaction;";
    if (className == NULL || strcmp(className, targetClassName) != 0) {
        if (className) jvmti->Deallocate((unsigned char*)className);
        return;
    }

    // Si la clase coincide, ahora filtramos por el nombre del método
    jvmti->GetMethodName(method, &methodName, &methodSignature, NULL);

    // 3. FILTRAR POR MÉTODO
    const char* targetMethodName = "processPacket";
    const char* targetMethodSignature = "(Lnet/minecraft/network/play/INetHandlerPlayClient;)V";

    if (methodName != NULL && methodSignature != NULL &&
        strcmp(methodName, targetMethodName) == 0 &&
        strcmp(methodSignature, targetMethodSignature) == 0) {
        
        // PERFE ya sabemos que estamos dentro del método correcto. Ahora vamos a por el campo.

        // 4. CACHEAR EL JFIELDID (si es la primera vez)
        // Para no tener que buscar el ID del campo "actionNumber" cada vez, lo buscamos una vez y lo guardamos
        if (actionNumberFieldID == NULL) {
            // "actionNumber" es el nombre del campo en Java que como dije antes nos interesa
            // "S" es el tipo de dato en la firma JNI para un short.
            actionNumberFieldID = jni->GetFieldID(methodClass, "actionNumber", "S");
            if (actionNumberFieldID == NULL) {
                 std::cerr << "[Agent] Error: No se pudo encontrar el fieldID para 'actionNumber'." << std::endl;
                 jvmti->Deallocate((unsigned char*)className);
                 jvmti->Deallocate((unsigned char*)methodName);
                 jvmti->Deallocate((unsigned char*)methodSignature);
                 return;
            }
        }

        // 5. Como dije antes, queremos obtener la instancia del objeto ('this')
        // Como es un método de instancia (no estático), sabemos que el objeto 'this' es la variable local en la posición 0
        jobject thisObject;
        jvmti->GetLocalObject(thread, 0, 0, &thisObject); // 0 es el frame actual, 0 es el slot de 'this'

        if (thisObject != NULL) {
            // Ahora que tenemos el objeto y el ID del campo, YA podemos obtener el valor :)
            jshort actionNumberValue = jni->GetShortField(thisObject, actionNumberFieldID);

            std::cout << "[Agent] actionNumber = " << actionNumberValue << std::endl;
        }
    }

    // Liberamos la memoria y ya
    if (className) jvmti->Deallocate((unsigned char*)className);
    if (methodName) jvmti->Deallocate((unsigned char*)methodName);
    if (methodSignature) jvmti->Deallocate((unsigned char*)methodSignature);
}
```

Ahora claro, te preguntarás (O QUIZÁS NO), ¿y cómo sabe Java que tiene que llamar a ese onMethodEntry que nosotros estamos definiendo? Para ello, tenemos que **inyectar un agente** a Minecraft, con un punto de entrada que le diga a la JVM "ey, tienes que irte aquí cada vez que entres a un método". Esto es muy simple de hacer y hay mucho código online sobre los métodos exactos para realizarlo. Como lo estamos haciendo para minecraft 1.8.9 (Java 8), usaremos `JVMTI_VERSION_1_2`.

```cpp
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    if (JNI_GetCreatedJavaVMs(&g_vm, 1, nullptr) != JNI_OK) {
        return 1;
    }

    JNIEnv* env = nullptr;
    if (g_vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) {
        return 1;
    }
    if (g_vm->GetEnv(reinterpret_cast<void**>(&g_jvmti), JVMTI_VERSION_1_2) != JNI_OK) {
        return 1;
    }

    // Según la documentación, debemos pedir permiso a la JVM para hacer ciertas cosas
    jvmtiCapabilities capabilities = {0};
    capabilities.can_generate_method_entry_events = 1; // Para usar onMethodEntry
    capabilities.can_generate_method_exit_events = 1;  // Para usar onMethodExit
    capabilities.can_access_local_variables = 1;       // Para poder obtener 'this' con GetLocalObject

    if (jvmti->AddCapabilities(&capabilities) != JVMTI_ERROR_NONE) {
        std::cerr << "[Agent] Error: No se pudieron añadir las capacidades requeridas." << std::endl;
        return JNI_ERR;
    }

    // Le decimos a la JVMTI qué funciones nuestras debe llamar para cada evento, estos son los CALLBACKS
    jvmtiEventCallbacks callbacks = {0};
    callbacks.MethodEntry = &onMethodEntry;
    callbacks.MethodExit = &onMethodExit;

    if (jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks)) != JVMTI_ERROR_NONE) {
        std::cerr << "[Agent] Error: No se pudieron configurar los callbacks." << std::endl;
        return JNI_ERR;
    }

    // y por último, activamos las notificaciones para los eventos que queremos escuchar
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, NULL);
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_EXIT, NULL);

    std::cout << "[Agent] Agente cargado y eventos habilitados correctamente." << std::endl;
    return JNI_OK;
}
```

La realidad fue un jarro de agua fría. No funcionaba?!?!?¿!!?!?!

El compilador Just-In-Time de la JVM es agresivo. Si un método es pequeño (como getActionNumber), el `JIT` puede decidir **eliminar la llamada al método** y simplemente "pegar" (inline) su código directamente donde se le llama.

Si el método processPacket fuera tan simple como para ser candidato a inlining (y básicament lo es...), la JVM podría optimizarlo.
Si el método ha sido "inlinado" (no tengo ni puta idea de si existe esta palabra), conceptualmente ya no existe una "entrada" o "salida" clara para ese método en el código nativo compilado, entonces no podremos interceptarlo. 

Aparentemente, puedes lanzar Java con flags para deshabilitar el inlining (`-XX:MaxInlineSize=0`) o incluso el JIT (`-Xint`), pero esto destruirá el rendimiento y solo debe usarse para depuración, además de que no vamos a decirle al usuario que cambie los jvm arguments.

Fue difícil darme cuenta de esto de arriba porque tras revisar con Cheat Engine, la hook sí que estaba funcionando para otras clases (no inline), y yo seguía como imbécil empeñado en hacer que no fuese inline. Pero hay varios problemas:

1. En el momento en que se ejecuta Agent_OnLoad, **la clase S32PacketConfirmTransaction ni siquiera ha sido cargada por la JVM**, SOLO se carga cuando clickeas en el botón de "multiplayer", y mucho menos su método processPacket ha sido ejecutado las suficientes veces como para que el JIT lo considere "caliente" y decida compilarlo. Es como intentar despedir a un empleado que todavía no ha sido contratado. La operación no tiene sentido porque el estado previo (ser un empleado/estar optimizado) no existe.

2. Aún así, aunque lo interceptase correctamente, justo cuando se carga la clase, **aparentemente esta ya se optimiza????**. Y claro como dije antes, una vez que un método ha sido """"inlinado"""", la llamada a ese método ha desaparecido del código compilado. Conceptualmente, ya no existe una "entrada" o "salida" para ese método en el código nativo. No puedes deoptimizar algo que ya no existe como una unidad separada.

Descartado el primer método, pasamos al siguiente plan: el evento onVMInit, que se activa al iniciar la JVM. La idea era usar `jmvti->SetBreakpoint()` para establecer un punto de interrupción al principio del método que queríamos interceptar. Al detenerse la ejecución, podríamos hacer nuestras "cosas" y luego reanudarla.

```cpp
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv *jvmti = NULL;
    vm->GetEnv((void**)&jvmti, JVMTI_VERSION_1_2);

    jvmtiCapabilities capabilities = {0};
    capabilities.can_set_breakpoint = 1; // obviamente necesario
    jvmtiError err = jvmti->AddCapabilities(&capabilities);
    if (err != JVMTI_ERROR_NONE) {
        std::cerr << "Error: No se pudo añadir la capacidad para breakpoints." << std::endl;
        return JNI_ERR;
    }

    jvmtiEventCallbacks callbacks = {0};
    callbacks.Breakpoint = &onBreakpoint; // Le decimos qué función llamar como antes
    jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));

    // HABILITAR LA NOTIFICACIÓN DEL EVENTO
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, NULL);
    
    std::cout << "Agente cargado. Listo para establecer breakpoints." << std::endl;

    // jmethodID miMetodo = ...;
    // jvmti->SetBreakpoint(miMetodo, 0); 
    // bla bla bla...
    return JNI_OK;
}
```

Esto devolvía un error puto raro que tenía el código 99, y tras buscar un poco me di cuenta de que significaba `JVMTI_ERROR_MUST_POSSESS_CAPABILITY`

```c
typedef enum {
    JVMTI_ERROR_NONE = 0,
    JVMTI_ERROR_INVALID_THREAD = 10,
    JVMTI_ERROR_INVALID_THREAD_GROUP = 11,
    JVMTI_ERROR_INVALID_PRIORITY = 12,
    JVMTI_ERROR_THREAD_NOT_SUSPENDED = 13,
    JVMTI_ERROR_THREAD_SUSPENDED = 14,
    JVMTI_ERROR_THREAD_NOT_ALIVE = 15,
    JVMTI_ERROR_INVALID_OBJECT = 20,
    JVMTI_ERROR_INVALID_CLASS = 21,
    JVMTI_ERROR_CLASS_NOT_PREPARED = 22,
    JVMTI_ERROR_INVALID_METHODID = 23,
    JVMTI_ERROR_INVALID_LOCATION = 24,
    JVMTI_ERROR_INVALID_FIELDID = 25,
    JVMTI_ERROR_INVALID_MODULE = 26,
    JVMTI_ERROR_NO_MORE_FRAMES = 31,
    JVMTI_ERROR_OPAQUE_FRAME = 32,
    JVMTI_ERROR_TYPE_MISMATCH = 34,
    JVMTI_ERROR_INVALID_SLOT = 35,
    JVMTI_ERROR_DUPLICATE = 40,
    JVMTI_ERROR_NOT_FOUND = 41,
    JVMTI_ERROR_INVALID_MONITOR = 50,
    JVMTI_ERROR_NOT_MONITOR_OWNER = 51,
    JVMTI_ERROR_INTERRUPT = 52,
    JVMTI_ERROR_INVALID_CLASS_FORMAT = 60,
    JVMTI_ERROR_CIRCULAR_CLASS_DEFINITION = 61,
    JVMTI_ERROR_FAILS_VERIFICATION = 62,
    JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED = 63,
    JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED = 64,
    JVMTI_ERROR_INVALID_TYPESTATE = 65,
    JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED = 66,
    JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED = 67,
    JVMTI_ERROR_UNSUPPORTED_VERSION = 68,
    JVMTI_ERROR_NAMES_DONT_MATCH = 69,
    JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED = 70,
    JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED = 71,
    JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_ATTRIBUTE_CHANGED = 72,
    JVMTI_ERROR_UNMODIFIABLE_CLASS = 79,
    JVMTI_ERROR_UNMODIFIABLE_MODULE = 80,
    JVMTI_ERROR_NOT_AVAILABLE = 98,
    JVMTI_ERROR_MUST_POSSESS_CAPABILITY = 99,
    JVMTI_ERROR_NULL_POINTER = 100,
    JVMTI_ERROR_ABSENT_INFORMATION = 101,
    JVMTI_ERROR_INVALID_EVENT_TYPE = 102,
    JVMTI_ERROR_ILLEGAL_ARGUMENT = 103,
    JVMTI_ERROR_NATIVE_METHOD = 104,
    JVMTI_ERROR_CLASS_LOADER_UNSUPPORTED = 106,
    JVMTI_ERROR_OUT_OF_MEMORY = 110,
    JVMTI_ERROR_ACCESS_DENIED = 111,
    JVMTI_ERROR_WRONG_PHASE = 112,
    JVMTI_ERROR_INTERNAL = 113,
    JVMTI_ERROR_UNATTACHED_THREAD = 115,
    JVMTI_ERROR_INVALID_ENVIRONMENT = 116,
    JVMTI_ERROR_MAX = 116
} jvmtiError;
```

Pero vamos a ver QUÉ COJONES SI ACABO DE PONER LITERALMENTE `capabilities.can_set_breakpoint = 1;` Y SEGUÍA FALLANDO??

tras buscar en internet, todos decían que únicamente ese permiso era necesario, pero no me fiaba una mierda y entonces pues copy pasteé todos los permisos que existen y ponerlos a 1

```c
typedef struct {
    unsigned int can_tag_objects : 1;
    unsigned int can_generate_field_modification_events : 1;
    unsigned int can_generate_field_access_events : 1;
    unsigned int can_get_bytecodes : 1;
    unsigned int can_get_synthetic_attribute : 1;
    unsigned int can_get_owned_monitor_info : 1;
    unsigned int can_get_current_contended_monitor : 1;
    unsigned int can_get_monitor_info : 1;
    unsigned int can_pop_frame : 1;
    unsigned int can_redefine_classes : 1;
    unsigned int can_signal_thread : 1;
    unsigned int can_get_source_file_name : 1;
    unsigned int can_get_line_numbers : 1;
    unsigned int can_get_source_debug_extension : 1;
    unsigned int can_access_local_variables : 1;
    unsigned int can_maintain_original_method_order : 1;
    unsigned int can_generate_single_step_events : 1;
    unsigned int can_generate_exception_events : 1;
    unsigned int can_generate_frame_pop_events : 1;
    unsigned int can_generate_breakpoint_events : 1;
    unsigned int can_suspend : 1;
    unsigned int can_redefine_any_class : 1;
    unsigned int can_get_current_thread_cpu_time : 1;
    unsigned int can_get_thread_cpu_time : 1;
    unsigned int can_generate_method_entry_events : 1;
    unsigned int can_generate_method_exit_events : 1;
    unsigned int can_generate_all_class_hook_events : 1;
    unsigned int can_generate_compiled_method_load_events : 1;
    unsigned int can_generate_monitor_events : 1;
    unsigned int can_generate_vm_object_alloc_events : 1;
    unsigned int can_generate_native_method_bind_events : 1;
    unsigned int can_generate_garbage_collection_events : 1;
    unsigned int can_generate_object_free_events : 1;
    unsigned int can_force_early_return : 1;
    unsigned int can_get_owned_monitor_stack_depth_info : 1;
    unsigned int can_get_constant_pool : 1;
    unsigned int can_set_native_method_prefix : 1;
    unsigned int can_retransform_classes : 1;
    unsigned int can_retransform_any_class : 1;
    unsigned int can_generate_resource_exhaustion_heap_events : 1;
    unsigned int can_generate_resource_exhaustion_threads_events : 1;
    unsigned int can_generate_early_vmstart : 1;
    unsigned int can_generate_early_class_hook_events : 1;
    unsigned int can_generate_sampled_object_alloc_events : 1;
    unsigned int : 4;
    unsigned int : 16;
    unsigned int : 16;
    unsigned int : 16;
    unsigned int : 16;
    unsigned int : 16;
} jvmtiCapabilities;
```

La puta JVM seguía lanzando `JVMTI_ERROR_MUST_POSSESS_CAPABILITY` después de tener **LITERALMENTE TODOS LOS PERMISOS**
Al indagar más, me di cuenta de que el método SIEMPRE devolvería ese error si la JVM no estaba en debug mode/no tenía un agente JDWP asignado, y aquí es cuando me quería meter un disparo en la cabeza.

**Tras innumerables fracasos** tratanto de poner hardware breakpoints a la función para examinar los registros de la CPU en ese momento y sacar el valor de la variable que contenía la transacción, y hacer algoritmos de byte array scanning para cada JVM existente en el universo (no es coña) con el fin de encontrar donde la JVM guardaba el codigo inline de S32PacketConfirmTransaction, **encontré un rayo de esperanza**: el evento `onClassFileLoadHook`. Este evento nos permitía interceptar el bytecode de una clase justo antes de que fuera cargado por la JVM. Esto significaba que podíamos modificar el código directamente en su forma binaria antes de que el JIT pudiera hacer de las suyas.

Entonces, así fue cuando traté de poner el breakpoint pero ahora con onClassFileLoadHook. Justo antes de cargar la clase, tendría todos los permisos para poder modificarla a mi gusto, ¿SUPONGO?

```cpp
static void JNICALL breakpointCallback(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread, jmethodID method, jlocation location) {
    jobject thisObject;
    jvmtiError err = jvmti->GetLocalObject(thread, 0, 0, &thisObject);
    if (err != JVMTI_ERROR_NONE) {
        std::cerr << "Error al obtener el objeto 'this'" << std::endl;
        return;
    }

    jclass packetClass = jni->GetObjectClass(thisObject);
    if (packetClass == nullptr) {
        std::cerr << "Error al obtener la clase del objeto" << std::endl;
        return;
    }

    jfieldID fieldId = jni->GetFieldID(packetClass, "actionNumber", "S");
    if (fieldId == nullptr) {
        std::cerr << "Error al obtener el ID del campo 'actionNumber'" << std::endl;
        return;
    }

    jshort actionNumber = jni->GetShortField(thisObject, fieldId);
    std::cout << "RECIBIDO TRANSACTION ID FROM ANTICHEAT: " << actionNumber << std::endl;
}

static void JNICALL onClassFileLoadHook(jvmtiEnv *jvmti, JNIEnv *jni,
                                      jclass class_being_redefined, jobject loader,
                                      const char* name, jobject protection_domain,
                                      jint class_data_len, const unsigned char* class_data,
                                      jint* new_class_data_len, unsigned char** new_class_data) {
    if (name != nullptr && strcmp(name, "net/minecraft/network/play/server/S32PacketConfirmTransaction") == 0) {
        std::cout << "dentro de clase S32PacketConfirmTransaction" << std::endl;

        jclass loadedClass = jni->FindClass(name);
        if (loadedClass == nullptr) {
            std::cerr << "No se pudo encontrar la clase " << name << std::endl;
            return;
        }

        jmethodID processPacketMethod;
        jvmtiError err = jvmti->GetMethodID(loadedClass, "processPacket", "(Lnet/minecraft/network/play/INetHandlerPlayClient;)V", &processPacketMethod);
        if (err != JVMTI_ERROR_NONE) {
            std::cerr << "No se pudo obtener el ID del método processPacket" << std::endl;
            return;
        }

        err = jvmti->SetBreakpoint(processPacketMethod, 0);
        if (err != JVMTI_ERROR_NONE) {
            std::cerr << "No se pudo establecer el punto de interrupción" << std::endl;
        } else {
            std::cout << "Punto de interrupción establecido correctamente en processPacket" << std::endl;
        }
    }
}
```

Al parecer, no había forma de poner breakpoints, la única idea que se me pasaba por la cabeza en esos momentos de inmensa desesperación era... Reescribir y recompilar la propia clase de Java en caliente e inyectar mi propio bytecode como si fuese yo una JVM?

***Pero a donde cojones voy yo compilando mi propio bytecode para la JVM en tiempo real? Habría que descartar esta idea no? Es demasiado loca y sería extremedamente difícil...... No Requiem...??????????????***

Pues sí, la instrumentación de bytecode en crudo es una tarea endiabladamente compleja. No es tan simple como "insertar código aquí". Se requiere un conocimiento profundo de la estructura de los archivos de clase de Java y del conjunto de instrucciones de la JVM. Además y para colmo, **no había ninguna ayuda en útil para hacer instrumentación de bytecode en todo puto Internet**, casi todo lo tuve que averiguar (incluso adivinar en muchos casos) yo por mi cuenta.

Comenzó entonces una ardua fase de investigación. Descubrí que el proceso era mucho más enrevesado de lo que había imaginado. Pero antes que nada, crearemos la hook en el class loader:
```cpp
void JNICALL onClassFileLoadHook(jvmtiEnv* /*jvmti*/, JNIEnv* /*env*/, jclass /*class_being_redefined*/, jobject /*loader*/, const char* name, jobject /*protection_domain*/, jint class_data_len, const unsigned char* class_data, jint* new_class_data_len, unsigned char** new_class_data) {
    if (!name) return; // por si acaso cuz i found very silly stuff when debugging...

    try {
        if (strcmp(name, "net/minecraft/network/play/server/S32PacketConfirmTransaction") == 0) {
            Log(INFO, "Hooking class: S32PacketConfirmTransaction");
            ClassInstrumenter instrumenter(class_data, class_data_len);
            std::vector<unsigned char> new_bytes;
            instrumenter.instrument_and_get_bytes(new_bytes, "actionNumber", "readPacketData", "(Lnet/minecraft/network/PacketBuffer;)V", "logServerTransactionId");

            if (new_bytes.size() > 0x7FFFFFFF) {
                throw std::runtime_error("ha excedido el límite de un jint y no tengo ni idea de porqué.");
            }

            g_jvmti->Allocate(new_bytes.size(), new_class_data);
            memcpy(*new_class_data, new_bytes.data(), new_bytes.size());
            *new_class_data_len = static_cast<jint>(new_bytes.size());

            Log(SUCCESS, "S32PacketConfirmTransaction instrumented successfully.");
        }
    }
    catch (const std::exception& e) {
        Log(FATAL, "Failed to instrument %s: %s", name, e.what());
    }
}
```

Ahora, tenemos que crear nuestra función para el bytecode, `instrument_and_get_bytes`, sin usar apis externas ni agentes de java como dije.

Para los más experimentados, sí, podría haber hookeado funciones de red a bajo nivel como `WSARecv` y `WSARecvfrom`, pero eso me daría un torrente de datos sin procesar. Necesitaba contexto, saber exactamente cuándo se leía ese actionNumber específico y no interceptar toda la red. Quería una solución más elegante y precisa además de no cagarla en temas de rendimiento para que no me viniesen a llorar después diciendo que cuando se inyectaba el agente su MC bajaba a 20fps.

Empecé **sin tener ni puñetera idea de qué hacer**, pero bueno PARA EMPEZAR supuse que necesitaba un parser capaz de desensamblar la estructura de un archivo de clase compilado, crear mi propio método en la clase (seguramente un `public static native`), y de alguna forma escribir a mano las instrucciones de la JVM necesarias para tomar el valor de actionNumber y pasárselo a mi nuevo método nativo, que luego lo mandaría a una función para analizar las transacciones del servidor sin corromper la clase original.

Así que bueno, antes de escribir una sola línea de código, me sumergí en la fuente de toda verdad en este dominio: la **Especificación de la Máquina Virtual de Java** (la JVMS). Intentar hacer esto sin leer la JVMS es como navegar en una tormenta sin brújula ni mapa. Es un documento denso y que preferiría tener que tirarme de un puente antes de leerlo, pero absolutamente indispensable. Describe con precisión quirúrgica cada byte de un archivo .class, el comportamiento de cada opcode y las reglas que el verificador de la JVM impone.

Mi principal foco fue la sección "The class File Format".

## Nuestra Odisea por el mundo del Bytecode
Un archivo .class no es solo código; es una estructura de datos compleja. Y su corazón, su núcleo simbólico, es el constant_pool (la Piscina de Constantes, suena como el puto culo en español XD).

Imagina LA PISCINA de constantes como el glosario o el índice de un libro muy complejo. Es una tabla que contiene todas las constantes y referencias simbólicas que la clase necesita para funcionar. Esto incluye:

**Literales**, que son cadenas de texto (UTF-8), números enteros, flotantes.

**Referencias a Clases**, los nombres de clases como java/lang/Object.

**Referencias a Campos (Fields)**, el nombre de un campo (actionNumber) y su tipo de datos.

**Referencias a Métodos (Methods)**, el nombre de un método (readPacketData), sus parámetros y su tipo de retorno.

**Pares Nombre-y-Tipo (NameAndType)**, simplemente una estructura que vincula el nombre de un campo o método con su descriptor de tipo.

Cada entrada en esta tabla tiene un "tag" de 1 byte que identifica su tipo (`CONSTANT_Utf8`, `CONSTANT_Methodref`, `CONSTANT_Fieldref`, etc...), seguido de los datos correspondientes. Cuando el bytecode necesita realizar una operación, como llamar a un método o acceder a un campo, no usa nombres directamente. En su lugar, utiliza un índice de 2 bytes que apunta a la entrada correspondiente en LA PISCINA de constantes.

Para nuestra misión, esto era crucial. Para poder llamar a un nuevo método nativo, primero tenía que existir en el ***LA PISCINA DE CONSTANTES*** (ok ya paro).

### PASO 1: Inyectar la Declaración de mi método

Mi objetivo era crear el equivalente a esta declaración nativa en Java dentro de la clase:
```java
public static native void logServerTransactionId(int transactionId);
```

Para ello, tuve que manipular el `constant_pool` y la lista de métodos de la clase, así que empecé a crear prototipos de funciones como desagraciado para todo lo que necesitaba:

`add_utf8`: Añadí las cadenas de texto necesarias al `constant_pool`: el nombre de mi método `(logServerTransactionId)` y su descriptor `((I)V)`. El descriptor es una forma estandarizada de representar la firma de un método. (I) significa que toma un argumento de tipo `int`, y `V` significa que retorna `void`.

`add_name_and_type`, esto para crear una entrada `CONSTANT_NameAndType` que asocia el índice del nombre con el índice del descriptor como dije arriba que era necesario.

`add_method_ref`, para finalmente, crear la entrada `CONSTANT_Methodref`. Esta es la referencia completa y utilizable. Vincula la clase que contiene el método (nuestra clase actual, this_class) con la entrada `NameAndType` creada anteriormente.

**Crear method_info:** Con las referencias listas en el `constant_pool`, creé la estructura method_info que representa al nuevo método. Esto lo pude observar mejor en https://docs.oracle.com/javase/specs/jvms/se7/html/jvms-4.html, exactamente en la tabla del apartado 4.1:
```
Table 4.1. Class access and property modifiers

Flag Name	Value	Interpretation
ACC_PUBLIC	0x0001	Declared public; may be accessed from outside its package.
ACC_FINAL	0x0010	Declared final; no subclasses allowed.
ACC_SUPER	0x0020	Treat superclass methods specially when invoked by the invokespecial instruction.
ACC_INTERFACE	0x0200	Is an interface, not a class.
ACC_ABSTRACT	0x0400	Declared abstract; must not be instantiated.
ACC_SYNTHETIC	0x1000	Declared synthetic; not present in the source code.
ACC_ANNOTATION	0x2000	Declared as an annotation type.
ACC_ENUM	0x4000	Declared as an enum type.
```
Como quiero que sea público, estático y a la vez nativo, supuse que tendría que hacer una máscara de bits:
0x0001 (`ACC_PUBLIC`) + 0x0008 (`ACC_STATIC`) + 0x0100 (`ACC_NATIVE`), eso iría en el valor de `access_flags`

Lo más importante aquí es que un método native no tiene cuerpo de código en Java. Por lo tanto, su lista de atributos está vacía. No tiene un atributo Code, que es donde reside el bytecode. ¡ACKTUALLYY! Un error común es intentar añadir un cuerpo a un método nativo

### PASO 2 Y 3: Localizando Objetivos
Con nuestro método de callback listo, necesitaba encontrar dos cosas: el método `readPacketData` donde inyectaríamos el código y la referencia al campo `actionNumber`.

**Primero, tuve que encontrar el método.** Recorrí la lista de `method_info` de la clase (recuerden que estoy interceptando TODAS las clases antes de que se carguen, así que necesitamos antes comprobar cuándo se carga la nuestra), obteniendo el nombre y el descriptor de cada uno desde el `constant_pool` y comparándolos con mi objetivo: "`readPacketData`" y "`(Lnet/minecraft/network/PacketBuffer;)V`" (muy importante el V del final).

De manera similar, **había que encontrar el campo**, recorrí otra vez mi querido y odiado a la vez `constant_pool` buscando entradas con el tag `CONSTANT_Fieldref`. Pero necesitaba su valor, así que gracias a la documentación, supe que era el valor 9:

```
Java Virtual Machine instructions do not rely on the run-time layout of classes, interfaces, class instances, or arrays. Instead, instructions refer to symbolic information in the constant_pool table.

All constant_pool table entries have the following general format:

cp_info {
    u1 tag;
    u1 info[];
}
Each item in the constant_pool table must begin with a 1-byte tag indicating the kind of cp_info entry. The contents of the info array vary with the value of tag. The valid tags and their values are listed in Table 4.3. Each tag byte must be followed by two or more bytes giving information about the specific constant. The format of the additional information varies with the tag value.

Table 4.3. Constant pool tags

Constant Type	Value
CONSTANT_Class	7
CONSTANT_Fieldref	9
CONSTANT_Methodref	10
CONSTANT_InterfaceMethodref	11
CONSTANT_String	8
CONSTANT_Integer	3
CONSTANT_Float	4
CONSTANT_Long	5
CONSTANT_Double	6
CONSTANT_NameAndType	12
CONSTANT_Utf8	1
CONSTANT_MethodHandle	15
CONSTANT_MethodType	16
CONSTANT_InvokeDynamic	18
```

Para cada una, seguí los índices internos hasta dar con una cuyo nombre fuera "actionNumber". El índice de esta entrada CONSTANT_Fieldref (`target_field_ref_idx`) era el que necesitaba para el bytecode.

### PASO 4: Forjando mi expansión de dominio

Aquí es donde la magia ocurre. Necesitaba escribir una secuencia de opcodes de la JVM para cargar el valor de actionNumber y pasárselo a mi método. Antes de analizar el código, tenemos que entender la pieza central de la ejecución en la JVM: el stack, y sí, odio llamarlo la pila y no sé porqué, pero trataré de llamarlo así.

La JVM es una **máquina basada en pila** (joder qué horrible). A diferencia de las arquitecturas basadas en registros como x86, la mayoría de las instrucciones de la JVM toman sus operandos de una estructura de memoria llamada Operand Stack (Pila de Operandos) y depositan sus resultados en ella.

Cada vez que se invoca un método, se crea un nuevo **marco de pila (stack frame)**. Cada marco contiene, entre otras cosas, una pila de operandos privada para ese método. Piénsalo como la LIFO (Last-In, First-Out) por excelencia, una pila de platos:

**Push** -> Colocar un plato en la cima de la pila (cargar una variable).
**Pop** -> Quitar el plato de la cima (ej: usar un valor en una operación).
Las instrucciones de bytecode son los verbos que manipulan esta pila.
Con esto en mente, y ya con conocimientos muy profundos sobre cómo funciona el stack, empecé a escribir el bytecode:

Quería saber cómo obtener una referencia al objeto "this", porque recordamos para los más olvidadizos que la estructura del método a interceptar es:

```java
    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayClient handler)
    {
        handler.handleConfirmTransaction(this);
    }
```

Así que me puse a leer stackoverflow como un degenerado y encontré esta joya: https://stackoverflow.com/questions/4641416/jvm-instruction-aload-0-in-the-main-method-points-to-args-instead-of-this

El primer bytecode por tanto, debía ser bajo mi entendimiento, `0x2a (aload_0)`. En cualquier método de instancia (no estático) de Java, la variable local en el índice 0 es implícitamente una referencia al propio objeto (`this`). Esta instrucción empuja (`push`) la referencia this a la cima de la pila de operandos. Ahora tenemos `[this]` en la pila. Ahora ecesitamos esta referencia para poder acceder a un campo de instancia como `actionNumber`.

Gracias a que soy el dios del inglés (xd) supuse rápidamente que 'getfield' (obtener campo) era lo que buscaba en la documentación:
```
getfield
A getfield instruction with operand CP is type safe iff CP refers to a constant pool entry denoting a field whose declared type is FieldType, declared in a class FieldClass, and one can validly replace a type matching FieldClass with type FieldType on the incoming operand stack yielding the outgoing type state. FieldClass must not be an array type. protected fields are subject to additional checks (§4.10.1.8).

instructionIsTypeSafe(getfield(CP), Environment, _Offset, StackFrame,
                      NextStackFrame, ExceptionStackFrame) :- 
    CP = field(FieldClass, FieldName, FieldDescriptor),
    parseFieldDescriptor(FieldDescriptor, FieldType),
    passesProtectedCheck(Environment, FieldClass, FieldName,
                         FieldDescriptor, StackFrame),
    validTypeTransition(Environment, [class(FieldClass)], FieldType,
                        StackFrame, NextStackFrame),
    exceptionStackFrame(StackFrame, ExceptionStackFrame).
```
Al tratar de leer en binario cuál era el opcode de esta función en mi disassembler, me encontré con `1011 0100`, que básicamente es 0xb4 en hexadecimal. Esta instrucción lee un campo de un objeto. Toma un operando de 2 bytes: el `target_field_ref_idx` que encontramos antes. 

Tras leer en IDA cómo era el flujo de control aquí, hice un bonito resumen. La operación funciona así: saca (`pop`) la referencia al objeto (`this`) de la pila, busca el campo `actionNumber` dentro de ese objeto y empuja (`push`) su valor (`short`, pero se extiende a `int` en la pila) a la cima de la pila. El estado de la pila pasa de `[this]` a `[actionNumber_value]`.**Ahora solo quedaba invocar el método**

Otra vez gracias a mis increíbles conocimientos de inglés supremos, busqué "invoke" (invocar) y lo encontré igual XD:

```
invokestatic
An invokestatic instruction is type safe iff all of the following conditions hold:

Its first operand, CP, refers to a constant pool entry denoting a method named MethodName with descriptor Descriptor.

MethodName is not <init>.

MethodName is not <clinit>.

One can validly replace types matching the argument types given in Descriptor on the incoming operand stack with the return type given in Descriptor, yielding the outgoing type state.

instructionIsTypeSafe(invokestatic(CP), Environment, _Offset, StackFrame,
                      NextStackFrame, ExceptionStackFrame) :- 
    CP = method(_MethodClassName, MethodName, Descriptor),
    MethodName \= '<init>',
    MethodName \= '<clinit>',
    parseMethodDescriptor(Descriptor, OperandArgList, ReturnType), 
    reverse(OperandArgList, StackArgList),
    validTypeTransition(Environment, StackArgList, ReturnType,
                        StackFrame, NextStackFrame),
    exceptionStackFrame(StackFrame, ExceptionStackFrame).
```

Su opcode era 0xb8. Esta instrucción invoca un método estático pero también toma un operando de 2 bytes, que es el índice de la referencia a nuestro método nativo (`native_method_ref_idx`) en el `constant_pool`. Saca de la pila los argumentos que el método necesita (en nuestro caso, un int). Como `actionNumber_value` está en la cima, es consumido por la llamada. Entonces el estado de la pila pasa de `[actionNumber_value]` a `[]` (ya que nuestro método retorna void).

Pues nada, ya tenemos la inyección y lo probé.

Inyectar el código no era suficiente. Hay que asegurarse de que la JVM no se dé cuenta de que hemos metido mano. Este artículo me vino de mucha ayuda:
https://kamilachyla.com/posts/jvm-spec/chapter_4_checks/

La posición de la inyección es clave. Para `readPacketData`, quería leer el valor de `actionNumber` después de que se hubiera leído del búfer de red. Por eso busqué la instrucción return (`0xb1`, intenté 0xC3 como subnormal pensando que estaba operando con registros y fallé) e inyecté mi código justo antes, asegurando que el campo ya estaría poblado.

Citando la página web de arriba:
`At no point in execution can the operand stack grow to more than max_stack item
and also no more values can be popped from operand stack than it contains
a type of every value stored into an array by aastore must be a reference type`

En resumen, el atributo `Code` de un método contiene un valor `max_stack`, que le dice a la JVM el tamaño máximo que alcanzará la pila de operandos durante la ejecución de ese método. Es crucial para la asignación de memoria y la verificación. Mi secuencia de bytecode `aload_0 -> getfield` alcanza una profundidad máxima de 1 en la pila (para los más curiosos, así es como lo determiné: https://stackoverflow.com/questions/65604246/determining-the-size-of-the-operand-stack-for-a-stack-frame), pero el método original ya realiza operaciones que requieren una profundidad de al menos 2 cuando lo vi en mi disassembler (`aload_0`, `aload_1` para llamar a `buf.readShort()`). El código defensivamente asegura que max_stack sea al menos 2, cubriendo tanto las necesidades originales como las de mi inyección. Un max_stack incorrecto conduce inevitablemente a un `VerifyError`.

Si el método original tuviera bloques try-catch, tendría una **tabla de excepciones**. Esta tabla define rangos de bytecode (`start_pc`, `end_pc`) protegidos por un manejador de excepciones (`handler_pc`). Al insertar N bytes de código, todas las direcciones posteriores a mi punto de inyección se desplazan, y esto es algo que ya conocía de antes al haber trabajado mucho en redirección de excepciones y stack unwinding: https://github.com/NotRequiem/veh-disasm. Por tanto era absolutamente vital recorrer esta tabla y sumar N a cualquier `start_pc`, `end_pc` o `handler_pc` que sea mayor o igual a la posición de inyección. Ignorar este paso haría que los manejadores de excepciones apuntaran a instrucciones incorrectas, que es lo que pasó. Al principio pensé que el código no estaba dentro de un try-catch (un SEH: https://learn.microsoft.com/en-us/cpp/cpp/structured-exception-handling-c-cpp?view=msvc-170), pero al parecer sí había una función más arriba embediendo todo (no sé si esta palabra existe otra vez) dentro de un handler

### PASO 5: CARGARME A MAHORAGA

Por último, faltaba el golpe final. Para ello me apoyé en https://docs.huihoo.com/javaone/2007/java-se/TS-1326.pdf y descubrí la existencia de `StackMapTable` tal como mencionaba el post:
```
Class File Modification Problems
• Lots of serialization and deserialization details
• Constant pool management
• Missing or unused constants
• Managing constant pool indexes/references
• Jump offsets
• Inserting or removing instructions from the method
• Computation of stack size and StackMapTable
• Requires a control flow analysis
```

El stack size ya lo hemos explicado pero no sabía qué cojones era el StackMapTable, así que me puse a investigar.
El atributo StackMapTable es una optimización para el verificador de bytecode introducida en Java 6 (entonces minecraft lo tenía porque ejecutaba Java 8). Contiene mierdas varias sobre el estado de la pila y las variables locales en puntos clave del código (como los destinos de los saltos) para que el verificador me destruya cada vez que trate de inyectar bytecode.

Así que, *Shi', here we go again....*

El StackMapTable cambió el juego. Es esencialmente una serie de "puntos de control" o marcos de pila (StackMapFrames). Cada marco es una instantánea que describe el estado de la máquina virtual en un punto específico del bytecode. Concretamente, describe los tipos de datos que se encuentran en las variables locales y en la pila de operandos.

Estos marcos no se proporcionan para cada instrucción. Solo son necesarios para los destinos de los saltos. Por ejemplo, en un bucle while o en una sentencia if, el bytecode salta a una nueva ubicación. El StackMapTable proporciona un marco para ese punto de destino, diciéndole al verificador: "Oye, si llegas aquí, puedes estar seguro de que las variables locales y la pila se verán así". Esto permite al verificador saltar directamente a esos puntos y continuar su análisis sin tener que simular toda la ruta de código que llevó hasta allí.

Así que, siguiendo pasos similares a los de antes, y ayudándome en dos páginas web específicamente:
https://docs.oracle.com/en/java/javase/24/docs/api/java.base/java/lang/classfile/attribute/StackMapTableAttribute.html
https://stackoverflow.com/questions/37309074/what-is-stackmap-table-in-jvm-bytecode

El nuevo plan sería:
> 1. Borrar cualquier StackMapTable existente.
> 2. Determinar el estado inicial de las variables locales basándome en la firma del método.
> 3. Crear un único full_frame al inicio del método (offset 0) que describiera este estado inicial.
> 4. Empaquetar todo esto en un nuevo atributo StackMapTable.

Esto no me llevó ni un día, ni dos, ni tre... Bueno, sí fueron 3, pero fue muy díficil:
```cpp
 void rebuild_stack_map_table(code_attribute_data& code_attr, const std::string& method_desc) {
     const uint8_t ITEM_OBJECT = 7, ITEM_INTEGER = 1, ITEM_UNINITIALIZED_THIS = 6;

     // Primero, como siempre, nos deshacemos de cualquier tabla preexistente
     for (auto it = code_attr.attributes.begin(); it != code_attr.attributes.end(); ) {
         if (get_cp_string(it->name_index) == "StackMapTable") it = code_attr.attributes.erase(it); else ++it;
     }

     attribute_info smt_attr;
     smt_attr.name_index = add_utf8("StackMapTable"); // Añadir "StackMapTable" a LA PISCINA DE CONSTANTES (vale, lo dije otra vez)
     std::vector<unsigned char> frame_data;
     std::vector<std::pair<uint8_t, uint16_t>> locals; // Aquí almacenaremos los tipos de las variables locales

     // ¿Es un método de instancia? Si no tiene el flag ACC_STATIC...
     if (!(access_flags & 0x0008)) {
         // La primera variable local (índice 0) es 'this' como ya explicamos arriba. Al inicio del método, está sin inicializar.
         locals.push_back({ ITEM_UNINITIALIZED_THIS, static_cast<uint16_t>(0) });
     }

     // Ahora, analizamos el descriptor del método para encontrar los parámetros
     for (const char* d = method_desc.c_str() + 1; *d != ')'; ++d) {
         if (*d == 'L') { // Es una referencia a un objeto
             const char* end = strchr(d, ';');
             // Añadimos el tipo de objeto (su nombre de clase) a la CP (constant pool por si acaso) y guardamos la referencia
             locals.push_back({ ITEM_OBJECT, add_class(std::string(d + 1, end - d - 1)) });
             d = end;
         }
         else if (*d == '[') { // Es un array
             const char* start = d; while (*d == '[') d++; if (*d == 'L') d = strchr(d, ';');
             locals.push_back({ ITEM_OBJECT, add_class(std::string(start, d - start + 1)) });
         }
         else { // si no es nada de eso, SUPONGO que es un tipo primitivo (I, S, B, C, Z -> Integer)
             locals.push_back({ ITEM_INTEGER, static_cast<uint16_t>(0) });
             if (*d == 'J' || *d == 'D') { // Con la excepción de que Longs y Doubles ocupan dos espacios en las locales
                 locals.push_back({ static_cast<uint8_t>(0)/*TOP*/, static_cast<uint16_t>(0) });
             }
         }
     }

     // Empezamos a construir el binario de la tabla
     write_u2(frame_data, 1); // num_entries = 1. Solo vamos a escribir un marco
     frame_data.push_back(255); // frame_type = full_frame. Es el más explícito
     write_u2(frame_data, 0); // offset_delta = 0. Nuestro marco está al inicio del código
     write_u2(frame_data, static_cast<uint16_t>(locals.size())); // Escribimos el número de locales

     // Escribimos cada tipo de variable local
     for (size_t i = 0; i < locals.size(); ++i) {
         frame_data.push_back(locals[i].first); // El tag del tipo
         if (locals[i].first == ITEM_OBJECT) write_u2(frame_data, locals[i].second); // Si es un objeto, su índice en la CP
     }

     // No hay nada en la pila de operandos al inicio del método así que ok
     write_u2(frame_data, 0); // básicamente, quedará como number_of_stack_items = 0
     
     smt_attr.info = frame_data;
     code_attr.attributes.push_back(smt_attr);
 }
 ```

Este código es un reflejo de mi derrame de sangre.....: entendí los tipos (`ITEM_OBJECT`, `ITEM_INTEGER`), cómo derivar las variables locales iniciales a partir de la firma del método, y cómo construir un full_frame que es el tipo de marco más detallado. Parecía PERFECTO

Con mi nuevo y reluciente rebuild_stack_map_table, modifiqué mi proceso. Inyecté el bytecode, actualicé los offsets de la tabla de excepciones, ajusté el max_stack, bla bla bla todo como hasta ahora y luego llamé a mi flamante función para generar un StackMapTable teóricamente perfecto.
Cargué la clase... y...

**¿Pero por qué?** Mi lógica para el marco inicial era sólida como una roca. El full_frame en el offset 0 describía con precisión el estado inicial de las variables locales. El error no estaba en el marco que creé, sino en *todos los marcos que no creé*.

Aquí está la brutal revelación: **un StackMapTable necesita un marco para CADA destino de salto dentro del método XD**

Mi código inyectado era lineal, no introducía jumps. Pero el método original, readPacketData, los tenía...? O, más importante aún, otros métodos que quisiera instrumentar en el futuro casi con seguridad los tendrían (bucles `for`, `while`, bloques `try-catch` que saltan a manejadores de excepciones).
Mi inyección de código desplazó todas las instrucciones posteriores. Si el código original tenía un `goto 100`, y yo inyectaba 7 bytes en la posición 20, ese `goto` ahora debería apuntar a 107. Aunque el actualizador de bytecode podría arreglar la instrucción `goto`, el StackMapTable original seguiría teniendo una entrada para el offset 100, que ahora es incorrecto.

O bien escribir un analizador de flujo de datos completo en C++, una tarea monstruosamente compleja que rivalizaría con el propio verificador de la JVM, o ser inteligente.

Fue entonces cuando la idea original, la que había descartado como "un truco", volvió a mí....
***"Espera... si las clases compiladas con versiones de Java anteriores a la 6 no tienen un StackMapTable, la JVM debe saber cómo manejarlas. Debe tener un plan B".***

JODER QUE PUTO GENIO SOY JODER JODER

Si una JVM moderna carga un archivo .class que especifica una versión reciente (Java 6+) pero carece de un atributo StackMapTable, no lanza un error. En su lugar, considera que el atributo es opcional (para la compatibilidad hacia atrás). Ante su ausencia, el verificador simplemente se encoge de hombros y dice: *"Bueno, supongo que tendré que hacer el trabajo a la antigua"*. Y entonces, realiza el análisis de flujo de datos completo calculando internamente toda la información que habría estado en la tabla.

### IMAGINARY TECHNIQUE: PURPLEEEE

La solución elegante fue castear simplemente eliminar el atributo por completo. Cuando una JVM moderna carga una clase que carece de este atributo, su verificador entra en un modo de compatibilidad hacia atrás, analiza el bytecode desde cero y recalcula toda la información de verificación él mismo

La conclusión fue a la vez humillante y liberadora. Mi intento de reconstruir el StackMapTable era un ejercicio de pura arrogancia. La verdadera solución de ingeniería no era recrear un sistema complejo y propenso a errores, sino entender las reglas del sistema más grande (la JVM) y usar su propio mecanismo de respaldo a mi favor ;).

Eliminar el StackMapTable no es un "jaks" como diría mi amiga Ale. Es una delegación de responsabilidad. Es reconocer que la JVM es mucho mejor calculando esa tabla que mi código en C++, y simplemente dejar que lo haga. Es una solución elegante, robusta y, lo que es más importante, correcta el 100% de las veces.

*Hoy hemos aprendido que, a veces, la línea de código más poderosa es la que se borra.*

### PASO FINAL!!: La Serialización
Con la clase totalmente parcheada, el último paso era volver a escribir toda la estructura modificada (magic, version, el nuevo constant_pool, la lista de métodos modificada, etc.) en un vector de bytes (out_bytes), listo para ser cargado por la JVM y ejecutar nuestro código nativo.

- ¿Profesor? No sabíamos que estábamos en un museo. 
- Pues claro que no lo estamos
- Entonces, ¿qué hacen estas dos obras de arte aquí? El primero yo, y el segundo...

 ESTE CÓDIGO

```cpp
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
```

**Aunque, sin embargo...**

Y así, camaradas, mi odisea en este enfrentamiento épico terminó.

Aunque no hayamos ganado completamente la batalla, esto no significa el fin. La derrota de un individuo, por más poderoso que sea, no extingue la llama de una causa si esta ha sido transmitida a otros. La muerte es una de esas sombras, una aparente pérdida que oculta una verdad más luminosa: el poder de la herencia y la continuidad de la voluntad a través de las generaciones. Con esto dicho, transfiero este proyecto a la comunidad para que puedan hacer uso de su poder... Y, quién sabe, quizás algún día ganar la guerra y conseguir un método de inyección binaria perfecto para cualquier JVM que exista en la faz de nuestro querido planeta.

El viaje fue largo y plagado de tecnicismos, pero el resultado es una técnica de una potencia y elegancia extraordinarias. Partimos de querer registrar un entero y terminamos diseccionando y reensamblando el tejido mismo de un programa Java en ejecución. Este proceso demuestra que, con la adecuada perseverancia, no hay caja negra que no podamos abrir.