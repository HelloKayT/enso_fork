# FPGA Counters

| Counter                    | Description |
| -------------------------- | ----------- |
| `IN_PKT`                   | Number of packets that arrived at the Ethernet port. |
| `OUT_PKT`                  | Number of packets that left the Ethernet port. |
| `OUT_IN_COMP_PKT`          | |
| `OUT_PARSER_PKT`           | |
| `MAX_PARSER_FIFO`          | |
| `FD_IN_PKT`                | |
| `FD_OUT_PKT`               | |
| `MAX_FD_OUT_FIFO`          | |
| `IN_DATAMOVER_PKT`         | |
| `IN_EMPTYLIST_PKT`         | |
| `OUT_EMPTYLIST_PKT`        | |
| `PKT_ETH`                  | |
| `PKT_DROP`                 | |
| `PKT_PCIE`                 | |
| `MAX_DM2PCIE_FIFO`         | Maximum occupancy of the packet FIFO connection the data mover to `pdu_gen`. |
| `MAX_DM2PCIE_META_FIFO`    | Maximum occupancy of the metadata FIFO connection the data mover to `pdu_gen`. |
| `PCIE_PKT`                 | |
| `PCIE_META`                | |
| `DM_PCIE_PKT`              | |
| `DM_PCIE_META`             | |
| `DM_ETH_PKT`               | |
| `RX_DMA_PKT`               | Number of packets sent to the DMA engine (RX path). |
| `RX_PKT_HEAD_UPD`          | Number of updates to the RX packet queue head pointer from software. |
| `TX_DSC_TAIL_UPD`          | Number of updates to the TX descriptor queue tail pointer from software. |
| `DMA_REQUEST`              | Number of actual DMA requests  sent to PCIe, a single packet may require multiple DMA requests as each flit requires a separate DMA requests and some packets also need a descriptor. |
| `RULE_SET`                 | Number of rules set in the Flow Table. |
| `EVICTION`                 | Number of evictions in the Flow Table. Evictions are currently not implemented so this counter effectively reports the number of ignored evictions. |
| `MAX_PDUGEN_PKT_FIFO`      | Maximum occupancy of the packet FIFO in the `pdu_gen` module. If either this FIFO or `MAX_PDUGEN_META_FIFO` are at their maximum capacity, this indicates that the DMA engine is not able to consume packets fast enough. |
| `MAX_PDUGEN_META_FIFO`     | Maximum occupancy of the metadata FIFO in the `pdu_gen` module. If either this FIFO or `MAX_PDUGEN_PKT_FIFO` are at their maximum capacity, this indicates that the DMA engine is not able to consume packets fast enough. |
| `PCIE_CORE_FULL`           | Number of cycles that a packet could not be sent to memory due to backpressure from PCIe. |
| `RX_DMA_DSC_CNT`           | Number of RX descriptors that were DMAed to host memory. |
| `RX_DMA_DSC_DROP_CNT`      | Number of RX descriptors that were dropped. This does not indicate a problem, it happens when hardware determines that software already knows the latest pointer for a given packet queue and does not need an extra descriptor. |
| `RX_DMA_PKT_FLIT_CNT`      | Number of RX flits DMAed to host memory. |
| `RX_DMA_PKT_FLIT_DROP_CNT` | Number of RX flits that should have been DMAed but were dropped instead. |
| `CPU_DSC_BUF_FULL`         | Number of times a packet could not be sent to memory because the corresponding descriptor buffer was full. |
| `CPU_DSC_BUF_IN`           | Number of packets that entered the descriptor queue manager. This number includes packets that do not trigger descriptors as well as placeholder packets used by the reactive descriptor mechanism. |
| `CPU_DSC_BUF_OUT`          | Number of packets that left the descriptor queue manager. This number includes packets that do not trigger descriptors as well as placeholder packets used by the reactive descriptor mechanism. |
| `CPU_PKT_BUF_FULL`         | Number of times a packet could not be sent to memory because the corresponding packet buffer was full. |
| `CPU_PKT_BUF_IN`           | Number of packets that entered the packet queue manager. This number also includes placeholder packets used by the reactive descriptor mechanism. |
| `CPU_PKT_BUF_OUT`          | Number of packets that left the packet queue manager. This number also includes placeholder packets used by the reactive descriptor mechanism. |
| `MAX_PCIE_PKT_FIFO`        | Maximum queue occupancy, in number of flits (64-byte chunks), ever observed in the FIFO that feeds packets to the DMA engine. This usually increases when one of the memory buffers becomes full (check the `CPU_DSC_BUF_FULL` and `CPU_PKT_BUF_FULL` to verify) or if the PCIe is back-pressuring the design (check `PCIE_CORE_FULL` to verify). Contention may also happen if the TX path is sending too many completion notifications. |
| `MAX_PCIE_META_FIFO`       | Maximum queue occupancy, in number of packets, ever observed in the FIFO that feeds metadata to the DMA engine. This usually increases when one of the memory buffers becomes full (check the `CPU_DSC_BUF_FULL` and `CPU_PKT_BUF_FULL` to verify) or if the PCIe is back-pressuring the design (check `PCIE_CORE_FULL` to verify).  Contention may also happen if the TX path is sending too many completion notifications. |
| `PCIE_RX_IGNORED_HEAD`     | When software updates the head pointer for an RX packet queue, the hardware needs to send a descriptor if there are packets left in the queue. But if software updates the pointer too often, the queue that holds these updates may overflow and cause some of these updates to be ignored. When this happens this counter is incremented. Note that the head pointer is always updated to the value that software set what is ignored is the extra descriptor, not the update itself. |
| `PCIE_TX_Q_FULL_SIGNALS`   | When any of the queues in the `cpu_to_fpga` module becomes full and we still try to write to it, the appropriate bit on this signal is set. This must remain zero to ensure correct behavior.<br/>- bit 0: set when `dsc_read_queue` becomes full.<br/>- bit 1: set when `rddm_desc_queue` becomes full.<br/>- bit 2: set when `rddm_prio_queue` becomes full.<br/>- bit 3: set when `meta_queue` becomes full.<br/>- bit 4: set when `pkt_queue` becomes full.<br/>- bit 5: set when `compl_buf` becomes full.<br/>- bit 6: set when `out_pkt` queue becomes full.<br/>- bit 7: set when `out_config` queue becomes full. |
| `PCIE_TX_DSC_CNT`          | Number of TX descriptors processed by `cpu_to_fpga`. |
| `PCIE_TX_EMPTY_TAIL_CNT`   | Number of TX tail pointer updates that were ignored since there were no descriptors to be read (i.e., the head was equal to the new tail value). |
| `PCIE_TX_DSC_READ_CNT`     | Number of TX descriptors read from host memory. |
| `PCIE_TX_PKT_READ_CNT`     | Number of TX packet flits read from host memory. |
| `PCIE_TX_BATCH_CNT`        | Number of batches that left `cpu_to_fpga`. |
| `PCIE_TX_MAX_INFLIGH_DSCS` | Maximum number of in-flight descriptor reads in the TX path. |
| `PCIE_TX_MAX_NB_REQ_DSCS`  | Maximum number of descriptors requested at once in the TX path. This is determined by hardware and is independent from the TX descriptor pointer updates controlled by software. |
| `PCIE_TX_DMA_PKT`          | Number of packets DMAed from CPU (TX path) that left the DMA engine. |
| `PCIE_TOP_FULL_SIGNALS_1`  | When any of the packet queue manager fifos become full in the `pcie_top` module and we still try to write to it, the bit on this signal corresponding to the packet queue is set. This must remain zero to ensure correct behavior. This signal is only relevant when `DEBUG` is defined before synthesis. |
| `PCIE_TOP_FULL_SIGNALS_2`  | When any of the remaining queues in the `pcie_top` module becomes full (besides the ones in `PCIE_TOP_FULL_SIGNALS_1`) and we still try to write to it, the appropriate bit on this signal is set. This must remain zero to ensure correct behavior. This signal is only relevant when `DEBUG` is defined before synthesis. <br/>- bit 0: set when `head_upd_queue` becomes full.<br/>- bit 1: set when `in_queue` becomes full.<br/>- bit 2: set when `dsc_q_mngr` becomes full.<br/>- bit 3: set when `fpga_to_cpu_inst` input packet fifo becomes full.<br/>- bit 4: set when `fpga_to_cpu_inst` input meta fifo becomes full.<br/>- bit 5: set when `fpga_to_cpu_inst` input completion fifo becomes full. |