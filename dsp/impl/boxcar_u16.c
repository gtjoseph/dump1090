/*
 * Given a buffer of uint16_t Q16 magnitude values
 * compute an equally-weighted moving average of N samples
 * (boxcar average).
 */
void STARCH_IMPL(boxcar_u16, u32) (const uint16_t *in, unsigned len, unsigned window, uint16_t *out)
{
    if (len < window)
        return; /* not enough data for a complete window */

    /* process first complete window */
    const uint16_t *tail = STARCH_ALIGNED(in);
    const uint16_t *head = tail;
    uint16_t *out_align = STARCH_ALIGNED(out);
    const uint32_t scale = 65536 / window; /* avoid a divide on each sample */

    uint32_t running_sum = 0;
    for (unsigned n = 0; n < window; ++n) {
        /* running_sum := sum(tail[0] .. head[-1]) */

        running_sum = running_sum + head[0];
        /* running_sum := sum(tail[0] .. head[0]) */

        head += 1;
        /* running_sum := sum(tail[0] .. head[-1]) */
    }

    out_align[0] = (uint16_t) (running_sum * scale / 65536);
    out_align += 1;

    /* for the rest of the input, do incremental updates for samples entering/exiting the window */
    unsigned remaining = len - window;
    while (remaining--) {
        /* running_sum := sum(tail[0] .. head[-1]) */

        running_sum = running_sum + head[0] - tail[0];
        /* running_sum := sum(tail[1] .. head[0]) */

        head += 1;
        tail += 1;
        /* running_sum := sum(tail[0] .. head[-1]) */

        out_align[0] = (uint16_t) (running_sum * scale / 65536);
        out_align += 1;
    }
}
