#include "operator.h"

#include <stddef.h>
#include <getopt.h>
#include <endian.h>

#include <snap_tools.h>
#include <libsnap.h>
#include <snap_hls_if.h>

#include "../../metal_fpga/include/action_metalfpga.h"

#include "../../metal/metal.h"

static const char help[] =
    "Usage: blowfish_decrypt [-h]\n"
    "  -k, --key              decryption key\n"
    "\n";

static char _key[16] = { 0 };

extern int optind;

static inline ssize_t
file_read(const char *fname, uint8_t *buff, size_t len)
{
    int rc;
    FILE *fp;

    if ((fname == NULL) || (buff == NULL) || (len == 0))
            return -EINVAL;

    fp = fopen(fname, "r");
    if (!fp) {
            fprintf(stderr, "err: Cannot open file %s: %s\n",
                    fname, strerror(errno));
            return -ENODEV;
    }
    rc = fread(buff, len, 1, fp);
    if (rc == -1) {
            fprintf(stderr, "err: Cannot read from %s: %s\n",
                    fname, strerror(errno));
            fclose(fp);
            return -EIO;
    }

    fclose(fp);
    return rc;
}

static const void* handle_opts(mtl_operator_invocation_args *args, uint64_t *length, bool *valid) {
    optind = 1; // Reset getopt

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            { "help", no_argument, NULL, 'h' },
            { "key", required_argument, NULL, 'k' }
        };

        int ch = getopt_long(args->argc, args->argv, "kh", long_options, &option_index);
        if (ch == -1)
            break;

        switch (ch) {
        case 'k': {
            file_read(optarg, _key, 16);
            break;
        }
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
    uint64_t *job_struct = (uint64_t*)snap_malloc(sizeof(_key));
    memcpy(job_struct, _key, sizeof(_key)); // No endianness conversions here

    metalfpga_job_t mjob;
    mjob.job_type = MTL_JOB_OP_BLOWFISH_DECRYPT_SET_KEY;
    mjob.job_address = (uint64_t)job_struct;

    struct snap_job cjob;
    snap_job_set(&cjob, &mjob, sizeof(mjob), NULL, 0);

    const unsigned long timeout = 10;
    int rc = snap_action_sync_execute_job(action, &cjob, timeout);

    free(job_struct);

    if (rc != 0)
        // Some error occurred
        return MTL_ERROR_INVALID_ARGUMENT;

    if (cjob.retc != SNAP_RETC_SUCCESS)
        // Some error occurred
        return MTL_ERROR_INVALID_ARGUMENT;

    return MTL_SUCCESS;
}

mtl_operator_specification op_blowfish_decrypt_specification = {
    { OP_BLOWFISH_DECRYPT_ENABLE_ID, OP_BLOWFISH_DECRYPT_STREAM_ID },
    "blowfish_decrypt",
    false,

    &handle_opts,
    &apply_config,
    NULL,
    NULL
};
