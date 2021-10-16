// (C) 2017-2018 Intel Corporation.
//
// Intel, the Intel logo, Intel, MegaCore, NIOS II, Quartus and TalkBack words
// and logos are trademarks of Intel Corporation or its subsidiaries in the
// U.S. and/or other countries. Other marks and brands may be claimed as the
// property of others. See Trademarks on intel.com for full list of Intel
// trademarks or the Trademarks & Brands Names Database (if Intel) or see
// www.intel.com/legal (if Altera). Your use of Intel Corporation's design
// tools, logic functions and other software and tools, and its AMPP partner
// logic functions, and any output files any of the foregoing (including
// device programming or simulation files), and any associated documentation
// or information are expressly subject to the terms and conditions of the
// Altera Program License Subscription Agreement, Intel MegaCore Function
// License Agreement, or other applicable license agreement, including,
// without limitation, that your use is for the sole purpose of programming
// logic devices manufactured by Intel and sold by Intel or its authorized
// distributors. Please refer to the applicable agreement for further details.

#include <algorithm>
#include <termios.h>
#include <time.h>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <endian.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <string.h>

#include <sched.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "api/intel_fpga_pcie_api.hpp"
// #include "intel_fpga_pcie_link_test.hpp"
#include "pcie.h"

#define SEND_BACK

static dsc_queue_t dsc_queue;
static uint32_t* pending_pkt_tails;

// HACK(sadok) This is used to decrement the packet queue id and use it as an
// index to the pending_pkt_tails array. This only works because packet queues
// for the same app are contiguous. This will no longer hold in the future. How
// bad would it be to use a hash table to keep pending_pkt_tails?
static uint32_t pkt_queue_id_offset;

static inline uint16_t get_pkt_size(uint8_t *addr) {
    uint16_t l2_len = 14 + 4;
    uint16_t l3_len = be16toh(*((uint16_t*) (addr+14+2))); // ipv4 total length
    uint16_t total_len = l2_len + l3_len;
    return total_len;
}

void print_buf(void* buf, const uint32_t nb_cache_lines)
{
    for (uint32_t i = 0; i < nb_cache_lines * 64; i++) {
        printf("%02x ", ((uint8_t*) buf)[i]);
        if ((i + 1) % 8 == 0) {
            printf(" ");
        }
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
        if ((i + 1) % 64 == 0) {
            printf("\n");
        }
    }
}

// adapted from ixy
static void* virt_to_phys(void* virt) {
	long pagesize = sysconf(_SC_PAGESIZE);
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
        return NULL;
    }
	// pagemap is an array of pointers for each normal-sized page
	if (lseek(fd, (uintptr_t) virt / pagesize * sizeof(uintptr_t),
            SEEK_SET) < 0) {
        close(fd);
        return NULL;
    }
	
    uintptr_t phy = 0;
    if (read(fd, &phy, sizeof(phy)) < 0) {
        close(fd);
        return NULL;
    }
    close(fd);

	if (!phy) {
        return NULL;
	}
	// bits 0-54 are the page number
	return (void*) ((phy & 0x7fffffffffffffULL) * pagesize 
                    + ((uintptr_t) virt) % pagesize);
}

// adapted from ixy
static void* get_huge_pages(int app_id, size_t size) {
    int fd;
    char huge_pages_path[128];

    snprintf(huge_pages_path, 128, "/mnt/huge/fd:%i", app_id);

    fd = open(huge_pages_path, O_CREAT | O_RDWR, S_IRWXU);
    if (fd == -1) {
        std::cerr << "(" << errno
                  << ") Problem opening huge page file descriptor" << std::endl;
        return NULL;
    }

    if (ftruncate(fd, (off_t) size)) {
        std::cerr << "(" << errno << ") Could not truncate huge page to size: "
                  << size << std::endl;
        close(fd);
        unlink(huge_pages_path);
        return NULL;
    }

    void* virt_addr = (void*) mmap(NULL, size,
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_HUGETLB, fd, 0);

    if (virt_addr == (void*) -1) {
        std::cerr << "(" << errno << ") Could not mmap huge page" << std::endl;
        close(fd);
        unlink(huge_pages_path);
        return NULL;
    }
    
    if (mlock(virt_addr, size)) {
        std::cerr << "(" << errno << ") Could not lock huge page" << std::endl;
        munmap(virt_addr, size);
        close(fd);
        unlink(huge_pages_path);
        return NULL;
    }

    // don't keep it around in the hugetlbfs
    close(fd);
    unlink(huge_pages_path);

    return virt_addr;
}

