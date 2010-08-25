#include "canvas_base.h"

typedef struct SpiceGlzDecoderWindow SpiceGlzDecoderWindow;

SpiceGlzDecoderWindow *glz_decoder_window_new(void);
void glz_decoder_window_destroy(SpiceGlzDecoderWindow *w);

SpiceGlzDecoder *glz_decoder_new(SpiceGlzDecoderWindow *w);
void glz_decoder_destroy(SpiceGlzDecoder *d);
