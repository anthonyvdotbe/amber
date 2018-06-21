/*
 * Copyright (c) 2003, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include <string.h>
#include "jvmti.h"
#include "agent_common.h"
#include "jni_tools.h"
#include "jvmti_tools.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================= */

/* scaffold objects */
static jlong timeout = 0;

/* constant names */
#define DEBUGEE_CLASS_NAME    "nsk/jvmti/ClassFileLoadHook/classfloadhk002"
#define TESTED_CLASS_NAME     "nsk/jvmti/ClassFileLoadHook/classfloadhk002r"
#define TESTED_CLASS_SIG      "L"TESTED_CLASS_NAME";"
#define BYTECODE_FIELD_SIG    "[B"
#define ORIG_BYTECODE_FIELD_NAME  "origClassBytes"

static unsigned char* origClassBytes = NULL;
static jint origClassSize = 0;

static volatile int eventsCount = 0;

/* ============================================================================= */

/** Check (strictly or not) if bytecode has expected size and bytes or complain an error. */
static int checkBytecode(const char kind[], jint size, const unsigned char bytes[],
                            jint expectedSize, const unsigned char expectedBytes[],
                            int strict) {
    int success = NSK_TRUE;

    NSK_DISPLAY3("Check %s bytecode: 0x%p:%d\n", kind, (void*)bytes, (int)size);
    if (nsk_getVerboseMode()) {
        nsk_printHexBytes("   ", 16, size, bytes);
    }

    if (bytes == NULL) {
        NSK_COMPLAIN2("Unexpected NULL pointer to %s bytecode in CLASS_FILE_LOAD_HOOK: 0x%p\n",
                                                            kind, (void*)bytes);
        return NSK_FALSE;
    }

    if (size <= 0) {
        NSK_COMPLAIN2("Unexpected zero size of %s bytecode in CLASS_FILE_LOAD_HOOK: %d\n",
                                                            kind, (int)size);
        return NSK_FALSE;
    }

    if (strict) {
        if (size != expectedSize) {
            NSK_COMPLAIN3("Unexpected size of %s bytecode in CLASS_FILE_LOAD_HOOK:\n"
                          "#   got size: %d\n"
                          "#   expected: %d\n",
                            kind, (int)size, (int)expectedSize);
            success = NSK_FALSE;
        } else {
            jint different = 0;
            jint i;

            for (i = 0; i < size; i++) {
                if (bytes[i] != expectedBytes[i]) {
                    different++;
                }
            }
            if (different > 0) {
                NSK_COMPLAIN2("Unexpected bytes in %s bytecode in CLASS_FILE_LOAD_HOOK:\n"
                              "#   different bytes: %d\n"
                              "#   total bytes:     %d\n",
                                (int)different, (int)size);
                success = NSK_FALSE;
            }
        }

        if (!success) {
            NSK_COMPLAIN2("Got %s bytecode is not equal to expected bytecode: %d bytes\n",
                                                                        kind, expectedSize);
            if (nsk_getVerboseMode()) {
                nsk_printHexBytes("   ", 16, expectedSize, expectedBytes);
            }
        } else {
            NSK_DISPLAY1("All %s bytecode is equal to expected one\n", kind);
        }
    }

    return success;
}