int dma_init(socket_internal* socket_entry, unsigned socket_id, unsigned nb_queues)
{
    void* uio_mmap_bar2_addr;
    int app_id;
    intel_fpga_pcie_dev *dev = socket_entry->dev;

    printf("Running with BATCH_SIZE: %i\n", BATCH_SIZE);
    printf("Running with DSC_BUF_SIZE: %i\n", DSC_BUF_SIZE);
    printf("Running with PKT_BUF_SIZE: %i\n", PKT_BUF_SIZE);

    // FIXME(sadok) should find a better identifier than core id
    app_id = sched_getcpu() * nb_queues + socket_id;
    if (app_id < 0) {
        std::cerr << "Could not get cpu id" << std::endl;
        return -1;
    }

    uio_mmap_bar2_addr = dev->uio_mmap(
        (1<<12) * (MAX_NB_FLOWS + MAX_NB_APPS), 2);
    if (uio_mmap_bar2_addr == MAP_FAILED) {
        std::cerr << "Could not get mmap uio memory!" << std::endl;
        return -1;
    }

    // set descriptor queue only for the first socket
    if (dsc_queue.ref_cnt == 0) {
        // Register associated with the descriptor queue. Descriptor queue
        // registers come after the packet queue ones, that's why we use 
        // MAX_NB_FLOWS as an offset.
        queue_regs_t *dsc_queue_regs = (queue_regs_t *) (
            (uint8_t*) uio_mmap_bar2_addr + 
            (app_id + MAX_NB_FLOWS) * MEMORY_SPACE_PER_QUEUE
        );

        dsc_queue.regs = dsc_queue_regs;
        dsc_queue.rx_buf =
            (pcie_rx_dsc_t*) get_huge_pages(app_id, ALIGNED_DSC_BUF_PAIR_SIZE);
        if (dsc_queue.rx_buf == NULL) {
            std::cerr << "Could not get huge page" << std::endl;
            return -1;
        }
        dsc_queue.tx_buf = (pcie_tx_dsc_t*) (
            (uint64_t) dsc_queue.rx_buf + ALIGNED_DSC_BUF_PAIR_SIZE / 2);

        uint64_t phys_addr = (uint64_t) virt_to_phys(dsc_queue.rx_buf);

        // Use first half of the huge page for RX and second half for TX.
        dsc_queue_regs->rx_mem_low = (uint32_t) phys_addr;
        dsc_queue_regs->rx_mem_high = (uint32_t) (phys_addr >> 32);

        phys_addr += ALIGNED_DSC_BUF_PAIR_SIZE / 2;
        dsc_queue_regs->tx_mem_low = (uint32_t) phys_addr;
        dsc_queue_regs->tx_mem_high = (uint32_t) (phys_addr >> 32);

        dsc_queue.rx_head_ptr = &dsc_queue_regs->rx_head;
        dsc_queue.tx_head_ptr = &dsc_queue_regs->tx_head;
        dsc_queue.tx_tail_ptr = &dsc_queue_regs->tx_tail;
        dsc_queue.rx_head = dsc_queue_regs->rx_head;
        dsc_queue.old_rx_head = dsc_queue.rx_head;
        dsc_queue.tx_head = dsc_queue_regs->tx_head;
        dsc_queue.tx_tail = dsc_queue_regs->tx_tail;

        // HACK(sadok) assuming that we know the number of queues beforehand
        pending_pkt_tails = (uint32_t*) malloc(
            sizeof(*pending_pkt_tails) * nb_queues);
        if (pending_pkt_tails == NULL) {
            std::cerr << "Could not allocate memory" << std::endl;
            return -1;
        }
        memset(pending_pkt_tails, 0, nb_queues);

        pkt_queue_id_offset = app_id - socket_id;
    }

    ++(dsc_queue.ref_cnt);

    // register associated with the packet queue
    queue_regs_t *pkt_queue_regs = (queue_regs_t *) (
        (uint8_t*) uio_mmap_bar2_addr + app_id * MEMORY_SPACE_PER_QUEUE
    );
    socket_entry->pkt_queue.regs = pkt_queue_regs;

    socket_entry->pkt_queue.buf = 
        (uint32_t*) get_huge_pages(app_id, ALIGNED_PKT_BUF_SIZE);
    if (socket_entry->pkt_queue.buf == NULL) {
        std::cerr << "Could not get huge page" << std::endl;
        return -1;
    }
    uint64_t phys_addr = (uint64_t) virt_to_phys(socket_entry->pkt_queue.buf);

    pkt_queue_regs->rx_mem_low = (uint32_t) phys_addr;
    pkt_queue_regs->rx_mem_high = (uint32_t) (phys_addr >> 32);

    socket_entry->pkt_queue.buf_phys_addr = phys_addr;

    socket_entry->app_id = app_id;
    socket_entry->pkt_queue.buf_head_ptr = &pkt_queue_regs->rx_head;
    socket_entry->pkt_queue.rx_head = pkt_queue_regs->rx_head;
    socket_entry->pkt_queue.old_rx_head = socket_entry->pkt_queue.rx_head;
    socket_entry->pkt_queue.rx_tail = socket_entry->pkt_queue.rx_tail;

    // make sure the last tail matches the current head
    pending_pkt_tails[socket_id] = socket_entry->pkt_queue.rx_head;

    return 0;
}

