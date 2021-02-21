#include <stdlib.h>

void STARCH_BENCHMARK(preamble_u16) (void)
{
    uint16_t *in = NULL, *out = NULL;
    const unsigned len = 65536;

    if (!(in = STARCH_BENCHMARK_ALLOC(len, uint16_t)) || !(out = STARCH_BENCHMARK_ALLOC(len, uint16_t))) {
        goto done;
    }

    srand(1);

    for (unsigned i = 0; i < len; ++i) {
        in[i] = rand() % 65536;
    }

    STARCH_BENCHMARK_RUN( preamble_u16, in, len, 5, out );

 done:
    STARCH_BENCHMARK_FREE(in);
    STARCH_BENCHMARK_FREE(out);
}

bool STARCH_BENCHMARK_VERIFY(preamble_u16) (const uint16_t *in, unsigned len, unsigned halfbit, uint16_t *out)
{
    bool okay = true;

    for (unsigned i = 0; i < len - halfbit * 9; ++i) {
        uint32_t sum = (in[i] + in[i + halfbit * 2] + in[i + halfbit * 7] + in[i + halfbit * 9]) / 4;
        if (out[i] != sum) {
            fprintf(stderr, "verification failed: at %u, expected %u, got %u\n",
                    i,
                    sum, out[i]);
            okay = false;
        }
    }

    return okay;
}
