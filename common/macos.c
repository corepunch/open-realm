#include "common.h"
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

void Sys_MkDir(LPCSTR directory){
#ifdef _WIN32
    _mkdir(directory);
#else
    mkdir(directory, 0777);
#endif
}