static inline void get_new_tails()
{
    pcie_rx_dsc_t* dsc_buf = dsc_queue.rx_buf;
    uint32_t dsc_buf_head = dsc_queue.rx_head;

    for (uint16_t i = 0; i < BATCH_SIZE; ++i) {
        pcie_rx_dsc_t* cur_desc = dsc_buf + dsc_buf_head;

        // check if the next descriptor was updated by the FPGA
        if (cur_desc->signal == 0) {
            break;
        }

        cur_desc->signal = 0;
        dsc_buf_head = (dsc_buf_head + 1) % DSC_BUF_SIZE;

        uint32_t pkt_queue_id = cur_desc->queue_id - pkt_queue_id_offset;
        pending_pkt_tails[pkt_queue_id] = (uint32_t) cur_desc->tail;

        // TODO(sadok) consider prefetching. Two options to consider:
        // (1) prefetch all the packets;
        // (2) pass the current queue as argument and prefetch packets for it,
        //     including potentially old packets.
    }

    // update descriptor buffer head
    asm volatile ("" : : : "memory"); // compiler memory barrier
    *(dsc_queue.rx_head_ptr) = dsc_buf_head;

    dsc_queue.rx_head = dsc_buf_head;
}

static inline int consume_queue(socket_internal* socket_entry, void** buf,
                                size_t len)
{
    uint32_t* pkt_buf = socket_entry->pkt_queue.buf;
    uint32_t pkt_buf_head = socket_entry->pkt_queue.rx_head;
    int app_id = socket_entry->app_id;

    *buf = &pkt_buf[pkt_buf_head * 16];

    uint32_t pkt_buf_tail = pending_pkt_tails[app_id];

    if (pkt_buf_tail == pkt_buf_head) {
        return 0;
    }

    // TODO(sadok): map the same page back to the end of the buffer so that we
    // can handle this case while avoiding that we trim the request to the end
    // of the buffer.
    // To ensure that we can return a contiguous region, we ceil the tail to 
    // PKT_BUF_SIZE
    uint32_t ceiled_tail;
    
    if (pkt_buf_tail > pkt_buf_head) {
        ceiled_tail = pkt_buf_tail;
    } else {
        ceiled_tail = PKT_BUF_SIZE;
    }

    uint32_t flit_aligned_size = (ceiled_tail - pkt_buf_head) * 64;

    // reached the buffer limit
    if (unlikely(flit_aligned_size > len)) {
        flit_aligned_size = len & 0xffffffc0; // align len to 64 bytes
    }

    pkt_buf_head = (pkt_buf_head + flit_aligned_size / 64) % PKT_BUF_SIZE;

    socket_entry->pkt_queue.rx_tail = pkt_buf_head;
    return flit_aligned_size;
}

int get_next_batch_from_queue(socket_internal* socket_entry, void** buf,
                              size_t len)
{
    get_new_tails();
    return consume_queue(socket_entry, buf, len);
}

