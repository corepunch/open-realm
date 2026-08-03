#ifndef net_platform_h
#define net_platform_h

#ifdef _WIN32
/* The engine intentionally uses Win32-style names for its portable public
 * types.  Shield those names while importing Winsock so the Windows SDK does
 * not replace the engine's DWORD/RECT/etc. ABI in translation units that also
 * include rendering and game headers. */
typedef int WINBOOL;
typedef int WINSDK_BOOL;
typedef WINBOOL *PBOOL;
typedef WINBOOL *LPBOOL;
#define _DEF_WINBOOL_
#define BYTE WINSDK_BYTE
#define BOOL WINSDK_BOOL
#define USHORT WINSDK_USHORT
#define LONG WINSDK_LONG
#define SHORT WINSDK_SHORT
#define DWORD WINSDK_DWORD
#define WORD WINSDK_WORD
#define DWORD_PTR WINSDK_DWORD_PTR
#define LONG_PTR WINSDK_LONG_PTR
#define INT_PTR WINSDK_INT_PTR
#define LONGLONG WINSDK_LONGLONG
#define ULONGLONG WINSDK_ULONGLONG
#define FLOAT WINSDK_FLOAT
#define HANDLE WINSDK_HANDLE
#define LPOVERLAPPED WINSDK_LPOVERLAPPED
#define TCHAR WINSDK_TCHAR
#define LCID WINSDK_LCID
#define PLONG WINSDK_PLONG
#define LPDWORD WINSDK_LPDWORD
#define LPBYTE WINSDK_LPBYTE
#define LPFLOAT WINSDK_LPFLOAT
#define LPCTSTR WINSDK_LPCTSTR
#define LPCSTR WINSDK_LPCSTR
#define LPTSTR WINSDK_LPTSTR
#define LPSTR WINSDK_LPSTR
#define LPCVOID WINSDK_LPCVOID
#define RECT WINSDK_RECT
#define LPRECT WINSDK_LPRECT
#define LPCRECT WINSDK_LPCRECT
#define DrawText WINSDK_DrawText
#define PlaySound WINSDK_PlaySound

#include <winsock2.h>
#include <ws2tcpip.h>

#undef BYTE
#undef BOOL
#undef USHORT
#undef LONG
#undef SHORT
#undef DWORD
#undef WORD
#undef DWORD_PTR
#undef LONG_PTR
#undef INT_PTR
#undef LONGLONG
#undef ULONGLONG
#undef FLOAT
#undef HANDLE
#undef LPOVERLAPPED
#undef TCHAR
#undef LCID
#undef PLONG
#undef LPDWORD
#undef LPBYTE
#undef LPFLOAT
#undef LPCTSTR
#undef LPCSTR
#undef LPTSTR
#undef LPSTR
#undef LPCVOID
#undef RECT
#undef LPRECT
#undef LPCRECT
#undef DrawText
#undef PlaySound
#undef MAKEFOURCC

typedef SOCKET net_socket_t;
typedef int net_socklen_t;
#define NET_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

typedef int net_socket_t;
typedef socklen_t net_socklen_t;
#define NET_INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

#endif /* net_platform_h */