/** Get classfile bytecode from a static field of given class. */
static int getBytecode(jvmtiEnv* jvmti, JNIEnv* jni, jclass cls,
                                    const char fieldName[], const char fieldSig[],
                                    jint* size, unsigned char* *bytes) {

    jfieldID fieldID = NULL;
    jbyteArray array = NULL;
    jbyte* elements;
    int i;

    NSK_DISPLAY1("Find static field: %s\n", fieldName);
    if (!NSK_JNI_VERIFY(jni, (fieldID =
            NSK_CPP_STUB4(GetStaticFieldID, jni, cls, fieldName, fieldSig)) != NULL)) {
        nsk_jvmti_setFailStatus();
        return NSK_FALSE;
    }
    NSK_DISPLAY1("  ... got fieldID: 0x%p\n", (void*)fieldID);

    NSK_DISPLAY1("Get classfile bytes array from static field: %s\n", fieldName);
    if (!NSK_JNI_VERIFY(jni, (array = (jbyteArray)
            NSK_CPP_STUB3(GetStaticObjectField, jni, cls, fieldID)) != NULL)) {
        nsk_jvmti_setFailStatus();
        return NSK_FALSE;
    }
    NSK_DISPLAY1("  ... got array object: 0x%p\n", (void*)array);

    if (!NSK_JNI_VERIFY(jni, (*size =
            NSK_CPP_STUB2(GetArrayLength, jni, array)) > 0)) {
        nsk_jvmti_setFailStatus();
        return NSK_FALSE;
    }
    NSK_DISPLAY1("  ... got array size: %d bytes\n", (int)*size);

    {
        jboolean isCopy;
        if (!NSK_JNI_VERIFY(jni, (elements =
                NSK_CPP_STUB3(GetByteArrayElements, jni, array,
                                                            &isCopy)) != NULL)) {
            nsk_jvmti_setFailStatus();
        return NSK_FALSE;
        }
    }
    NSK_DISPLAY1("  ... got elements list: 0x%p\n", (void*)elements);

    if (!NSK_JVMTI_VERIFY(
            NSK_CPP_STUB3(Allocate, jvmti, *size, bytes))) {
        nsk_jvmti_setFailStatus();
        return NSK_FALSE;
    }
    NSK_DISPLAY1("  ... created bytes array: 0x%p\n", (void*)*bytes);

    for (i = 0; i < *size; i++) {
        (*bytes)[i] = (unsigned char)elements[i];
    }
    NSK_DISPLAY1("  ... copied bytecode: %d bytes\n", (int)*size);

    NSK_DISPLAY1("Release elements list: 0x%p\n", (void*)elements);
    NSK_TRACE(NSK_CPP_STUB4(ReleaseByteArrayElements, jni, array, elements, JNI_ABORT));
    NSK_DISPLAY0("  ... released\n");

    return NSK_TRUE;
}

/* ============================================================================= */

/** Agent algorithm. */
static void JNICALL
agentProc(jvmtiEnv* jvmti, JNIEnv* jni, void* arg) {
    NSK_DISPLAY0("Wait for debuggee to become ready\n");
    if (!NSK_VERIFY(nsk_jvmti_waitForSync(timeout)))
        return;

    /* perform testing */
    {
        NSK_DISPLAY0(">>> Obtain original bytecode of tested class\n");
        {
            jclass debugeeClass = NULL;

            NSK_DISPLAY1("Find debugee class: %s\n", DEBUGEE_CLASS_NAME);
            if (!NSK_JNI_VERIFY(jni, (debugeeClass =
                    NSK_CPP_STUB2(FindClass, jni, DEBUGEE_CLASS_NAME)) != NULL)) {
                nsk_jvmti_setFailStatus();
                return;
            }
            NSK_DISPLAY1("  ... found class: 0x%p\n", (void*)debugeeClass);

            if (!NSK_VERIFY(getBytecode(jvmti, jni, debugeeClass,
                                        ORIG_BYTECODE_FIELD_NAME,
                                        BYTECODE_FIELD_SIG,
                                        &origClassSize, &origClassBytes)))
                return;
        }

        NSK_DISPLAY0(">>> Testcase #1: Load tested class and check CLASS_FILE_LOAD_HOOK event\n");
        {
            jvmtiEvent event = JVMTI_EVENT_CLASS_FILE_LOAD_HOOK;

            NSK_DISPLAY1("Enable event: %s\n", "CLASS_FILE_LOAD_HOOK");
            if (!NSK_VERIFY(nsk_jvmti_enableEvents(JVMTI_ENABLE, 1, &event, NULL)))
                return;
            NSK_DISPLAY0("  ... event enabled\n");

            NSK_DISPLAY0("Let debugee to load tested class\n");
            if (!NSK_VERIFY(nsk_jvmti_resumeSync()))
                return;
            NSK_DISPLAY0("Wait for tested class to be loaded\n");
            if (!NSK_VERIFY(nsk_jvmti_waitForSync(timeout)))
                return;

            NSK_DISPLAY1("Disable event: %s\n", "CLASS_FILE_LOAD_HOOK");
            if (NSK_VERIFY(nsk_jvmti_enableEvents(JVMTI_DISABLE, 1, &event, NULL))) {
                NSK_DISPLAY0("  ... event disabled\n");
            }

            NSK_DISPLAY1("Check if event was received: %s\n", "CLASS_FILE_LOAD_HOOK");
            if (eventsCount != 1) {
                NSK_COMPLAIN3("Unexpected number of %s events for tested class:\n"
                              "#   got events: %d\n"
                              "#   expected:   %d\n",
                                "CLASS_FILE_LOAD_HOOK",
                                eventsCount, 1);
                nsk_jvmti_setFailStatus();
            } else {
                NSK_DISPLAY1("  ... received: %d events\n", eventsCount);
            }
        }

        NSK_DISPLAY0(">>> Clean used data\n");
        {
            NSK_DISPLAY1("Deallocate bytecode array: 0x%p\n", (void*)origClassBytes);
            if (!NSK_JVMTI_VERIFY(
                        NSK_CPP_STUB2(Deallocate, jvmti, origClassBytes))) {
                nsk_jvmti_setFailStatus();
            }
        }
    }

    NSK_DISPLAY0("Let debugee to finish\n");
    if (!NSK_VERIFY(nsk_jvmti_resumeSync()))
        return;
}