// return next batch among all open sockets
int get_next_batch(socket_internal* socket_entries, int* sockfd, void** buf,
                   size_t len)
{
    pcie_rx_dsc_t* dsc_buf = dsc_queue.rx_buf;
    uint32_t dsc_buf_head = dsc_queue.rx_head;
    uint32_t old_dsc_buf_head = dsc_queue.old_rx_head;

    pcie_rx_dsc_t* cur_desc = dsc_buf + dsc_buf_head;

    // check if the next descriptor was updated by the FPGA
    if (unlikely(cur_desc->signal == 0)) {
        return 0;
    }

    cur_desc->signal = 0;
    dsc_buf_head = (dsc_buf_head + 1) % DSC_BUF_SIZE;

    uint32_t pkt_queue_id = cur_desc->queue_id - pkt_queue_id_offset;
    pending_pkt_tails[pkt_queue_id] = (uint32_t) cur_desc->tail;

    // TODO(sadok) consider prefetching. Two options to consider:
    // (1) prefetch all the packets;
    // (2) pass the current queue as argument and prefetch packets for it,
    //     including potentially old packets.

    uint32_t head_gap = (dsc_buf_head - old_dsc_buf_head) % DSC_BUF_SIZE;
    if (head_gap >= BATCH_SIZE) {
        // update descriptor buffer head
        asm volatile ("" : : : "memory"); // compiler memory barrier
        *(dsc_queue.rx_head_ptr) = dsc_buf_head;
        dsc_queue.old_rx_head = dsc_buf_head;
    }
    dsc_queue.rx_head = dsc_buf_head;

    *sockfd = pkt_queue_id;
    socket_internal* socket_entry = &socket_entries[pkt_queue_id];
    return consume_queue(socket_entry, buf, len);
}

inline void get_new_tx_head(socket_internal* socket_entries)
{
    pcie_tx_dsc_t* tx_buf = dsc_queue.tx_buf;
    uint32_t head = dsc_queue.tx_head;
    uint32_t new_head = dsc_queue.tx_head = *(dsc_queue.tx_head_ptr);

    // Advance pointer for pkt queues that were already sent.
    while (head != new_head) {
        pcie_tx_dsc_t* tx_dsc = tx_buf + head;
        uint32_t pkt_queue_id = tx_dsc->rx_pkt_queue_id;
        socket_internal* socket_entry = &socket_entries[pkt_queue_id];

        socket_entry->pkt_queue.old_rx_head += (
            tx_dsc->length >> TX_DSC_LEN_OFFSET) / 64;
        *(socket_entry->pkt_queue.buf_head_ptr) = 
            socket_entry->pkt_queue.old_rx_head;

        head = (head + 1) % DSC_BUF_SIZE;
    }
}

// HACK(sadok): socket_entries is used to retrieve new head and should not be
//              needed.
void advance_ring_buffer(socket_internal* socket_entries,
                         socket_internal* socket_entry)
{
    uint32_t rx_pkt_tail = socket_entry->pkt_queue.rx_tail;
    
    // HACK(sadok): also sending packet here. Should move to a separate
    //              function.
#ifdef SEND_BACK
    uint64_t buf_phys_addr = socket_entry->pkt_queue.buf_phys_addr;
    uint32_t old_rx_head = socket_entry->pkt_queue.old_rx_head;
    uint32_t rx_pkt_head = socket_entry->pkt_queue.rx_head;
    int app_id = socket_entry->app_id;
    pcie_tx_dsc_t* tx_buf = dsc_queue.tx_buf;
    uint32_t tx_tail = dsc_queue.tx_tail;

    uint32_t free_slots =
        (dsc_queue.tx_head - dsc_queue.tx_tail - 1) % DSC_BUF_SIZE;

    // if (free_slots < DSC_BUF_SIZE / 2) {
        do {
            get_new_tx_head(socket_entries);
            free_slots =
                (dsc_queue.tx_head - dsc_queue.tx_tail - 1) % DSC_BUF_SIZE;
        } while (free_slots == 0);  // Block until we can send.
    // }

    pcie_tx_dsc_t* tx_dsc = tx_buf + tx_tail;
    tx_dsc->phys_addr = buf_phys_addr + rx_pkt_head * 64;
    uint64_t length = ((rx_pkt_tail - rx_pkt_head) % PKT_BUF_SIZE) * 64;
    
    // TODO(sadok): change hardware to avoid shift.
    tx_dsc->length = length << TX_DSC_LEN_OFFSET;
    tx_dsc->rx_pkt_queue_id = app_id;

    tx_tail = (tx_tail + 1) % DSC_BUF_SIZE;
    dsc_queue.tx_tail = tx_tail;

    asm volatile ("" : : : "memory"); // compiler memory barrier
    *(dsc_queue.tx_tail_ptr) = tx_tail;

    // Hack to force new descriptor to be sent.
    *(socket_entry->pkt_queue.buf_head_ptr) = old_rx_head;
#else
    asm volatile ("" : : : "memory"); // compiler memory barrier
    *(socket_entry->pkt_queue.buf_head_ptr) = rx_pkt_tail;
#endif  // SEND_BACK

    socket_entry->pkt_queue.rx_head = rx_pkt_tail;
}

