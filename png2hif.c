#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <spng.h>
#include <stdbool.h>
#include <lzma.h>

spng_ctx *ctx;
unsigned char *out;
char *pngbuf = NULL;

enum {
  ERROR,
  NOT_PNG,
  LIBSPNG_ERROR,
  LIBLZMA_ERROR,
};

void pth_exit(char *str, unsigned int code) {
  spng_ctx_free(ctx);
  free(out);
  free(pngbuf);
  perror(str);
  exit(code);
}

int main(int argc, char *argv[argc + 1]) {
  char *infile = argv[1];
  char *outfile = argv[2];

  FILE *png = fopen(infile, "rb");
  if (!png) pth_exit("fopen()", ERROR);

  int r = 0;

  fseek(png, 0, SEEK_END);

  long siz_pngbuf = ftell(png);
  rewind(png);

  pngbuf = malloc(siz_pngbuf);
  if (pngbuf == NULL) pth_exit("malloc()", ERROR);

  if (fread(pngbuf, siz_pngbuf, 1, png) != 1) pth_exit("fread()", ERROR);

  ctx = spng_ctx_new(0);
  if (ctx == NULL) pth_exit("spng_ctx_new()", LIBSPNG_ERROR);

  r = spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);
  if (r) {
    puts(spng_strerror(r));
    pth_exit("spng_set_src_action()", LIBSPNG_ERROR);
  }

  r = spng_set_png_buffer(ctx, pngbuf, siz_pngbuf);
  if (r) {
    puts(spng_strerror(r));
    pth_exit("spng_set_png_buffer()", LIBSPNG_ERROR);
  }

  struct spng_ihdr ihdr;
  r = spng_get_ihdr(ctx, &ihdr);
  if (r) {
    puts(spng_strerror(r));
    pth_exit("spng_get_ihdr()", LIBSPNG_ERROR);
  }

  size_t out_size;
  r = spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &out_size);
  if (r) {
    puts(spng_strerror(r));
    pth_exit("spng_decoded_image_size()", LIBSPNG_ERROR);
  }

  uint8_t *png_buf = malloc(out_size);

  spng_decode_image(ctx, png_buf, out_size, SPNG_FMT_RGBA8, SPNG_DECODE_USE_TRNS);

  union {
    uint32_t dimensions;
    uint8_t bytes[4];
  } dw, dh;

  dw.dimensions = ihdr.width;
  dh.dimensions = ihdr.height;

  uint8_t header[12] = {
    0xDE, 0xAD, 0xFA, 0xCE,
    dw.bytes[3], dw.bytes[2], dw.bytes[1], dw.bytes[0],
    dh.bytes[3], dh.bytes[2], dh.bytes[1], dh.bytes[0],
  };

  size_t hif_size = (size_t)(out_size / 4 * 3);
  uint8_t *hif_data_buf = malloc(hif_size);
  uint8_t *hif_zbuf = malloc(hif_size);
  uint8_t *hif_buf = malloc(hif_size + 12);

  memcpy(hif_buf, header, 12);

  for (size_t i = 0, j = 12; i < out_size && j < hif_size; i++) {
    if (i % 4 != 3) {
      hif_data_buf[j] = png_buf[i];
      j++;
    }
  }

  lzma_stream defstream = LZMA_STREAM_INIT;
  uint32_t preset = 9 | 1;
  defstream.avail_in = (size_t) hif_size + 1;
  defstream.next_in = (uint8_t *) hif_data_buf;
  defstream.avail_out = (size_t) hif_size + 1;
  defstream.next_out = (uint8_t *) hif_zbuf;

  lzma_ret ret = lzma_easy_encoder(&defstream, preset, LZMA_CHECK_CRC64);
  if (ret != LZMA_OK) pth_exit("lzma_easy_encoder()", LIBLZMA_ERROR);

  lzma_action action = LZMA_FINISH;
  ret = lzma_code(&defstream, action);
  if (ret != LZMA_OK && ret != LZMA_STREAM_END)
    pth_exit("lzma_code()", LIBLZMA_ERROR);

  memcpy(hif_buf + 12, hif_zbuf, defstream.total_out);

  FILE *hif = fopen(outfile, "wb");
  if (hif == NULL) pth_exit("fopen()", ERROR);

  for (size_t i = 0; i < 255; i++) {
    printf("%" PRIu8 " ", hif_buf[i]);
  }

  if (fwrite(hif_buf, 1, defstream.total_out + 12, hif) != (defstream.total_out + 12))
    pth_exit("fwrite()", ERROR);

  spng_ctx_free(ctx);
  free(out);
  free(pngbuf);
  free(png_buf);
  free(hif_buf);

  return EXIT_SUCCESS;
}
