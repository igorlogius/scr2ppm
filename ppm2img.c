/**
 * description: converts a binary PPM (P6) from stdin to PNG or JPEG on stdout
 * usage: ppm2img png|jpg [quality]
 * repo: https://github.com/igorlogius/scr2ppm
 **/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static void
writeOut(void *ctx, void *data, int size){
    fwrite(data, 1, (size_t)size, (FILE *)ctx);
}

int
main(int argc, char *argv[]){
    int w, h, maxv, ok;
    int quality = 90;

    if (argc < 2 || (strcmp(argv[1], "png") != 0 && strcmp(argv[1], "jpg") != 0)) {
        fprintf(stderr, "usage: %s png|jpg [quality]\n", argv[0]);
        return 1;
    }
    if (argc > 2) { quality = atoi(argv[2]); }
    if (quality < 1 || quality > 100) { quality = 90; }

    if (scanf("P6 %d %d %d", &w, &h, &maxv) != 3 || w <= 0 || h <= 0 || maxv != 255) {
        fprintf(stderr, "ppm2img: input is not a valid 8-bit P6 PPM\n");
        return 1;
    }
    getchar(); /* the single whitespace after the header */

    size_t len = (size_t)w * (size_t)h * 3;
    unsigned char *px = malloc(len);
    if (px == NULL || fread(px, 1, len, stdin) != len) {
        fprintf(stderr, "ppm2img: failed to read pixel data\n");
        return 1;
    }

    if (strcmp(argv[1], "png") == 0) {
        ok = stbi_write_png_to_func(writeOut, stdout, w, h, 3, px, w * 3);
    } else {
        ok = stbi_write_jpg_to_func(writeOut, stdout, w, h, 3, px, quality);
    }
    free(px);
    if (!ok) { fprintf(stderr, "ppm2img: encoding failed\n"); return 1; }
    return 0;
}
