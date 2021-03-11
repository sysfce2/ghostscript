/* Copyright (C) 2001-2021 Artifex Software, Inc.
   All Rights Reserved.

   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied,
   modified or distributed except as expressly authorized under the terms
   of the license contained in the file LICENSE in this distribution.

   Refer to licensing information at http://www.artifex.com or contact
   Artifex Software, Inc.,  1305 Grant Avenue - Suite 200, Novato,
   CA 94945, U.S.A., +1(415)492-9861, for further information.
*/

/*
Functions to serialize a type 1 font so that it can then be
passed to FreeType via the FAPI FreeType bridge.
Started by Graham Asher, 9th August 2002.
*/
#include "stdpre.h"
#include "gzstate.h"
#include "wrfont.h"
#include "write_t2.h"
#include "gxfont.h"
#include "gxfont1.h"

/*
Public structures and functions in this file are prefixed with FF_ because they are part of
the FAPI FreeType implementation.
*/

static void
write_4_byte_int(unsigned char *a_output, long a_int)
{
    a_output[0] = (unsigned char)(a_int >> 24);
    a_output[1] = (unsigned char)(a_int >> 16);
    a_output[2] = (unsigned char)(a_int >> 8);
    a_output[3] = (unsigned char)(a_int & 0xFF);
}

static void
write_type2_int(gs_fapi_font * a_fapi_font, WRF_output * a_output, long a_int)
{
    if (a_int >= -107 && a_int <= 107)
        WRF_wbyte(a_fapi_font->memory, a_output, (unsigned char)(a_int + 139));
    else if (a_int >= -32768 && a_int <= 32767) {
        if (a_int >= 108 && a_int <= 1131)
            a_int += 63124;
        else if (a_int >= -1131 && a_int <= -108)
            a_int = -a_int + 64148;
        else
            WRF_wbyte(a_fapi_font->memory, a_output, 28);
        WRF_wbyte(a_fapi_font->memory, a_output, (unsigned char)(a_int >> 8));
        WRF_wbyte(a_fapi_font->memory, a_output, (unsigned char)(a_int & 0xFF));
    } else {
        unsigned char buffer[4];

        WRF_wbyte(a_fapi_font->memory, a_output, 29);
        write_4_byte_int(buffer, a_int);
        WRF_wtext(a_fapi_font->memory, a_output, buffer, 4);
    }
}

static void
write_type2_float(gs_fapi_font * a_fapi_font, WRF_output * a_output, float a_float)
{
    char buffer[32];
    const char *p = buffer;
    int high = true;
    char c = 0;

    gs_sprintf(buffer, "%f", a_float);
    WRF_wbyte(a_fapi_font->memory, a_output, 30);
    for (;;) {
        char n = 0;

        if (*p >= '0' && *p <= '9')
            n = (char)(*p - '0');
        else if (*p == '.')
            n = 0xA;
        else if (*p == 'e' || *p == 'E') {
            if (p[1] == '-') {
                p++;
                n = 0xC;
            } else
                n = 0xB;
        } else if (*p == '-')
            n = 0xE;
        else if (*p == 0)
            n = 0xF;
        if (high) {
            if (*p == 0)
                WRF_wbyte(a_fapi_font->memory, a_output, 0xFF);
            else
                c = (char)(n << 4);
        } else {
            c |= n;
            WRF_wbyte(a_fapi_font->memory, a_output, c);
        }

        if (*p == 0)
            break;

        high = !high;
        p++;
    }
}

static void
write_header(gs_fapi_font * a_fapi_font, WRF_output * a_output)
{
    WRF_wtext(a_fapi_font->memory, a_output, (const unsigned char *)"\x1\x0\x4\x1", 4);
}

static void
write_name_index(gs_fapi_font * a_fapi_font, WRF_output * a_output)
{
    /* Write a dummy name of 'x'. */
    WRF_wtext(a_fapi_font->memory, a_output, (const unsigned char *)"\x0\x1\x1\x1\x2" "x", 6);
}

