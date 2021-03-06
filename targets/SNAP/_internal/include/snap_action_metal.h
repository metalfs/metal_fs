#pragma once

#include <stdint.h>
#include <stdbool.h>

namespace metal {
namespace fpga {

const uint64_t ActionType = 0xFB060001;

enum class JobType : uint64_t {
    ReadImageInfo = 0,
    // data is written to job_address
    // 64bit words direct data:
    //   word2: bytes_written       | W

    Map = 1,
    // 64bit words at job_address:
    //   word0: slot                | R
    //   ...
    //   word(2n+8): extent n begin | R
    //   word(2n+9): extent n count | R

    ConfigureStreams = 2,
    // 64bit words at job_address:
    //   word0:                     | R
    //     halfword0: stream 0 dest | R
    //     halfword1: stream 1 dest | R
    //   ...
    //   word3:                     | R
    //     halfword0: stream 6 dest | R
    //     halfword1: stream 7 dest | R

    ResetPerfmon = 3,

    ConfigurePerfmon = 4,
    // 64bit words at job_address:
    //   word0: stream id 0         | R
    //   word0: stream id 1         | R

    ReadPerfmonCounters = 5,
    // 64bit words at job_address:
    //   word0: global clock ctr    | W
    // 32bit words at job_address + 8:
    //   word0: counter0            | W
    //   word1: counter1            | W
    //   word2: counter2            | W
    //   word3: counter3            | W
    //   word4: counter4            | W
    //   word5: counter5            | W
    //   word6: counter6            | W
    //   word7: counter7            | W
    //   word8: counter8            | W
    //   word9: counter9            | W

    RunOperators = 6,
    // no indirect payload data
    // 64bit words direct data:
    //   word0: enable mask         | R
    //   word1: perfmon_enable      | R
    //   word2: bytes_written       | W

    ConfigureOperator = 7
    // 32bit words at job_address:
    //   word0: offset in sizeof(snapu32_t) | R
    //   word1: length in sizeof(snapu32_t) | R
    //   word2: operator id         | R
    //   word3: prepare             | R
    // <...> configuration data
};
const uint64_t StorageBlockSizeD = 18;
const uint64_t StorageBlockSize = 1 << StorageBlockSizeD;
const uint64_t PagefileSize = 128 * StorageBlockSize;
const uint64_t MaxExtentsPerFile = 512;

enum class ExtmapSlot : uint64_t {
    CardDRAMRead = 0,
    CardDRAMWrite = 1,
    NVMeRead = 2,
    NVMeWrite = 3
};

enum class AddressType : uint16_t {
    Host = 0,
    CardDRAM = 1,
    NVMe = 2,
    Null = 3,
    Random = 4
};

enum class MapType : uint16_t {
    None = 0,
    DRAM = 1,
    NVMe = 2,
    DRAMAndNVMe = 3
};

struct Address {
    uint64_t addr;
    uint32_t size;
    AddressType type;
    MapType map;
}; /* 16 bytes */

struct Job {
    uint64_t job_address;
    JobType job_type;
    Address source;
    Address destination;
    uint64_t direct_data[4];
};

}  // namespace fpga
}  // namespace metal
