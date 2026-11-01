#include "common/mpq.h"
#include <jni.h>

/* Validate staged imports with the same archive reader used by the game. */
JNIEXPORT jboolean JNICALL Java_org_openrealm_wc3_LauncherActivity_checkArchive(JNIEnv *env, jclass cls, jstring path) {
    (void)cls;
    char const *name = (*env)->GetStringUTFChars(env, path, NULL);
    if (!name) return JNI_FALSE;
    handle_t archive = NULL;
    bool ok = SFileOpenArchive(name, 0, 0, &archive);
    if (ok) SFileCloseArchive(archive);
    (*env)->ReleaseStringUTFChars(env, path, name);
    return ok;
}
