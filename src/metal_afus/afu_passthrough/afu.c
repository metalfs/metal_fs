#include "afu.h"

#include <stddef.h>
#include <getopt.h>

#include "../../metal/metal.h"

#define AFU_PASSTHROUGH_ID 2

static const char help[] =
    "Usage: passthrough [-h]\n"
    "\n";

extern int optind;
static const void* handle_opts(int argc, char *argv[], uint64_t *length, bool *valid) {
    optind = 1; // Reset getopt
    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            { "help", no_argument, NULL, 'h' }
        };

        int ch = getopt_long(argc, argv, "h", long_options, &option_index);
        if (ch == -1)
            break;

        switch (ch) {
        case 'h':
        default:
            *length = sizeof(help);
            *valid = false;
            return help;
        }
    }

    *valid = true;
    *length = 0;
    return "";
}

static int apply_config(struct snap_action *action) {
    // Nothing to do
    return MTL_SUCCESS;
}

mtl_afu_specification afu_passthrough_specification = {
    { AFU_PASSTHROUGH_ID, 1 },
    "passthrough",

    &handle_opts,
    &apply_config
};
