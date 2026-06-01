/***********************************************************************
net_compat.h -- a thin compatibility layer that lets the Winsock-based
tcping sources also build against POSIX sockets (Linux / macOS).

The goal is to keep the existing call sites looking like Winsock while
mapping the handful of genuinely different pieces (socket type, error
codes, non-blocking mode, sleeping, secure-CRT string helpers, local
time) onto their POSIX equivalents.  Genuinely platform-specific
behaviour (console colour, Ctrl-C handling, high-resolution timing) is
handled at the call sites with small helpers declared here.
***********************************************************************/

#if !defined(NET_COMPAT_H)
#define NET_COMPAT_H

#include <chrono>

#ifdef _WIN32

  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>

#else  // ---- POSIX ----

  #include <sys/socket.h>
  #include <sys/types.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <signal.h>
  #include <time.h>
  #include <cstdio>
  #include <cstring>

  typedef int SOCKET;
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR   (-1)

  // Winsock spells "struct addrinfo" as ADDRINFO.
  typedef struct addrinfo ADDRINFO;

  inline int closesocket(SOCKET s) { return ::close(s); }
  inline int WSAGetLastError() { return errno; }

  // A few Winsock error names the code references directly, mapped to errno.
  #define WSAEWOULDBLOCK EWOULDBLOCK
  #define WSAEINPROGRESS EINPROGRESS
  #define WSAETIMEDOUT   ETIMEDOUT

  // Winsock startup/teardown are no-ops off Windows.
  typedef int WSADATA;
  typedef int WSAData;
  inline int WSAStartup(unsigned short, WSAData*) { return 0; }
  inline int WSACleanup() { return 0; }
  #ifndef MAKEWORD
  #define MAKEWORD(a, b) 0
  #endif

  // Minimal shims for the MSVC "secure CRT" functions used by the sources.
  // Return values are ignored at every call site, so we keep them simple.
  #define sprintf_s(buf, size, ...) snprintf((buf), (size), __VA_ARGS__)

  inline int strcpy_s(char* dst, size_t size, const char* src) {
      if (dst == NULL || src == NULL || size == 0) {
          return 22;  // EINVAL
      }
      snprintf(dst, size, "%s", src);  // always NUL-terminates, truncates safely
      return 0;
  }

#endif // _WIN32

// ---- Helpers shared by both platforms ----

// True if a non-blocking connect() reported "still connecting" rather than
// an outright failure.
inline bool connect_in_progress(int err) {
#ifdef _WIN32
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
#else
    return err == EINPROGRESS || err == EWOULDBLOCK;
#endif
}

// Put a socket into (non-)blocking mode.  Returns 0 on success.
inline int set_nonblocking(SOCKET s, bool nonblocking) {
#ifdef _WIN32
    u_long mode = nonblocking ? 1 : 0;
    return ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    if (nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(s, F_SETFL, flags);
#endif
}

// Sleep for the given number of milliseconds.
inline void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

// A monotonic high-resolution timestamp in milliseconds.  Replaces the
// QueryPerformanceCounter dance and works the same on every platform.
inline double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(
               steady_clock::now().time_since_epoch())
        .count();
}

// Fill *out_tm with the current local time.
inline void local_time_now(struct tm* out_tm) {
    time_t t = time(NULL);
#ifdef _WIN32
    localtime_s(out_tm, &t);
#else
    localtime_r(&t, out_tm);
#endif
}

#endif // !defined(NET_COMPAT_H)
