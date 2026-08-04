#ifndef RB_IMAGE_H
#define RB_IMAGE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { unsigned char* pixels; int w, h; } rb_image_t;   /* always RGBA8 */
int  rb_image_load(const char* path, rb_image_t* out);            /* 1 ok, 0 fail (no crash) */
void rb_image_downscale_max(rb_image_t* img, int max_edge);       /* box-filter in place */
void rb_image_free(rb_image_t* img);

/* DOOM-0294: next free "dev-shots/shot-NNNN.png", creating dev-shots/ if needed.
   Shared so every tier's -devshot writes to one place under one naming scheme --
   a capture harness that has to know which renderer produced the file is a
   harness that will read a stale one. Returns 1 on success, 0 if 9999 are taken. */
int  rb_devshot_path(char* out, int outsz);
#ifdef __cplusplus
}
#endif
#endif
