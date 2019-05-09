#include "operators.h"

#include <snap_types.h>

#include "mtl_endian.h"

static void poll_interrupts(snapu8_t mask, snapu8_t* interrupt_reg) {
    snapu8_t aggregate = 0;
    while (aggregate != mask) {
        aggregate |= *interrupt_reg;
    }
}

mtl_retc_t do_run_operators(
    snap_membus_t * mem_in,
    snap_membus_t * mem_out,
    axi_datamover_command_stream_t &mm2s_cmd,
    axi_datamover_status_stream_t &mm2s_sts,
    axi_datamover_command_stream_t &s2mm_cmd,
    axi_datamover_status_stream_t &s2mm_sts,
    snapu32_t *random_ctrl,
    snapu8_t *interrupt_reg,
    snapu64_t &enable_mask
) {
    {
#pragma HLS DATAFLOW
        // The order should only matter when executing in the test bench

        // Input Operators
        op_mem_read(
            mm2s_cmd,
            mm2s_sts,
            random_ctrl,
            read_mem_config);

        // Output Operators
        op_mem_write(
            s2mm_cmd,
            s2mm_sts,
            write_mem_config);

        poll_interrupts(enable_mask, interrupt_reg);
    }

    return SNAP_RETC_SUCCESS;
}

mtl_retc_t action_run_operators(
    snap_membus_t * mem_in,
    snap_membus_t * mem_out,
    axi_datamover_command_stream_t &mm2s_cmd,
    axi_datamover_status_stream_t &mm2s_sts,
    axi_datamover_command_stream_t &s2mm_cmd,
    axi_datamover_status_stream_t &s2mm_sts,
    snapu32_t *metal_ctrl,
    snapu8_t *interrupt_reg,
    snapu64_t &enable_mask
) {
    snapu32_t *random_ctrl =
        (metal_ctrl + (0x44A20000 / sizeof(uint32_t)));

    snapu32_t *operator_ctrl =
        (metal_ctrl + (0x44A40000 / sizeof(uint32_t)));

    const snapu32_t operator_offset = 0x10000 / sizeof(uint32_t);

    // 0x00 : Control signals
    //        bit 0  - ap_start (Read/Write/SC)
    //        bit 1  - ap_done (Read/COR)
    //        bit 2  - ap_idle (Read)
    //        bit 3  - ap_ready (Read)
    //        bit 7  - auto_restart (Read/Write)
    //        others - reserved
    // 0x04 : Global Interrupt Enable Register
    //        bit 0  - Global Interrupt Enable (Read/Write)
    //        others - reserved
    // 0x08 : IP Interrupt Enable Register (Read/Write)
    //        bit 0  - Channel 0 (ap_done)
    //        bit 1  - Channel 1 (ap_ready)
    // 0x0c : IP Interrupt Status Register (Read/TOW)
    //        bit 0  - Channel 0 (ap_done)
    //        others - reserved
    // (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

    for (int i = 0; i < 8; ++i) {
        if (enable_mask[i]) {
            operator_ctrl[i * operator_offset] = 1;
        }
    }

    do_run_operators(mem_in, mem_out, mm2s_cmd, mm2s_sts, s2mm_cmd, s2mm_sts, random_ctrl, interrupt_reg, enable_mask);
}