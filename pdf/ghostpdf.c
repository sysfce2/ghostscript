/* Copyright (C) 2018-2021 Artifex Software, Inc.
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

/* Top level PDF access routines */
#include "ghostpdf.h"
#include "plmain.h"
#include "pdf_types.h"
#include "pdf_dict.h"
#include "pdf_array.h"
#include "pdf_int.h"
#include "pdf_misc.h"
#include "pdf_stack.h"
#include "pdf_file.h"
#include "pdf_loop_detect.h"
#include "pdf_trans.h"
#include "pdf_gstate.h"
#include "stream.h"
#include "strmio.h"
#include "pdf_colour.h"
#include "pdf_font.h"
#include "pdf_text.h"
#include "pdf_page.h"
#include "pdf_check.h"
#include "pdf_optcontent.h"
#include "pdf_sec.h"
#include "pdf_doc.h"
#include "pdf_repair.h"
#include "pdf_xref.h"
#include "pdf_device.h"

/*
 * Convenience routine to check if a given string exists in a dictionary
 * verify its contents and print it in a particular fashion to stdout. This
 * is used to display information about the PDF in response to -dPDFINFO
 */
static int dump_info_string(pdf_context *ctx, pdf_dict *source_dict, const char *Key)
{
    int code;
    pdf_string *s = NULL;
    char *Cstr;

    code = pdfi_dict_knownget_type(ctx, source_dict, Key, PDF_STRING, (pdf_obj **)&s);
    if (code > 0) {
        Cstr = (char *)gs_alloc_bytes(ctx->memory, s->length + 1, "Working memory for string dumping");
        if (Cstr) {
            memcpy(Cstr, s->data, s->length);
            Cstr[s->length] = 0x00;
            dmprintf2(ctx->memory, "%s: %s\n", Key, Cstr);
            gs_free_object(ctx->memory, Cstr, "Working memory for string dumping");
        }
        code = 0;
    }
    pdfi_countdown(s);

    return code;
}

static int pdfi_output_metadata(pdf_context *ctx)
{
    int code = 0;

    if (ctx->num_pages > 1)
        dmprintf2(ctx->memory, "\n        %s has %"PRIi64" pages\n\n", ctx->filename, ctx->num_pages);
    else
        dmprintf2(ctx->memory, "\n        %s has %"PRIi64" page.\n\n", ctx->filename, ctx->num_pages);

    if (ctx->Info != NULL) {
        pdf_name *n = NULL;
        char *Cstr;

        code = dump_info_string(ctx, ctx->Info, "Title");
        if (code < 0) {
            if (ctx->args.pdfstoponerror)
                return code;
        }

        code = dump_info_string(ctx, ctx->Info, "Author");
        if (code < 0) {
            if (ctx->args.pdfstoponerror)
                return code;
        }

        code = dump_info_string(ctx, ctx->Info, "Subject");
        if (code < 0) {
            if (ctx->args.pdfstoponerror)
                return code;
        }

        code = dump_info_string(ctx, ctx->Info, "Keywords");
        if (code < 0) {
            if (ctx->args.pdfstoponerror)
                return code;
        }

        code = dump_info_string(ctx, ctx->Info, "Creator");
        if (code < 0) {
            if (ctx->args.pdfstoponerror)
                return code;
        }

        code = dump_info_string(ctx, ctx->Info, "Producer");
        if (code < 0) {
            if (ctx->args.pdfstoponerror)
                return code;
        }

        code = dump_info_string(ctx, ctx->Info, "CreationDate");
        if (code < 0) {
            if (ctx->args.pdfstoponerror)
                return code;
        }

        code = dump_info_string(ctx, ctx->Info, "ModDate");
        if (code < 0) {
            if (ctx->args.pdfstoponerror)
                return code;
        }


        code = pdfi_dict_knownget_type(ctx, ctx->Info, "Trapped", PDF_NAME, (pdf_obj **)&n);
        if (code > 0) {
            Cstr = (char *)gs_alloc_bytes(ctx->memory, n->length + 1, "Working memory for string dumping");
            if (Cstr) {
                memcpy(Cstr, n->data, n->length);
                Cstr[n->length] = 0x00;
                dmprintf1(ctx->memory, "Trapped: %s\n\n", Cstr);
                gs_free_object(ctx->memory, Cstr, "Working memory for string dumping");
            }
            code = 0;
        }
        pdfi_countdown(n);
        n = NULL;
    }
    return code;
}

/*
 * Convenience routine to check if a given *Box exists in a page dictionary
 * verify its contents and print it in a particular fashion to stdout. This
 * is used to display information about the PDF in response to -dPDFINFO
 */
static int pdfi_dump_box(pdf_context *ctx, pdf_dict *page_dict, const char *Key)
{
    int code, i;
    pdf_array *a = NULL;
    double f;

    code = pdfi_dict_knownget_type(ctx, page_dict, Key, PDF_ARRAY, (pdf_obj **)&a);
    if (code > 0) {
        if (pdfi_array_size(a) != 4) {
            dmprintf1(ctx->memory, "Error - %s does not contain 4 values.\n", Key);
            code = gs_note_error(gs_error_rangecheck);
        } else {
            dmprintf1(ctx->memory, " %s: [", Key);
            for (i = 0; i < pdfi_array_size(a); i++) {
                code = pdfi_array_get_number(ctx, a, (uint64_t)i, &f);
                if (i != 0)
                    dmprintf(ctx->memory, " ");
                if (code == 0) {
                    if (a->values[i]->type == PDF_INT)
                        dmprintf1(ctx->memory, "%"PRIi64"", ((pdf_num *)a->values[i])->value.i);
                    else
                        dmprintf1(ctx->memory, "%f", ((pdf_num *)a->values[i])->value.d);
                } else {
                    dmprintf(ctx->memory, "NAN");
                }
            }
            dmprintf(ctx->memory, "]");
        }
    }
    pdfi_countdown(a);
    return code;
}

/*
 * This routine along with pdfi_output_metadtaa above, dumps certain kinds
 * of metadata from the PDF file, and from each page in the PDF file. It is
 * intended to duplicate the pdf_info.ps functionality of the PostScript-based
 * PDF interpreter in Ghostscript.
 *
 * It is not yet complete, we don't allow an option for dumping media sizes
 * we always emit them, and the switches -dDumpFontsNeeded, -dDumpXML,
 * -dDumpFontsUsed and -dShowEmbeddedFonts are not implemented at all yet.
 */
static int pdfi_output_page_info(pdf_context *ctx, uint64_t page_num)
{
    int code;
    bool known = false;
    double f;
    pdf_dict *page_dict = NULL;

    code = pdfi_page_get_dict(ctx, page_num, &page_dict);
    if (code < 0)
        return code;

    dmprintf1(ctx->memory, "Page %"PRIi64"", page_num + 1);

    code = pdfi_dict_knownget_number(ctx, page_dict, "UserUnit", &f);
    if (code > 0)
        dmprintf1(ctx->memory, " UserUnit: %f ", f);
    if (code < 0) {
        pdfi_countdown(page_dict);
        return code;
    }

    code = pdfi_dump_box(ctx, page_dict, "MediaBox");
    if (code < 0) {
        if (code != gs_error_undefined && ctx->args.pdfstoponerror) {
            pdfi_countdown(page_dict);
            return code;
        }
    }

    code = pdfi_dump_box(ctx, page_dict, "CropBox");
    if (code < 0) {
        if (code != gs_error_undefined && ctx->args.pdfstoponerror) {
            pdfi_countdown(page_dict);
            return code;
        }
    }

    code = pdfi_dump_box(ctx, page_dict, "BleedBox");
    if (code < 0) {
        if (code != gs_error_undefined && ctx->args.pdfstoponerror) {
            pdfi_countdown(page_dict);
            return code;
        }
    }

    code = pdfi_dump_box(ctx, page_dict, "TrimBox");
    if (code < 0) {
        if (code != gs_error_undefined && ctx->args.pdfstoponerror) {
            pdfi_countdown(page_dict);
            return code;
        }
    }

    code = pdfi_dump_box(ctx, page_dict, "ArtBox");
    if (code < 0) {
        if (code != gs_error_undefined && ctx->args.pdfstoponerror) {
            pdfi_countdown(page_dict);
            return code;
        }
    }

    code = pdfi_dict_knownget_number(ctx, page_dict, "Rotate", &f);
    if (code > 0)
        dmprintf1(ctx->memory, "    Rotate = %d ", (int)f);
    if (code < 0) {
        pdfi_countdown(page_dict);
        return code;
    }

    code = pdfi_check_page(ctx, page_dict, false);
    if (code < 0) {
        if (ctx->args.pdfstoponerror)
            return code;
    } else {
        if (ctx->page.has_transparency)
            dmprintf(ctx->memory, "     Page uses transparency features");
    }

    code = pdfi_dict_known(ctx, page_dict, "Annots", &known);
    if (code < 0) {
        if (code != gs_error_undefined && ctx->args.pdfstoponerror)
            return code;
    } else {
        if (known == true)
            dmprintf(ctx->memory, "     Page contains Annotations");
    }

    dmprintf(ctx->memory, "\n\n");
    pdfi_countdown(page_dict);

    return 0;
}

