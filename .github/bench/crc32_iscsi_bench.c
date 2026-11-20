/*
 * Direct-call benchmark of the aarch64 crc32_iscsi kernels.
 *
 * 1. Checks every kernel against crc32_iscsi_base for all lengths 0..4200
 *    and several buffer offsets.
 * 2. Times every kernel at a set of sizes: warm cache, one thread, the same
 *    64-byte-aligned buffer, randomized kernel order per round, median of
 *    all rounds.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "crc.h"

typedef unsigned int (*crc_function)(unsigned char *, int, unsigned int);

extern unsigned int
crc32_iscsi_crc_ext(unsigned char *, int, unsigned int);
extern unsigned int
crc32_iscsi_3crc_fold(unsigned char *, int, unsigned int);
extern unsigned int
crc32_iscsi_refl_pmull(unsigned char *, int, unsigned int);
extern unsigned int
crc32_iscsi_x6(unsigned char *, int, unsigned int);

static unsigned int
crc32_iscsi_dispatched(unsigned char *buffer, int length, unsigned int seed)
{
        return crc32_iscsi(buffer, length, seed);
}

struct kernel {
        const char *name;
        crc_function function;
};

static const struct kernel kernels[] = {
        { "crc_ext", crc32_iscsi_crc_ext },
        { "3crc_fold", crc32_iscsi_3crc_fold },
        { "refl_pmull", crc32_iscsi_refl_pmull },
        { "x6", crc32_iscsi_x6 },
        { "dispatched", crc32_iscsi_dispatched },
};
#define KERNEL_COUNT (int) (sizeof(kernels) / sizeof(kernels[0]))

static const int sizes[] = { 64, 128, 256, 512, 1000, 1023, 1024, 1025, 2048, 4096, 8192, 65536, 1048576 };
#define SIZE_COUNT (int) (sizeof(sizes) / sizeof(sizes[0]))

#define ROUNDS         15
#define SAMPLE_SECONDS 0.03
#define CHECK_MAX_LEN  4200
#define BUFFER_SIZE    (1048576 + 64)

static double
now_seconds(void)
{
        struct timespec timestamp;
        clock_gettime(CLOCK_MONOTONIC, &timestamp);
        return timestamp.tv_sec + timestamp.tv_nsec * 1e-9;
}

static int
compare_double(const void *left, const void *right)
{
        double difference = *(const double *) left - *(const double *) right;
        return (difference > 0) - (difference < 0);
}

static volatile unsigned int sink;

/* Throughput in GB/s (decimal) of one sample of about SAMPLE_SECONDS. */
static double
sample(crc_function function, unsigned char *buffer, int length)
{
        long iterations = 0;
        long batch = 1;
        unsigned int crc = 0;
        double start = now_seconds();
        double elapsed;

        do {
                for (long index = 0; index < batch; index++)
                        crc = function(buffer, length, crc);
                iterations += batch;
                batch *= 2;
                elapsed = now_seconds() - start;
        } while (elapsed < SAMPLE_SECONDS);
        sink = crc;
        return (double) iterations * length / elapsed / 1e9;
}

static int
check(unsigned char *buffer)
{
        static const int offsets[] = { 0, 1, 3, 8, 13 };
        int failures = 0;

        for (int offset_index = 0; offset_index < (int) (sizeof(offsets) / sizeof(offsets[0]));
             offset_index++) {
                unsigned char *data = buffer + offsets[offset_index];
                for (int length = 0; length <= CHECK_MAX_LEN; length++) {
                        unsigned int seed = (unsigned int) rand();
                        unsigned int expected = crc32_iscsi_base(data, length, seed);
                        for (int kernel_index = 0; kernel_index < KERNEL_COUNT; kernel_index++) {
                                unsigned int actual =
                                        kernels[kernel_index].function(data, length, seed);
                                if (actual != expected) {
                                        if (failures < 20)
                                                printf("MISMATCH %s offset=%d len=%d: "
                                                       "0x%08x != 0x%08x\n",
                                                       kernels[kernel_index].name,
                                                       offsets[offset_index], length, actual,
                                                       expected);
                                        failures++;
                                }
                        }
                }
        }
        return failures;
}

int
main(void)
{
        unsigned char *buffer;
        static double samples[SIZE_COUNT][KERNEL_COUNT][ROUNDS];
        int order[KERNEL_COUNT];

        if (posix_memalign((void **) &buffer, 64, BUFFER_SIZE)) {
                printf("alloc error\n");
                return 1;
        }
        srand(1);
        for (int index = 0; index < BUFFER_SIZE; index++)
                buffer[index] = (unsigned char) rand();

        int failures = check(buffer);
        printf("correctness vs crc32_iscsi_base, lengths 0..%d x 5 offsets: %s (%d mismatches)\n\n",
               CHECK_MAX_LEN, failures ? "FAIL" : "PASS", failures);

        for (int round = 0; round < ROUNDS; round++) {
                for (int size_index = 0; size_index < SIZE_COUNT; size_index++) {
                        for (int index = 0; index < KERNEL_COUNT; index++)
                                order[index] = index;
                        for (int index = KERNEL_COUNT - 1; index > 0; index--) {
                                int other = rand() % (index + 1);
                                int swap = order[index];
                                order[index] = order[other];
                                order[other] = swap;
                        }
                        for (int index = 0; index < KERNEL_COUNT; index++) {
                                int kernel_index = order[index];
                                samples[size_index][kernel_index][round] =
                                        sample(kernels[kernel_index].function, buffer,
                                               sizes[size_index]);
                        }
                }
        }

        printf("median throughput, GB/s (decimal), %d rounds x ~%.0f ms per sample\n\n", ROUNDS,
               SAMPLE_SECONDS * 1000);
        printf("| Bytes |");
        for (int kernel_index = 0; kernel_index < KERNEL_COUNT; kernel_index++)
                printf(" %s |", kernels[kernel_index].name);
        printf("\n|---:|");
        for (int kernel_index = 0; kernel_index < KERNEL_COUNT; kernel_index++)
                printf("---:|");
        printf("\n");
        for (int size_index = 0; size_index < SIZE_COUNT; size_index++) {
                printf("| %d |", sizes[size_index]);
                for (int kernel_index = 0; kernel_index < KERNEL_COUNT; kernel_index++) {
                        qsort(samples[size_index][kernel_index], ROUNDS, sizeof(double),
                              compare_double);
                        printf(" %.2f |", samples[size_index][kernel_index][ROUNDS / 2]);
                }
                printf("\n");
        }
        return failures ? 1 : 0;
}
