// model_fade.glsl — DOOM-0631: a Thing's model fades into its sprite instead of popping.
// The one copy of the test; mesh.frag and pathtrace.comp include it.
//
// In the fade band r_mesh.c draws both the model and the sprite. Each vertex's flags carry
// the model's share of the pixels in sixteenths, and the sprite's carry FLAG_FADE_INV too.
// An ordered 4x4 dither gives the model that share of every 4x4 block and the sprite
// exactly the rest, so the two never both fill a pixel and never both leave one empty
// where both cover it.
#ifndef MODEL_FADE_GLSL
#define MODEL_FADE_GLSL

const int FLAG_FADE_SHIFT = 12;        // matches RB_MESH_FADE_SHIFT in r_mesh.h
const int FLAG_FADE_MASK  = 0xF000;    // matches RB_MESH_FADE_MASK
const int FLAG_FADE_INV   = 0x10000;   // matches RB_MESH_FADE_INV

// Whether the pixel px shows a surface with these vertex flags. Always true outside a fade.
bool modelFadeKeeps(int flags, ivec2 px)
{
    const int bayer[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    int  share = (flags & FLAG_FADE_MASK) >> FLAG_FADE_SHIFT;
    if (share == 0)
        return true;
    bool model = bayer[(px.y & 3) * 4 + (px.x & 3)] < share;
    return ((flags & FLAG_FADE_INV) != 0) ? !model : model;
}

#endif