static int
write_word_entry(gs_fapi_font * a_fapi_font, WRF_output * a_output,
                 int a_feature_id, int a_feature_count, bool a_two_byte_op,
                 int a_op, int a_divisor)
{
    int code = 0;

    if (a_feature_count > 0) {
        int i;

        for (i = 0; i < a_feature_count; i++) {
            /* Get the value and convert it from unsigned to signed. */
            short x;
            code = a_fapi_font->get_word(a_fapi_font, a_feature_id, i, (unsigned short *)&x);
            if (code < 0)
                return code;

            /* Divide by the divisor to bring it back to font units. */
            x = (short)(x / a_divisor);
            write_type2_int(a_fapi_font, a_output, x);
        }
        if (a_two_byte_op)
            WRF_wbyte(a_fapi_font->memory, a_output, 12);
        WRF_wbyte(a_fapi_font->memory, a_output, (unsigned char)a_op);
    }
    return code;
}

static int
write_delta_array_entry(gs_fapi_font * a_fapi_font, WRF_output * a_output,
                        int a_feature_id, bool a_two_byte_op, int a_op,
                        int a_divisor)
{
    int i, code;

    /* NOTE that the feature index (a_feature_id) must be preceded by the count index for this to work. */
    unsigned short count;

    code = a_fapi_font->get_word(a_fapi_font, a_feature_id - 1, 0, &count);

    if (code >= 0 && count > 0) {
        short prev_value = 0;

        for (i = 0; i < count; i++) {
            /* Get the value and convert it from unsigned to signed. */
            short value;
            code = a_fapi_font->get_word(a_fapi_font, a_feature_id, i, (unsigned short *)&value);
            if (code < 0)
                return code;

            /* Divide by the divisor to bring it back to font units. */
            value = (short)(value / a_divisor);
            write_type2_int(a_fapi_font, a_output, value - prev_value);
            prev_value = value;
        }
        if (a_two_byte_op)
            WRF_wbyte(a_fapi_font->memory, a_output, 12);
        WRF_wbyte(a_fapi_font->memory, a_output, (unsigned char)a_op);
    }
    return code;
}

static int
write_float_entry(gs_fapi_font * a_fapi_font, WRF_output * a_output,
                  int a_feature_id, int a_feature_count, bool a_two_byte_op,
                  int a_op)
{
    if (a_feature_count > 0) {
        int i, code;
        float x;

        for (i = 0; i < a_feature_count; i++) {
            code = a_fapi_font->get_float(a_fapi_font, a_feature_id, i, &x);
            if (code < 0)
                return code;

            write_type2_float(a_fapi_font, a_output, x);
        }
        if (a_two_byte_op)
            WRF_wbyte(a_fapi_font->memory, a_output, 12);
        WRF_wbyte(a_fapi_font->memory, a_output, (unsigned char)a_op);
    }
    return 0;
}

static int
write_font_dict_index(gs_fapi_font * a_fapi_font, WRF_output * a_output,
                      unsigned char **a_charset_offset_ptr,
                      unsigned char **a_charstrings_offset_ptr,
                      unsigned char **a_private_dict_length_ptr)
{
    unsigned char *data_start = 0;
    int code;

    WRF_wtext(a_fapi_font->memory, a_output, (const unsigned char *)"\x0\x1\x2\x0\x1\x0\x0", 7);     /* count = 1, offset size = 2, first offset = 1, last offset = 0 (to be filled in later). */
    if (a_output->m_pos)
        data_start = a_output->m_pos;
    code = write_word_entry(a_fapi_font, a_output, gs_fapi_font_feature_FontBBox, 4, false, 5, 1);
    if (code < 0)
        return code;

    code = write_float_entry(a_fapi_font, a_output, gs_fapi_font_feature_FontMatrix, 6, true, 7);
    if (code < 0)
        return code;

    write_type2_int(a_fapi_font, a_output, 0);       /* 0 = Standard Encoding. */
    WRF_wbyte(a_fapi_font->memory, a_output, 16);    /* 16 = opcode for 'encoding'. */
    *a_charset_offset_ptr = a_output->m_pos;
    WRF_wtext(a_fapi_font->memory, a_output, (const unsigned char *)"\x1d" "xxxx", 5);       /* placeholder for the offset to the charset, which will be a 5-byte integer. */
    WRF_wbyte(a_fapi_font->memory, a_output, 15);    /* opcode for 'charset' */
    *a_charstrings_offset_ptr = a_output->m_pos;
    WRF_wtext(a_fapi_font->memory, a_output, (const unsigned char *)"\x1d" "xxxx", 5);       /* placeholder for the offset to the Charstrings index, which will be a 5-byte integer. */
    WRF_wbyte(a_fapi_font->memory, a_output, 17);    /* opcode for 'Charstrings' */
    *a_private_dict_length_ptr = a_output->m_pos;
    WRF_wtext(a_fapi_font->memory, a_output, (const unsigned char *)"\x1d" "xxxx\x1d" "yyyy", 10);   /* placeholder for size and offset of Private dictionary, which will be 5-byte integers. */
    WRF_wbyte(a_fapi_font->memory, a_output, 18);    /* opcode for 'Private' */
    if (a_output->m_pos) {
        int last_offset = a_output->m_pos - data_start + 1;

        data_start[-2] = (unsigned char)(last_offset >> 8);
        data_start[-1] = (unsigned char)(last_offset & 0xFF);
    }
    return 0;
}