static void
pdfi_report_errors(pdf_context *ctx)
{
    int code;

    if (ctx->pdf_errors == E_PDF_NOERROR && ctx->pdf_warnings == W_PDF_NOWARNING)
        return;

    if (ctx->pdf_errors != E_PDF_NOERROR) {
        dmprintf(ctx->memory, "The following errors were encountered at least once while processing this file:\n");
        if (ctx->pdf_errors & E_PDF_NOHEADER)
            dmprintf(ctx->memory, "\tThe file does not have a valid PDF header.\n");
        if (ctx->pdf_errors & E_PDF_NOHEADERVERSION)
            dmprintf(ctx->memory, "\tThe file header does not contain a version number.\n");
        if (ctx->pdf_errors & E_PDF_NOSTARTXREF)
            dmprintf(ctx->memory, "\tThe file does not contain a 'startxref' token.\n");
        if (ctx->pdf_errors & E_PDF_BADSTARTXREF)
            dmprintf(ctx->memory, "\tThe file contain a 'startxref' token, but it does not point to an xref table.\n");
        if (ctx->pdf_errors & E_PDF_BADXREFSTREAM)
            dmprintf(ctx->memory, "\tThe file uses an XRefStm, but the stream is invalid.\n");
        if (ctx->pdf_errors & E_PDF_BADXREF)
            dmprintf(ctx->memory, "\tThe file uses an xref table, but the table is invalid.\n");
        if (ctx->pdf_errors & E_PDF_SHORTXREF)
            dmprintf(ctx->memory, "\tThe file uses an xref table, but the table has ferwer entires than expected.\n");
        if (ctx->pdf_errors & E_PDF_MISSINGENDSTREAM)
            dmprintf(ctx->memory, "\tA content stream is missing an 'endstream' token.\n");
        if (ctx->pdf_errors & E_PDF_MISSINGENDOBJ)
            dmprintf(ctx->memory, "\tAn object is missing an 'endobj' token.\n");
        if (ctx->pdf_errors & E_PDF_UNKNOWNFILTER)
            dmprintf(ctx->memory, "\tThe file attempted to use an unrecognised decompression filter.\n");
        if (ctx->pdf_errors & E_PDF_MISSINGWHITESPACE)
            dmprintf(ctx->memory, "\tA missing white space was detected while trying to read a number.\n");
        if (ctx->pdf_errors & E_PDF_MALFORMEDNUMBER)
            dmprintf(ctx->memory, "\tA malformed number was detected.\n");
        if (ctx->pdf_errors & E_PDF_UNESCAPEDSTRING)
            dmprintf(ctx->memory, "\tA string used a '(' character without an escape.\n");
        if (ctx->pdf_errors & E_PDF_BADOBJNUMBER)
            dmprintf(ctx->memory, "\tThe file contained a reference to an object number larger than the number of xref entries.\n");
        if (ctx->pdf_errors & E_PDF_TOKENERROR)
            dmprintf(ctx->memory, "\tAn operator in a content stream returned an error.\n");
        if (ctx->pdf_errors & E_PDF_KEYWORDTOOLONG)
            dmprintf(ctx->memory, "\tA keyword (outside a content stream) was too long (> 255).\n");
        if (ctx->pdf_errors & E_PDF_BADPAGETYPE)
            dmprintf(ctx->memory, "\tAn entry in the Pages array was a dictionary with a /Type key whose value was not /Page.\n");
        if (ctx->pdf_errors & E_PDF_CIRCULARREF)
            dmprintf(ctx->memory, "\tAn indirect object caused a circular reference to itself.\n");
        if (ctx->pdf_errors & E_PDF_UNREPAIRABLE)
            dmprintf(ctx->memory, "\tFile could not be repaired.\n");
        if (ctx->pdf_errors & E_PDF_REPAIRED)
            dmprintf(ctx->memory, "\tFile had an error that needed to be repaired.\n");
        if (ctx->pdf_errors & E_PDF_BADSTREAM)
            dmprintf(ctx->memory, "\tFile had an error in a stream.\n");
        if (ctx->pdf_errors & E_PDF_MISSINGOBJ)
            dmprintf(ctx->memory, "\tThe file contained a reference to an object number that is missing.\n");
        if (ctx->pdf_errors & E_PDF_BADPAGEDICT)
            dmprintf(ctx->memory, "\tThe file contained a bad Pages dictionary.  Couldn't process it.\n");
        if (ctx->pdf_errors & E_PDF_OUTOFMEMORY)
            dmprintf(ctx->memory, "\tThe interpeter ran out of memory while processing this file.\n");
        if (ctx->pdf_errors & E_PDF_PAGEDICTERROR)
            dmprintf(ctx->memory, "\tA page had a bad Page dict and was skipped.\n");
        if (ctx->pdf_errors & E_PDF_STACKUNDERFLOWERROR)
            dmprintf(ctx->memory, "\tToo few operands for an operator, operator was skipped.\n");
        if (ctx->pdf_errors & E_PDF_BADSTREAMDICT)
            dmprintf(ctx->memory, "\tA stream dictionary was not followed by a 'stream' keyword.\n");
        if (ctx->pdf_errors & E_PDF_DEREF_FREE_OBJ)
            dmprintf(ctx->memory, "\tAn attempt was made to access an object marked as free in the xref.\n");
        if (ctx->pdf_errors & E_PDF_INVALID_TRANS_XOBJECT)
            dmprintf(ctx->memory, "\tAn invalid transparency group XObject was ignored.\n");
        if (ctx->pdf_errors & E_PDF_NO_SUBTYPE)
            dmprintf(ctx->memory, "\tAn object was missing the required /Subtype.\n");
        if (ctx->pdf_errors & E_PDF_IMAGECOLOR_ERROR)
            dmprintf(ctx->memory, "\tAn image had an unknown or invalid colorspace.\n");
    }

    if (ctx->pdf_warnings != W_PDF_NOWARNING) {
        dmprintf(ctx->memory, "The following warnings were encountered at least once while processing this file:\n");
        if (ctx->pdf_warnings & W_PDF_BAD_XREF_SIZE)
            dmprintf(ctx->memory, "\tThe file contains an xref with more entries than the declared /Size in the trailer dictionary.\n");
        if (ctx->pdf_warnings & W_PDF_BAD_INLINEFILTER)
            dmprintf(ctx->memory, "\tThe file attempted to use an inline decompression filter other than on an inline image.\n");
        if (ctx->pdf_warnings & W_PDF_BAD_INLINECOLORSPACE)
            dmprintf(ctx->memory, "\tThe file attempted to use an inline image color space other than on an inline image.\n");
        if (ctx->pdf_warnings & W_PDF_BAD_INLINEIMAGEKEY)
            dmprintf(ctx->memory, "\tThe file attempted to use an inline image dictionary key with an image XObject.\n");
        if (ctx->pdf_warnings & W_PDF_IMAGE_ERROR)
            dmprintf(ctx->memory, "\tThe file has an error when rendering an image.\n");
        if (ctx->pdf_warnings & W_PDF_BAD_IMAGEDICT)
            dmprintf(ctx->memory, "\tThe file attempted to use an image with a bad value in the image dict.\n");
        if (ctx->pdf_warnings & W_PDF_TOOMANYQ)
            dmprintf(ctx->memory, "\tA content stream had unmatched q/Q operations (too many Q's).\n");
        if (ctx->pdf_warnings & W_PDF_TOOMANYq)
            dmprintf(ctx->memory, "\tA content stream had unmatched q/Q operations (too many q's).\n");
        if (ctx->pdf_warnings & W_PDF_STACKGARBAGE)
            dmprintf(ctx->memory, "\tA content stream left entries on the stack.\n");
        if (ctx->pdf_warnings & W_PDF_STACKUNDERFLOW)
            dmprintf(ctx->memory, "\tA content stream consumed too many arguments (stack underflow).\n");
        if (ctx->pdf_warnings & W_PDF_GROUPERROR)
            dmprintf(ctx->memory, "\tA transparency group was not terminated.\n");
        if (ctx->pdf_warnings & W_PDF_OPINVALIDINTEXT)
            dmprintf(ctx->memory, "\tAn operator (eg q/Q) was used in a text block where it is not permitted.\n");
        if (ctx->pdf_warnings & W_PDF_NOTINCHARPROC)
            dmprintf(ctx->memory, "\tA d0 or d1 operator was encountered outside a CharProc.\n");
        if (ctx->pdf_warnings & W_PDF_NESTEDTEXTBLOCK)
            dmprintf(ctx->memory, "\tEncountered a BT while already in a text block.\n");
        if (ctx->pdf_warnings & W_PDF_ETNOTEXTBLOCK)
            dmprintf(ctx->memory, "\tEncountered an ET while not in a text block.\n");
        if (ctx->pdf_warnings & W_PDF_TEXTOPNOBT)
            dmprintf(ctx->memory, "\tEncountered a text position or show operator without a prior BT operator.\n");
        if (ctx->pdf_warnings & W_PDF_BADICC_USE_ALT)
            dmprintf(ctx->memory, "\tCouldn't set ICC profile space, used Alternate space instead.\n");
        if (ctx->pdf_warnings & W_PDF_BADICC_USECOMPS)
            dmprintf(ctx->memory, "\tCouldn't set ICC profile space, used number of profile components to select a space.\n");
        if (ctx->pdf_warnings & W_PDF_BADTRSWITCH)
            dmprintf(ctx->memory, "\tSwitching from a text rendering mode including clip, to a mode which does not, is invalid.\n");
        if (ctx->pdf_warnings & W_PDF_BADSHADING)
            dmprintf(ctx->memory, "\tThe file has an error when interpreting a Shading object.\n");
        if (ctx->pdf_warnings & W_PDF_BADPATTERN)
            dmprintf(ctx->memory, "\tThe file has an error when interpreting a Pattern object.\n");
        if (ctx->pdf_warnings & W_PDF_NONSTANDARD_OP)
            dmprintf(ctx->memory, "\tThe file uses a non-standard PDF operator.\n");
        if (ctx->pdf_warnings & W_PDF_NUM_EXPONENT)
            dmprintf(ctx->memory, "\tThe file uses numbers with exponents, which is not standard PDF.\n");
        if (ctx->pdf_warnings & W_PDF_STREAM_HAS_CONTENTS)
            dmprintf(ctx->memory, "\tA stream dictionary has no stream and instead uses a /Contents entry, which is invalid.\n");
        if (ctx->pdf_warnings & W_PDF_STREAM_BAD_DECODEPARMS)
            dmprintf(ctx->memory, "\tA stream dictionary has an invalid /DecodeParms entry\n");
        if (ctx->pdf_warnings & W_PDF_MASK_ERROR)
            dmprintf(ctx->memory, "\tAn image dictionary has an invalid /Mask entry\n");
        if (ctx->pdf_warnings & W_PDF_ANNOT_AP_ERROR)
            dmprintf(ctx->memory, "\tAn Annotation has an invalid AP entry.\n");
        if (ctx->pdf_warnings & W_PDF_BAD_NAME_ESCAPE)
            dmprintf(ctx->memory, "\tA name contained a '#' escape character but it was not a valid escape.\n");
        if (ctx->pdf_warnings & W_PDF_TYPECHECK)
            dmprintf(ctx->memory, "\tAn object was of the wrong type, and was ignored.\n");
        if (ctx->pdf_warnings & W_PDF_BAD_TRAILER)
            dmprintf(ctx->memory, "\tAn entry in the Trailer dictionary was invalid, and was ignored.\n");
    }

    dmprintf(ctx->memory, "\n   **** This file had errors that were repaired or ignored.\n");
    if (ctx->Info) {
        pdf_string *s = NULL;

        code = pdfi_dict_knownget_type(ctx, ctx->Info, "Producer", PDF_STRING, (pdf_obj **)&s);
        if (code > 0) {
            char *cs;

            cs = (char *)gs_alloc_bytes(ctx->memory, s->length + 1, "temporary string for error report");
            memcpy(cs, s->data, s->length);
            cs[s->length] = 0x00;
            dmprintf1(ctx->memory, "   **** The file was produced by: \n   **** >>>> %s <<<<\n", cs);
            gs_free_object(ctx->memory, cs, "temporary string for error report");
        }
        pdfi_countdown(s);
    }
    dmprintf(ctx->memory, "   **** Please notify the author of the software that produced this\n");
    dmprintf(ctx->memory, "   **** file that it does not conform to Adobe's published PDF\n");
    dmprintf(ctx->memory, "   **** specification.\n\n");
}

