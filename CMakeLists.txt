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

add_subdirectory(thirdparty/libuv)
include_directories(thirdparty/libuv/include) 

add_compile_options(-funwind-tables -fasynchronous-unwind-tables -fpermissive -g -O0) #  -rdynamic) # -O2)

include_directories(thirdparty/lwip/include)
include_directories(thirdparty/port/include)
include_directories(./src)
include_directories(.)

# 允许mac和linux编译
add_library(lwip STATIC 
  thirdparty/lwip/core/init.c
  thirdparty/lwip/core/def.c
  thirdparty/lwip/core/dns.c
  thirdparty/lwip/core/inet_chksum.c
  thirdparty/lwip/core/mem.c
  thirdparty/lwip/core/memp.c
  thirdparty/lwip/core/netif.c
  thirdparty/lwip/core/pbuf.c
  thirdparty/lwip/core/raw.c
  thirdparty/lwip/core/stats.c
  thirdparty/lwip/core/sys.c
  thirdparty/lwip/core/altcp.c
  thirdparty/lwip/core/altcp_alloc.c
  thirdparty/lwip/core/altcp_tcp.c
  thirdparty/lwip/core/tcp.c
  thirdparty/lwip/core/tcp_in.c
  thirdparty/lwip/core/tcp_out.c
  thirdparty/lwip/core/timeouts.c
  thirdparty/lwip/core/udp.c
  thirdparty/lwip/core/ip.c

  thirdparty/lwip/core/ipv4/autoip.c
  thirdparty/lwip/core/ipv4/dhcp.c
  thirdparty/lwip/core/ipv4/etharp.c
  thirdparty/lwip/core/ipv4/icmp.c
  thirdparty/lwip/core/ipv4/igmp.c
  thirdparty/lwip/core/ipv4/ip4_frag.c
  thirdparty/lwip/core/ipv4/ip4.c
  thirdparty/lwip/core/ipv4/ip4_addr.c
  
  thirdparty/lwip/api/api_lib.c
  thirdparty/lwip/api/api_msg.c
  thirdparty/lwip/api/err.c
  thirdparty/lwip/api/if_api.c
  thirdparty/lwip/api/netbuf.c
  thirdparty/lwip/api/netdb.c
  thirdparty/lwip/api/netifapi.c
  thirdparty/lwip/api/sockets.c
  thirdparty/lwip/api/tcpip.c

  thirdparty/lwip/netif/ethernet.c
  thirdparty/lwip/netif/bridgeif.c
  thirdparty/lwip/netif/bridgeif_fdb.c
  thirdparty/lwip/netif/slipif.c

  thirdparty/port/sys_arch.c
  thirdparty/port/perf.c
)

add_executable(termtunnel
src/entry.c
src/utils.c
src/pty.c
src/agent.c
src/log.c
src/pipe.c
src/repl.c
thirdparty/linenoise.c
src/fsm.c
thirdparty/base64.c
thirdparty/tinyfiledialogs/tinyfiledialogs.c
src/vnet.c
src/state.c
thirdparty/queue/queue.c
thirdparty/queue/queue_internal.c
thirdparty/setproctitle.c
thirdparty/lwip/core/ip.c   # mac hack
src/fileexchange.c
src/socksproxy.c
src/agentcall.c
src/portforward.c
)

target_link_libraries(termtunnel uv_a lwip)
if (LINUX)
    target_link_libraries(termtunnel PUBLIC "-static")
endif()