/**
Write the character set. Return the number of characters.
For the moment this is always 1. The number cannot be obtained
via the FAPI interface, and FreeType doesn't need to know anything more
than the fact that there is at least one character.
*/
static int
write_charset(gs_fapi_font * a_fapi_font, WRF_output * a_output, unsigned char *a_charset_offset_ptr)
{
    const int characters = 2; /* .notdef + one other */
    int i = 0;

    /* Write the offset to the start of the charset to the top dictionary. */
    if (a_output->m_pos) {
        write_4_byte_int(a_charset_offset_ptr + 1, a_output->m_count);
    }
    /*
       Write the charset. Write one less than the number of characters,
       because the first one is assumed to be .notdef. For the moment
       write all the others as .notdef (SID = 0) because we don't actually
       need the charset at the moment.
     */
    WRF_wbyte(a_fapi_font->memory, a_output, 0);     /* format = 0 */
    for (i = 1; i < characters; i++) {
        WRF_wbyte(a_fapi_font->memory, a_output, 0);
        WRF_wbyte(a_fapi_font->memory, a_output, 0);
    }

    return characters;
}

/**
Write a set of empty charstrings. The only reason for the existence of the charstrings index is to tell
FreeType how many glyphs there are.
*/
static void
write_charstrings_index(gs_fapi_font * a_fapi_font, WRF_output * a_output, int a_characters,
                        unsigned char *a_charstrings_offset_ptr)
{
    /* Write the offset to the charstrings index to the top dictionary. */
    if (a_output->m_pos) {
        write_4_byte_int(a_charstrings_offset_ptr + 1, a_output->m_count);
    }

    /* Write the index. */
    WRF_wbyte(a_fapi_font->memory, a_output, (unsigned char)(a_characters >> 8));
    WRF_wbyte(a_fapi_font->memory, a_output, (unsigned char)(a_characters & 0xFF));
    WRF_wbyte(a_fapi_font->memory, a_output, 1);     /* offset size = 1. */
    while (a_characters-- >= 0)
        WRF_wbyte(a_fapi_font->memory, a_output, 1); /* offset = 1 */
}

static int
write_gsubrs_index(gs_fapi_font * a_fapi_font, WRF_output * a_output)
{
    unsigned char *cur_offset = 0;
    unsigned char *data_start = 0;
    int i;
    unsigned short count;
    int code = a_fapi_font->get_word(a_fapi_font,
                                      gs_fapi_font_feature_GlobalSubrs_count,
                                      0, &count);

    if (code < 0)
        return code;

    WRF_wbyte(a_fapi_font->memory, a_output, (unsigned char)(count >> 8));
    WRF_wbyte(a_fapi_font->memory, a_output, (unsigned char)(count & 0xFF));

    if (count <= 0)
        return 0;

    WRF_wbyte(a_fapi_font->memory, a_output, 4);     /* offset size = 4 bytes */
    WRF_wtext(a_fapi_font->memory, a_output, (const unsigned char *)"\x0\x0\x0\x1", 4);      /* first offset = 1 */

    if (a_output->m_pos)
        cur_offset = a_output->m_pos;

    /* Write dummy bytes for the offsets at the end of each data item. */
    for (i = 0; i < count; i++)
        WRF_wtext(a_fapi_font->memory, a_output, (const unsigned char *)"xxxx", 4);

    if (a_output->m_pos)
        data_start = a_output->m_pos;

    for (i = 0; i < count; i++) {
        long buffer_size = a_output->m_limit - a_output->m_count < 0 ? 0 : a_output->m_limit - a_output->m_count;
        int length = a_fapi_font->get_gsubr(a_fapi_font, i, a_output->m_pos, buffer_size);

        if (length < 0)
            return length;

        if (a_output->m_pos)
            a_output->m_pos += length;

        a_output->m_count += length;
        if (cur_offset) {
            long pos = a_output->m_pos - data_start + 1;

            write_4_byte_int(cur_offset, pos);
            cur_offset += 4;
        }
    }
    return 0;
}