/* Name table
 * I've been trying to avoid this for as long as possible, but it seems it cannot
 * be evaded. We need functions to get an index for a given string (which will
 * add the string to the table if its not present) and to cleear up the table
 * on finishing a PDF file.
 */

int pdfi_get_name_index(pdf_context *ctx, char *name, int len, unsigned int *returned)
{
    pdfi_name_entry_t *e = NULL, *last_entry = NULL, *new_entry = NULL;
    int index = 0;

    if (ctx->name_table == NULL) {
        e = NULL;
    } else {
        e = ctx->name_table;
    }

    while(e != NULL) {
        if (e->len == len) {
            if (memcmp(e->name, name, e->len) == 0) {
                *returned = e->index;
                return 0;
            }
        }
        last_entry = e;
        index = e->index;
        e = e->next;
    }

    new_entry = (pdfi_name_entry_t *)gs_alloc_bytes(ctx->memory, sizeof(pdfi_name_entry_t), "Alloc name table entry");
    if (new_entry == NULL)
        return_error(gs_error_VMerror);
    memset(new_entry, 0x00, sizeof(pdfi_name_entry_t));
    new_entry->name = (char *)gs_alloc_bytes(ctx->memory, len+1, "Alloc name table name");
    if (new_entry->name == NULL) {
        gs_free_object(ctx->memory, new_entry, "Failed to allocate name entry");
            return_error(gs_error_VMerror);
    }
    memset(new_entry->name, 0x00, len+1);
    memcpy(new_entry->name, name, len);
    new_entry->len = len;
    new_entry->index = ++index;

    if (last_entry)
        last_entry->next = new_entry;
    else
        ctx->name_table = new_entry;

    *returned = new_entry->index;
    return 0;
}

