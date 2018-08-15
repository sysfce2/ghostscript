/* Copyright (C) 2001-2018 Artifex Software, Inc.
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

/* Graphics state operations for the PDF interpreter */

#ifndef PDF_GSTATE_OPERATORS
#define PDF_GSTATE_OPERATORS

int pdfi_concat(pdf_context *ctx);
int pdfi_gsave(pdf_context *ctx);
int pdfi_grestore(pdf_context *ctx);
int pdfi_setlinewidth(pdf_context *ctx);
int pdfi_setlinejoin(pdf_context *ctx);
int pdfi_setlinecap(pdf_context *ctx);
int pdfi_setflat(pdf_context *ctx);
int pdfi_setdash(pdf_context *ctx);
int pdfi_setmiterlimit(pdf_context *ctx);

#endif
