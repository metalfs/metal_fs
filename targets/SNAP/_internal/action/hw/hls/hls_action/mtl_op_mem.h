#pragma once

#include <metal/stream.h>
#include <snap_action_metal.h>

#include "mtl_definitions.h"
#include "mtl_extmap.h"

namespace metal {
namespace fpga {

const uint64_t DRAMBaseOffset = 0x8000000000000000;

const uint64_t NVMeDRAMBaseOffset = 0x200000000;  // According to https://github.com/open-power/snap/blob/master/hardware/doc/NVMe.md
const uint64_t NVMeDRAMReadOffset  = 0;
const uint64_t NVMeDRAMWriteOffset = (1u << 31); // 2 GiB

typedef struct axi_datamover_status {
    ap_uint<8> data;
    ap_uint<1> keep;
    ap_uint<1> last;
} axi_datamover_status_t;
typedef hls::stream<axi_datamover_status_t> axi_datamover_status_stream_t;

class DatamoverIbttStatus : public ap_uint<32> {
public:
    auto end_of_packet()         -> decltype(ap_uint<32>::operator[](31)) { return (*this)[31]; }
    auto num_bytes_transferred() -> decltype(ap_uint<32>::range(30, 8))   { return this->range(30, 8); }
};

typedef struct axi_datamover_ibtt_status {
    DatamoverIbttStatus data;
    ap_uint<1> keep;
    ap_uint<1> last;
} axi_datamover_ibtt_status_t;
typedef hls::stream<axi_datamover_ibtt_status_t> axi_datamover_status_ibtt_stream_t;

class DatamoverCommand : public ap_uint<103> {
public:
    DatamoverCommand() : ap_uint(0) {};
    auto end_of_frame()      -> decltype(ap_uint<103>::operator[](30)) { return (*this)[30]; }
    auto axi_burst_type()    -> decltype(ap_uint<103>::operator[](23)) { return (*this)[23]; }
    auto effective_address() -> decltype(ap_uint<103>::range(95, 32))  { return this->range(95, 32); }
    auto transfer_bytes()    -> decltype(ap_uint<103>::range(22, 0))   { return this->range(22, 0); }
};

typedef hls::stream<DatamoverCommand> axi_datamover_command_stream_t;

class NVMeCommand : public ap_uint<168> {
public:
    auto dram_offset()          -> decltype(ap_uint<168>::range(63, 0))     { return this->range(63, 0); }
    auto nvme_block_offset()    -> decltype(ap_uint<168>::range(127, 64))   { return this->range(127, 64); }
    auto num_blocks()           -> decltype(ap_uint<168>::range(159, 128))  { return this->range(159, 128); }
    auto drive()                -> decltype(ap_uint<168>::range(162, 160))  { return this->range(162, 160); }
};

typedef hls::stream<NVMeCommand> NVMeCommandStream;
typedef hls::stream<ap_uint<1>> NVMeResponseStream;

struct MemoryTransferResult {
    snap_bool_t last;
    snapu32_t num_bytes;
};

extern mtl_extmap_t dram_read_extmap;
extern mtl_extmap_t dram_write_extmap;
#ifdef NVME_ENABLED
extern mtl_extmap_t nvme_read_extmap;
extern mtl_extmap_t nvme_write_extmap;
#endif

mtl_retc_t op_mem_set_config(Address &address, snap_bool_t read, Address &config, snapu32_t *data_preselect_switch_ctrl);

#ifdef NVME_ENABLED
void preload_nvme_blocks(const Address &address, mtl_extmap_t &dram_extentmap, mtl_extmap_t &nvme_extentmap, NVMeCommandStream &nvme_read_cmd, NVMeResponseStream &nvme_read_resp);
#endif

void op_mem_read(
    axi_datamover_command_stream_t &mm2s_cmd,
    axi_datamover_status_stream_t &mm2s_sts,
    snapu32_t *random_ctrl,
#ifdef NVME_ENABLED
    NVMeCommandStream &nvme_read_cmd,
    NVMeResponseStream &nvme_read_resp,
#endif
    Address &config);

uint64_t op_mem_write(
    axi_datamover_command_stream_t &s2mm_cmd,
    axi_datamover_status_ibtt_stream_t &s2mm_sts,
#ifdef NVME_ENABLED
    NVMeCommandStream &nvme_write_cmd,
    NVMeResponseStream &nvme_write_resp,
#endif
    Address &config);

extern Address read_mem_config;
extern Address write_mem_config;

struct Transfer {
    uint64_t address;
    uint64_t size;
    AddressType type;
};

struct TransferElement {
    Transfer data;
    snap_bool_t last;
};

void load_nvme_data(hls::stream<TransferElement> &in, hls::stream<TransferElement> &out
#ifdef NVME_ENABLED
    , hls::stream<uint64_t> &nvme_transfers
    , NVMeCommandStream &nvme_cmd
    , NVMeResponseStream &nvme_resp
#endif
);

void write_nvme_data(hls::stream<TransferElement> &in
#ifdef NVME_ENABLED
    , hls::stream<uint64_t> &nvme_transfers
    , NVMeCommandStream &nvme_cmd
    , NVMeResponseStream &nvme_resp
#endif
);

enum class TransferType {
    Read,
    Write
};

template<TransferType T>
void issue_partial_transfers(const Address& transfer
    , mtl_extmap_t &dram_extentmap
    , hls::stream<TransferElement> &partial_transfers
#ifdef NVME_ENABLED
    , mtl_extmap_t &nvme_extentmap
    , hls::stream<uint64_t> &nvme_transfers
#endif
);

void transfer_to_stream(hls::stream<TransferElement> &in, axi_datamover_command_stream_t &dm_cmd, axi_datamover_status_stream_t &dm_sts);
void transfer_from_stream(hls::stream<TransferElement> &in, hls::stream<TransferElement> &out, axi_datamover_command_stream_t &dm_cmd, axi_datamover_status_ibtt_stream_t &dm_sts, uint64_t *size);

}  // namespace fpga
}  // namespace metal

#include "mtl_op_mem.hpp"