static int pdfi_free_name_table(pdf_context *ctx)
{
    if (ctx->name_table) {
        pdfi_name_entry_t *next = NULL, *e = (pdfi_name_entry_t *)ctx->name_table;

        while (e != NULL) {
            next = (pdfi_name_entry_t *)e->next;
            gs_free_object(ctx->memory, e->name, "free name table entries");
            gs_free_object(ctx->memory, e, "free name table entries");
            e = next;
        }
    }
    ctx->name_table = NULL;
    return 0;
}

int pdfi_name_from_index(pdf_context *ctx, int index, unsigned char **name, unsigned int *len)
{
    pdfi_name_entry_t *e = (pdfi_name_entry_t *)ctx->name_table;

    while (e != NULL) {
        if (e->index == index) {
            *name = (unsigned char *)e->name;
            *len = e->len;
            return 0;
        }
        e = e->next;
    }

    return_error(gs_error_undefined);
}

int pdfi_separation_name_from_index(gs_gstate *pgs, gs_separation_name index, unsigned char **name, unsigned int *len)
{
    pdfi_int_gstate *igs = (pdfi_int_gstate *)pgs->client_data;
    pdf_context *ctx = NULL;
    pdfi_name_entry_t *e = NULL;

    if (igs == NULL)
        return_error(gs_error_undefined);

    ctx = igs->ctx;
    if (ctx == NULL)
        return_error(gs_error_undefined);

    e = (pdfi_name_entry_t *)ctx->name_table;

    while (e != NULL) {
        if (e->index == index) {
            *name = (unsigned char *)e->name;
            *len = e->len;
            return 0;
        }
        e = e->next;
    }

    return_error(gs_error_undefined);
}

/* These functions are used by the 'PL' implementation, eventually we will */
/* need to have custom PostScript operators to process the file or at      */
/* (least pages from it).                                                  */

int pdfi_close_pdf_file(pdf_context *ctx)
{
    if (ctx->main_stream) {
        if (ctx->main_stream->s) {
            sfclose(ctx->main_stream->s);
        }
        gs_free_object(ctx->memory, ctx->main_stream, "Closing main PDF file");
        ctx->main_stream = NULL;
    }
    ctx->main_stream_length = 0;

    if (ctx->filename) {
        gs_free_object(ctx->memory, ctx->filename, "pdfi_close_pdf_file, free copy of filename");
        ctx->filename = NULL;
    }

    pdfi_clear_context(ctx);
    return 0;
}

static int pdfi_process(pdf_context *ctx)
{
    int code, i;

    /* Loop over each page and either render it or output the
     * required information.
     */
    for (i=0;i < ctx->num_pages;i++) {
        if (ctx->args.first_page != 0) {
            if (i < ctx->args.first_page - 1)
                continue;
        }
        if (ctx->args.last_page != 0) {
            if (i > ctx->args.last_page - 1)
                break;;
        }
        if (ctx->args.pdfinfo)
            code = pdfi_output_page_info(ctx, i);
        else
            code = pdfi_page_render(ctx, i, true);

        if (code < 0 && ctx->args.pdfstoponerror)
            goto exit;
        code = 0;
    }
 exit:
    pdfi_report_errors(ctx);

    return code;
}

/* This works by reading each embedded file referenced by the collection. If it
 * has a MIME type indicating it's a PDF file, or somewhere in the first 2KB it
 * has the PDF header (%PDF-) then we treat it as a PDF file. We read the contents
 * of the refrenced stream and write them to disk in a scratch file.
 *
 * We then process each scratch file in turn. Note that we actually return an
 * array of strings; the first string is the temporary filename, the second is
 * the entry from the names tree. Since this can be in UTF16-BE format, it can
 * contain embedded single byte NULL characters, so we can't use a regular C
 * string. Instead we use a triple byte NULL termination.
 *
 * It ought to be possible to do all the processing without creating scratch files, by saving the
 * current file state, and opening a new 'file' on the stream in the original PDF
 * file. But I couldn't immediately get that to work.
 * So this is a FIXME future enhancement.
 */
