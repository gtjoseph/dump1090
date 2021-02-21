#include <stdlib.h>
#include <stdio.h>
#include <math.h>

void STARCH_BENCHMARK(boxcar_u16) (void)
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

    STARCH_BENCHMARK_RUN( boxcar_u16, in, len, 16, out );

 done:
    STARCH_BENCHMARK_FREE(in);
    STARCH_BENCHMARK_FREE(out);
}

bool STARCH_BENCHMARK_VERIFY(boxcar_u16) (const uint16_t *in, unsigned len, unsigned window, uint16_t *out)
{
    bool okay = true;

    for (unsigned i = 0; i <= len - window; ++i) {
        uint32_t sum = 0;
        for (unsigned j = 0; j < window; ++j) {
            sum = sum + in[i + j];
        }

        sum = sum / window;
        if (out[i] != sum) {
            fprintf(stderr, "verification failed: window %u..%u expected %u, got %u\n",
                    i, i + window - 1,
                    sum, out[i]);
            okay = false;
        }
    }

    return okay;
}
