#include "action_metalfpga.H"

#include "operators.h"
#include "axi_perfmon.h"
#include "axi_switch.h"
#include "mtl_jobs.h"
#include "mtl_endian.h"
#include <metal/stream.h>

#include "mtl_op_mem.h"

#define HW_RELEASE_LEVEL       0x00000013

// ------------------------------------------------
// -------------- ACTION ENTRY POINT --------------
// ------------------------------------------------
void hls_action(snap_membus_t * din,
                snap_membus_t * dout,
                metal::fpga::axi_datamover_command_stream_t &mm2s_cmd,
                metal::fpga::axi_datamover_status_stream_t &mm2s_sts,
                metal::fpga::axi_datamover_command_stream_t &s2mm_cmd,
                metal::fpga::axi_datamover_status_ibtt_stream_t &s2mm_sts,
                snapu32_t *metal_ctrl,
                hls::stream<snapu8_t> &interrupt_reg,
            #ifdef NVME_ENABLED
                metal::fpga::NVMeCommandStream &nvme_read_cmd,
                metal::fpga::NVMeResponseStream &nvme_read_resp,
                metal::fpga::NVMeCommandStream &nvme_write_cmd,
                metal::fpga::NVMeResponseStream &nvme_write_resp,
            #endif
                metal::fpga::action_reg * action_reg,
                action_RO_config_reg * action_config)
{
    // Configure Host Memory AXI Interface
#pragma HLS INTERFACE m_axi port=din bundle=host_mem offset=slave depth=512 \
    max_read_burst_length=64 max_write_burst_length=64
#pragma HLS INTERFACE s_axilite port=din bundle=ctrl_reg offset=0x030

#pragma HLS INTERFACE m_axi port=dout bundle=host_mem offset=slave depth=512 \
    max_read_burst_length=64 max_write_burst_length=64
#pragma HLS INTERFACE s_axilite port=dout bundle=ctrl_reg offset=0x040

    // Configure Host Memory AXI Lite Master Interface
#pragma HLS DATA_PACK variable=action_config
#pragma HLS INTERFACE s_axilite port=action_config bundle=ctrl_reg  offset=0x010
#pragma HLS DATA_PACK variable=action_reg
#pragma HLS INTERFACE s_axilite port=action_reg bundle=ctrl_reg offset=0x100
#pragma HLS INTERFACE s_axilite port=return bundle=ctrl_reg

#pragma HLS INTERFACE axis port=interrupt_reg

#ifdef NVME_ENABLED
    #pragma HLS INTERFACE axis port=nvme_read_cmd
    #pragma HLS INTERFACE axis port=nvme_read_resp
    #pragma HLS INTERFACE axis port=nvme_write_cmd
    #pragma HLS INTERFACE axis port=nvme_write_resp
#endif

    // Configure AXI4 Stream Interface
#pragma HLS INTERFACE axis port=mm2s_cmd
#pragma HLS INTERFACE axis port=mm2s_sts
#pragma HLS INTERFACE axis port=s2mm_cmd
#pragma HLS INTERFACE axis port=s2mm_sts

#pragma HLS INTERFACE m_axi port=metal_ctrl depth=32

    // Required Action Type Detection
    switch (action_reg->Control.flags) {
    case 0:
        action_config->action_type = metal::fpga::ActionType;
        action_config->release_level = (snapu32_t)HW_RELEASE_LEVEL;
        action_reg->Control.Retc = (snapu32_t)0xe00f;
        break;
    default:
        action_reg->Control.Retc = metal::fpga::process_action(
            din,
            dout,
#ifdef NVME_ENABLED
            nvme_read_cmd,
            nvme_read_resp,
            nvme_write_cmd,
            nvme_write_resp,
#endif
            mm2s_cmd,
            mm2s_sts,
            s2mm_cmd,
            s2mm_sts,
            metal_ctrl,
            interrupt_reg,
            action_reg);
        break;
    }
}

#ifndef __SYNTHESIS__
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

int main(int argc, char* argv[]) {
  return Catch::Session().run(argc, argv);
}
#endif