// // Send a byte buffer through a socket. 
// // TODO(sadok): Add callback to free address.
// void send_buffer(socket_internal* socket_entries, void* buf, size_t len)
// {
//     // get physical address.
// }

// FIXME(sadok) This should be in the kernel
// int send_control_message(socket_internal* socket_entry, unsigned int nb_rules,
//                          unsigned int nb_queues)
// {
//     unsigned next_queue = 0;
//     block_s block;
//     queue_regs* global_block = (queue_regs *) socket_entry->kdata;
//     queue_regs* uio_data_bar2 = socket_entry->uio_data_bar2;
    
//     if (socket_entry->app_id != 0) {
//         std::cerr << "Can only send control messages from app 0" << std::endl;
//         return -1;
//     }

//     for (unsigned i = 0; i < nb_rules; ++i) {
//         block.pdu_id = 0;
//         block.dst_port = 80;
//         block.src_port = 8080;
//         block.dst_ip = 0xc0a80101; // inet_addr("192.168.1.1");
//         block.src_ip = 0xc0a80000 + i; // inet_addr("192.168.0.0");
//         block.protocol = 0x11;
//         block.pdu_size = 0x0;
//         block.pdu_flit = 0x0;
//         block.queue_id = next_queue; // FIXME(sadok) specify the relevant queue

//         next_queue = (next_queue + 1) % nb_queues;

//         // std::cout << "Setting rule: " << nb_rules << std::endl;
//         // print_block(&block);

//         socket_entry->tx_cpu_tail = tx_copy_head(socket_entry->tx_cpu_tail,
//             global_block, &block, socket_entry->kdata);
//         asm volatile ("" : : : "memory"); // compiler memory barrier
//         uio_data_bar2->tx_tail = socket_entry->tx_cpu_tail;

//         // asm volatile ("" : : : "memory"); // compiler memory barrier
//         // print_queue_regs(uio_data_bar2);
//     }

//     return 0;
// }

int dma_finish(socket_internal* socket_entry)
{
    queue_regs_t* pkt_queue_regs = socket_entry->pkt_queue.regs;
    pkt_queue_regs->rx_mem_low = 0;
    pkt_queue_regs->rx_mem_high = 0;

    munmap(socket_entry->pkt_queue.buf, ALIGNED_PKT_BUF_SIZE);

    if (dsc_queue.ref_cnt == 0) {
        return 0;
    }

    if (--(dsc_queue.ref_cnt) == 0) {
        dsc_queue.regs->rx_mem_low = 0;
        dsc_queue.regs->rx_mem_high = 0;
        munmap(dsc_queue.rx_buf, ALIGNED_DSC_BUF_PAIR_SIZE);
        free(pending_pkt_tails);
    }

    return 0;
}

void print_slot(uint32_t *rp_addr, uint32_t start, uint32_t range)
{
    for (unsigned int i = 0; i < range*16; ++i) {
        if (i%16==0) {
            printf("\n");
        }
        printf("rp_addr[%d] = 0x%08x \n", 16*start + i, rp_addr[16*start+i]);
    }
}

void print_fpga_reg(intel_fpga_pcie_dev *dev, unsigned nb_regs)
{
    uint32_t temp_r;
    for (unsigned int i = 0; i < nb_regs; ++i) {
        dev->read32(2, reinterpret_cast<void*>(0 + i*4), &temp_r);
        printf("fpga_reg[%d] = 0x%08x \n", i, temp_r);
    }
} 

