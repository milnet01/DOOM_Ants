#ifndef RB_IMAGE_H
#define RB_IMAGE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { unsigned char* pixels; int w, h; } rb_image_t;   /* always RGBA8 */
int  rb_image_load(const char* path, rb_image_t* out);            /* 1 ok, 0 fail (no crash) */
/* Size from the file header alone, no decode: 1 ok, 0 fail. Lets a caller budget
   before it pays for the pixels (DOOM-0410). */
int  rb_image_info(const char* path, int* w, int* h);
/* Box-filter in place so the longest edge is <= max_edge. Returns what it did, so
   the caller can log it: RB_DS_NONE (already fits), RB_DS_DONE (shrunk), or
   RB_DS_OOM (could not allocate; img untouched and still OVER the limit). */
enum { RB_DS_NONE = 0, RB_DS_DONE = 1, RB_DS_OOM = -1 };
int  rb_image_downscale_max(rb_image_t* img, int max_edge);
/* The size rb_image_downscale_max would produce, without doing it. */
void rb_image_fit_max(int w, int h, int max_edge, int* nw, int* nh);
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