static int
write_subrs_index(gs_fapi_font * a_fapi_font, WRF_output * a_output)
{
    unsigned char *cur_offset = 0;
    unsigned char *data_start = 0;
    int i;
    unsigned short count;
    int code =
        a_fapi_font->get_word(a_fapi_font, gs_fapi_font_feature_Subrs_count,
                              0, &count);

    if (code < 0)
        return code;

    WRF_wbyte(a_fapi_font->memory, a_output, (unsigned char)(count >> 8));
    WRF_wbyte(a_fapi_font->memory, a_output, (unsigned char)(count & 0xFF));

    if (count <= 0)
        return 0;

    WRF_wbyte(a_fapi_font->memory, a_output, 4);     /* offset size = 4 bytes */
    WRF_wtext(a_fapi_font->memory, a_output, (const unsigned char *)"\x0\x0\x0\x1", 4);      /* first offset = 1 */

    if (a_output->m_pos)
        cur_offset = a_output->m_pos;

    /* Write dummy bytes for the offsets at the end of each data item. */
    for (i = 0; i < count; i++)
        WRF_wtext(a_fapi_font->memory, a_output, (const unsigned char *)"xxxx", 4);

    if (a_output->m_pos)
        data_start = a_output->m_pos;

    for (i = 0; i < count; i++) {
        long buffer_size = a_output->m_limit - a_output->m_count;
        int length = a_fapi_font->get_subr(a_fapi_font, i, a_output->m_pos, buffer_size);

        if (length < 0)
            return length;

        if (a_output->m_pos)
            a_output->m_pos += length;

        a_output->m_count += length;
        if (cur_offset) {
            long pos = a_output->m_pos - data_start + 1;

            write_4_byte_int(cur_offset, pos);
            cur_offset += 4;
        }
    }
    return 0;
}

