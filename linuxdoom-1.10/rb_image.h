#ifndef RB_IMAGE_H
#define RB_IMAGE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { unsigned char* pixels; int w, h; } rb_image_t;   /* always RGBA8 */
int  rb_image_load(const char* path, rb_image_t* out);            /* 1 ok, 0 fail (no crash) */
void rb_image_downscale_max(rb_image_t* img, int max_edge);       /* box-filter in place */
void rb_image_free(rb_image_t* img);
#ifdef __cplusplus
}
#endif
#endif
