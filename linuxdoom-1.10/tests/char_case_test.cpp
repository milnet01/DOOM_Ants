// char_case_test.cpp — DOOM-0403: D_ToUpper maps a BYTE, never a signed char.
//
// Every site that turns a character into a HUD-font glyph writes
// `toupper(c) - HU_FONTSTART` and range-checks the result. `char` is signed on
// the platforms this builds for, so a byte at 0x80 or above arrives at toupper
// as a negative int -- and the standard defines the function only for a value
// representable as unsigned char, or EOF. WAD lump names, save descriptions,
// chat lines and a PWAD's finale text are all bytes this engine does not choose.
//
// The two C runtimes genuinely disagree. Measured with one source file compiled
// twice: for 0xE9, glibc returns 233 and the mingw CRT under Wine returns -23.
//
// Removing the cast from D_ToUpper turns this red on both, and the gap between
// them is the point. On glibc it fails 2 checks, both at 0xFF, which is the one
// byte that sign-extends to -1 and collides with EOF; every other high byte
// happens to come back right, which is how the defect survived here unnoticed.
// Built with the mingw compiler and run under Wine, the same mutation fails 256
// checks -- every byte from 0x80 up. The platform this project also ships for is
// the one where it is not a near miss.
#include <cstdio>
#include <cctype>

#include "../doomtype.h"
#include "check_util.h"

int main()
{
    int b;

    // --- The contract: for every byte, the answer is the unsigned mapping. ---
    for (b = 0; b < 256; b++)
    {
        char msg[64];
        int want = std::toupper((unsigned char) b);
        sprintf(msg, "byte 0x%02X maps to its unsigned-char case", b);
        check(D_ToUpper(b) == want, msg);
    }

    // --- A signed char and its byte are the same character. ---
    // This is the pair the call sites actually form: they pass *p, a char.
    for (b = 128; b < 256; b++)
    {
        char msg[64];
        char signed_form = (char) b;
        sprintf(msg, "char 0x%02X and byte 0x%02X agree", (unsigned char) signed_form, b);
        check(D_ToUpper(signed_form) == D_ToUpper(b), msg);
    }

    // --- The result is always a byte, never negative. ---
    // This is what the call sites depend on: a negative return underflows
    // `- HU_FONTSTART` and is rejected by the wrong half of the range check.
    for (b = 0; b < 256; b++)
    {
        char msg[64];
        int r = D_ToUpper((char) b);
        sprintf(msg, "D_ToUpper of char 0x%02X is in 0..255", b);
        check(r >= 0 && r <= 255, msg);
    }

    // --- The ASCII mapping the font actually uses is unchanged. ---
    check(D_ToUpper('a') == 'A', "lower-case ASCII still uppercases");
    check(D_ToUpper('Z') == 'Z', "upper-case ASCII is left alone");
    check(D_ToUpper('!') == '!', "HU_FONTSTART is left alone");
    check(D_ToUpper('_') == '_', "HU_FONTEND is left alone");
    check(D_ToUpper(' ') == ' ', "space is left alone");
    check(D_ToUpper('7') == '7', "a digit is left alone");

    return check_summary("char_case_test");
}
