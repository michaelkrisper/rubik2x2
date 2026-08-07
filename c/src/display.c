#include "display.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Face-colour RGB, close to a real cube. This puzzle uses purple where a
 * standard cube has orange. Indexed by colour id: r w b y p g. */
static const uint8_t palette[CUBE_COLORS][3] = {
    {196, 30, 58}, {245, 245, 245}, {0, 81, 186},
    {255, 213, 0}, {137, 66, 175}, {0, 158, 96},
};

enum { CELL_WIDTH = 4, FACE_WIDTH = CELL_WIDTH * 2 };

static const char gap[] = "  ";
static const char reset[] = "\x1b[0m";
static const char dim[] = "\x1b[2m";

/* The side faces, left to right. */
static const struct {
    const char *name;
    int side;
} band[] = {
    {"LEFT", CUBE_LEFT}, {"FRONT", CUBE_FRONT}, {"RIGHT", CUBE_RIGHT}, {"BACK", CUBE_BACK},
};

display_color_t display_detect(FILE *stream)
{
    if (getenv("NO_COLOR"))
        return DISPLAY_PLAIN;
    const int descriptor = fileno(stream);
    if ((descriptor < 0 || !isatty(descriptor)) && !getenv("FORCE_COLOR"))
        return DISPLAY_PLAIN;

    const char *const term = getenv("TERM");
    if (term && strcmp(term, "dumb") == 0)
        return DISPLAY_PLAIN;
    const char *const colorterm = getenv("COLORTERM");
    if (colorterm && (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0))
        return DISPLAY_TRUECOLOR;
    return DISPLAY_ANSI256;
}

/* Nearest index in the xterm 6x6x6 colour cube. */
static int ansi256(const uint8_t rgb[static 3])
{
    return 16 + 36 * (rgb[0] * 5 / 255) + 6 * (rgb[1] * 5 / 255) + (rgb[2] * 5 / 255);
}

static void paint(FILE *stream, uint8_t color_id, display_color_t color)
{
    if (color == DISPLAY_PLAIN) {
        const char letter = (char)(cube_color_chars[color_id] - ('a' - 'A'));
        fprintf(stream, " %c  ", letter);
        return;
    }
    const uint8_t *const rgb = palette[color_id];
    if (color == DISPLAY_TRUECOLOR)
        fprintf(stream, "\x1b[48;2;%u;%u;%um", rgb[0], rgb[1], rgb[2]);
    else
        fprintf(stream, "\x1b[48;5;%dm", ansi256(rgb));
    fprintf(stream, "%*s%s", CELL_WIDTH, "", reset);
}

/* Terminal cells are about twice as tall as they are wide, so colour blocks get
 * two rows to come out square; letters stay on one, where squareness would only
 * cost readability. */
static int cell_height(display_color_t color)
{
    return color == DISPLAY_PLAIN ? 1 : 2;
}

static void face_row(FILE *stream, cube_t state, int side, int row, display_color_t color)
{
    const int base = side * 4;
    /* BACK is seen from behind, so its rows and columns both reverse. */
    const int first = side == CUBE_BACK ? (row == 0 ? 3 : 1) : (row == 0 ? 0 : 2);
    const int step = side == CUBE_BACK ? -1 : 1;
    paint(stream, state.facelet[base + first], color);
    paint(stream, state.facelet[base + first + step], color);
}

static void label(FILE *stream, const char *text, display_color_t color)
{
    const int length = (int)strlen(text);
    const int left = (FACE_WIDTH - length) / 2;
    if (color != DISPLAY_PLAIN)
        fputs(dim, stream);
    fprintf(stream, "%*s%s%*s", left, "", text, FACE_WIDTH - length - left, "");
    if (color != DISPLAY_PLAIN)
        fputs(reset, stream);
}

void display_render(FILE *stream, cube_t state, display_color_t color)
{
    const int height = cell_height(color);
    const int indent = FACE_WIDTH + (int)strlen(gap);

    fprintf(stream, "%*s", indent, "");
    label(stream, "UP", color);
    fputc('\n', stream);
    for (int row = 0; row < 2; row++)
        for (int repeat = 0; repeat < height; repeat++) {
            fprintf(stream, "%*s", indent, "");
            face_row(stream, state, CUBE_UP, row, color);
            fputc('\n', stream);
        }

    fputc('\n', stream);
    for (size_t i = 0; i < sizeof band / sizeof *band; i++) {
        if (i)
            fputs(gap, stream);
        label(stream, band[i].name, color);
    }
    fputc('\n', stream);
    for (int row = 0; row < 2; row++)
        for (int repeat = 0; repeat < height; repeat++) {
            for (size_t i = 0; i < sizeof band / sizeof *band; i++) {
                if (i)
                    fputs(gap, stream);
                face_row(stream, state, band[i].side, row, color);
            }
            fputc('\n', stream);
        }

    fputc('\n', stream);
    fprintf(stream, "%*s", indent, "");
    label(stream, "DOWN", color);
    fputc('\n', stream);
    for (int row = 0; row < 2; row++)
        for (int repeat = 0; repeat < height; repeat++) {
            fprintf(stream, "%*s", indent, "");
            face_row(stream, state, CUBE_DOWN, row, color);
            fputc('\n', stream);
        }
}
