/* Drawing a cube for a terminal.
 *
 * Deliberately separate from cube.h: the model is 24 bytes and knows nothing
 * about how anyone looks at it.
 *
 * The picture is the standard unfolded net -- UP above FRONT, the four side
 * faces in a band, DOWN below -- so it matches the order a state string is
 * written in. BACK is drawn as seen from behind, which is what makes the band
 * read as one continuous strip around the cube.
 */
#ifndef RUBIK_DISPLAY_H
#define RUBIK_DISPLAY_H

#include <stdio.h>

#include "cube.h"

typedef enum {
    DISPLAY_PLAIN,      /* colour letters, one row per facelet */
    DISPLAY_ANSI256,
    DISPLAY_TRUECOLOR,
} display_color_t;

/* What the terminal on `stream` can actually take, honouring NO_COLOR,
 * FORCE_COLOR and whether output is a terminal at all. */
display_color_t display_detect(FILE *stream);

void display_render(FILE *stream, cube_t state, display_color_t color);

#endif /* RUBIK_DISPLAY_H */