int pdfi_prep_collection(pdf_context *ctx, uint64_t *TotalFiles, char ***names_array)
{
    int code = 0, i, NumEmbeddedFiles = 0;
    pdf_obj *Names = NULL, *EmbeddedFiles = NULL, *FileNames = NULL;
    pdf_obj *EF = NULL, *F = NULL;
    char **working_array = NULL;

    if (pdfi_dict_knownget_type(ctx, ctx->Root, "Names", PDF_DICT, &Names))
    {
        if(pdfi_dict_knownget_type(ctx, (pdf_dict *)Names, "EmbeddedFiles", PDF_DICT, &EmbeddedFiles))
        {
            if (pdfi_dict_knownget_type(ctx, (pdf_dict *)EmbeddedFiles, "Names", PDF_ARRAY, &FileNames))
            {
                int ix = 0, index = 0;
                gp_file *scratch_file = NULL;
                char scratch_name[gp_file_name_sizeof];

                NumEmbeddedFiles = pdfi_array_size((pdf_array *)FileNames) / 2;

                working_array = (char **)gs_alloc_bytes(ctx->memory, NumEmbeddedFiles * 2 * sizeof(char *), "Collection file working names array");
                if (working_array == NULL) {
                    code = gs_note_error(gs_error_VMerror);
                    goto exit;
                }
                memset(working_array, 0x00, NumEmbeddedFiles * 2 * sizeof(char *));

                for (ix = 0;ix < NumEmbeddedFiles;ix++)
                {
                    pdf_obj *File = NULL;
                    pdf_obj *Subtype = NULL;

                    code = pdfi_array_get(ctx, (pdf_array *)FileNames, (ix * 2) + 1, &File);
                    if (code < 0)
                        break;

                    if (File->type == PDF_DICT)
                    {
                        if (pdfi_dict_knownget_type(ctx, (pdf_dict *)File, "EF", PDF_DICT, &EF))
                        {
                            if (pdfi_dict_knownget_type(ctx, (pdf_dict *)EF, "F", PDF_STREAM, &F))
                            {
                                pdf_dict *stream_dict = NULL;
                                pdf_c_stream *s;

                                /* pdfi_dict_from_object does not increment the reference count of the stream dictionary
                                 * so we do not need to count it down later.
                                 */
                                code = pdfi_dict_from_obj(ctx, F, &stream_dict);
                                if (code >= 0) {
                                    if (!pdfi_dict_knownget_type(ctx, stream_dict, "Subtype", PDF_NAME, &Subtype))
                                    {
                                        /* No Subtype, (or not a name) we can't check the Mime type, so try to read the first 2Kb
                                         * and look for a %PDF- in that. If not present, assume its not a PDF
                                         */
                                        code = pdfi_seek(ctx, ctx->main_stream, pdfi_stream_offset(ctx, (pdf_stream *)F), SEEK_SET);
                                        if (code >= 0) {
                                            code = pdfi_filter(ctx, (pdf_stream *)F, ctx->main_stream, &s, false);
                                            if (code >= 0) {
                                                char Buffer[2048];
                                                int bytes;

                                                bytes = pdfi_read_bytes(ctx, (byte *)Buffer, 1, 2048, s);
                                                pdfi_countdown(s);
                                                /* Assertion; the smallest real PDF file is at least 400 bytes */
                                                if (bytes >= 400) {
                                                    if (strstr(Buffer, "%PDF-") == NULL)
                                                        code = -1;
                                                } else
                                                    code = -1;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        if (!pdfi_name_is((const pdf_name *)Subtype, "application/pdf"))
                                            code = -1;
                                    }

                                    if (code >= 0)
                                    {
                                        /* Appears to be a PDF file. Create a scratch file to hold it, and then
                                         * read the file from the PDF, and write it to the scratch file. Record
                                         * the scratch filename in the working_array for later processing.
                                         */
                                        scratch_file = gp_open_scratch_file(ctx->memory, "gpdf-collection-", scratch_name, "wb");
                                        if (scratch_file != NULL) {
                                            code = pdfi_seek(ctx, ctx->main_stream, pdfi_stream_offset(ctx, (pdf_stream *)F), SEEK_SET);
                                            if (code >= 0)
                                            {
                                                double L;
                                                pdf_c_stream *SubFile_stream = NULL;

                                                /* Start by setting up the file to be read. Apply a SubFileDecode so that, if the input stream
                                                 * is not compressed we will stop reading when we get to the end of the stream.
                                                 */
                                                if (pdfi_dict_knownget_number(ctx, stream_dict, "Length", &L))
                                                {

                                                    code = pdfi_apply_SubFileDecode_filter(ctx, (int)L, NULL, ctx->main_stream, &SubFile_stream, false);
                                                    if (code >= 0)
                                                        code = pdfi_filter(ctx, (pdf_stream *)F, SubFile_stream, &s, false);
                                                } else
                                                    code = pdfi_filter(ctx, (pdf_stream *)F, ctx->main_stream, &s, false);

                                                if (code >= 0)
                                                {
                                                    char Buffer[2048];
                                                    int bytes;
                                                    pdf_string *Name = NULL;

                                                    /* Read the stream contents and write them to the scratch file */
                                                    do {
                                                        bytes = pdfi_read_bytes(ctx, (byte *)Buffer, 1, 2048, s);
                                                        (void)gp_fwrite(Buffer, 1, bytes, scratch_file);
                                                    } while (bytes > 0);

                                                    /* Create an entry for the Description in the names array */
                                                    code = pdfi_array_get(ctx, (pdf_array *)FileNames, ix * 2, (pdf_obj **)&Name);
                                                    if (code >= 0) {
                                                        if (Name->type == PDF_STRING) {
                                                            working_array[(index * 2) + 1] = (char *)gs_alloc_bytes(ctx->memory, Name->length + 3, "Collection file names array entry");
                                                            if (working_array[(index * 2) + 1] != NULL) {
                                                                memset(working_array[(index * 2) + 1], 0x00, Name->length + 3);
                                                                memcpy(working_array[(index * 2) + 1], Name->data, Name->length);
                                                            }
                                                        }
                                                        pdfi_countdown(Name);
                                                        Name = NULL;
                                                    }

                                                    /* And now the scratch file name */
                                                    working_array[index * 2] = (char *)gs_alloc_bytes(ctx->memory, strlen(scratch_name) + 3, "Collection file names array entry");
                                                    if (working_array[index * 2] != NULL) {
                                                        memset(working_array[index * 2], 0x00, strlen(scratch_name) + 3);
                                                        strcpy(working_array[index * 2], scratch_name);
                                                    }

                                                    index++;
                                                    (*TotalFiles)++;
                                                    pdfi_countdown(s);
                                                }
                                                if (SubFile_stream != NULL)
                                                    pdfi_countdown(SubFile_stream);
                                            }
                                            gp_fclose(scratch_file);
                                        } else
                                            dmprintf(ctx->memory, "\n   **** Warning: Failed to open a scratch file.\n");
                                    }
                                }
                            }
                        }
                    }
                    pdfi_countdown(Subtype);
                    Subtype = NULL;
                    pdfi_countdown(F);
                    F = NULL;
                    pdfi_countdown(EF);
                    EF = NULL;
                    pdfi_countdown(File);
                    File = NULL;
                }
            } else {
                dmprintf(ctx->memory, "\n   **** Warning: Failed to read EmbeededFiles Names tree.\n");
            }
        } else {
            dmprintf(ctx->memory, "\n   **** Warning: Failed to read EmbeddedFiles.\n");
        }
    } else {
        dmprintf(ctx->memory, "\n   **** Warning: Failed to find Names tree.\n");
    }

exit:
    if (code >= 0) {
        uint64_t ix = 0;

        (*names_array) = (char **)gs_alloc_bytes(ctx->memory, *TotalFiles * 2 * sizeof(char *), "Collection file namesarray");
        for (i = 0; i < NumEmbeddedFiles;i++) {
            if (working_array[i * 2] != NULL && working_array[(i * 2) + 1] != NULL) {
                (*names_array)[ix * 2] = working_array[i * 2];
                working_array[i * 2] = NULL;
                (*names_array)[(ix * 2) + 1] = working_array[(i * 2) + 1];
                working_array[(i * 2) + 1] = NULL;
                ix++;
            }
        }
    }

    for (i = 0; i < NumEmbeddedFiles;i++)
        gs_free_object(ctx->memory, working_array[i], "free collection temporary filenames");
    gs_free_object(ctx->memory, working_array, "free collection working array");
    pdfi_countdown(F);
    pdfi_countdown(EF);
    pdfi_countdown(FileNames);
    pdfi_countdown(EmbeddedFiles);
    pdfi_countdown(Names);
    return code;
}

static int pdfi_process_collection(pdf_context *ctx)
{
    int code, i;
    uint64_t TotalFiles = 0, ix = 0;
    char **names_array = NULL;

    code = pdfi_prep_collection(ctx, &TotalFiles, &names_array);
    if (code >= 0 && TotalFiles > 0)
    {
        /* names_array is full of pointers to the scratch file names containing PDF files.
         * Now we need to run each PDF file. First we close down the current file.
         */
        (void)pdfi_close_pdf_file(ctx);

        for (ix = 0;ix < TotalFiles * 2;ix+=2) {
            if (names_array[ix] != NULL) {
                (void)pdfi_process_pdf_file(ctx, names_array[ix]);
                (void)pdfi_close_pdf_file(ctx);
            }
        }
    } else
        /* We didn't find any PDF files in the Embedded Files. So just run the
         * pages in the container file (the original PDF file)
         */
        pdfi_process(ctx);

    for (i = 0; i < TotalFiles * 2;i++)
        gs_free_object(ctx->memory, names_array[i], "free collection temporary filenames");
    gs_free_object(ctx->memory, names_array, "free collection names array");

    return 0;
}

int pdfi_process_pdf_file(pdf_context *ctx, char *filename)
{
    int code = 0;

    code = pdfi_open_pdf_file(ctx, filename);
    if (code < 0) {
        pdfi_report_errors(ctx);
        return code;
    }

    /* Need to do this here so that ctx->writepdfmarks will be setup
     * It is also called in pdfi_page_render()
     * TODO: Should probably look into that..
     */
    pdfi_device_set_flags(ctx);
    /* Do any custom device configuration */
    pdfi_device_misc_config(ctx);

    if (ctx->Collection != NULL)
        code = pdfi_process_collection(ctx);
    else
        code = pdfi_process(ctx);

    pdfi_close_pdf_file(ctx);
    return code;
}

static int pdfi_init_file(pdf_context *ctx)
{
    int code = 0;
    pdf_obj *o = NULL;

    code = pdfi_read_xref(ctx);
    if (code < 0) {
        if (ctx->is_hybrid) {
            /* If its a hybrid file, and we failed to read the XrefStm, try
             * again, but this time read the xref table instead.
             */
            ctx->pdf_errors |= E_PDF_BADXREFSTREAM;
            pdfi_countdown(ctx->xref_table);
            ctx->xref_table = NULL;
            ctx->prefer_xrefstm = false;
            code = pdfi_read_xref(ctx);
            if (code < 0)
                goto exit;
        } else {
            ctx->pdf_errors |= E_PDF_BADXREF;
            goto exit;
        }
    }

    if (ctx->Trailer) {
        code = pdfi_dict_get(ctx, ctx->Trailer, "Encrypt", &o);
        if (code < 0 && code != gs_error_undefined)
            goto exit;
        if (code == 0) {
            code = pdfi_initialise_Decryption(ctx);
            if (code < 0)
                goto exit;
        }
    }

read_root:
    if (ctx->Trailer) {
        code = pdfi_read_Root(ctx);
        if (code < 0) {
            /* If we couldn#'t find the Root object, and we were using the XrefStm
             * from a hybrid file, then try again, but this time use the xref table
             */
            if (code == gs_error_undefined && ctx->is_hybrid && ctx->prefer_xrefstm) {
                ctx->pdf_errors |= E_PDF_BADXREFSTREAM;
                pdfi_countdown(ctx->xref_table);
                ctx->xref_table = NULL;
                ctx->prefer_xrefstm = false;
                code = pdfi_read_xref(ctx);
                if (code < 0) {
                    ctx->pdf_errors |= E_PDF_BADXREF;
                    goto exit;
                }
                code = pdfi_read_Root(ctx);
                if (code < 0)
                    goto exit;
            } else {
                int code1 = pdfi_repair_file(ctx);
                if (code1 < 0)
                    goto exit;
                goto read_root;
            }
        }
    }

    if (ctx->Trailer) {
        code = pdfi_read_Info(ctx);
        if (code < 0 && code != gs_error_undefined) {
            if (ctx->args.pdfstoponerror)
                goto exit;
            pdfi_clearstack(ctx);
        }
    }

    if (!ctx->Root) {
        dmprintf(ctx->memory, "Catalog dictionary not located in file, unable to proceed\n");
        return_error(gs_error_syntaxerror);
    }

    code = pdfi_read_Pages(ctx);
    if (code < 0)
        goto exit;

    code = pdfi_doc_page_array_init(ctx);
    if (code < 0)
        goto exit;

    if (ctx->num_pages == 0)
        dmprintf(ctx->memory, "\n   **** Warning: PDF document has no pages.\n");

    code = pdfi_doc_trailer(ctx);
    if (code < 0)
        goto exit;

    pdfi_read_OptionalRoot(ctx);

    if (ctx->args.pdfinfo) {
        code = pdfi_output_metadata(ctx);
        if (code < 0 && ctx->args.pdfstoponerror)
            goto exit;
    }

exit:
    pdfi_countdown(o);
    return code;
}

int pdfi_set_input_stream(pdf_context *ctx, stream *stm)
{
    byte *Buffer = NULL;
    char *s = NULL;
    float version = 0.0;
    gs_offset_t Offset = 0;
    int64_t bytes = 0, leftover = 0;
    bool found = false;
    int code;

    /* In case of broken PDF files, the repair could run off the end of the
     * file, so make sure that doing so does *not* automagically close the file
     */
    stm->close_at_eod = false;

    ctx->main_stream = (pdf_c_stream *)gs_alloc_bytes(ctx->memory, sizeof(pdf_c_stream), "PDF interpreter allocate main PDF stream");
    if (ctx->main_stream == NULL)
        return_error(gs_error_VMerror);
    memset(ctx->main_stream, 0x00, sizeof(pdf_c_stream));
    ctx->main_stream->s = stm;

    Buffer = gs_alloc_bytes(ctx->memory, BUF_SIZE, "PDF interpreter - allocate working buffer for file validation");
    if (Buffer == NULL) {
        code = gs_error_VMerror;
        goto error;
    }

    /* Determine file size */
    pdfi_seek(ctx, ctx->main_stream, 0, SEEK_END);
    ctx->main_stream_length = pdfi_tell(ctx->main_stream);
    Offset = BUF_SIZE;
    bytes = BUF_SIZE;
    pdfi_seek(ctx, ctx->main_stream, 0, SEEK_SET);

    bytes = Offset = min(BUF_SIZE - 1, ctx->main_stream_length);

    if (ctx->args.pdfdebug)
        dmprintf(ctx->memory, "%% Reading header\n");

    bytes = pdfi_read_bytes(ctx, Buffer, 1, Offset, ctx->main_stream);
    if (bytes <= 0) {
        emprintf(ctx->memory, "Failed to read any bytes from input stream\n");
        code = gs_error_ioerror;
        goto error;
    }
    if (bytes < 8) {
        emprintf(ctx->memory, "Failed to read enough bytes for a valid PDF header from input stream\n");
        code = gs_error_ioerror;
        goto error;
    }
    Buffer[Offset] = 0x00;

    /* First check for existence of header */
    s = strstr((char *)Buffer, "%PDF");
    if (s == NULL) {
        if (ctx->args.pdfdebug) {
            if (ctx->filename)
                dmprintf1(ctx->memory, "%% File %s does not appear to be a PDF file (no %%PDF in first 2Kb of file)\n", ctx->filename);
            else
                dmprintf1(ctx->memory, "%% File %s does not appear to be a PDF stream (no %%PDF in first 2Kb of stream)\n", ctx->filename);
        }
        ctx->pdf_errors |= E_PDF_NOHEADER;
    } else {
        /* Now extract header version (may be overridden later) */
        if (sscanf(s + 5, "%f", &version) != 1) {
            if (ctx->args.pdfdebug)
                dmprintf(ctx->memory, "%% Unable to read PDF version from header\n");
            ctx->HeaderVersion = 0;
            ctx->pdf_errors |= E_PDF_NOHEADERVERSION;
        }
        else {
            ctx->HeaderVersion = version;
        }
        if (ctx->args.pdfdebug)
            dmprintf1(ctx->memory, "%% Found header, PDF version is %f\n", ctx->HeaderVersion);
    }

    /* Jump to EOF and scan backwards looking for startxref */
    pdfi_seek(ctx, ctx->main_stream, 0, SEEK_END);

    if (ctx->args.pdfdebug)
        dmprintf(ctx->memory, "%% Searching for 'startxerf' keyword\n");

    /* Initially read min(BUF_SIZE, file_length) bytes of data to the buffer */
    bytes = Offset;

    do {
        byte *last_lineend = NULL;
        uint32_t read;

        if (pdfi_seek(ctx, ctx->main_stream, ctx->main_stream_length - Offset, SEEK_SET) != 0) {
            emprintf1(ctx->memory, "File is smaller than %"PRIi64" bytes\n", (int64_t)Offset);
            code = gs_error_ioerror;
            goto error;
        }
        read = pdfi_read_bytes(ctx, Buffer, 1, bytes, ctx->main_stream);

        if (read <= 0) {
            emprintf1(ctx->memory, "Failed to read %"PRIi64" bytes from file\n", (int64_t)bytes);
            code = gs_error_ioerror;
            goto error;
        }

        /* When reading backwards, if we ran out of data in the last buffer while looking
         * for a 'startxref, but we had found a linefeed, then we preserved everything
         * from the beginning of the buffer up to that linefeed, by copying it to the end
         * of the buffer and reducing the number of bytes to read so that it should have filled
         * in the gap. If we didn't read enough bytes, then we have a gap between the end of
         * the data we just read and the leftover data from teh last buffer. Move the preserved
         * data down to meet the end of the data we just read.
         */
        if (bytes != read && leftover != 0)
            memcpy(Buffer + read, Buffer + bytes, leftover);

        /* As above, if we had any leftover data from the last buffer then increase the
         * number of bytes available by that amount. We increase 'bytes' (the number of bytes
         * to read) to the same value, which should mean we read an entire buffer's worth. Of
         * course if we have any data left out of this buffer we'll reduce bytes again...
         */
        read = bytes = read + leftover;

        /* Now search backwards in the buffer for the startxref token */
        while(read) {
            if (memcmp(Buffer + read - 9, "startxref", 9) == 0) {
                found = true;
                break;
            } else {
                if (Buffer[read - 1] == 0x0a || Buffer[read - 1] == 0x0d)
                    last_lineend = Buffer + read;
            }
            read--;
        }
        if (found) {
            byte *b = Buffer + read;

            /* Success! stop now */
            if(sscanf((char *)b, " %ld", &ctx->startxref) != 1) {
                dmprintf(ctx->memory, "Unable to read offset of xref from PDF file\n");
            }
            break;
        } else {
            /* Our file read could conceivably have read back to the point where we read
             * part of the 'startxref' token, but not all of it. So we want to preserve
             * the data in the buffer, but not all of it obviously! The 'startxref' should be followed
             * by a line ending, so above we keep a note of the last line ending. If we found one, then
             * we preserve from the start of the buffer to that point. This could slow us up if the file
             * Is broken, or has a load of junk after the EOF, because we could potentially be saving a
             * lot of data on each pass, but that's only going to happen with bad files.
             * Note we reduce the number of bytes to read so that it just fits into the buffer up to the
             * beginning of the data we preserved.
             */
            if (last_lineend) {
                leftover = last_lineend - Buffer;
                memcpy(Buffer + bytes - leftover, last_lineend, leftover);
                bytes -= leftover;
            } else
                leftover = 0;
        }

        Offset += bytes;
    } while(Offset < ctx->main_stream_length);

    if (!found)
        ctx->pdf_errors |= E_PDF_NOSTARTXREF;

    code = pdfi_init_file(ctx);

error:
    gs_free_object(ctx->memory, Buffer, "PDF interpreter - allocate working buffer for file validation");
    return code;
}

int pdfi_open_pdf_file(pdf_context *ctx, char *filename)
{
    stream *s = NULL;
    int code;

    if (ctx->args.pdfdebug)
        dmprintf1(ctx->memory, "%% Attempting to open %s as a PDF file\n", filename);

    ctx->filename = (char *)gs_alloc_bytes(ctx->memory, strlen(filename) + 1, "copy of filename");
    if (ctx->filename == NULL)
        return_error(gs_error_VMerror);
    strcpy(ctx->filename, filename);

    s = sfopen(filename, "r", ctx->memory);
    if (s == NULL) {
        emprintf1(ctx->memory, "Failed to open file %s\n", filename);
        return_error(gs_error_ioerror);
    }
    code = pdfi_set_input_stream(ctx, s);
    return code;
}

/***********************************************************************************/
/* Highest level functions. The context we create here is returned to the 'PL'     */
/* implementation, in future we plan to return it to PostScript by wrapping a      */
/* gargabe collected object 'ref' around it and returning that to the PostScript   */
/* world. custom PostScript operators will then be able to render pages, annots,   */
/* AcroForms etc by passing the opaque object back to functions here, allowing     */
/* the interpreter access to its context.                                          */

/* We start with routines for creating and destroying the interpreter context */
pdf_context *pdfi_create_context(gs_memory_t *pmem)
{
    pdf_context *ctx = NULL;
    gs_gstate *pgs = NULL;
    int code = 0;

    ctx = (pdf_context *) gs_alloc_bytes(pmem->non_gc_memory,
            sizeof(pdf_context), "pdf_create_context");

    pgs = gs_gstate_alloc(pmem);

    if (!ctx || !pgs)
    {
        if (ctx)
            gs_free_object(pmem->non_gc_memory, ctx, "pdf_create_context");
        if (pgs)
            gs_gstate_free(pgs);
        return NULL;
    }

    memset(ctx, 0, sizeof(pdf_context));
    ctx->memory = pmem->non_gc_memory;

    ctx->stack_bot = (pdf_obj **)gs_alloc_bytes(ctx->memory, INITIAL_STACK_SIZE * sizeof (pdf_obj *), "pdf_imp_allocate_interp_stack");
    if (ctx->stack_bot == NULL) {
        gs_free_object(pmem->non_gc_memory, ctx, "pdf_create_context");
        gs_gstate_free(pgs);
        return NULL;
    }
    ctx->stack_size = INITIAL_STACK_SIZE;
    ctx->stack_top = ctx->stack_bot - sizeof(pdf_obj *);
    code = sizeof(pdf_obj *);
    code *= ctx->stack_size;
    ctx->stack_limit = ctx->stack_bot + ctx->stack_size;

    code = pdfi_init_font_directory(ctx);
    if (code < 0) {
        gs_free_object(pmem->non_gc_memory, ctx->stack_bot, "pdf_create_context");
        gs_free_object(pmem->non_gc_memory, ctx, "pdf_create_context");
        gs_gstate_free(pgs);
        return NULL;
    }

    code = gsicc_init_iccmanager(pgs);
    if (code < 0) {
        gs_free_object(ctx->memory, ctx->font_dir, "pdf_create_context");
        gs_free_object(pmem->non_gc_memory, ctx->stack_bot, "pdf_create_context");
        gs_free_object(pmem->non_gc_memory, ctx, "pdf_create_context");
        gs_gstate_free(pgs);
        return NULL;
    }

    ctx->pgs = pgs;
    pdfi_gstate_set_client(ctx, pgs);

    /* Declare PDL client support for high level patterns, for the benefit
     * of pdfwrite and other high-level devices
     */
    ctx->pgs->have_pattern_streams = true;
    ctx->device_state.preserve_tr_mode = 0;
    ctx->args.notransparency = false;

    ctx->main_stream = NULL;

    /* Setup some flags that don't default to 'false' */
    ctx->args.showannots = true;
    ctx->args.preserveannots = true;
    /* NOTE: For testing certain annotations on cluster, might want to set this to false */
    ctx->args.printed = true; /* TODO: Should be true if OutputFile is set, false otherwise */

    /* Initially, prefer the XrefStm in a hybrid file */
    ctx->prefer_xrefstm = true;

    /* We decrypt strings from encrypted files until we start a page */
    ctx->encryption.decrypt_strings = true;
    ctx->get_glyph_name = pdfi_glyph_name;
    ctx->get_glyph_index = pdfi_glyph_index;

    ctx->job_gstate_level = ctx->pgs->level;
    /* Weirdly the graphics library wants us to always have two gstates, the
     * initial state and at least one saved state. if we don't then when we
     * grestore back to the initial state, it immediately saves another one.
     */
    code = gs_gsave(ctx->pgs);
#if REFCNT_DEBUG
    ctx->UID = 1;
#endif
#if CACHE_STATISTICS
    ctx->hits = 0;
    ctx->misses = 0;
    ctx->compressed_hits = 0;
    ctx->compressed_misses = 0;
#endif
    return ctx;
}

/* Purge all */
static bool
pdfi_fontdir_purge_all(const gs_memory_t * mem, cached_char * cc, void *dummy)
{
    return true;
}

#if DEBUG_CACHE
#if DEBUG_CACHE_FREE
static void
pdfi_print_cache(pdf_context *ctx)
{
    pdf_obj_cache_entry *entry = ctx->cache_LRU, *next;

    dmprintf1(ctx->memory, "CACHE: #entries=%d\n", ctx->cache_entries);
    while(entry) {
        next = entry->next;
#if REFCNT_DEBUG
        dmprintf5(ctx->memory, "UID:%ld, Object:%d, refcnt:%d, next=%p, prev=%p\n",
                  entry->o->UID, entry->o->object_num, entry->o->refcnt,
                  entry->next, entry->previous);
#else
        dmprintf4(ctx->memory, "Object:%d, refcnt:%d, next=%p, prev=%p\n",
                  entry->o->object_num, entry->o->refcnt,
                  entry->next, entry->previous);
#endif
        entry = next;
    }
}
#else
static void
pdfi_print_cache(pdf_context *ctx)
{}
#endif
#endif /* DEBUG */

/* pdfi_clear_context frees all the PDF objects associated with interpreting a given
 * PDF file. Once we've called this we can happily run another file. This function is
 * called by pdf_free_context (in case of errors during the file leaving state around)
 * and by pdfi_close_pdf_file.
 */
int pdfi_clear_context(pdf_context *ctx)
{
#if CACHE_STATISTICS
    float compressed_hit_rate = 0.0, hit_rate = 0.0;

    if (ctx->compressed_hits > 0 || ctx->compressed_misses > 0)
        compressed_hit_rate = (float)ctx->compressed_hits / (float)(ctx->compressed_hits + ctx->compressed_misses);
    if (ctx->hits > 0 || ctx->misses > 0)
        hit_rate = (float)ctx->hits / (float)(ctx->hits + ctx->misses);

    dmprintf1(ctx->memory, "Number of normal object cache hits: %"PRIi64"\n", ctx->hits);
    dmprintf1(ctx->memory, "Number of normal object cache misses: %"PRIi64"\n", ctx->misses);
    dmprintf1(ctx->memory, "Number of compressed object cache hits: %"PRIi64"\n", ctx->compressed_hits);
    dmprintf1(ctx->memory, "Number of compressed object cache misses: %"PRIi64"\n", ctx->compressed_misses);
    dmprintf1(ctx->memory, "Normal object cache hit rate: %f\n", hit_rate);
    dmprintf1(ctx->memory, "Compressed object cache hit rate: %f\n", compressed_hit_rate);
#endif
    if (ctx->args.PageList) {
        gs_free_object(ctx->memory, ctx->args.PageList, "pdfi_free_context");
        ctx->args.PageList = NULL;
    }
    if (ctx->Trailer) {
        pdfi_countdown(ctx->Trailer);
        ctx->Trailer = NULL;
    }

    if (ctx->AcroForm) {
        pdfi_countdown(ctx->AcroForm);
        ctx->AcroForm = NULL;
    }

    if(ctx->Root) {
        pdfi_countdown(ctx->Root);
        ctx->Root = NULL;
    }

    if (ctx->Info) {
        pdfi_countdown(ctx->Info);
        ctx->Info = NULL;
    }

    if (ctx->PagesTree) {
        pdfi_countdown(ctx->PagesTree);
        ctx->PagesTree = NULL;
    }

    pdfi_doc_page_array_free(ctx);

    if (ctx->xref_table) {
        pdfi_countdown(ctx->xref_table);
        ctx->xref_table = NULL;
    }

    pdfi_free_OptionalRoot(ctx);

    if (ctx->stack_bot)
        pdfi_clearstack(ctx);

    if (ctx->filename) {
        /* This should already be closed! */
        pdfi_close_pdf_file(ctx);
        gs_free_object(ctx->memory, ctx->filename, "pdfi_free_context, free copy of filename");
        ctx->filename = NULL;
    }

    if (ctx->main_stream) {
        gs_free_object(ctx->memory, ctx->main_stream, "pdfi_free_context, free main PDF stream");
        ctx->main_stream = NULL;
    }
    ctx->main_stream_length = 0;

    if(ctx->pgs != NULL) {
        gx_pattern_cache_free(ctx->pgs->pattern_cache);
        ctx->pgs->pattern_cache = NULL;
        if (ctx->pgs->font)
            pdfi_countdown_current_font(ctx);

        /* We use gs_grestore_only() instead of gs_grestore, because gs_grestore
         * will not restore below two gstates and we want to clear the entire
         * stack of saved states, back to the initial state.
         */
        while (ctx->pgs->level != ctx->job_gstate_level && ctx->pgs->saved)
            gs_grestore_only(ctx->pgs);
    }

    pdfi_free_DefaultQState(ctx);
    pdfi_oc_free(ctx);

    if(ctx->encryption.EKey) {
        pdfi_countdown(ctx->encryption.EKey);
        ctx->encryption.EKey = NULL;
    }
    if (ctx->encryption.Password) {
        gs_free_object(ctx->memory, ctx->encryption.Password, "PDF Password from params");
        ctx->encryption.Password = NULL;
    }

    if (ctx->cache_entries != 0) {
        pdf_obj_cache_entry *entry = ctx->cache_LRU, *next;

#if DEBUG_CACHE
        int count;
        bool stop = true;
        pdf_obj_cache_entry *prev;

        do {
            pdfi_print_cache(ctx);
            entry = ctx->cache_LRU;
            stop = true;
            while(entry) {
                pdfi_print_cache(ctx);
                next = entry->next;
                prev = entry->previous;

                /* pass through the cache, count down any objects which are only referenced by the cache (refcnt == 1)
                 * this may cause other objects (referred to by the newly freed object) to decrement their refcnt
                 * until they are also only pointed at by the cache.
                 */
                if (entry->o->refcnt == 1) {
                    stop = false;
                    pdfi_countdown(entry->o);
                    if (prev != NULL)
                        prev->next = next;
                    else
                        ctx->cache_LRU = next;
                    if (next)
                        next->previous = prev;
                    ctx->cache_entries--;
                    gs_free_object(ctx->memory, entry, "pdfi_free_context, free LRU");
                }
                entry = next;
            }
        } while (stop == false);

        entry = ctx->cache_LRU;
        while(entry) {
            next = entry->next;
            prev = entry->previous;
            count = entry->o->refcnt;
            dbgmprintf1(ctx->memory, "CLEANUP cache entry obj %d", entry->o->object_num);
            dbgmprintf1(ctx->memory, " has refcnt %d\n", count);
            entry = next;
        }
#else
        while(entry) {
            next = entry->next;
            pdfi_countdown(entry->o);
            ctx->cache_entries--;
            gs_free_object(ctx->memory, entry, "pdfi_free_context, free LRU");
            entry = next;
#if REFCNT_DEBUG
            ctx->cache_LRU = entry;
#endif
        }
#endif
        ctx->cache_LRU = ctx->cache_MRU = NULL;
        ctx->cache_entries = 0;
    }

    /* We can't free the font directory before the graphics library fonts fonts are freed, as they reference the font_dir.
     * graphics library fonts are refrenced from pdf_font objects, and those may be in the cache, which means they
     * won't be freed until we empty the cache. So we can't free 'font_dir' until after the cache has been cleared.
     */
    if (ctx->font_dir)
        gx_purge_selected_cached_chars(ctx->font_dir, pdfi_fontdir_purge_all, (void *)NULL);

    pdfi_countdown(ctx->pdffontmap);
    ctx->pdffontmap = NULL;

    return 0;
}

int pdfi_free_context(pdf_context *ctx)
{
    pdfi_clear_context(ctx);

    gs_free_object(ctx->memory, ctx->stack_bot, "pdfi_free_context");

    pdfi_free_name_table(ctx);

    /* And here we free the initial graphics state */
    while (ctx->pgs->saved)
        gs_grestore_only(ctx->pgs);

    gs_gstate_free(ctx->pgs);

    ctx->pgs = NULL;

    if (ctx->font_dir)
        gs_free_object(ctx->memory, ctx->font_dir, "pdfi_free_context");

    gs_free_object(ctx->memory, ctx, "pdfi_free_context");
    return 0;
}

/* These routines are used from the PostScript interpreter inteerface. It is important that
 * the 'interpreter part of the graphics state' should be a pdfi interpreter context while pdfi is running
 * but the PostScript itnerpreter context when the PostScript interpreter is running. If we are going
 * to inherit the PostScript graphics state for pdfi, then we need to turn it into a 'pdfi'
 * graphics state for the duration of the interpretation, and back to a PostScript one when
 * we return to the PostScript interpreter.
 *
 * Bizarrely it appears that the interpreter part of the gstate does not obey grestore, instead we copy
 * the 'current' context to the saved context when we do a grestore. This seems wrong to me, but
 * it seems to be what happens, so we can't rely on grestore to put back the interpreter context, but must
 * do so ourselves.
 *
 * Hence the 'from_PS' routine fills in pointers with the current context and procs, wit the expectation that
 * these will be saved and used to restore the data in the 'to_PS' routine.
 */
void pdfi_gstate_from_PS(pdf_context *ctx, gs_gstate *pgs, void **saved_client_data, gs_gstate_client_procs *saved_procs)
{
    *saved_client_data = pgs->client_data;
    *saved_procs = pgs->client_procs;
    pdfi_gstate_set_client(ctx, pgs);
    return;
}

void pdfi_gstate_to_PS(pdf_context *ctx, gs_gstate *pgs, void *client_data, const gs_gstate_client_procs *procs)
{
    pgs->client_procs.free(pgs->client_data, pgs->memory, pgs);
    pgs->client_data = NULL;
    gs_gstate_set_client(pgs, client_data, procs, true);
    return;
}
