cmake_minimum_required(VERSION 2.8)
project(termtunnel C)
set(CMAKE_C_STANDARD 11)
if (CMAKE_HOST_WIN32)
    set(WINDOWS 1)
elseif (CMAKE_HOST_APPLE)
    set(MACOS 1)
elseif (CMAKE_HOST_UNIX)
    set(LINUX 1)
endif ()

IF (NOT CYGWIN)
add_subdirectory(thirdparty/libuv)
include_directories(thirdparty/libuv/include) 
endif()

add_compile_options(-fpermissive -O3)

include_directories(${PROJECT_SOURCE_DIR}/thirdparty/lwip/include)
include_directories(${PROJECT_SOURCE_DIR}/thirdparty/port/include)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR})

add_executable(termtunnel
src/entry.c
src/utils.c
src/pty.c
src/agent.c
src/log.c
src/pipe.c
src/repl.c
src/fsm.c
src/vnet.c
src/state.c
src/fileexchange.c
src/socksproxy.c
src/agentcall.c
src/portforward.c
thirdparty/lwip/core/ip.c 
thirdparty/lwip/core/init.c
thirdparty/lwip/core/def.c
thirdparty/lwip/core/inet_chksum.c
thirdparty/lwip/core/mem.c
thirdparty/lwip/core/memp.c
thirdparty/lwip/core/netif.c
thirdparty/lwip/core/pbuf.c
thirdparty/lwip/core/stats.c
thirdparty/lwip/core/tcp.c
thirdparty/lwip/core/tcp_in.c
thirdparty/lwip/core/tcp_out.c
thirdparty/lwip/core/timeouts.c
thirdparty/lwip/core/ipv4/etharp.c
thirdparty/lwip/core/ipv4/icmp.c
thirdparty/lwip/core/ipv4/ip4_frag.c
thirdparty/lwip/core/ipv4/ip4.c
thirdparty/lwip/core/ipv4/ip4_addr.c
thirdparty/lwip/api/api_lib.c
thirdparty/lwip/api/api_msg.c
thirdparty/lwip/api/err.c
thirdparty/lwip/api/netbuf.c
thirdparty/lwip/api/sockets.c
thirdparty/lwip/api/tcpip.c
thirdparty/lwip/netif/ethernet.c
thirdparty/port/sys_arch.c
#thirdparty/port/perf.c
thirdparty/base64.c
thirdparty/linenoise.c
thirdparty/queue/queue.c
thirdparty/queue/queue_internal.c
thirdparty/setproctitle.c
thirdparty/tinyfiledialogs/tinyfiledialogs.c
)

IF (CYGWIN)  # cygwin use system default libuv without epoll.
target_link_libraries(termtunnel uv)
ELSE()
target_link_libraries(termtunnel uv_a)
ENDIF()

if (LINUX)
    target_link_libraries(termtunnel PUBLIC "-static")
endif()
