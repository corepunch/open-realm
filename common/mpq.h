#ifndef MPQ_H
#define MPQ_H

#include "shared.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SFILE_OPEN_FROM_MPQ 0
#define SFILE_INVALID_POS ((uint32_t)-1)

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef FILE_BEGIN
#define FILE_BEGIN 0
#endif

#ifndef FILE_CURRENT
#define FILE_CURRENT 1
#endif

#ifndef FILE_END
#define FILE_END 2
#endif

typedef struct {
    char cFileName[MAX_PATH];
    char *szPlainName;
    uint32_t dwHashIndex;
    uint32_t dwBlockIndex;
    uint32_t dwFileSize;
    uint32_t dwFileFlags;
    uint32_t dwCompSize;
    uint32_t dwFileTimeLo;
    uint32_t dwFileTimeHi;
    uint32_t lcLocale;
} sfileFindData_t;

bool SFileOpenArchive(cstring_t filename, uint32_t priority, uint32_t flags, handle_t *archive);
bool SFileOpenArchiveFromMemory(void const *data, uint32_t size, uint32_t flags, handle_t *archive);
bool SFileCloseArchive(handle_t archive);

bool SFileCreateArchive(cstring_t filename, uint32_t flags, uint32_t maxFiles, handle_t *archive);
bool SFileAddFile(handle_t archive, cstring_t sourceFile, cstring_t archivedName);
bool SFileAddFileFromBuffer(handle_t archive, cstring_t archivedName, void const *data, uint32_t size);

bool SFileOpenFileEx(handle_t archive, cstring_t fileName, uint32_t searchScope, handle_t *file);
bool SFileOpenFileFromArchiveMemory(uint8_t *data, uint32_t size, cstring_t fileName, uint32_t searchScope, handle_t *file);
bool SFileCloseFile(handle_t file);

bool SFileReadFile(handle_t file, void *buffer, uint32_t toRead, uint32_t *bytesRead, void *overlapped);
uint32_t SFileGetFileSize(handle_t file, uint32_t *highSize);
uint32_t SFileSetFilePointer(handle_t file, int32_t distance, int32_t *distanceHigh, uint32_t moveMethod);

bool SFileExtractFile(handle_t archive, cstring_t toExtract, cstring_t extracted, uint32_t flags);

handle_t SFileFindFirstFile(handle_t archive, cstring_t mask, sfileFindData_t *findData, cstring_t listFile);
bool SFileFindNextFile(handle_t find, sfileFindData_t *findData);
bool SFileFindClose(handle_t find);

#ifdef MPQ_TEST_API
bool Mpq_TestDecompressSector(uint8_t const *src, uint32_t src_size, uint8_t *dst, uint32_t dst_size, uint32_t *out_size);
uint32_t Mpq_TestHashString(char const *str, uint32_t hash_type);
bool Mpq_TestEncryptBlock(uint8_t *data, uint32_t size, uint32_t seed);
#endif

#ifdef __cplusplus
}
#endif

#endif
