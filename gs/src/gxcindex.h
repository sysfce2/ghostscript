/* Copyright (C) 1995, 1996, 1997 Aladdin Enterprises.  All rights reserved.

   This file is part of Aladdin Ghostscript.

   Aladdin Ghostscript is distributed with NO WARRANTY OF ANY KIND.  No author
   or distributor accepts any responsibility for the consequences of using it,
   or for whether it serves any particular purpose or works at all, unless he
   or she says so in writing.  Refer to the Aladdin Ghostscript Free Public
   License (the "License") for full details.

   Every copy of Aladdin Ghostscript must include a copy of the License,
   normally in a plain ASCII text file named PUBLIC.  The License grants you
   the right to copy, modify and redistribute Aladdin Ghostscript, but only
   under certain conditions described in the License.  Among other things, the
   License requires that the copyright notice and this notice be preserved on
   all copies.
 */


/* Define the device color index type and macros */

#ifndef gxcindex_INCLUDED
#  define gxcindex_INCLUDED

#include "gsbitops.h"		/* for sample_store macros */

/*
 * Internally, a (pure) device color is represented by opaque values of
 * type gx_color_index, which are tied to the specific device.  The driver
 * maps between these values and RGB[alpha] or CMYK values.  In this way,
 * the driver can convert RGB values to its most natural color representation,
 * and have the graphics library cache the result.
 */

/* Define the type for device color indices. */
typedef unsigned long gx_color_index;

#define arch_log2_sizeof_color_index arch_log2_sizeof_long
#define arch_sizeof_color_index arch_sizeof_long

/* Define the 'transparent' color index. */
#define gx_no_color_index_value (-1)	/* no cast -> can be used in #if */

/* The SGI C compiler provided with Irix 5.2 gives error messages */
/* if we use the proper definition of gx_no_color_index: */
/*#define gx_no_color_index ((gx_color_index)gx_no_color_index_value) */
/* Instead, we must spell out the typedef: */
#define gx_no_color_index ((unsigned long)gx_no_color_index_value)

/*
 * Define macros for accumulating a scan line of a colored image.
 * The usage is as follows:
 *      declare_line_accum(line, bpp, xo);
 *      for ( x = xo; x < xe; ++x )
 *        {     << compute color at x >>
 *              line_accum(color, bpp);
 *        }
 *      line_accum_copy(dev, line, bpp, xo, xe, raster, y);
 * This code must be enclosed in { }, since declare_line_accum declares
 * variables.  Supported values of bpp are 1, 2, 4, 8, 12, 16, 24, 32.
 *
 * Note that declare_line_accum declares the variables l_dptr, l_dbyte, l_dbit,
 * and l_xprev.  Other code in the loop may use these variables.
 */
#define declare_line_accum(line, bpp, xo)\
	sample_store_declare_setup(l_dptr, l_dbit, l_dbyte, line, 0, bpp);\
	int l_xprev = (xo)
#define line_accum(color, bpp)\
	sample_store_next32(color, l_dptr, l_dbit, bpp, l_dbyte)
#define line_accum_skip(bpp)\
	sample_store_skip_next(l_dptr, l_dbit, bpp, l_dbyte)
#define line_accum_store(bpp)\
	sample_store_flush(l_dptr, l_dbit, bpp, l_dbyte)
#define line_accum_copy(dev, line, bpp, xo, xe, raster, y)\
	if ( (xe) > l_xprev )\
	  { int code;\
	    line_accum_store(bpp);\
	    code = (*dev_proc(dev, copy_color))\
	      (dev, line, l_xprev - (xo), raster,\
	       gx_no_bitmap_id, l_xprev, y, (xe) - l_xprev, 1);\
	    if ( code < 0 )\
	      return code;\
	  }

#endif /* gxcindex_INCLUDED */
