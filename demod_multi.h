#ifndef DEMOD_MULTI_H
#define DEMOD_MULTI_H

struct mag_buf;

void demodulateMulti(struct mag_buf *mag);
int demodulateMultiInit(demodulator_context_t *ctx);
void demodulateMultiFree(void);
void demodulateMultiHelp(void);

#endif
