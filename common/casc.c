/*
 * casc.c — CASC archive reader via CascLib (StarCraft II compatible)
 *
 * Thin wrapper that maps the engine's SFile* API (mpq.h) to CascLib,
 * letting the engine read CASC storage (SC2 retail) or MPQ archives
 * depending on the build configuration.
 *
 * Build with USE_CASC=1 to link this instead of mpq.c.
 * Requires CascLib (https://github.com/ladislav-zezula/CascLib):
 *   brew install casclib   OR   build from source and install
 */

#ifdef HAVE_CASC

#include "mpq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <CascLib.h>

/* CascLib uses HANDLE; engine uses handle_t (void *). */

bool SFileOpenArchive(cstring_t filename, uint32_t priority, uint32_t flags, handle_t *archive)
{
    (void)priority;
    (void)flags;
    return CascOpenStorage(filename, 0, (HANDLE *)archive);
}

bool SFileOpenArchiveFromMemory(void const *data, uint32_t size, uint32_t flags, handle_t *archive)
{
    (void)data; (void)size; (void)flags; (void)archive;
    return false;
}

bool SFileCloseArchive(handle_t archive)
{
    return CascCloseStorage((HANDLE)archive);
}

bool SFileCreateArchive(cstring_t filename, uint32_t flags, uint32_t maxFiles, handle_t *archive)
{
    (void)filename; (void)flags; (void)maxFiles; (void)archive;
    return false;
}

bool SFileAddFile(handle_t archive, cstring_t sourceFile, cstring_t archivedName)
{
    (void)archive; (void)sourceFile; (void)archivedName;
    return false;
}

bool SFileAddFileFromBuffer(handle_t archive, cstring_t archivedName, void const *data, uint32_t size)
{
    (void)archive; (void)archivedName; (void)data; (void)size;
    return false;
}

bool SFileOpenFileEx(handle_t archive, cstring_t fileName, uint32_t searchScope, handle_t *file)
{
    (void)searchScope;
    return CascOpenFile((HANDLE)archive, fileName, 0, CASC_OPEN_BY_NAME, (HANDLE *)file);
}

bool SFileOpenFileFromArchiveMemory(uint8_t *data, uint32_t size, cstring_t fileName, uint32_t searchScope, handle_t *file)
{
    (void)data; (void)size; (void)fileName; (void)searchScope; (void)file;
    return false;
}

bool SFileCloseFile(handle_t file)
{
    return CascCloseFile((HANDLE)file);
}

bool SFileReadFile(handle_t file, void *buffer, uint32_t toRead, uint32_t *bytesRead, void *overlapped)
{
    (void)overlapped;
    DWORD read = 0;
    bool ok = CascReadFile((HANDLE)file, buffer, toRead, &read);
    if (bytesRead)
        *bytesRead = read;
    return ok;
}

uint32_t SFileGetFileSize(handle_t file, uint32_t *highSize)
{
    if (highSize)
        *highSize = 0;
    DWORD hi = 0;
    DWORD lo = CascGetFileSize((HANDLE)file, &hi);
    if (highSize)
        *highSize = hi;
    return lo;
}

uint32_t SFileSetFilePointer(handle_t file, int32_t distance, int32_t *distanceHigh, uint32_t moveMethod)
{
    LONG hi = distanceHigh ? *distanceHigh : 0;
    DWORD result = CascSetFilePointer((HANDLE)file, distance, &hi, moveMethod);
    if (distanceHigh)
        *distanceHigh = hi;
    return result;
}

bool SFileExtractFile(handle_t archive, cstring_t toExtract, cstring_t extracted, uint32_t flags)
{
    (void)flags;
    handle_t file;
    if (!SFileOpenFileEx(archive, toExtract, SFILE_OPEN_FROM_MPQ, &file))
        return false;

    FILE *out = fopen(extracted, "wb");
    if (!out) {
        SFileCloseFile(file);
        return false;
    }

    uint8_t buf[65536];
    uint32_t read;
    while (SFileReadFile(file, buf, sizeof(buf), &read, NULL) && read > 0)
        fwrite(buf, 1, read, out);

    fclose(out);
    SFileCloseFile(file);
    return true;
}

typedef struct {
    HANDLE find;
    HANDLE storage;
} cascFind_t;

handle_t SFileFindFirstFile(handle_t archive, cstring_t mask, sfileFindData_t *findData, cstring_t listFile)
{
    (void)listFile;
    CASC_FIND_DATA cfd;
    memset(&cfd, 0, sizeof(cfd));

    HANDLE hFind = CascFindFirstFile((HANDLE)archive, mask, &cfd, NULL);
    if (!hFind)
        return NULL;

    memset(findData, 0, sizeof(*findData));
    strncpy(findData->cFileName, cfd.szFileName, sizeof(findData->cFileName) - 1);
    findData->szPlainName = findData->cFileName;
    findData->dwFileSize = (uint32_t)cfd.dwFileSize;

    cascFind_t *cf = calloc(1, sizeof(*cf));
    if (!cf) {
        CascFindClose(hFind);
        return NULL;
    }
    cf->find = hFind;
    cf->storage = archive;
    return (handle_t)cf;
}

bool SFileFindNextFile(handle_t find, sfileFindData_t *findData)
{
    cascFind_t *cf = (cascFind_t *)find;
    if (!cf)
        return false;

    CASC_FIND_DATA cfd;
    memset(&cfd, 0, sizeof(cfd));
    if (!CascFindNextFile(cf->find, &cfd))
        return false;

    memset(findData, 0, sizeof(*findData));
    strncpy(findData->cFileName, cfd.szFileName, sizeof(findData->cFileName) - 1);
    findData->szPlainName = findData->cFileName;
    findData->dwFileSize = (uint32_t)cfd.dwFileSize;
    return true;
}

bool SFileFindClose(handle_t find)
{
    cascFind_t *cf = (cascFind_t *)find;
    if (cf) {
        CascFindClose(cf->find);
        free(cf);
    }
    return true;
}

#endif /* HAVE_CASC */