/* ============================================================================= */

/** Callback for CLASS_FILE_LOAD_HOOK event **/
static void JNICALL
callbackClassFileLoadHook(jvmtiEnv *jvmti_env, JNIEnv *env,
                            jclass class_being_redefined,
                            jobject loader, const char* name, jobject protection_domain,
                            jint class_data_len, const unsigned char* class_data,
                            jint *new_class_data_len, unsigned char** new_class_data) {

    NSK_DISPLAY5("  <CLASS_FILE_LOAD_HOOK>: name: %s, loader: 0x%p, redefined: 0x%p, bytecode: 0x%p:%d\n",
                        nsk_null_string(name), (void*)loader, (void*)class_being_redefined,
                        (void*)class_data, (int)class_data_len);

    if (name != NULL && (strcmp(name, TESTED_CLASS_NAME) == 0)) {
        NSK_DISPLAY1("SUCCESS! CLASS_FILE_LOAD_HOOK for tested class: %s\n", TESTED_CLASS_NAME);
        eventsCount++;

        NSK_DISPLAY1("Check class_being_redefined: 0x%p\n", (void*)class_being_redefined);
        if (class_being_redefined != NULL) {
            NSK_COMPLAIN1("Unexpected not NULL class_being_redefined in CLASS_FILE_LOAD_HOOK: 0x%p\n",
                                                    (void*)class_being_redefined);
            nsk_jvmti_setFailStatus();
        }

        if (!checkBytecode("original", class_data_len, class_data,
                                        origClassSize, origClassBytes, NSK_TRUE)) {
            nsk_jvmti_setFailStatus();
        }
    }
}

/* ============================================================================= */

/** Agent library initialization. */
#ifdef STATIC_BUILD
JNIEXPORT jint JNICALL Agent_OnLoad_classfloadhk002(JavaVM *jvm, char *options, void *reserved) {
    return Agent_Initialize(jvm, options, reserved);
}
JNIEXPORT jint JNICALL Agent_OnAttach_classfloadhk002(JavaVM *jvm, char *options, void *reserved) {
    return Agent_Initialize(jvm, options, reserved);
}
JNIEXPORT jint JNI_OnLoad_classfloadhk002(JavaVM *jvm, char *options, void *reserved) {
    return JNI_VERSION_1_8;
}
#endif
jint Agent_Initialize(JavaVM *jvm, char *options, void *reserved) {
    jvmtiEnv* jvmti = NULL;

    /* init framework and parse options */
    if (!NSK_VERIFY(nsk_jvmti_parseOptions(options)))
        return JNI_ERR;

    timeout = nsk_jvmti_getWaitTime() * 60 * 1000;

    /* create JVMTI environment */
    if (!NSK_VERIFY((jvmti =
            nsk_jvmti_createJVMTIEnv(jvm, reserved)) != NULL))
        return JNI_ERR;

    NSK_DISPLAY1("Add required capability: %s\n", "can_generate_eraly_class_hook_events");
    {
        jvmtiCapabilities caps;

        memset(&caps, 0, sizeof(caps));
        caps.can_generate_all_class_hook_events = 1;
        if (!NSK_JVMTI_VERIFY(
                NSK_CPP_STUB2(AddCapabilities, jvmti, &caps))) {
            return JNI_ERR;
        }
    }
    NSK_DISPLAY0("  ... added\n");

    NSK_DISPLAY1("Set callback for event: %s\n", "CLASS_FILE_LOAD_HOOK");
    {
        jvmtiEventCallbacks callbacks;
        jint size = (jint)sizeof(callbacks);

        memset(&callbacks, 0, sizeof(callbacks));
        callbacks.ClassFileLoadHook = callbackClassFileLoadHook;
        if (!NSK_JVMTI_VERIFY(
                NSK_CPP_STUB3(SetEventCallbacks, jvmti, &callbacks, size))) {
            return JNI_ERR;
        }
    }
    NSK_DISPLAY0("  ... set\n");

    /* register agent proc and arg */
    if (!NSK_VERIFY(nsk_jvmti_setAgentProc(agentProc, NULL)))
        return JNI_ERR;

    return JNI_OK;
}

/* ============================================================================= */

#ifdef __cplusplus
}
#endif
