/*
 * Given a buffer of uint16_t Q16 magnitude values
 * generate a preamble correlation score for a preamble starting at each offset
 * in turn, where the correlation score is
 *
 *   (in[0] + in[2*N] + in[7*N] + in[9*N]) / 4
 *
 * where N is the number of samples per halfbit (i.e. Fs / 2e6)
 */
void STARCH_IMPL(preamble_u16, u32_single) (const uint16_t *in, unsigned len, unsigned halfbit, uint16_t *out)
{
    if (len < 9 * halfbit)
        return; /* not enough data for a complete window */

    const uint16_t * restrict in_align = STARCH_ALIGNED(in);
    uint16_t * restrict out_align = STARCH_ALIGNED(out);
    const unsigned N2 = halfbit * 2;
    const unsigned N7 = halfbit * 7;
    const unsigned N9 = halfbit * 9;

    unsigned remaining = len - N9;
    while (remaining--) {
        out_align[0] = ((uint32_t)in_align[0] + in_align[N2] + in_align[N7] + in_align[N9]) / 4;

        in_align += 1;
        out_align += 1;
    }
}

void STARCH_IMPL(preamble_u16, u32_separate) (const uint16_t *in, unsigned len, unsigned halfbit, uint16_t *out)
{
    if (len < 9 * halfbit)
        return; /* not enough data for a complete window */

    uint16_t * restrict out_align = STARCH_ALIGNED(out);

    const uint16_t * in0 = STARCH_ALIGNED(in);
    const uint16_t * in2 = in0 + halfbit * 2;
    const uint16_t * in7 = in0 + halfbit * 7;
    const uint16_t * in9 = in0 + halfbit * 9;

    unsigned remaining = len - halfbit * 9;
    while (remaining--) {
        out_align[0] = ((uint32_t)in0[0] + in2[0] + in7[0] + in9[0]) / 4;

        in0 += 1;
        in2 += 1;
        in7 += 1;
        in9 += 1;
        out_align += 1;
    }
}
