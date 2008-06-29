
#include "jni.h"

#include "jni.h"

#include <windows.h>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <cstring>
#include "Main.h"
#include "Utils.h"
#include "cbas_CodeEditor.h"

using namespace std;

bool init = false;

StaticInfo *staticInfo;
int loadCount = 0;

map<string, HINSTANCE> libs;

jmethodID map_contains;
jmethodID map_get;
jmethodID map_put;
jmethodID map_remove;

jfieldID threadsID;

jclass Integer;
jmethodID integer_intValue;
jmethodID integer_new;

extern "C" JNIEXPORT void JNICALL Java_cbas_CodeEditor_implNativeInit(JNIEnv *env, jobject pthis) {
    if (!init) {
        init = true;
        staticInfo = new StaticInfo;
        staticInfo->active = false;
        staticInfo->messageLoopHandle = CreateThread(0, 0, &messageLoop, 0, 0, &staticInfo->messageLoopID);
        staticInfo->vm = 0;
        while (!staticInfo->active) {
            Sleep(50);
        }
    }
    loadCount++;
    env->GetJavaVM(&staticInfo->vm);
    SendMessage(staticInfo->messageGlobalHWND, WM_COMMAND, ATTACH_JVM, (long)pthis);
    jclass Map = env->FindClass("java/util/Map");
    map_contains = env->GetMethodID(Map, "containsKey", "(Ljava/lang/Object;)Z");
    map_get = env->GetMethodID(Map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
    map_put = env->GetMethodID(Map, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    map_remove = env->GetMethodID(Map, "remove", "(Ljava/lang/Object;)Ljava/lang/Object;");
    env->DeleteLocalRef(Map);
    jclass CodeEditor = env->GetObjectClass(pthis);
    threadsID = env->GetFieldID(CodeEditor, "threads", "Ljava/util/Map;");
    env->DeleteLocalRef(CodeEditor);
    Integer = (jclass) env->NewGlobalRef(env->FindClass("java/lang/Integer"));
    integer_intValue = env->GetMethodID(Integer, "intValue", "()I");
    integer_new = env->GetMethodID(Integer, "<init>", "(I)V");
    CloseHandle(CreateThread(0, 0, &launchMonitor, (void*) env->NewGlobalRef(pthis), 0, 0));
}

extern "C" JNIEXPORT void JNIEXPORT Java_cbas_CodeEditor_implPause(JNIEnv *env, jobject pthis, jstring jname) {
    jobject threads = env->GetObjectField(pthis, threadsID);
    jobject threadInt = env->CallObjectMethod(threads, map_get, jname);
    if (threadInt)
        SuspendThread((HANDLE) env->CallIntMethod(threadInt, integer_intValue));
    env->DeleteLocalRef(threads);
    env->DeleteLocalRef(threadInt);
}

extern "C" JNIEXPORT void JNIEXPORT Java_cbas_CodeEditor_implStop(JNIEnv *env, jobject pthis, jstring jname) {
    jobject threads = env->GetObjectField(pthis, threadsID);
    jobject threadInt = env->CallObjectMethod(threads, map_get, jname);
    if (threadInt)
        TerminateThread((HANDLE) env->CallIntMethod(threadInt, integer_intValue), 1337);
    env->DeleteLocalRef(threads);
    env->DeleteLocalRef(threadInt);
}

extern "C" JNIEXPORT void JNIEXPORT Java_cbas_CodeEditor_implReleaseLib(JNIEnv *env, jobject pthis, jstring path) {
    const char* chars = env->GetStringUTFChars(path,0);
    string str(chars);
    env->ReleaseStringUTFChars(path,chars);
    FreeLibrary(libs[str]);
    libs[str] = 0;
}

typedef unsigned long (*scriptmain)(void* data) __attribute__((stdcall));

extern "C" JNIEXPORT jint JNIEXPORT Java_cbas_CodeEditor_implRun(JNIEnv *env, jobject pthis, jstring jname) {
    jobject threads = env->GetObjectField(pthis, threadsID);
    jobject threadInt = env->CallObjectMethod(threads, map_get, jname);
    jint res = 0;
    if (threadInt) {
        ResumeThread((HANDLE) env->CallIntMethod(threadInt, integer_intValue));
    } else {
        const char* chars = env->GetStringUTFChars(jname, 0);
        string name((char*) chars);
        char path[MAX_PATH];
        strcpy(path,chars);
        env->ReleaseStringUTFChars(jname, chars);
        HINSTANCE h = libs[name];
        if (libs[name] == 0) {
            h = LoadLibrary(path);
            libs[name] = h;
        }
        scriptmain _impl_scriptmain = (scriptmain) GetProcAddress(h, "_impl_scriptmain@4");
        if (!_impl_scriptmain) {
            res = 1;
            goto Die;
        }
        jint handle = (jint) CreateThread(0, 0, _impl_scriptmain, (void*) staticInfo, 0, 0);
        jobject integer = env->NewObject(Integer, integer_new, handle);
        jobject threads = env->GetObjectField(pthis, threadsID);
        env->CallVoidMethod(threads,map_put,jname,integer);
        env->DeleteLocalRef(integer);
        env->DeleteLocalRef(threads);
    }
Die:
    env->DeleteLocalRef(threads);
    env->DeleteLocalRef(threadInt);
    return res;
}

extern "C" JNIEXPORT void JNIEXPORT Java_cbas_CodeEditor_implExit(JNIEnv *env, jobject pthis) {
    if (loadCount == 1 && init) {
        env->DeleteGlobalRef(Integer);
        CloseHandle(staticInfo->messageLoopHandle);
        SendMessage(staticInfo->messageGlobalHWND, WM_CLOSE, 0, 0);
        init = false;
    }
    if (loadCount > 0) loadCount--;
}

void println(char* str) {
    SendMessage(staticInfo->messageGlobalHWND, WM_COMMAND, PRINT_LN, (long) str);
}

long GlobalProc(HWND hwnd, unsigned int Message, unsigned int wParam, long lParam) {
    static JNIEnv* env = 0;
    static jobject out = 0;
    static jmethodID println = 0;
    switch (Message) {
        case WM_CLOSE:
            if (env) staticInfo->vm->DetachCurrentThread();
            DestroyWindow(hwnd);
            PostQuitMessage(0);
            staticInfo->active = false;
            break;
        case WM_COMMAND:
            switch (wParam) {
                case CREATE_WINDOW:
                    CreateWindowData* data = reinterpret_cast<CreateWindowData*> (lParam);
                    *data->hwndRef = CreateWindowEx(data->dwStyle, data->lpClassName, data->lpWindowName, data->dwStyle, data->x, data->y, data->nWidth, data->nHeight, data->hWndParent, data->hMenu, data->hInstance, data->lpParam);
                    break;
                case ATTACH_JVM:
                    if (env) staticInfo->vm->DetachCurrentThread();
                    staticInfo->vm->AttachCurrentThread((void**) & env, 0);
                    jobject pthis = (jobject)lParam;
                    jclass CodeEditor = env->GetObjectClass(pthis);
                    jfieldID outID = env->GetFieldID(CodeEditor, "out", "Ljava/io/PrintStream;");
                    out = env->GetObjectField(pthis, outID);
                    env->DeleteLocalRef(CodeEditor);
                    jclass PrintStream = env->FindClass("java/io/PrintStream");
                    println = env->GetMethodID(PrintStream, "println", "(Ljava/lang/String;)V");
                    env->DeleteLocalRef(PrintStream);
                    break;
                case PRINT_LN:
                    if (env) {
                        jstring str = env->NewStringUTF((char*) lParam);
                        env->CallVoidMethod(out, println, str);
                        env->DeleteLocalRef(str);
                    }
                    break;
            }
    }
    return DefWindowProc(hwnd, Message, wParam, lParam);
}

unsigned long launchMonitor(void* threadData) {
    JavaVM* myVM = staticInfo->vm;
    JNIEnv* env;
    myVM->AttachCurrentThread((void**) & env, 0);
    jobject pthis = (jobject) threadData;
    jclass CodeEditor = env->GetObjectClass(pthis);
    jfieldID living = env->GetFieldID(CodeEditor, "living", "Z");
    jmethodID finished = env->GetMethodID(CodeEditor, "finished", "(Ljava/lang/String;I)V");
    env->DeleteLocalRef(CodeEditor);
    jclass Map = env->FindClass("java/util/Map");
    jmethodID keySet = env->GetMethodID(Map,"keySet","()Ljava/util/Set;");
    env->DeleteLocalRef(Map);
    jclass Set = env->FindClass("java/util/Set");
    jmethodID iterator = env->GetMethodID(Set,"iterator","()Ljava/util/Iterator;");
    env->DeleteLocalRef(Set);
    jclass Iterator = env->FindClass("java/util/Iterator");
    jmethodID next = env->GetMethodID(Iterator,"next","()Ljava/lang/Object;");
    jmethodID hasNext = env->GetMethodID(Iterator,"hasNext","()Z");
    env->DeleteLocalRef(Iterator);
    jobject threads = env->NewGlobalRef(env->GetObjectField(pthis, threadsID));
    while (env->GetBooleanField(pthis, living)) {
        jobject set = env->CallObjectMethod(threads,keySet);
        jobject iter = env->CallObjectMethod(set,iterator);
        while (env->CallBooleanMethod(iter,hasNext)) {
            jstring path = (jstring)env->CallObjectMethod(iter,next);
            jobject integer = env->CallObjectMethod(threads,map_get,path);
            HANDLE thread = (HANDLE) env->CallIntMethod(integer,integer_intValue);
            env->DeleteLocalRef(integer);
            unsigned long res;
            GetExitCodeThread(thread, &res);
            if (res != STILL_ACTIVE) {
                CloseHandle(thread);
                env->CallVoidMethod(pthis,finished,path,(jint)res);
                env->CallVoidMethod(threads,map_remove,path);
                break;
            }
            env->DeleteLocalRef(path);
        }
        env->DeleteLocalRef(iter);
        env->DeleteLocalRef(set);
        Sleep(500);
    }
    env->DeleteGlobalRef(threads);
    env->DeleteGlobalRef(pthis);
    staticInfo->vm->DetachCurrentThread();
    return 0;
}

unsigned long messageLoop(void* threadData) {
    WNDCLASSEX wndclass;
    memset(&wndclass, 0, sizeof (WNDCLASSEX));
    wndclass.cbSize = sizeof (WNDCLASSEX);
    wndclass.lpfnWndProc = GlobalProc;
    wndclass.hCursor = LoadCursor(0, IDC_ARROW);
    wndclass.lpszClassName = "GHOST_WINDOW";
    wndclass.hIconSm = 0;
    RegisterClassEx(&wndclass);
    staticInfo->messageGlobalHWND = CreateWindowEx(0, "GHOST_WINDOW", "CBAS-GhostWindow", 0, 0, 0, 0, 0, 0, 0, 0, 0);
    staticInfo->active = true;
    MSG msg;
    while (GetMessage(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}