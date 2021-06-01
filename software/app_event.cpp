
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <sched.h>
#include <pthread.h>

// pick one of the two below
#include "fd/fd.h"
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h> 

#define ZERO_COPY

#include "app.h"

#define BUF_LEN 100000

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

static volatile int keep_running = 1;

void int_handler(int signal __attribute__((unused)))
{
    keep_running = 0;
}

int main(int argc, const char* argv[])
{
    int result;
    uint64_t recv_bytes = 0;
    uint64_t nb_batches = 0;

    if (argc != 6) {
        std::cerr << "Usage: " << argv[0] << " core port nb_rules nb_queues nb_cycles"
                  << std::endl;
        return 1;
    }

    int port = atoi(argv[2]);
    unsigned nb_rules = atoi(argv[3]);
    int nb_queues = atoi(argv[4]);
    uint32_t nb_cycles = atoi(argv[5]);

    signal(SIGINT, int_handler);

    std::cout << "running test with " << nb_rules << " rules" << std::endl;
    
    std::thread socket_thread = std::thread([&recv_bytes, port, nb_rules, 
        nb_queues, &nb_batches, &nb_cycles]
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        std::cout << "Running socket on CPU " << sched_getcpu() << std::endl;

        for (int i = 0; i < nb_queues; ++i) {
            // TODO(sadok) can we make this a valid file descriptor?
            int socket_fd = socket(AF_INET, SOCK_DGRAM, nb_queues);

            if (socket_fd == -1) {
                std::cerr << "Problem creating socket (" << errno << "): "
                          << strerror(errno) << std::endl;
                exit(2);
            }

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));

            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = inet_addr("10.0.0.2"); // htonl(INADDR_ANY);
            addr.sin_port = htons(port);

            if (bind(socket_fd, (struct sockaddr*) &addr, nb_rules, nb_rules)) {
                std::cerr << "Problem binding socket (" << errno << "): "
                          << strerror(errno) <<  std::endl;
                exit(3);
            }
        }

        unsigned char* buf;

        while (keep_running) {
            int socket_fd;
            int recv_len = recv_select(&socket_fd, (void**) &buf, BUF_LEN, 0);
            if (unlikely(recv_len < 0)) {
                std::cerr << "Error receiving" << std::endl;
                exit(4);
            }
            // ensuring that we are modifying every cache line
            for (uint32_t i = 0; i < ((uint32_t) recv_len) / 64; ++i) {
                ++buf[i*64];
            }
            for (uint32_t i = 0; i < nb_cycles; ++i) {
                asm("nop");
            }
            if (recv_len > 0) {
                ++nb_batches;
                free_pkt_buf(socket_fd);
            }
            recv_bytes += recv_len;
        }

        // TODO(sadok) it is also common to use the close() syscall to close a
        // UDP socket
        for (int socket_fd = 0; socket_fd < nb_queues; ++socket_fd) {
            shutdown(socket_fd, SHUT_RDWR);
        }
    });

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(atoi(argv[1]), &cpuset);
    result = pthread_setaffinity_np(socket_thread.native_handle(),
                                    sizeof(cpuset), &cpuset);
    if (result < 0) {
        std::cerr << "Error setting CPU affinity" << std::endl;
        return 6;
    }

    while (keep_running) {
        uint64_t recv_bytes_before = recv_bytes;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << std::dec << "Goodput: " << 
            ((double) recv_bytes - recv_bytes_before) * 8. /1e6
            << " Mbps  #bytes: " << recv_bytes << "  #batches: " << nb_batches
            << std::endl;
    }

    socket_thread.join();

    return 0;
}