static int
write_private_dict(gs_fapi_font * a_fapi_font, WRF_output * a_output,
                   unsigned char *a_private_dict_length_ptr)
{
    int code, initial = a_output->m_count;
    unsigned short count;
    /* Write the offset to the start of the private dictionary to the top dictionary. */
    unsigned char *start = a_output->m_pos;
    unsigned long lval;
    gs_font_type1 *t1 = (gs_font_type1 *) a_fapi_font->client_font_data;

    if (a_output->m_pos)
        write_4_byte_int(a_private_dict_length_ptr + 6, a_output->m_count);

    code = write_word_entry(a_fapi_font, a_output, gs_fapi_font_feature_BlueFuzz, 1,
                     true, 11, 16);
    if (code < 0)
        return code;
    code = a_fapi_font->get_long(a_fapi_font, gs_fapi_font_feature_BlueScale, 0, &lval);
    if (code < 0)
        return code;

    write_type2_float(a_fapi_font, a_output, (float)((double)lval/65536.0));

    WRF_wbyte(a_fapi_font->memory, a_output, 12);
    WRF_wbyte(a_fapi_font->memory, a_output, 9);

    code = write_word_entry(a_fapi_font, a_output, gs_fapi_font_feature_BlueShift, 1,
                     true, 10, 16);
    if (code < 0)
        return code;

    code = write_delta_array_entry(a_fapi_font, a_output,
                            gs_fapi_font_feature_BlueValues, false, 6, 16);
    if (code < 0)
        return code;

    code = write_delta_array_entry(a_fapi_font, a_output,
                            gs_fapi_font_feature_OtherBlues, false, 7, 16);
    if (code < 0)
        return code;

    code = write_delta_array_entry(a_fapi_font, a_output,
                            gs_fapi_font_feature_FamilyBlues, false, 8, 16);
    if (code < 0)
        return code;

    code = write_delta_array_entry(a_fapi_font, a_output,
                            gs_fapi_font_feature_FamilyOtherBlues, false, 9,
                            16);
    if (code < 0)
        return code;

    code = write_word_entry(a_fapi_font, a_output, gs_fapi_font_feature_ForceBold, 1,
                     true, 14, 1);
    if (code < 0)
        return code;

    code = write_word_entry(a_fapi_font, a_output, gs_fapi_font_feature_StdHW, 1,
                     false, 10, 16);
    if (code < 0)
        return code;

    code = write_word_entry(a_fapi_font, a_output, gs_fapi_font_feature_StdVW, 1,
                     false, 11, 16);
    if (code < 0)
        return code;

    code = write_delta_array_entry(a_fapi_font, a_output,
                            gs_fapi_font_feature_StemSnapH, true, 12, 16);
    if (code < 0)
        return code;

    code = write_delta_array_entry(a_fapi_font, a_output,
                            gs_fapi_font_feature_StemSnapV, true, 13, 16);
    if (code < 0)
        return code;

    /*
       Write the default width and the nominal width. These values are not available via
       the FAPI interface so we have to get a pointer to the Type 1 font structure and
       extract them directly.
     */
    write_type2_float(a_fapi_font, a_output, fixed2float(t1->data.defaultWidthX));
    WRF_wbyte(a_fapi_font->memory, a_output, 20);

    write_type2_float(a_fapi_font, a_output, fixed2float(t1->data.nominalWidthX));
    WRF_wbyte(a_fapi_font->memory, a_output, 21);

    code =
        a_fapi_font->get_word(a_fapi_font, gs_fapi_font_feature_Subrs_count,
                              0, &count);
    if (code < 0)
        return code;

    /* If we have local /Subrs we need to make a new dict ( see calling routine) and
     * we also need to add an entry to the Provate dict with an offset to the /Subrs
     * dict. This is complicated by the fact that the offset includes the data for
     * the offset (its contained in the Private dict) and the size of the data depends
     * on its value (because of number representation).
     */
    if (count) {
        int n = 1, n1;

        do {
            n1 = a_output->m_count - initial + 1 + n;   /* one for the operator, plus the size needed for the representation */
            switch (n) {
                case 1:
                    if (n1 >= -107 && n1 <= 107) {
                        write_type2_int(a_fapi_font, a_output, n1);
                        n = 5;
                    }
                    break;
                case 2:
                    if ((n1 >= 108 && n1 <= 1131)
                        || (n1 >= -1131 && n1 <= -108)) {
                        write_type2_int(a_fapi_font, a_output, n1);
                        n = 5;
                    }
                    break;
                case 3:
                    if (n1 >= -32768 && n1 <= 32767) {
                        write_type2_int(a_fapi_font, a_output, n1);
                        n = 5;
                    }
                    break;
                case 4:
                    break;
                case 5:
                    write_type2_int(a_fapi_font, a_output, n1);
                    break;
            }
            n++;
        }
        while (n < 5);

        WRF_wbyte(a_fapi_font->memory, a_output, 19);
    }

    /* Write the length in bytes of the private dictionary to the top dictionary. */
    if (a_output->m_pos)
        write_4_byte_int(a_private_dict_length_ptr + 1,
                         a_output->m_pos - start);
    return 0;
}

/**
Write a Type 2 font in binary format and return its length in bytes.
If a_buffer_size is less than the total length, only a_buffer_size bytes are written, but the total
length is returned correctly.
*/
long
gs_fapi_serialize_type2_font(gs_fapi_font * a_fapi_font,
                             unsigned char *a_buffer, long a_buffer_size)
{
    unsigned char *charset_offset_ptr = NULL;
    unsigned char *charstrings_offset_ptr = NULL;
    unsigned char *private_dict_length_ptr = NULL;
    int characters = 0;
    int code;

    WRF_output output;

    WRF_init(&output, a_buffer, a_buffer_size);

    write_header(a_fapi_font, &output);
    write_name_index(a_fapi_font, &output);
    code = write_font_dict_index(a_fapi_font, &output, &charset_offset_ptr,
                          &charstrings_offset_ptr, &private_dict_length_ptr);
    if (code < 0)
        return (long)code;

    /* Write an empty string index. */
    WRF_wtext(a_fapi_font->memory, &output, (const unsigned char *)"\x0\x0", 2);

    write_gsubrs_index(a_fapi_font, &output);
    code = characters = write_charset(a_fapi_font, &output, charset_offset_ptr);
    if (code < 0)
        return (long)code;

    write_charstrings_index(a_fapi_font, &output, characters, charstrings_offset_ptr);

    code = write_private_dict(a_fapi_font, &output, private_dict_length_ptr);
    if (code < 0)
        return (long)code;

    code = write_subrs_index(a_fapi_font, &output);

    if (code < 0)
        return (long)code;

    return output.m_count;
}