void print_queue_regs(queue_regs* regs)
{
    printf("rx_tail = %d \n", regs->rx_tail);
    printf("rx_head = %d \n", regs->rx_head);
    printf("rx_mem_low = 0x%08x \n", regs->rx_mem_low);
    printf("rx_mem_high = 0x%08x \n", regs->rx_mem_high);

    printf("tx_tail = %d \n", regs->tx_tail);
    printf("tx_head = %d \n", regs->tx_head);
    printf("tx_mem_low = 0x%08x \n", regs->tx_mem_low);
    printf("tx_mem_high = 0x%08x \n", regs->tx_mem_high);
}

void print_buffer(uint32_t* buf, uint32_t nb_flits)
{
    for (uint32_t i = 0; i < nb_flits; ++i) {
        for (uint32_t j = 0; j < 8; ++j) {
            for (uint32_t k = 0; k < 8; ++k) {
                printf("%02x ", ((uint8_t*) buf)[i*64 + j*8 + k] );
            }
            printf("\n");
        }
        printf("\n");
    }
}

// uint32_t tx_copy_head(uint32_t tx_tail, queue_regs *global_block, 
//         block_s *block, uint32_t *kdata) {
//     // uint32_t pdu_flit;
//     // uint32_t copy_flit;
//     uint32_t free_slot;
//     uint32_t tx_head;
//     // uint32_t copy_size;
//     uint32_t base_addr;

//     tx_head = global_block->tx_head;
//     // calculate free_slot
//     if (tx_tail < tx_head) {
//         free_slot = tx_head - tx_tail - 1;
//     } else {
//         //CPU2FPGA ring buffer does not have the global register. 
//         //the free_slot should be at most one slot smaller than CPU2FPGA ring buffer.
//         free_slot = C2F_BUFFER_SIZE - tx_tail + tx_head - 1;
//     }
//     //printf("free_slot = %d; tx_head = %d; tx_tail = %d\n", 
//     //        free_slot, tx_head, tx_tail);   
//     //block when the CPU2FPGA ring buffer is almost full
//     while (free_slot < 1) { 
//         //recalculate free_slot
//     	tx_head = global_block->tx_head;
//     	if (tx_tail < tx_head) {
//     	    free_slot = tx_head - tx_tail - 1;
//     	} else {
//     	    free_slot = C2F_BUFFER_SIZE - tx_tail + tx_head - 1;
//     	}
//     }
//     base_addr = C2F_BUFFER_OFFSET + tx_tail * 16;

//     // printf("tx base addr: 0x%08x\n", base_addr);

//     //memcpy(&kdata[base_addr], src_addr, 16*4); //each flit is 512 bit

//     //Fake match
//     kdata[base_addr + PDU_ID_OFFSET] = block->pdu_id;
//     //PDU_SIZE
//     kdata[base_addr + PDU_PORTS_OFFSET] = (((uint32_t) block->src_port) << 16) | ((uint32_t) block->dst_port);
//     kdata[base_addr + PDU_DST_IP_OFFSET] = block->dst_ip;
//     kdata[base_addr + PDU_SRC_IP_OFFSET] = block->src_ip;
//     kdata[base_addr + PDU_PROTOCOL_OFFSET] = block->protocol;
//     kdata[base_addr + PDU_SIZE_OFFSET] = block->pdu_size;
//     //exclude the rule flits and header flit
//     kdata[base_addr + PDU_FLIT_OFFSET] = block->pdu_flit - 1;
//     kdata[base_addr + ACTION_OFFSET] = ACTION_NO_MATCH;
//     kdata[base_addr + QUEUE_ID_LO_OFFSET] = block->queue_id & 0xFFFFFFFF;
//     kdata[base_addr + QUEUE_ID_HI_OFFSET] = (block->queue_id >> 32) & 0xFFFFFFFF;
    
//     // print_slot(kdata, base_addr/16, 1);

//     //update tx_tail
//     if(tx_tail == C2F_BUFFER_SIZE-1){
//         tx_tail = 0;
//     } else {
//         tx_tail += 1;
//     }
//     return tx_tail;
// }
