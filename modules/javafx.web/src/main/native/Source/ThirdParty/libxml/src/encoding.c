/*
 * encoding.c : implements the encoding conversion functions needed for XML
 *
 * Related specs:
 * rfc2044        (UTF-8 and UTF-16) F. Yergeau Alis Technologies
 * rfc2781        UTF-16, an encoding of ISO 10646, P. Hoffman, F. Yergeau
 * [ISO-10646]    UTF-8 and UTF-16 in Annexes
 * [ISO-8859-1]   ISO Latin-1 characters codes.
 * [UNICODE]      The Unicode Consortium, "The Unicode Standard --
 *                Worldwide Character Encoding -- Version 1.0", Addison-
 *                Wesley, Volume 1, 1991, Volume 2, 1992.  UTF-8 is
 *                described in Unicode Technical Report #4.
 * [US-ASCII]     Coded Character Set--7-bit American Standard Code for
 *                Information Interchange, ANSI X3.4-1986.
 *
 * See Copyright for the status of this software.
 *
 * daniel@veillard.com
 *
 * Original code for IsoLatin1 and UTF-16 by "Martin J. Duerst" <duerst@w3.org>
 */

#define IN_LIBXML
#include "libxml.h"

#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <stdlib.h>

#ifdef LIBXML_ICONV_ENABLED
#include <errno.h>
#endif

#include <libxml/encoding.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#ifdef LIBXML_HTML_ENABLED
#include <libxml/HTMLparser.h>
#endif
#include <libxml/xmlerror.h>

#include "private/buf.h"
#include "private/enc.h"
#include "private/error.h"

#ifdef LIBXML_ICU_ENABLED
#include <unicode/ucnv.h>
/* Size of pivot buffer, same as icu/source/common/ucnv.cpp CHUNK_SIZE */
#define ICU_PIVOT_BUF_SIZE 1024
typedef struct _uconv_t uconv_t;
struct _uconv_t {
  UConverter *uconv; /* for conversion between an encoding and UTF-16 */
  UConverter *utf8; /* for conversion between UTF-8 and UTF-16 */
  UChar      pivot_buf[ICU_PIVOT_BUF_SIZE];
  UChar      *pivot_source;
  UChar      *pivot_target;
};
#endif

typedef struct _xmlCharEncodingAlias xmlCharEncodingAlias;
typedef xmlCharEncodingAlias *xmlCharEncodingAliasPtr;
struct _xmlCharEncodingAlias {
    const char *name;
    const char *alias;
};

static xmlCharEncodingAliasPtr xmlCharEncodingAliases = NULL;
static int xmlCharEncodingAliasesNb = 0;
static int xmlCharEncodingAliasesMax = 0;

static int xmlLittleEndian = 1;

/************************************************************************
 *                                    *
 *        Conversions To/From UTF8 encoding            *
 *                                    *
 ************************************************************************/

/**
 * asciiToUTF8:
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @in:  a pointer to an array of ASCII chars
 * @inlen:  the length of @in
 *
 * Take a block of ASCII chars in and try to convert it to an UTF-8
 * block of chars out.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 *
 * The value of @inlen after return is the number of octets consumed
 *     if the return value is positive, else unpredictable.
 * The value of @outlen after return is the number of octets produced.
 */
static int
asciiToUTF8(unsigned char* out, int *outlen,
              const unsigned char* in, int *inlen) {
    unsigned char* outstart = out;
    const unsigned char* base = in;
    const unsigned char* processed = in;
    unsigned char* outend = out + *outlen;
    const unsigned char* inend;
    unsigned int c;

    inend = in + (*inlen);
    while ((in < inend) && (out - outstart + 5 < *outlen)) {
    c= *in++;

        if (out >= outend)
        break;
        if (c < 0x80) {
        *out++ = c;
    } else {
        *outlen = out - outstart;
        *inlen = processed - base;
        return(XML_ENC_ERR_INPUT);
    }

    processed = (const unsigned char*) in;
    }
    *outlen = out - outstart;
    *inlen = processed - base;
    return(*outlen);
}

#ifdef LIBXML_OUTPUT_ENABLED
/**
 * UTF8Toascii:
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @in:  a pointer to an array of UTF-8 chars
 * @inlen:  the length of @in
 *
 * Take a block of UTF-8 chars in and try to convert it to an ASCII
 * block of chars out.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 *
 * The value of @inlen after return is the number of octets consumed
 *     if the return value is positive, else unpredictable.
 * The value of @outlen after return is the number of octets produced.
 */
static int
UTF8Toascii(unsigned char* out, int *outlen,
              const unsigned char* in, int *inlen) {
    const unsigned char* processed = in;
    const unsigned char* outend;
    const unsigned char* outstart = out;
    const unsigned char* instart = in;
    const unsigned char* inend;
    unsigned int c, d;
    int trailing;

    if ((out == NULL) || (outlen == NULL) || (inlen == NULL))
        return(XML_ENC_ERR_INTERNAL);
    if (in == NULL) {
        /*
     * initialization nothing to do
     */
    *outlen = 0;
    *inlen = 0;
    return(0);
    }
    inend = in + (*inlen);
    outend = out + (*outlen);
    while (in < inend) {
    d = *in++;
    if      (d < 0x80)  { c= d; trailing= 0; }
    else if (d < 0xC0) {
        /* trailing byte in leading position */
        *outlen = out - outstart;
        *inlen = processed - instart;
        return(XML_ENC_ERR_INPUT);
        } else if (d < 0xE0)  { c= d & 0x1F; trailing= 1; }
        else if (d < 0xF0)  { c= d & 0x0F; trailing= 2; }
        else if (d < 0xF8)  { c= d & 0x07; trailing= 3; }
    else {
        /* no chance for this in Ascii */
        *outlen = out - outstart;
        *inlen = processed - instart;
        return(XML_ENC_ERR_INPUT);
    }

    if (inend - in < trailing) {
        break;
    }

    for ( ; trailing; trailing--) {
        if ((in >= inend) || (((d= *in++) & 0xC0) != 0x80))
        break;
        c <<= 6;
        c |= d & 0x3F;
    }

    /* assertion: c is a single UTF-4 value */
    if (c < 0x80) {
        if (out >= outend)
        break;
        *out++ = c;
    } else {
        /* no chance for this in Ascii */
        *outlen = out - outstart;
        *inlen = processed - instart;
        return(XML_ENC_ERR_INPUT);
    }
    processed = in;
    }
    *outlen = out - outstart;
    *inlen = processed - instart;
    return(*outlen);
}
#endif /* LIBXML_OUTPUT_ENABLED */

/**
 * isolat1ToUTF8:
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @in:  a pointer to an array of ISO Latin 1 chars
 * @inlen:  the length of @in
 *
 * Take a block of ISO Latin 1 chars in and try to convert it to an UTF-8
 * block of chars out.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 *
 * The value of @inlen after return is the number of octets consumed
 *     if the return value is positive, else unpredictable.
 * The value of @outlen after return is the number of octets produced.
 */
int
isolat1ToUTF8(unsigned char* out, int *outlen,
              const unsigned char* in, int *inlen) {
    unsigned char* outstart = out;
    const unsigned char* base = in;
    unsigned char* outend;
    const unsigned char* inend;
    const unsigned char* instop;

    if ((out == NULL) || (in == NULL) || (outlen == NULL) || (inlen == NULL))
    return(XML_ENC_ERR_INTERNAL);

    outend = out + *outlen;
    inend = in + (*inlen);
    instop = inend;

    while ((in < inend) && (out < outend - 1)) {
    if (*in >= 0x80) {
        *out++ = (((*in) >>  6) & 0x1F) | 0xC0;
            *out++ = ((*in) & 0x3F) | 0x80;
        ++in;
    }
    if ((instop - in) > (outend - out)) instop = in + (outend - out);
    while ((in < instop) && (*in < 0x80)) {
        *out++ = *in++;
    }
    }
    if ((in < inend) && (out < outend) && (*in < 0x80)) {
        *out++ = *in++;
    }
    *outlen = out - outstart;
    *inlen = in - base;
    return(*outlen);
}

/**
 * UTF8ToUTF8:
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @inb:  a pointer to an array of UTF-8 chars
 * @inlenb:  the length of @in in UTF-8 chars
 *
 * No op copy operation for UTF8 handling.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 *
 *     The value of *inlen after return is the number of octets consumed
 *     if the return value is positive, else unpredictable.
 */
static int
UTF8ToUTF8(unsigned char* out, int *outlen,
           const unsigned char* inb, int *inlenb)
{
    int len;

    if ((out == NULL) || (outlen == NULL) || (inlenb == NULL))
    return(XML_ENC_ERR_INTERNAL);
    if (inb == NULL) {
        /* inb == NULL means output is initialized. */
        *outlen = 0;
        *inlenb = 0;
        return(0);
    }
    if (*outlen > *inlenb) {
    len = *inlenb;
    } else {
    len = *outlen;
    }
    if (len < 0)
    return(XML_ENC_ERR_INTERNAL);

    /*
     * FIXME: Conversion functions must assure valid UTF-8, so we have
     * to check for UTF-8 validity. Preferably, this converter shouldn't
     * be used at all.
     */
    memcpy(out, inb, len);

    *outlen = len;
    *inlenb = len;
    return(*outlen);
}


#ifdef LIBXML_OUTPUT_ENABLED
/**
 * UTF8Toisolat1:
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @in:  a pointer to an array of UTF-8 chars
 * @inlen:  the length of @in
 *
 * Take a block of UTF-8 chars in and try to convert it to an ISO Latin 1
 * block of chars out.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 *
 * The value of @inlen after return is the number of octets consumed
 *     if the return value is positive, else unpredictable.
 * The value of @outlen after return is the number of octets produced.
 */
int
UTF8Toisolat1(unsigned char* out, int *outlen,
              const unsigned char* in, int *inlen) {
    const unsigned char* processed = in;
    const unsigned char* outend;
    const unsigned char* outstart = out;
    const unsigned char* instart = in;
    const unsigned char* inend;
    unsigned int c, d;
    int trailing;

    if ((out == NULL) || (outlen == NULL) || (inlen == NULL))
        return(XML_ENC_ERR_INTERNAL);
    if (in == NULL) {
        /*
     * initialization nothing to do
     */
    *outlen = 0;
    *inlen = 0;
    return(0);
    }
    inend = in + (*inlen);
    outend = out + (*outlen);
    while (in < inend) {
    d = *in++;
    if      (d < 0x80)  { c= d; trailing= 0; }
    else if (d < 0xC0) {
        /* trailing byte in leading position */
        *outlen = out - outstart;
        *inlen = processed - instart;
        return(XML_ENC_ERR_INPUT);
        } else if (d < 0xE0)  { c= d & 0x1F; trailing= 1; }
        else if (d < 0xF0)  { c= d & 0x0F; trailing= 2; }
        else if (d < 0xF8)  { c= d & 0x07; trailing= 3; }
    else {
        /* no chance for this in IsoLat1 */
        *outlen = out - outstart;
        *inlen = processed - instart;
        return(XML_ENC_ERR_INPUT);
    }

    if (inend - in < trailing) {
        break;
    }

    for ( ; trailing; trailing--) {
        if (in >= inend)
        break;
        if (((d= *in++) & 0xC0) != 0x80) {
        *outlen = out - outstart;
        *inlen = processed - instart;
        return(XML_ENC_ERR_INPUT);
        }
        c <<= 6;
        c |= d & 0x3F;
    }

    /* assertion: c is a single UTF-4 value */
    if (c <= 0xFF) {
        if (out >= outend)
        break;
        *out++ = c;
    } else {
        /* no chance for this in IsoLat1 */
        *outlen = out - outstart;
        *inlen = processed - instart;
        return(XML_ENC_ERR_INPUT);
    }
    processed = in;
    }
    *outlen = out - outstart;
    *inlen = processed - instart;
    return(*outlen);
}
#endif /* LIBXML_OUTPUT_ENABLED */

/**
 * UTF16LEToUTF8:
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @inb:  a pointer to an array of UTF-16LE passwd as a byte array
 * @inlenb:  the length of @in in UTF-16LE chars
 *
 * Take a block of UTF-16LE ushorts in and try to convert it to an UTF-8
 * block of chars out. This function assumes the endian property
 * is the same between the native type of this machine and the
 * inputed one.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 *
 * The value of *inlen after return is the number of octets consumed
 * if the return value is positive, else unpredictable.
 */
static int
UTF16LEToUTF8(unsigned char* out, int *outlen,
            const unsigned char* inb, int *inlenb)
{
    unsigned char* outstart = out;
    const unsigned char* processed = inb;
    unsigned char* outend;
    unsigned short* in = (unsigned short *) (void *) inb;
    unsigned short* inend;
    unsigned int c, d, inlen;
    unsigned char *tmp;
    int bits;

    if (*outlen == 0) {
        *inlenb = 0;
        return(0);
    }
    outend = out + *outlen;
    if ((*inlenb % 2) == 1)
        (*inlenb)--;
    inlen = *inlenb / 2;
    inend = in + inlen;
    while ((in < inend) && (out - outstart + 5 < *outlen)) {
        if (xmlLittleEndian) {
        c= *in++;
    } else {
        tmp = (unsigned char *) in;
        c = *tmp++;
        c = c | (*tmp << 8);
        in++;
    }
        if ((c & 0xFC00) == 0xD800) {    /* surrogates */
        if (in >= inend) {           /* handle split mutli-byte characters */
        break;
        }
        if (xmlLittleEndian) {
        d = *in++;
        } else {
        tmp = (unsigned char *) in;
        d = *tmp++;
        d = d | (*tmp << 8);
        in++;
        }
            if ((d & 0xFC00) == 0xDC00) {
                c &= 0x03FF;
                c <<= 10;
                c |= d & 0x03FF;
                c += 0x10000;
            }
            else {
        *outlen = out - outstart;
        *inlenb = processed - inb;
            return(XML_ENC_ERR_INPUT);
        }
        }

    /* assertion: c is a single UTF-4 value */
        if (out >= outend)
        break;
        if      (c <    0x80) {  *out++=  c;                bits= -6; }
        else if (c <   0x800) {  *out++= ((c >>  6) & 0x1F) | 0xC0;  bits=  0; }
        else if (c < 0x10000) {  *out++= ((c >> 12) & 0x0F) | 0xE0;  bits=  6; }
        else                  {  *out++= ((c >> 18) & 0x07) | 0xF0;  bits= 12; }

        for ( ; bits >= 0; bits-= 6) {
            if (out >= outend)
            break;
            *out++= ((c >> bits) & 0x3F) | 0x80;
        }
    processed = (const unsigned char*) in;
    }
    *outlen = out - outstart;
    *inlenb = processed - inb;
    return(*outlen);
}

#ifdef LIBXML_OUTPUT_ENABLED
/**
 * UTF8ToUTF16LE:
 * @outb:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @outb
 * @in:  a pointer to an array of UTF-8 chars
 * @inlen:  the length of @in
 *
 * Take a block of UTF-8 chars in and try to convert it to an UTF-16LE
 * block of chars out.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 */
static int
UTF8ToUTF16LE(unsigned char* outb, int *outlen,
            const unsigned char* in, int *inlen)
{
    unsigned short* out = (unsigned short *) (void *) outb;
    const unsigned char* processed = in;
    const unsigned char *const instart = in;
    unsigned short* outstart= out;
    unsigned short* outend;
    const unsigned char* inend;
    unsigned int c, d;
    int trailing;
    unsigned char *tmp;
    unsigned short tmp1, tmp2;

    /* UTF16LE encoding has no BOM */
    if ((out == NULL) || (outlen == NULL) || (inlen == NULL))
        return(XML_ENC_ERR_INTERNAL);
    if (in == NULL) {
    *outlen = 0;
    *inlen = 0;
    return(0);
    }
    inend= in + *inlen;
    outend = out + (*outlen / 2);
    while (in < inend) {
      d= *in++;
      if      (d < 0x80)  { c= d; trailing= 0; }
      else if (d < 0xC0) {
          /* trailing byte in leading position */
      *outlen = (out - outstart) * 2;
      *inlen = processed - instart;
      return(XML_ENC_ERR_INPUT);
      } else if (d < 0xE0)  { c= d & 0x1F; trailing= 1; }
      else if (d < 0xF0)  { c= d & 0x0F; trailing= 2; }
      else if (d < 0xF8)  { c= d & 0x07; trailing= 3; }
      else {
    /* no chance for this in UTF-16 */
    *outlen = (out - outstart) * 2;
    *inlen = processed - instart;
    return(XML_ENC_ERR_INPUT);
      }

      if (inend - in < trailing) {
          break;
      }

      for ( ; trailing; trailing--) {
          if ((in >= inend) || (((d= *in++) & 0xC0) != 0x80))
          break;
          c <<= 6;
          c |= d & 0x3F;
      }

      /* assertion: c is a single UTF-4 value */
        if (c < 0x10000) {
            if (out >= outend)
            break;
        if (xmlLittleEndian) {
        *out++ = c;
        } else {
        tmp = (unsigned char *) out;
        *tmp = (unsigned char) c; /* Explicit truncation */
        *(tmp + 1) = c >> 8 ;
        out++;
        }
        }
        else if (c < 0x110000) {
            if (out+1 >= outend)
            break;
            c -= 0x10000;
        if (xmlLittleEndian) {
        *out++ = 0xD800 | (c >> 10);
        *out++ = 0xDC00 | (c & 0x03FF);
        } else {
        tmp1 = 0xD800 | (c >> 10);
        tmp = (unsigned char *) out;
        *tmp = (unsigned char) tmp1; /* Explicit truncation */
        *(tmp + 1) = tmp1 >> 8;
        out++;

        tmp2 = 0xDC00 | (c & 0x03FF);
        tmp = (unsigned char *) out;
        *tmp  = (unsigned char) tmp2; /* Explicit truncation */
        *(tmp + 1) = tmp2 >> 8;
        out++;
        }
        }
        else
        break;
    processed = in;
    }
    *outlen = (out - outstart) * 2;
    *inlen = processed - instart;
    return(*outlen);
}

/**
 * UTF8ToUTF16:
 * @outb:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @outb
 * @in:  a pointer to an array of UTF-8 chars
 * @inlen:  the length of @in
 *
 * Take a block of UTF-8 chars in and try to convert it to an UTF-16
 * block of chars out.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 */
static int
UTF8ToUTF16(unsigned char* outb, int *outlen,
            const unsigned char* in, int *inlen)
{
    if (in == NULL) {
    /*
     * initialization, add the Byte Order Mark for UTF-16LE
     */
        if (*outlen >= 2) {
        outb[0] = 0xFF;
        outb[1] = 0xFE;
        *outlen = 2;
        *inlen = 0;
        return(2);
    }
    *outlen = 0;
    *inlen = 0;
    return(0);
    }
    return (UTF8ToUTF16LE(outb, outlen, in, inlen));
}
#endif /* LIBXML_OUTPUT_ENABLED */

/**
 * UTF16BEToUTF8:
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @inb:  a pointer to an array of UTF-16 passed as a byte array
 * @inlenb:  the length of @in in UTF-16 chars
 *
 * Take a block of UTF-16 ushorts in and try to convert it to an UTF-8
 * block of chars out. This function assumes the endian property
 * is the same between the native type of this machine and the
 * inputed one.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 *
 * The value of *inlen after return is the number of octets consumed
 * if the return value is positive, else unpredictable.
 */
static int
UTF16BEToUTF8(unsigned char* out, int *outlen,
            const unsigned char* inb, int *inlenb)
{
    unsigned char* outstart = out;
    const unsigned char* processed = inb;
    unsigned char* outend;
    unsigned short* in = (unsigned short *) (void *) inb;
    unsigned short* inend;
    unsigned int c, d, inlen;
    unsigned char *tmp;
    int bits;

    if (*outlen == 0) {
        *inlenb = 0;
        return(0);
    }
    outend = out + *outlen;
    if ((*inlenb % 2) == 1)
        (*inlenb)--;
    inlen = *inlenb / 2;
    inend= in + inlen;
    while ((in < inend) && (out - outstart + 5 < *outlen)) {
    if (xmlLittleEndian) {
        tmp = (unsigned char *) in;
        c = *tmp++;
        c = (c << 8) | *tmp;
        in++;
    } else {
        c= *in++;
    }
        if ((c & 0xFC00) == 0xD800) {    /* surrogates */
        if (in >= inend) {           /* handle split mutli-byte characters */
                break;
        }
        if (xmlLittleEndian) {
        tmp = (unsigned char *) in;
        d = *tmp++;
        d = (d << 8) | *tmp;
        in++;
        } else {
        d= *in++;
        }
            if ((d & 0xFC00) == 0xDC00) {
                c &= 0x03FF;
                c <<= 10;
                c |= d & 0x03FF;
                c += 0x10000;
            }
            else {
        *outlen = out - outstart;
        *inlenb = processed - inb;
            return(XML_ENC_ERR_INPUT);
        }
        }

    /* assertion: c is a single UTF-4 value */
        if (out >= outend)
        break;
        if      (c <    0x80) {  *out++=  c;                bits= -6; }
        else if (c <   0x800) {  *out++= ((c >>  6) & 0x1F) | 0xC0;  bits=  0; }
        else if (c < 0x10000) {  *out++= ((c >> 12) & 0x0F) | 0xE0;  bits=  6; }
        else                  {  *out++= ((c >> 18) & 0x07) | 0xF0;  bits= 12; }

        for ( ; bits >= 0; bits-= 6) {
            if (out >= outend)
            break;
            *out++= ((c >> bits) & 0x3F) | 0x80;
        }
    processed = (const unsigned char*) in;
    }
    *outlen = out - outstart;
    *inlenb = processed - inb;
    return(*outlen);
}

#ifdef LIBXML_OUTPUT_ENABLED
/**
 * UTF8ToUTF16BE:
 * @outb:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @outb
 * @in:  a pointer to an array of UTF-8 chars
 * @inlen:  the length of @in
 *
 * Take a block of UTF-8 chars in and try to convert it to an UTF-16BE
 * block of chars out.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 */
static int
UTF8ToUTF16BE(unsigned char* outb, int *outlen,
            const unsigned char* in, int *inlen)
{
    unsigned short* out = (unsigned short *) (void *) outb;
    const unsigned char* processed = in;
    const unsigned char *const instart = in;
    unsigned short* outstart= out;
    unsigned short* outend;
    const unsigned char* inend;
    unsigned int c, d;
    int trailing;
    unsigned char *tmp;
    unsigned short tmp1, tmp2;

    /* UTF-16BE has no BOM */
    if ((outb == NULL) || (outlen == NULL) || (inlen == NULL))
        return(XML_ENC_ERR_INTERNAL);
    if (in == NULL) {
    *outlen = 0;
    *inlen = 0;
    return(0);
    }
    inend= in + *inlen;
    outend = out + (*outlen / 2);
    while (in < inend) {
      d= *in++;
      if      (d < 0x80)  { c= d; trailing= 0; }
      else if (d < 0xC0)  {
          /* trailing byte in leading position */
      *outlen = out - outstart;
      *inlen = processed - instart;
      return(XML_ENC_ERR_INPUT);
      } else if (d < 0xE0)  { c= d & 0x1F; trailing= 1; }
      else if (d < 0xF0)  { c= d & 0x0F; trailing= 2; }
      else if (d < 0xF8)  { c= d & 0x07; trailing= 3; }
      else {
          /* no chance for this in UTF-16 */
      *outlen = out - outstart;
      *inlen = processed - instart;
      return(XML_ENC_ERR_INPUT);
      }

      if (inend - in < trailing) {
          break;
      }

      for ( ; trailing; trailing--) {
          if ((in >= inend) || (((d= *in++) & 0xC0) != 0x80))  break;
          c <<= 6;
          c |= d & 0x3F;
      }

      /* assertion: c is a single UTF-4 value */
        if (c < 0x10000) {
            if (out >= outend)  break;
        if (xmlLittleEndian) {
        tmp = (unsigned char *) out;
        *tmp = c >> 8;
        *(tmp + 1) = (unsigned char) c; /* Explicit truncation */
        out++;
        } else {
        *out++ = c;
        }
        }
        else if (c < 0x110000) {
            if (out+1 >= outend)  break;
            c -= 0x10000;
        if (xmlLittleEndian) {
        tmp1 = 0xD800 | (c >> 10);
        tmp = (unsigned char *) out;
        *tmp = tmp1 >> 8;
        *(tmp + 1) = (unsigned char) tmp1; /* Explicit truncation */
        out++;

        tmp2 = 0xDC00 | (c & 0x03FF);
        tmp = (unsigned char *) out;
        *tmp = tmp2 >> 8;
        *(tmp + 1) = (unsigned char) tmp2; /* Explicit truncation */
        out++;
        } else {
        *out++ = 0xD800 | (c >> 10);
        *out++ = 0xDC00 | (c & 0x03FF);
        }
        }
        else
        break;
    processed = in;
    }
    *outlen = (out - outstart) * 2;
    *inlen = processed - instart;
    return(*outlen);
}
#endif /* LIBXML_OUTPUT_ENABLED */

/************************************************************************
 *                                    *
 *        Generic encoding handling routines            *
 *                                    *
 ************************************************************************/

/**
 * xmlDetectCharEncoding:
 * @in:  a pointer to the first bytes of the XML entity, must be at least
 *       2 bytes long (at least 4 if encoding is UTF4 variant).
 * @len:  pointer to the length of the buffer
 *
 * Guess the encoding of the entity using the first bytes of the entity content
 * according to the non-normative appendix F of the XML-1.0 recommendation.
 *
 * Returns one of the XML_CHAR_ENCODING_... values.
 */
xmlCharEncoding
xmlDetectCharEncoding(const unsigned char* in, int len)
{
    if (in == NULL)
        return(XML_CHAR_ENCODING_NONE);
    if (len >= 4) {
    if ((in[0] == 0x00) && (in[1] == 0x00) &&
        (in[2] == 0x00) && (in[3] == 0x3C))
        return(XML_CHAR_ENCODING_UCS4BE);
    if ((in[0] == 0x3C) && (in[1] == 0x00) &&
        (in[2] == 0x00) && (in[3] == 0x00))
        return(XML_CHAR_ENCODING_UCS4LE);
    if ((in[0] == 0x00) && (in[1] == 0x00) &&
        (in[2] == 0x3C) && (in[3] == 0x00))
        return(XML_CHAR_ENCODING_UCS4_2143);
    if ((in[0] == 0x00) && (in[1] == 0x3C) &&
        (in[2] == 0x00) && (in[3] == 0x00))
        return(XML_CHAR_ENCODING_UCS4_3412);
    if ((in[0] == 0x4C) && (in[1] == 0x6F) &&
        (in[2] == 0xA7) && (in[3] == 0x94))
        return(XML_CHAR_ENCODING_EBCDIC);
    if ((in[0] == 0x3C) && (in[1] == 0x3F) &&
        (in[2] == 0x78) && (in[3] == 0x6D))
        return(XML_CHAR_ENCODING_UTF8);
    /*
     * Although not part of the recommendation, we also
     * attempt an "auto-recognition" of UTF-16LE and
     * UTF-16BE encodings.
     */
    if ((in[0] == 0x3C) && (in[1] == 0x00) &&
        (in[2] == 0x3F) && (in[3] == 0x00))
        return(XML_CHAR_ENCODING_UTF16LE);
    if ((in[0] == 0x00) && (in[1] == 0x3C) &&
        (in[2] == 0x00) && (in[3] == 0x3F))
        return(XML_CHAR_ENCODING_UTF16BE);
    }
    if (len >= 3) {
    /*
     * Errata on XML-1.0 June 20 2001
     * We now allow an UTF8 encoded BOM
     */
    if ((in[0] == 0xEF) && (in[1] == 0xBB) &&
        (in[2] == 0xBF))
        return(XML_CHAR_ENCODING_UTF8);
    }
    /* For UTF-16 we can recognize by the BOM */
    if (len >= 2) {
    if ((in[0] == 0xFE) && (in[1] == 0xFF))
        return(XML_CHAR_ENCODING_UTF16BE);
    if ((in[0] == 0xFF) && (in[1] == 0xFE))
        return(XML_CHAR_ENCODING_UTF16LE);
    }
    return(XML_CHAR_ENCODING_NONE);
}

/**
 * xmlCleanupEncodingAliases:
 *
 * Unregisters all aliases
 */
void
xmlCleanupEncodingAliases(void) {
    int i;

    if (xmlCharEncodingAliases == NULL)
    return;

    for (i = 0;i < xmlCharEncodingAliasesNb;i++) {
    if (xmlCharEncodingAliases[i].name != NULL)
        xmlFree((char *) xmlCharEncodingAliases[i].name);
    if (xmlCharEncodingAliases[i].alias != NULL)
        xmlFree((char *) xmlCharEncodingAliases[i].alias);
    }
    xmlCharEncodingAliasesNb = 0;
    xmlCharEncodingAliasesMax = 0;
    xmlFree(xmlCharEncodingAliases);
    xmlCharEncodingAliases = NULL;
}

/**
 * xmlGetEncodingAlias:
 * @alias:  the alias name as parsed, in UTF-8 format (ASCII actually)
 *
 * Lookup an encoding name for the given alias.
 *
 * Returns NULL if not found, otherwise the original name
 */
const char *
xmlGetEncodingAlias(const char *alias) {
    int i;
    char upper[100];

    if (alias == NULL)
    return(NULL);

    if (xmlCharEncodingAliases == NULL)
    return(NULL);

    for (i = 0;i < 99;i++) {
        upper[i] = (char) toupper((unsigned char) alias[i]);
    if (upper[i] == 0) break;
    }
    upper[i] = 0;

    /*
     * Walk down the list looking for a definition of the alias
     */
    for (i = 0;i < xmlCharEncodingAliasesNb;i++) {
    if (!strcmp(xmlCharEncodingAliases[i].alias, upper)) {
        return(xmlCharEncodingAliases[i].name);
    }
    }
    return(NULL);
}

/**
 * xmlAddEncodingAlias:
 * @name:  the encoding name as parsed, in UTF-8 format (ASCII actually)
 * @alias:  the alias name as parsed, in UTF-8 format (ASCII actually)
 *
 * Registers an alias @alias for an encoding named @name. Existing alias
 * will be overwritten.
 *
 * Returns 0 in case of success, -1 in case of error
 */
int
xmlAddEncodingAlias(const char *name, const char *alias) {
    int i;
    char upper[100];
    char *nameCopy, *aliasCopy;

    if ((name == NULL) || (alias == NULL))
    return(-1);

    for (i = 0;i < 99;i++) {
        upper[i] = (char) toupper((unsigned char) alias[i]);
    if (upper[i] == 0) break;
    }
    upper[i] = 0;

    if (xmlCharEncodingAliasesNb >= xmlCharEncodingAliasesMax) {
        xmlCharEncodingAliasPtr tmp;
        size_t newSize = xmlCharEncodingAliasesMax ?
                         xmlCharEncodingAliasesMax * 2 :
                         20;

        tmp = (xmlCharEncodingAliasPtr)
              xmlRealloc(xmlCharEncodingAliases,
                         newSize * sizeof(xmlCharEncodingAlias));
        if (tmp == NULL)
            return(-1);
        xmlCharEncodingAliases = tmp;
        xmlCharEncodingAliasesMax = newSize;
    }

    /*
     * Walk down the list looking for a definition of the alias
     */
    for (i = 0;i < xmlCharEncodingAliasesNb;i++) {
    if (!strcmp(xmlCharEncodingAliases[i].alias, upper)) {
        /*
         * Replace the definition.
         */
        nameCopy = xmlMemStrdup(name);
            if (nameCopy == NULL)
                return(-1);
        xmlFree((char *) xmlCharEncodingAliases[i].name);
        xmlCharEncodingAliases[i].name = nameCopy;
        return(0);
    }
    }
    /*
     * Add the definition
     */
    nameCopy = xmlMemStrdup(name);
    if (nameCopy == NULL)
        return(-1);
    aliasCopy = xmlMemStrdup(upper);
    if (aliasCopy == NULL) {
        xmlFree(nameCopy);
        return(-1);
    }
    xmlCharEncodingAliases[xmlCharEncodingAliasesNb].name = nameCopy;
    xmlCharEncodingAliases[xmlCharEncodingAliasesNb].alias = aliasCopy;
    xmlCharEncodingAliasesNb++;
    return(0);
}

/**
 * xmlDelEncodingAlias:
 * @alias:  the alias name as parsed, in UTF-8 format (ASCII actually)
 *
 * Unregisters an encoding alias @alias
 *
 * Returns 0 in case of success, -1 in case of error
 */
int
xmlDelEncodingAlias(const char *alias) {
    int i;

    if (alias == NULL)
    return(-1);

    if (xmlCharEncodingAliases == NULL)
    return(-1);
    /*
     * Walk down the list looking for a definition of the alias
     */
    for (i = 0;i < xmlCharEncodingAliasesNb;i++) {
    if (!strcmp(xmlCharEncodingAliases[i].alias, alias)) {
        xmlFree((char *) xmlCharEncodingAliases[i].name);
        xmlFree((char *) xmlCharEncodingAliases[i].alias);
        xmlCharEncodingAliasesNb--;
        memmove(&xmlCharEncodingAliases[i], &xmlCharEncodingAliases[i + 1],
            sizeof(xmlCharEncodingAlias) * (xmlCharEncodingAliasesNb - i));
        return(0);
    }
    }
    return(-1);
}

/**
 * xmlParseCharEncoding:
 * @name:  the encoding name as parsed, in UTF-8 format (ASCII actually)
 *
 * Compare the string to the encoding schemes already known. Note
 * that the comparison is case insensitive accordingly to the section
 * [XML] 4.3.3 Character Encoding in Entities.
 *
 * Returns one of the XML_CHAR_ENCODING_... values or XML_CHAR_ENCODING_NONE
 * if not recognized.
 */
xmlCharEncoding
xmlParseCharEncoding(const char* name)
{
    const char *alias;
    char upper[500];
    int i;

    if (name == NULL)
    return(XML_CHAR_ENCODING_NONE);

    /*
     * Do the alias resolution
     */
    alias = xmlGetEncodingAlias(name);
    if (alias != NULL)
    name = alias;

    for (i = 0;i < 499;i++) {
        upper[i] = (char) toupper((unsigned char) name[i]);
    if (upper[i] == 0) break;
    }
    upper[i] = 0;

    if (!strcmp(upper, "")) return(XML_CHAR_ENCODING_NONE);
    if (!strcmp(upper, "UTF-8")) return(XML_CHAR_ENCODING_UTF8);
    if (!strcmp(upper, "UTF8")) return(XML_CHAR_ENCODING_UTF8);

    /*
     * NOTE: if we were able to parse this, the endianness of UTF16 is
     *       already found and in use
     */
    if (!strcmp(upper, "UTF-16")) return(XML_CHAR_ENCODING_UTF16LE);
    if (!strcmp(upper, "UTF16")) return(XML_CHAR_ENCODING_UTF16LE);

    if (!strcmp(upper, "ISO-10646-UCS-2")) return(XML_CHAR_ENCODING_UCS2);
    if (!strcmp(upper, "UCS-2")) return(XML_CHAR_ENCODING_UCS2);
    if (!strcmp(upper, "UCS2")) return(XML_CHAR_ENCODING_UCS2);

    /*
     * NOTE: if we were able to parse this, the endianness of UCS4 is
     *       already found and in use
     */
    if (!strcmp(upper, "ISO-10646-UCS-4")) return(XML_CHAR_ENCODING_UCS4LE);
    if (!strcmp(upper, "UCS-4")) return(XML_CHAR_ENCODING_UCS4LE);
    if (!strcmp(upper, "UCS4")) return(XML_CHAR_ENCODING_UCS4LE);


    if (!strcmp(upper,  "ISO-8859-1")) return(XML_CHAR_ENCODING_8859_1);
    if (!strcmp(upper,  "ISO-LATIN-1")) return(XML_CHAR_ENCODING_8859_1);
    if (!strcmp(upper,  "ISO LATIN 1")) return(XML_CHAR_ENCODING_8859_1);

    if (!strcmp(upper,  "ISO-8859-2")) return(XML_CHAR_ENCODING_8859_2);
    if (!strcmp(upper,  "ISO-LATIN-2")) return(XML_CHAR_ENCODING_8859_2);
    if (!strcmp(upper,  "ISO LATIN 2")) return(XML_CHAR_ENCODING_8859_2);

    if (!strcmp(upper,  "ISO-8859-3")) return(XML_CHAR_ENCODING_8859_3);
    if (!strcmp(upper,  "ISO-8859-4")) return(XML_CHAR_ENCODING_8859_4);
    if (!strcmp(upper,  "ISO-8859-5")) return(XML_CHAR_ENCODING_8859_5);
    if (!strcmp(upper,  "ISO-8859-6")) return(XML_CHAR_ENCODING_8859_6);
    if (!strcmp(upper,  "ISO-8859-7")) return(XML_CHAR_ENCODING_8859_7);
    if (!strcmp(upper,  "ISO-8859-8")) return(XML_CHAR_ENCODING_8859_8);
    if (!strcmp(upper,  "ISO-8859-9")) return(XML_CHAR_ENCODING_8859_9);

    if (!strcmp(upper, "ISO-2022-JP")) return(XML_CHAR_ENCODING_2022_JP);
    if (!strcmp(upper, "SHIFT_JIS")) return(XML_CHAR_ENCODING_SHIFT_JIS);
    if (!strcmp(upper, "EUC-JP")) return(XML_CHAR_ENCODING_EUC_JP);

    return(XML_CHAR_ENCODING_ERROR);
}

/**
 * xmlGetCharEncodingName:
 * @enc:  the encoding
 *
 * The "canonical" name for XML encoding.
 * C.f. http://www.w3.org/TR/REC-xml#charencoding
 * Section 4.3.3  Character Encoding in Entities
 *
 * Returns the canonical name for the given encoding
 */

const char*
xmlGetCharEncodingName(xmlCharEncoding enc) {
    switch (enc) {
        case XML_CHAR_ENCODING_ERROR:
        return(NULL);
        case XML_CHAR_ENCODING_NONE:
        return(NULL);
        case XML_CHAR_ENCODING_UTF8:
        return("UTF-8");
        case XML_CHAR_ENCODING_UTF16LE:
        return("UTF-16");
        case XML_CHAR_ENCODING_UTF16BE:
        return("UTF-16");
        case XML_CHAR_ENCODING_EBCDIC:
            return("EBCDIC");
        case XML_CHAR_ENCODING_UCS4LE:
            return("ISO-10646-UCS-4");
        case XML_CHAR_ENCODING_UCS4BE:
            return("ISO-10646-UCS-4");
        case XML_CHAR_ENCODING_UCS4_2143:
            return("ISO-10646-UCS-4");
        case XML_CHAR_ENCODING_UCS4_3412:
            return("ISO-10646-UCS-4");
        case XML_CHAR_ENCODING_UCS2:
            return("ISO-10646-UCS-2");
        case XML_CHAR_ENCODING_8859_1:
        return("ISO-8859-1");
        case XML_CHAR_ENCODING_8859_2:
        return("ISO-8859-2");
        case XML_CHAR_ENCODING_8859_3:
        return("ISO-8859-3");
        case XML_CHAR_ENCODING_8859_4:
        return("ISO-8859-4");
        case XML_CHAR_ENCODING_8859_5:
        return("ISO-8859-5");
        case XML_CHAR_ENCODING_8859_6:
        return("ISO-8859-6");
        case XML_CHAR_ENCODING_8859_7:
        return("ISO-8859-7");
        case XML_CHAR_ENCODING_8859_8:
        return("ISO-8859-8");
        case XML_CHAR_ENCODING_8859_9:
        return("ISO-8859-9");
        case XML_CHAR_ENCODING_2022_JP:
            return("ISO-2022-JP");
        case XML_CHAR_ENCODING_SHIFT_JIS:
            return("Shift-JIS");
        case XML_CHAR_ENCODING_EUC_JP:
            return("EUC-JP");
    case XML_CHAR_ENCODING_ASCII:
        return(NULL);
    }
    return(NULL);
}

/************************************************************************
 *                                    *
 *            Char encoding handlers                *
 *                                    *
 ************************************************************************/

#if !defined(LIBXML_ICONV_ENABLED) && !defined(LIBXML_ICU_ENABLED) && \
    defined(LIBXML_ISO8859X_ENABLED)

#define DECLARE_ISO_FUNCS(n) \
    static int ISO8859_##n##ToUTF8(unsigned char* out, int *outlen, \
                                   const unsigned char* in, int *inlen); \
    static int UTF8ToISO8859_##n(unsigned char* out, int *outlen, \
                                 const unsigned char* in, int *inlen);

/** DOC_DISABLE */
DECLARE_ISO_FUNCS(2)
DECLARE_ISO_FUNCS(3)
DECLARE_ISO_FUNCS(4)
DECLARE_ISO_FUNCS(5)
DECLARE_ISO_FUNCS(6)
DECLARE_ISO_FUNCS(7)
DECLARE_ISO_FUNCS(8)
DECLARE_ISO_FUNCS(9)
DECLARE_ISO_FUNCS(10)
DECLARE_ISO_FUNCS(11)
DECLARE_ISO_FUNCS(13)
DECLARE_ISO_FUNCS(14)
DECLARE_ISO_FUNCS(15)
DECLARE_ISO_FUNCS(16)
/** DOC_ENABLE */

#endif /* LIBXML_ISO8859X_ENABLED */

#ifdef LIBXML_ICONV_ENABLED
  #define EMPTY_ICONV , (iconv_t) -1, (iconv_t) -1
#else
  #define EMPTY_ICONV
#endif

#ifdef LIBXML_ICU_ENABLED
  #define EMPTY_UCONV , NULL, NULL
#else
  #define EMPTY_UCONV
#endif

#define MAKE_HANDLER(name, in, out) \
    { (char *) name, in, out EMPTY_ICONV EMPTY_UCONV }

static const xmlCharEncodingHandler defaultHandlers[] = {
#ifdef LIBXML_OUTPUT_ENABLED
    MAKE_HANDLER("UTF-16LE", UTF16LEToUTF8, UTF8ToUTF16LE)
    ,MAKE_HANDLER("UTF-16BE", UTF16BEToUTF8, UTF8ToUTF16BE)
    ,MAKE_HANDLER("UTF-16", UTF16LEToUTF8, UTF8ToUTF16)
    ,MAKE_HANDLER("ISO-8859-1", isolat1ToUTF8, UTF8Toisolat1)
    ,MAKE_HANDLER("ASCII", asciiToUTF8, UTF8Toascii)
    ,MAKE_HANDLER("US-ASCII", asciiToUTF8, UTF8Toascii)
#ifdef LIBXML_HTML_ENABLED
    ,MAKE_HANDLER("HTML", NULL, UTF8ToHtml)
#endif
#else
    MAKE_HANDLER("UTF-16LE", UTF16LEToUTF8, NULL)
    ,MAKE_HANDLER("UTF-16BE", UTF16BEToUTF8, NULL)
    ,MAKE_HANDLER("UTF-16", UTF16LEToUTF8, NULL)
    ,MAKE_HANDLER("ISO-8859-1", isolat1ToUTF8, NULL)
    ,MAKE_HANDLER("ASCII", asciiToUTF8, NULL)
    ,MAKE_HANDLER("US-ASCII", asciiToUTF8, NULL)
#endif /* LIBXML_OUTPUT_ENABLED */

#if !defined(LIBXML_ICONV_ENABLED) && !defined(LIBXML_ICU_ENABLED) && \
    defined(LIBXML_ISO8859X_ENABLED)
    ,MAKE_HANDLER("ISO-8859-2", ISO8859_2ToUTF8, UTF8ToISO8859_2)
    ,MAKE_HANDLER("ISO-8859-3", ISO8859_3ToUTF8, UTF8ToISO8859_3)
    ,MAKE_HANDLER("ISO-8859-4", ISO8859_4ToUTF8, UTF8ToISO8859_4)
    ,MAKE_HANDLER("ISO-8859-5", ISO8859_5ToUTF8, UTF8ToISO8859_5)
    ,MAKE_HANDLER("ISO-8859-6", ISO8859_6ToUTF8, UTF8ToISO8859_6)
    ,MAKE_HANDLER("ISO-8859-7", ISO8859_7ToUTF8, UTF8ToISO8859_7)
    ,MAKE_HANDLER("ISO-8859-8", ISO8859_8ToUTF8, UTF8ToISO8859_8)
    ,MAKE_HANDLER("ISO-8859-9", ISO8859_9ToUTF8, UTF8ToISO8859_9)
    ,MAKE_HANDLER("ISO-8859-10", ISO8859_10ToUTF8, UTF8ToISO8859_10)
    ,MAKE_HANDLER("ISO-8859-11", ISO8859_11ToUTF8, UTF8ToISO8859_11)
    ,MAKE_HANDLER("ISO-8859-13", ISO8859_13ToUTF8, UTF8ToISO8859_13)
    ,MAKE_HANDLER("ISO-8859-14", ISO8859_14ToUTF8, UTF8ToISO8859_14)
    ,MAKE_HANDLER("ISO-8859-15", ISO8859_15ToUTF8, UTF8ToISO8859_15)
    ,MAKE_HANDLER("ISO-8859-16", ISO8859_16ToUTF8, UTF8ToISO8859_16)
#endif
};

#define NUM_DEFAULT_HANDLERS \
    (sizeof(defaultHandlers) / sizeof(defaultHandlers[0]))

static const xmlCharEncodingHandler xmlUTF8Handler =
    MAKE_HANDLER("UTF-8", UTF8ToUTF8, UTF8ToUTF8);

static const xmlCharEncodingHandler *xmlUTF16LEHandler = &defaultHandlers[0];
static const xmlCharEncodingHandler *xmlUTF16BEHandler = &defaultHandlers[1];
static const xmlCharEncodingHandler *xmlLatin1Handler = &defaultHandlers[3];
static const xmlCharEncodingHandler *xmlAsciiHandler = &defaultHandlers[4];

/* the size should be growable, but it's not a big deal ... */
#define MAX_ENCODING_HANDLERS 50
static xmlCharEncodingHandlerPtr *handlers = NULL;
static int nbCharEncodingHandler = 0;

/**
 * xmlNewCharEncodingHandler:
 * @name:  the encoding name, in UTF-8 format (ASCII actually)
 * @input:  the xmlCharEncodingInputFunc to read that encoding
 * @output:  the xmlCharEncodingOutputFunc to write that encoding
 *
 * Create and registers an xmlCharEncodingHandler.
 *
 * Returns the xmlCharEncodingHandlerPtr created (or NULL in case of error).
 */
xmlCharEncodingHandlerPtr
xmlNewCharEncodingHandler(const char *name,
                          xmlCharEncodingInputFunc input,
                          xmlCharEncodingOutputFunc output) {
    xmlCharEncodingHandlerPtr handler;
    const char *alias;
    char upper[500];
    int i;
    char *up = NULL;

    /*
     * Do the alias resolution
     */
    alias = xmlGetEncodingAlias(name);
    if (alias != NULL)
    name = alias;

    /*
     * Keep only the uppercase version of the encoding.
     */
    if (name == NULL)
    return(NULL);
    for (i = 0;i < 499;i++) {
        upper[i] = (char) toupper((unsigned char) name[i]);
    if (upper[i] == 0) break;
    }
    upper[i] = 0;
    up = xmlMemStrdup(upper);
    if (up == NULL)
    return(NULL);

    /*
     * allocate and fill-up an handler block.
     */
    handler = (xmlCharEncodingHandlerPtr)
              xmlMalloc(sizeof(xmlCharEncodingHandler));
    if (handler == NULL) {
        xmlFree(up);
    return(NULL);
    }
    memset(handler, 0, sizeof(xmlCharEncodingHandler));
    handler->input = input;
    handler->output = output;
    handler->name = up;

#ifdef LIBXML_ICONV_ENABLED
    handler->iconv_in = (iconv_t) -1;
    handler->iconv_out = (iconv_t) -1;
#endif
#ifdef LIBXML_ICU_ENABLED
    handler->uconv_in = NULL;
    handler->uconv_out = NULL;
#endif

    /*
     * registers and returns the handler.
     */
    xmlRegisterCharEncodingHandler(handler);
    return(handler);
}

/**
 * xmlInitCharEncodingHandlers:
 *
 * DEPRECATED: Alias for xmlInitParser.
 */
void
xmlInitCharEncodingHandlers(void) {
    xmlInitParser();
}

/**
 * xmlInitEncodingInternal:
 *
 * Initialize the char encoding support.
 */
void
xmlInitEncodingInternal(void) {
    unsigned short int tst = 0x1234;
    unsigned char *ptr = (unsigned char *) &tst;

    if (*ptr == 0x12) xmlLittleEndian = 0;
    else xmlLittleEndian = 1;
}

/**
 * xmlCleanupCharEncodingHandlers:
 *
 * DEPRECATED: This function will be made private. Call xmlCleanupParser
 * to free global state but see the warnings there. xmlCleanupParser
 * should be only called once at program exit. In most cases, you don't
 * have call cleanup functions at all.
 *
 * Cleanup the memory allocated for the char encoding support, it
 * unregisters all the encoding handlers and the aliases.
 */
void
xmlCleanupCharEncodingHandlers(void) {
    xmlCleanupEncodingAliases();

    if (handlers == NULL) return;

    for (;nbCharEncodingHandler > 0;) {
        nbCharEncodingHandler--;
    if (handlers[nbCharEncodingHandler] != NULL) {
        if (handlers[nbCharEncodingHandler]->name != NULL)
        xmlFree(handlers[nbCharEncodingHandler]->name);
        xmlFree(handlers[nbCharEncodingHandler]);
    }
    }
    xmlFree(handlers);
    handlers = NULL;
    nbCharEncodingHandler = 0;
}

/**
 * xmlRegisterCharEncodingHandler:
 * @handler:  the xmlCharEncodingHandlerPtr handler block
 *
 * Register the char encoding handler, surprising, isn't it ?
 */
void
xmlRegisterCharEncodingHandler(xmlCharEncodingHandlerPtr handler) {
    if (handler == NULL)
        return;
    if (handlers == NULL) {
        handlers = xmlMalloc(MAX_ENCODING_HANDLERS * sizeof(handlers[0]));
        if (handlers == NULL)
            goto free_handler;
    }

    if (nbCharEncodingHandler >= MAX_ENCODING_HANDLERS)
        goto free_handler;
    handlers[nbCharEncodingHandler++] = handler;
    return;

free_handler:
    if (handler != NULL) {
        if (handler->name != NULL) {
            xmlFree(handler->name);
        }
        xmlFree(handler);
    }
}

#ifdef LIBXML_ICONV_ENABLED
static int
xmlCreateIconvHandler(const char *name, xmlCharEncodingHandler **out) {
    xmlCharEncodingHandlerPtr enc = NULL;
    iconv_t icv_in = (iconv_t) -1;
    iconv_t icv_out = (iconv_t) -1;
    int ret;

    *out = NULL;

    icv_in = iconv_open("UTF-8", name);
    if (icv_in == (iconv_t) -1) {
        if (errno == EINVAL)
            ret = XML_ERR_UNSUPPORTED_ENCODING;
        else if (errno == ENOMEM)
            ret = XML_ERR_NO_MEMORY;
        else
            ret = XML_ERR_SYSTEM;
        goto error;
    }

    icv_out = iconv_open(name, "UTF-8");
    if (icv_out == (iconv_t) -1) {
        if (errno == EINVAL)
            ret = XML_ERR_UNSUPPORTED_ENCODING;
        else if (errno == ENOMEM)
            ret = XML_ERR_NO_MEMORY;
        else
            ret = XML_ERR_SYSTEM;
        goto error;
    }

    enc = xmlMalloc(sizeof(*enc));
    if (enc == NULL) {
        ret = XML_ERR_NO_MEMORY;
        goto error;
    }
    memset(enc, 0, sizeof(*enc));

    enc->name = xmlMemStrdup(name);
    if (enc->name == NULL) {
        ret = XML_ERR_NO_MEMORY;
        goto error;
    }
    enc->iconv_in = icv_in;
    enc->iconv_out = icv_out;

    *out = enc;
    return(0);

error:
    if (enc != NULL)
        xmlFree(enc);
    if (icv_in != (iconv_t) -1)
        iconv_close(icv_in);
    if (icv_out != (iconv_t) -1)
        iconv_close(icv_out);
    return(ret);
}
#endif /* LIBXML_ICONV_ENABLED */

#ifdef LIBXML_ICU_ENABLED
static int
openIcuConverter(const char* name, int toUnicode, uconv_t **out)
{
    UErrorCode status;
    uconv_t *conv;

    *out = NULL;

    conv = (uconv_t *) xmlMalloc(sizeof(uconv_t));
    if (conv == NULL)
        return(XML_ERR_NO_MEMORY);

    conv->pivot_source = conv->pivot_buf;
    conv->pivot_target = conv->pivot_buf;

    status = U_ZERO_ERROR;
    conv->uconv = ucnv_open(name, &status);
    if (U_FAILURE(status))
        goto error;

    status = U_ZERO_ERROR;
    if (toUnicode) {
        ucnv_setToUCallBack(conv->uconv, UCNV_TO_U_CALLBACK_STOP,
                                                NULL, NULL, NULL, &status);
    }
    else {
        ucnv_setFromUCallBack(conv->uconv, UCNV_FROM_U_CALLBACK_STOP,
                                                NULL, NULL, NULL, &status);
    }
    if (U_FAILURE(status))
        goto error;

    status = U_ZERO_ERROR;
    conv->utf8 = ucnv_open("UTF-8", &status);
    if (U_FAILURE(status))
        goto error;

    *out = conv;
    return(0);

error:
    if (conv->uconv)
        ucnv_close(conv->uconv);
    xmlFree(conv);

    if (status == U_FILE_ACCESS_ERROR)
        return(XML_ERR_UNSUPPORTED_ENCODING);
    if (status == U_MEMORY_ALLOCATION_ERROR)
        return(XML_ERR_NO_MEMORY);
    return(XML_ERR_SYSTEM);
}

static void
closeIcuConverter(uconv_t *conv)
{
    if (conv == NULL)
        return;
    ucnv_close(conv->uconv);
    ucnv_close(conv->utf8);
    xmlFree(conv);
}

static int
xmlCreateUconvHandler(const char *name, xmlCharEncodingHandler **out) {
    xmlCharEncodingHandlerPtr enc = NULL;
    uconv_t *ucv_in = NULL;
    uconv_t *ucv_out = NULL;
    int ret;

    ret = openIcuConverter(name, 1, &ucv_in);
    if (ret != 0)
        goto error;
    ret = openIcuConverter(name, 0, &ucv_out);
    if (ret != 0)
        goto error;

    enc = (xmlCharEncodingHandlerPtr)
           xmlMalloc(sizeof(xmlCharEncodingHandler));
    if (enc == NULL) {
        ret = XML_ERR_NO_MEMORY;
        goto error;
    }
    memset(enc, 0, sizeof(xmlCharEncodingHandler));

    enc->name = xmlMemStrdup(name);
    if (enc->name == NULL) {
        ret = XML_ERR_NO_MEMORY;
        goto error;
    }
    enc->input = NULL;
    enc->output = NULL;
#ifdef LIBXML_ICONV_ENABLED
    enc->iconv_in = (iconv_t) -1;
    enc->iconv_out = (iconv_t) -1;
#endif
    enc->uconv_in = ucv_in;
    enc->uconv_out = ucv_out;

    *out = enc;
    return(0);

error:
    if (enc != NULL)
        xmlFree(enc);
    if (ucv_in != NULL)
        closeIcuConverter(ucv_in);
    if (ucv_out != NULL)
        closeIcuConverter(ucv_out);
    return(ret);
}
#endif /* LIBXML_ICU_ENABLED */

/**
 * xmlFindExtraHandler:
 * @name:  a string describing the char encoding.
 * @output:  boolean, use handler for output
 * @out:  pointer to resulting handler
 *
 * Search the non-default handlers for an exact match.
 *
 * Returns 0 on success, 1 if no handler was found, -1 if a memory
 * allocation failed.
 */
static int
xmlFindExtraHandler(const char *name, int output,
                    xmlCharEncodingHandler **out) {
    int ret;
    int i;

    (void) ret;

    if (handlers != NULL) {
        for (i = 0; i < nbCharEncodingHandler; i++) {
            xmlCharEncodingHandler *handler = handlers[i];

            if (!xmlStrcasecmp((const xmlChar *) name,
                               (const xmlChar *) handler->name)) {
                if (output) {
                    if (handler->output != NULL) {
                        *out = handler;
                        return(0);
                    }
                } else {
                    if (handler->input != NULL) {
                        *out = handler;
                        return(0);
                    }
                }
            }
        }
    }

#ifdef LIBXML_ICONV_ENABLED
    ret = xmlCreateIconvHandler(name, out);
    if (*out != NULL)
        return(0);
    if (ret != XML_ERR_UNSUPPORTED_ENCODING)
        return(ret);
#endif /* LIBXML_ICONV_ENABLED */

#ifdef LIBXML_ICU_ENABLED
    ret = xmlCreateUconvHandler(name, out);
    if (*out != NULL)
        return(0);
    if (ret != XML_ERR_UNSUPPORTED_ENCODING)
        return(ret);
#endif /* LIBXML_ICU_ENABLED */

    return(XML_ERR_UNSUPPORTED_ENCODING);
}

/**
 * xmlFindHandler:
 * @name:  a string describing the char encoding.
 * @output:  boolean, use handler for output
 * @out:  pointer to resulting handler
 *
 * Search all handlers for an exact match.
 *
 * Returns 0 on success, 1 if no handler was found, -1 if a memory
 * allocation failed.
 */
static int
xmlFindHandler(const char *name, int output, xmlCharEncodingHandler **out) {
    int i;

    /*
     * Check for default handlers
     */
    for (i = 0; i < (int) NUM_DEFAULT_HANDLERS; i++) {
        xmlCharEncodingHandler *handler;

        handler = (xmlCharEncodingHandler *) &defaultHandlers[i];

        if (xmlStrcasecmp((const xmlChar *) name,
                          (const xmlChar *) handler->name) == 0) {
            if (output) {
                if (handler->output != NULL) {
                    *out = handler;
                    return(0);
                }
            } else {
                if (handler->input != NULL) {
                    *out = handler;
                    return(0);
                }
            }
        }
    }

    /*
     * Check for other handlers
     */
    return(xmlFindExtraHandler(name, output, out));
}

/**
 * xmlLookupCharEncodingHandler:
 * @enc:  an xmlCharEncoding value.
 * @out:  pointer to result
 *
 * Find or create a handler matching the encoding. If no default or
 * registered handler could be found, try to create a handler using
 * iconv or ICU if supported.
 *
 * The handler must be closed with xmlCharEncCloseFunc.
 *
 * Available since 2.13.0.
 *
 * Returns an xmlParserErrors error code.
 */
int
xmlLookupCharEncodingHandler(xmlCharEncoding enc,
                             xmlCharEncodingHandler **out) {
    const char *name = NULL;
    static const char *const ebcdicNames[] = {
        "EBCDIC", "ebcdic", "EBCDIC-US", "IBM-037"
    };
    static const char *const ucs4Names[] = {
        "ISO-10646-UCS-4", "UCS-4", "UCS4"
    };
    static const char *const ucs2Names[] = {
        "ISO-10646-UCS-2", "UCS-2", "UCS2"
    };
    static const char *const shiftJisNames[] = {
        "SHIFT-JIS", "SHIFT_JIS", "Shift_JIS",
    };
    const char *const *names = NULL;
    int numNames = 0;
    int ret;
    int i;

    if (out == NULL)
        return(XML_ERR_ARGUMENT);
    *out = NULL;

    switch (enc) {
        case XML_CHAR_ENCODING_ERROR:
        return(XML_ERR_UNSUPPORTED_ENCODING);
        case XML_CHAR_ENCODING_NONE:
        return(0);
        case XML_CHAR_ENCODING_UTF8:
        return(0);
        case XML_CHAR_ENCODING_UTF16LE:
        *out = (xmlCharEncodingHandler *) xmlUTF16LEHandler;
            return(0);
        case XML_CHAR_ENCODING_UTF16BE:
        *out = (xmlCharEncodingHandler *) xmlUTF16BEHandler;
            return(0);
        case XML_CHAR_ENCODING_EBCDIC:
            names = ebcdicNames;
            numNames = sizeof(ebcdicNames) / sizeof(ebcdicNames[0]);
        break;
        case XML_CHAR_ENCODING_UCS4BE:
        case XML_CHAR_ENCODING_UCS4LE:
            names = ucs4Names;
            numNames = sizeof(ucs4Names) / sizeof(ucs4Names[0]);
        break;
        case XML_CHAR_ENCODING_UCS4_2143:
        break;
        case XML_CHAR_ENCODING_UCS4_3412:
        break;
        case XML_CHAR_ENCODING_UCS2:
            names = ucs2Names;
            numNames = sizeof(ucs2Names) / sizeof(ucs2Names[0]);
        break;

        case XML_CHAR_ENCODING_ASCII:
        *out = (xmlCharEncodingHandler *) xmlAsciiHandler;
            return(0);
        case XML_CHAR_ENCODING_8859_1:
        *out = (xmlCharEncodingHandler *) xmlLatin1Handler;
            return(0);
        case XML_CHAR_ENCODING_8859_2:
        name = "ISO-8859-2";
        break;
        case XML_CHAR_ENCODING_8859_3:
        name = "ISO-8859-3";
        break;
        case XML_CHAR_ENCODING_8859_4:
        name = "ISO-8859-4";
        break;
        case XML_CHAR_ENCODING_8859_5:
        name = "ISO-8859-5";
        break;
        case XML_CHAR_ENCODING_8859_6:
        name = "ISO-8859-6";
        break;
        case XML_CHAR_ENCODING_8859_7:
        name = "ISO-8859-7";
        break;
        case XML_CHAR_ENCODING_8859_8:
        name = "ISO-8859-8";
        break;
        case XML_CHAR_ENCODING_8859_9:
        name = "ISO-8859-9";
        break;

        case XML_CHAR_ENCODING_2022_JP:
            name = "ISO-2022-JP";
        break;
        case XML_CHAR_ENCODING_SHIFT_JIS:
            names = shiftJisNames;
            numNames = sizeof(shiftJisNames) / sizeof(shiftJisNames[0]);
        break;
        case XML_CHAR_ENCODING_EUC_JP:
            name = "EUC-JP";
        break;
    default:
        break;
    }

    if (name != NULL)
        return(xmlFindExtraHandler(name, 0, out));

    if (names != NULL) {
        for (i = 0; i < numNames; i++) {
            ret = xmlFindExtraHandler(names[i], 0, out);
            if (*out != NULL)
                return(0);
            if (ret != XML_ERR_UNSUPPORTED_ENCODING)
                return(ret);
        }
    }

    return(XML_ERR_UNSUPPORTED_ENCODING);
}

/**
 * xmlGetCharEncodingHandler:
 * @enc:  an xmlCharEncoding value.
 *
 * DEPRECATED: Use xmlLookupCharEncodingHandler which has better error
 * reporting.
 *
 * Returns the handler or NULL if no handler was found or an error
 * occurred.
 */
xmlCharEncodingHandlerPtr
xmlGetCharEncodingHandler(xmlCharEncoding enc) {
    xmlCharEncodingHandler *ret;

    xmlLookupCharEncodingHandler(enc, &ret);
    return(ret);
}

/**
 * xmlOpenCharEncodingHandler:
 * @name:  a string describing the char encoding.
 * @output:  boolean, use handler for output
 * @out:  pointer to result
 *
 * Find or create a handler matching the encoding. If no default or
 * registered handler could be found, try to create a handler using
 * iconv or ICU if supported.
 *
 * The handler must be closed with xmlCharEncCloseFunc.
 *
 * If the encoding is UTF-8, a NULL handler and no error code will
 * be returned.
 *
 * Available since 2.13.0.
 *
 * Returns an xmlParserErrors error code.
 */
int
xmlOpenCharEncodingHandler(const char *name, int output,
                           xmlCharEncodingHandler **out) {
    const char *nalias;
    const char *norig;
    xmlCharEncoding enc;
    int ret;

    if (out == NULL)
        return(XML_ERR_ARGUMENT);
    *out = NULL;

    if (name == NULL)
        return(XML_ERR_ARGUMENT);

    if ((xmlStrcasecmp(BAD_CAST name, BAD_CAST "UTF-8") == 0) ||
        (xmlStrcasecmp(BAD_CAST name, BAD_CAST "UTF8") == 0))
        return(XML_ERR_OK);

    /*
     * Do the alias resolution
     */
    norig = name;
    nalias = xmlGetEncodingAlias(name);
    if (nalias != NULL)
    name = nalias;

    ret = xmlFindHandler(name, output, out);
    if (*out != NULL)
        return(0);
    if (ret != XML_ERR_UNSUPPORTED_ENCODING)
        return(ret);

    /*
     * Fallback using the canonical names
     *
     * TODO: We should make sure that the name of the returned
     * handler equals norig.
     */
    enc = xmlParseCharEncoding(norig);
    return(xmlLookupCharEncodingHandler(enc, out));
}

/**
 * xmlFindCharEncodingHandler:
 * @name:  a string describing the char encoding.
 *
 * DEPRECATED: Use xmlOpenCharEncodingHandler which has better error
 * reporting.
 *
 * Returns the handler or NULL if no handler was found or an error
 * occurred.
 */
xmlCharEncodingHandlerPtr
xmlFindCharEncodingHandler(const char *name) {
    xmlCharEncodingHandler *ret;

    /*
     * This handler shouldn't be used, but we must return a non-NULL
     * handler.
     */
    if ((xmlStrcasecmp(BAD_CAST name, BAD_CAST "UTF-8") == 0) ||
        (xmlStrcasecmp(BAD_CAST name, BAD_CAST "UTF8") == 0))
        return((xmlCharEncodingHandlerPtr) &xmlUTF8Handler);

    xmlOpenCharEncodingHandler(name, 0, &ret);
    return(ret);
}

/************************************************************************
 *                                    *
 *        ICONV based generic conversion functions        *
 *                                    *
 ************************************************************************/

#ifdef LIBXML_ICONV_ENABLED
/**
 * xmlIconvWrapper:
 * @cd:        iconv converter data structure
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @in:  a pointer to an array of input bytes
 * @inlen:  the length of @in
 *
 * Returns an XML_ENC_ERR code.
 *
 * The value of @inlen after return is the number of octets consumed
 *     as the return value is positive, else unpredictable.
 * The value of @outlen after return is the number of octets produced.
 */
static int
xmlIconvWrapper(iconv_t cd, unsigned char *out, int *outlen,
                const unsigned char *in, int *inlen) {
    size_t icv_inlen, icv_outlen;
    const char *icv_in = (const char *) in;
    char *icv_out = (char *) out;
    size_t ret;

    if ((out == NULL) || (outlen == NULL) || (inlen == NULL) || (in == NULL)) {
        if (outlen != NULL) *outlen = 0;
        return(XML_ENC_ERR_INTERNAL);
    }
    icv_inlen = *inlen;
    icv_outlen = *outlen;
    /*
     * Some versions take const, other versions take non-const input.
     */
    ret = iconv(cd, (void *) &icv_in, &icv_inlen, &icv_out, &icv_outlen);
    *inlen -= icv_inlen;
    *outlen -= icv_outlen;
    if (ret == (size_t) -1) {
        if (errno == EILSEQ)
            return(XML_ENC_ERR_INPUT);
        if (errno == E2BIG)
            return(XML_ENC_ERR_SPACE);
        if (errno == EINVAL)
            return(XML_ENC_ERR_PARTIAL);
        return(XML_ENC_ERR_INTERNAL);
    }
    return(XML_ENC_ERR_SUCCESS);
}
#endif /* LIBXML_ICONV_ENABLED */

/************************************************************************
 *                                    *
 *        ICU based generic conversion functions        *
 *                                    *
 ************************************************************************/

#ifdef LIBXML_ICU_ENABLED
/**
 * xmlUconvWrapper:
 * @cd: ICU uconverter data structure
 * @toUnicode : non-zero if toUnicode. 0 otherwise.
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @in:  a pointer to an array of input bytes
 * @inlen:  the length of @in
 *
 * Returns an XML_ENC_ERR code.
 *
 * The value of @inlen after return is the number of octets consumed
 *     as the return value is positive, else unpredictable.
 * The value of @outlen after return is the number of octets produced.
 */
static int
xmlUconvWrapper(uconv_t *cd, int toUnicode, unsigned char *out, int *outlen,
                const unsigned char *in, int *inlen) {
    const char *ucv_in = (const char *) in;
    char *ucv_out = (char *) out;
    UErrorCode err = U_ZERO_ERROR;

    if ((out == NULL) || (outlen == NULL) || (inlen == NULL) || (in == NULL)) {
        if (outlen != NULL) *outlen = 0;
        return(XML_ENC_ERR_INTERNAL);
    }

    /*
     * Note that the ICU API is stateful. It can always consume a certain
     * amount of input even if the output buffer would overflow. The
     * remaining input must be processed by calling ucnv_convertEx with a
     * possibly empty input buffer.
     *
     * ucnv_convertEx is always called with reset and flush set to 0,
     * so we don't mess up the state. This should never generate
     * U_TRUNCATED_CHAR_FOUND errors.
     *
     * This also means that ICU xmlCharEncodingHandlers should never be
     * reused. It would be a lot nicer if there was a way to emulate the
     * stateless iconv API.
     */
    if (toUnicode) {
        /* encoding => UTF-16 => UTF-8 */
        ucnv_convertEx(cd->utf8, cd->uconv, &ucv_out, ucv_out + *outlen,
                       &ucv_in, ucv_in + *inlen, cd->pivot_buf,
                       &cd->pivot_source, &cd->pivot_target,
                       cd->pivot_buf + ICU_PIVOT_BUF_SIZE, 0, 0, &err);
    } else {
        /* UTF-8 => UTF-16 => encoding */
        ucnv_convertEx(cd->uconv, cd->utf8, &ucv_out, ucv_out + *outlen,
                       &ucv_in, ucv_in + *inlen, cd->pivot_buf,
                       &cd->pivot_source, &cd->pivot_target,
                       cd->pivot_buf + ICU_PIVOT_BUF_SIZE, 0, 0, &err);
    }
    *inlen = ucv_in - (const char*) in;
    *outlen = ucv_out - (char *) out;
    if (U_SUCCESS(err)) {
        return(XML_ENC_ERR_SUCCESS);
    }
    if (err == U_BUFFER_OVERFLOW_ERROR)
        return(XML_ENC_ERR_SPACE);
    if (err == U_INVALID_CHAR_FOUND || err == U_ILLEGAL_CHAR_FOUND)
        return(XML_ENC_ERR_INPUT);
    return(XML_ENC_ERR_PARTIAL);
}
#endif /* LIBXML_ICU_ENABLED */

/************************************************************************
 *                                    *
 *        The real API used by libxml for on-the-fly conversion    *
 *                                    *
 ************************************************************************/

/**
 * xmlEncConvertError:
 * @code:  XML_ENC_ERR code
 *
 * Convert XML_ENC_ERR to libxml2 error codes.
 */
static int
xmlEncConvertError(int code) {
    int ret;

    switch (code) {
        case XML_ENC_ERR_SUCCESS:
            ret = XML_ERR_OK;
            break;
        case XML_ENC_ERR_INPUT:
            ret = XML_ERR_INVALID_ENCODING;
            break;
        case XML_ENC_ERR_MEMORY:
            ret = XML_ERR_NO_MEMORY;
            break;
        default:
            ret = XML_ERR_INTERNAL_ERROR;
            break;
    }

    return(ret);
}

/**
 * xmlEncInputChunk:
 * @handler:  encoding handler
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @in:  a pointer to an array of input bytes
 * @inlen:  the length of @in
 *
 * The value of @inlen after return is the number of octets consumed
 *     as the return value is 0, else unpredictable.
 * The value of @outlen after return is the number of octets produced.
 *
 * Returns an XML_ENC_ERR code.
 */
int
xmlEncInputChunk(xmlCharEncodingHandler *handler, unsigned char *out,
                 int *outlen, const unsigned char *in, int *inlen) {
    int ret;

    if (handler->input != NULL) {
        int oldinlen = *inlen;

        ret = handler->input(out, outlen, in, inlen);
        if (ret >= 0) {
            /*
             * The built-in converters don't signal XML_ENC_ERR_SPACE.
             */
            if (*inlen < oldinlen) {
                if (*outlen > 0)
                    ret = XML_ENC_ERR_SPACE;
                else
                    ret = XML_ENC_ERR_PARTIAL;
            } else {
                ret = XML_ENC_ERR_SUCCESS;
            }
        }
    }
#ifdef LIBXML_ICONV_ENABLED
    else if (handler->iconv_in != (iconv_t) -1) {
        ret = xmlIconvWrapper(handler->iconv_in, out, outlen, in, inlen);
    }
#endif /* LIBXML_ICONV_ENABLED */
#ifdef LIBXML_ICU_ENABLED
    else if (handler->uconv_in != NULL) {
        ret = xmlUconvWrapper(handler->uconv_in, 1, out, outlen, in, inlen);
    }
#endif /* LIBXML_ICU_ENABLED */
    else {
        *outlen = 0;
        *inlen = 0;
        ret = XML_ENC_ERR_INTERNAL;
    }

    /* Ignore partial errors when reading. */
    if (ret == XML_ENC_ERR_PARTIAL)
        ret = XML_ENC_ERR_SUCCESS;

    return(ret);
}

/**
 * xmlEncOutputChunk:
 * @handler:  encoding handler
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @in:  a pointer to an array of input bytes
 * @inlen:  the length of @in
 *
 * Returns an XML_ENC_ERR code.
 *
 * The value of @inlen after return is the number of octets consumed
 *     as the return value is 0, else unpredictable.
 * The value of @outlen after return is the number of octets produced.
 */
static int
xmlEncOutputChunk(xmlCharEncodingHandler *handler, unsigned char *out,
                  int *outlen, const unsigned char *in, int *inlen) {
    int ret;

    if (handler->output != NULL) {
        int oldinlen = *inlen;

        ret = handler->output(out, outlen, in, inlen);
        if (ret >= 0) {
            /*
             * The built-in converters don't signal XML_ENC_ERR_SPACE.
             */
            if (*inlen < oldinlen) {
                if (*outlen > 0)
                    ret = XML_ENC_ERR_SPACE;
                else
                    ret = XML_ENC_ERR_PARTIAL;
            } else {
                ret = XML_ENC_ERR_SUCCESS;
            }
        }
    }
#ifdef LIBXML_ICONV_ENABLED
    else if (handler->iconv_out != (iconv_t) -1) {
        ret = xmlIconvWrapper(handler->iconv_out, out, outlen, in, inlen);
    }
#endif /* LIBXML_ICONV_ENABLED */
#ifdef LIBXML_ICU_ENABLED
    else if (handler->uconv_out != NULL) {
        ret = xmlUconvWrapper(handler->uconv_out, 0, out, outlen, in, inlen);
    }
#endif /* LIBXML_ICU_ENABLED */
    else {
        *outlen = 0;
        *inlen = 0;
        ret = XML_ENC_ERR_INTERNAL;
    }

    /* We shouldn't generate partial sequences when writing. */
    if (ret == XML_ENC_ERR_PARTIAL)
        ret = XML_ENC_ERR_INTERNAL;

    return(ret);
}

/**
 * xmlCharEncFirstLine:
 * @handler:   char encoding transformation data structure
 * @out:  an xmlBuffer for the output.
 * @in:  an xmlBuffer for the input
 *
 * DEPERECATED: Don't use.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 */
int
xmlCharEncFirstLine(xmlCharEncodingHandler *handler, xmlBufferPtr out,
                    xmlBufferPtr in) {
    return(xmlCharEncInFunc(handler, out, in));
}

/**
 * xmlCharEncInput:
 * @input: a parser input buffer
 *
 * Generic front-end for the encoding handler on parser input
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 */
int
xmlCharEncInput(xmlParserInputBufferPtr input)
{
    int ret;
    size_t avail;
    size_t toconv;
    int c_in;
    int c_out;
    xmlBufPtr in;
    xmlBufPtr out;
    const xmlChar *inData;
    size_t inTotal = 0;

    if ((input == NULL) || (input->encoder == NULL) ||
        (input->buffer == NULL) || (input->raw == NULL))
        return(XML_ENC_ERR_INTERNAL);
    out = input->buffer;
    in = input->raw;

    toconv = xmlBufUse(in);
    if (toconv == 0)
        return (0);
    inData = xmlBufContent(in);
    inTotal = 0;

    do {
        c_in = toconv > INT_MAX / 2 ? INT_MAX / 2 : toconv;

        avail = xmlBufAvail(out);
        if (avail > INT_MAX)
            avail = INT_MAX;
        if (avail < 4096) {
            if (xmlBufGrow(out, 4096) < 0) {
                input->error = XML_ERR_NO_MEMORY;
                return(XML_ENC_ERR_MEMORY);
            }
            avail = xmlBufAvail(out);
        }

        c_in = toconv;
        c_out = avail;
        ret = xmlEncInputChunk(input->encoder, xmlBufEnd(out), &c_out,
                               inData, &c_in);
        inTotal += c_in;
        inData += c_in;
        toconv -= c_in;
        xmlBufAddLen(out, c_out);
    } while (ret == XML_ENC_ERR_SPACE);

    xmlBufShrink(in, inTotal);

    if (input->rawconsumed > ULONG_MAX - (unsigned long)c_in)
        input->rawconsumed = ULONG_MAX;
    else
        input->rawconsumed += c_in;

    if (((ret != 0) && (c_out == 0)) ||
        (ret == XML_ENC_ERR_MEMORY)) {
        if (input->error == 0)
            input->error = xmlEncConvertError(ret);
        return(ret);
    }

    return (c_out);
}

/**
 * xmlCharEncInFunc:
 * @handler:    char encoding transformation data structure
 * @out:  an xmlBuffer for the output.
 * @in:  an xmlBuffer for the input
 *
 * Generic front-end for the encoding handler input function
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 */
int
xmlCharEncInFunc(xmlCharEncodingHandler * handler, xmlBufferPtr out,
                 xmlBufferPtr in)
{
    int ret;
    int written;
    int toconv;

    if (handler == NULL)
        return(XML_ENC_ERR_INTERNAL);
    if (out == NULL)
        return(XML_ENC_ERR_INTERNAL);
    if (in == NULL)
        return(XML_ENC_ERR_INTERNAL);

    toconv = in->use;
    if (toconv == 0)
        return (0);
    written = out->size - out->use -1; /* count '\0' */
    if (toconv * 2 >= written) {
        xmlBufferGrow(out, out->size + toconv * 2);
        written = out->size - out->use - 1;
    }
    ret = xmlEncInputChunk(handler, &out->content[out->use], &written,
                           in->content, &toconv);
    xmlBufferShrink(in, toconv);
    out->use += written;
    out->content[out->use] = 0;

    return (written? written : ret);
}

#ifdef LIBXML_OUTPUT_ENABLED
/**
 * xmlCharEncOutput:
 * @output: a parser output buffer
 * @init: is this an initialization call without data
 *
 * Generic front-end for the encoding handler on parser output
 * a first call with @init == 1 has to be made first to initiate the
 * output in case of non-stateless encoding needing to initiate their
 * state or the output (like the BOM in UTF16).
 * In case of UTF8 sequence conversion errors for the given encoder,
 * the content will be automatically remapped to a CharRef sequence.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 */
int
xmlCharEncOutput(xmlOutputBufferPtr output, int init)
{
    int ret;
    size_t written;
    int writtentot = 0;
    size_t toconv;
    int c_in;
    int c_out;
    xmlBufPtr in;
    xmlBufPtr out;

    if ((output == NULL) || (output->encoder == NULL) ||
        (output->buffer == NULL) || (output->conv == NULL))
        return(XML_ENC_ERR_INTERNAL);
    out = output->conv;
    in = output->buffer;

retry:

    written = xmlBufAvail(out);

    /*
     * First specific handling of the initialization call
     */
    if (init) {
        c_in = 0;
        c_out = written;
        /* TODO: Check return value. */
        xmlEncOutputChunk(output->encoder, xmlBufEnd(out), &c_out,
                          NULL, &c_in);
        xmlBufAddLen(out, c_out);
        return(c_out);
    }

    /*
     * Conversion itself.
     */
    toconv = xmlBufUse(in);
    if (toconv > 64 * 1024)
        toconv = 64 * 1024;
    if (toconv * 4 >= written) {
        if (xmlBufGrow(out, toconv * 4) < 0) {
            ret = XML_ENC_ERR_MEMORY;
            goto error;
        }
        written = xmlBufAvail(out);
    }
    if (written > 256 * 1024)
        written = 256 * 1024;

    c_in = toconv;
    c_out = written;
    ret = xmlEncOutputChunk(output->encoder, xmlBufEnd(out), &c_out,
                            xmlBufContent(in), &c_in);
    xmlBufShrink(in, c_in);
    xmlBufAddLen(out, c_out);
    writtentot += c_out;

    if (ret == XML_ENC_ERR_SPACE)
        goto retry;

    /*
     * Attempt to handle error cases
     */
    if (ret == XML_ENC_ERR_INPUT) {
        xmlChar charref[20];
        int len = xmlBufUse(in);
        xmlChar *content = xmlBufContent(in);
        int cur, charrefLen;

        cur = xmlGetUTF8Char(content, &len);
        if (cur <= 0)
            goto error;

        /*
         * Removes the UTF8 sequence, and replace it by a charref
         * and continue the transcoding phase, hoping the error
         * did not mangle the encoder state.
         */
        charrefLen = snprintf((char *) &charref[0], sizeof(charref),
                         "&#%d;", cur);
        xmlBufGrow(out, charrefLen * 4);
        c_out = xmlBufAvail(out);
        c_in = charrefLen;
        ret = xmlEncOutputChunk(output->encoder, xmlBufEnd(out), &c_out,
                                charref, &c_in);
        if ((ret < 0) || (c_in != charrefLen)) {
            ret = XML_ENC_ERR_INTERNAL;
            goto error;
        }

        xmlBufShrink(in, len);
        xmlBufAddLen(out, c_out);
        writtentot += c_out;
        goto retry;
    }

error:
    if (((writtentot <= 0) && (ret != 0)) ||
        (ret == XML_ENC_ERR_MEMORY)) {
        if (output->error == 0)
            output->error = xmlEncConvertError(ret);
        return(ret);
    }

    return(writtentot);
}
#endif

/**
 * xmlCharEncOutFunc:
 * @handler:    char encoding transformation data structure
 * @out:  an xmlBuffer for the output.
 * @in:  an xmlBuffer for the input
 *
 * Generic front-end for the encoding handler output function
 * a first call with @in == NULL has to be made firs to initiate the
 * output in case of non-stateless encoding needing to initiate their
 * state or the output (like the BOM in UTF16).
 * In case of UTF8 sequence conversion errors for the given encoder,
 * the content will be automatically remapped to a CharRef sequence.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 */
int
xmlCharEncOutFunc(xmlCharEncodingHandler *handler, xmlBufferPtr out,
                  xmlBufferPtr in) {
    int ret;
    int written;
    int writtentot = 0;
    int toconv;

    if (handler == NULL) return(XML_ENC_ERR_INTERNAL);
    if (out == NULL) return(XML_ENC_ERR_INTERNAL);

retry:

    written = out->size - out->use;

    if (written > 0)
    written--; /* Gennady: count '/0' */

    /*
     * First specific handling of in = NULL, i.e. the initialization call
     */
    if (in == NULL) {
        toconv = 0;
        /* TODO: Check return value. */
        xmlEncOutputChunk(handler, &out->content[out->use], &written,
                          NULL, &toconv);
        out->use += written;
        out->content[out->use] = 0;
        return(0);
    }

    /*
     * Conversion itself.
     */
    toconv = in->use;
    if (toconv * 4 >= written) {
        xmlBufferGrow(out, toconv * 4);
    written = out->size - out->use - 1;
    }
    ret = xmlEncOutputChunk(handler, &out->content[out->use], &written,
                            in->content, &toconv);
    xmlBufferShrink(in, toconv);
    out->use += written;
    writtentot += written;
    out->content[out->use] = 0;

    if (ret == XML_ENC_ERR_SPACE)
        goto retry;

    /*
     * Attempt to handle error cases
     */
    if (ret == XML_ENC_ERR_INPUT) {
        xmlChar charref[20];
        int len = in->use;
        const xmlChar *utf = (const xmlChar *) in->content;
        int cur, charrefLen;

        cur = xmlGetUTF8Char(utf, &len);
        if (cur <= 0)
            return(ret);

        /*
         * Removes the UTF8 sequence, and replace it by a charref
         * and continue the transcoding phase, hoping the error
         * did not mangle the encoder state.
         */
        charrefLen = snprintf((char *) &charref[0], sizeof(charref),
                         "&#%d;", cur);
        xmlBufferShrink(in, len);
        xmlBufferGrow(out, charrefLen * 4);
        written = out->size - out->use - 1;
        toconv = charrefLen;
        ret = xmlEncOutputChunk(handler, &out->content[out->use], &written,
                                charref, &toconv);
        if ((ret < 0) || (toconv != charrefLen))
            return(XML_ENC_ERR_INTERNAL);

        out->use += written;
        writtentot += written;
        out->content[out->use] = 0;
        goto retry;
    }
    return(writtentot ? writtentot : ret);
}

/**
 * xmlCharEncCloseFunc:
 * @handler:    char encoding transformation data structure
 *
 * Generic front-end for encoding handler close function
 *
 * Returns 0 if success, or -1 in case of error
 */
int
xmlCharEncCloseFunc(xmlCharEncodingHandler *handler) {
    int ret = 0;
    int tofree = 0;
    int i = 0;

    if (handler == NULL) return(-1);

    for (i = 0; i < (int) NUM_DEFAULT_HANDLERS; i++) {
        if (handler == &defaultHandlers[i])
            return(0);
    }

    if (handlers != NULL) {
        for (i = 0;i < nbCharEncodingHandler; i++) {
            if (handler == handlers[i])
                return(0);
    }
    }
#ifdef LIBXML_ICONV_ENABLED
    /*
     * Iconv handlers can be used only once, free the whole block.
     * and the associated icon resources.
     */
    if ((handler->iconv_out != (iconv_t) -1) || (handler->iconv_in != (iconv_t) -1)) {
        tofree = 1;
    if (handler->iconv_out != (iconv_t) -1) {
        if (iconv_close(handler->iconv_out))
        ret = -1;
        handler->iconv_out = (iconv_t) -1;
    }
    if (handler->iconv_in != (iconv_t) -1) {
        if (iconv_close(handler->iconv_in))
        ret = -1;
        handler->iconv_in = (iconv_t) -1;
    }
    }
#endif /* LIBXML_ICONV_ENABLED */
#ifdef LIBXML_ICU_ENABLED
    if ((handler->uconv_out != NULL) || (handler->uconv_in != NULL)) {
        tofree = 1;
    if (handler->uconv_out != NULL) {
        closeIcuConverter(handler->uconv_out);
        handler->uconv_out = NULL;
    }
    if (handler->uconv_in != NULL) {
        closeIcuConverter(handler->uconv_in);
        handler->uconv_in = NULL;
    }
    }
#endif
    if (tofree) {
        /* free up only dynamic handlers iconv/uconv */
        if (handler->name != NULL)
            xmlFree(handler->name);
        handler->name = NULL;
        xmlFree(handler);
    }

    return(ret);
}

/**
 * xmlByteConsumed:
 * @ctxt: an XML parser context
 *
 * This function provides the current index of the parser relative
 * to the start of the current entity. This function is computed in
 * bytes from the beginning starting at zero and finishing at the
 * size in byte of the file if parsing a file. The function is
 * of constant cost if the input is UTF-8 but can be costly if run
 * on non-UTF-8 input.
 *
 * Returns the index in bytes from the beginning of the entity or -1
 *         in case the index could not be computed.
 */
long
xmlByteConsumed(xmlParserCtxtPtr ctxt) {
    xmlParserInputPtr in;

    if (ctxt == NULL) return(-1);
    in = ctxt->input;
    if (in == NULL)  return(-1);
    if ((in->buf != NULL) && (in->buf->encoder != NULL)) {
        unsigned int unused = 0;
    xmlCharEncodingHandler * handler = in->buf->encoder;
        /*
     * Encoding conversion, compute the number of unused original
     * bytes from the input not consumed and subtract that from
     * the raw consumed value, this is not a cheap operation
     */
        if (in->end - in->cur > 0) {
        unsigned char convbuf[32000];
        const unsigned char *cur = (const unsigned char *)in->cur;
        int toconv = in->end - in->cur, written = 32000;

        int ret;

            do {
                toconv = in->end - cur;
                written = 32000;
                ret = xmlEncOutputChunk(handler, &convbuf[0], &written,
                                        cur, &toconv);
                if ((ret != XML_ENC_ERR_SUCCESS) && (ret != XML_ENC_ERR_SPACE))
                    return(-1);
                unused += written;
                cur += toconv;
            } while (ret == XML_ENC_ERR_SPACE);
    }
    if (in->buf->rawconsumed < unused)
        return(-1);
    return(in->buf->rawconsumed - unused);
    }
    return(in->consumed + (in->cur - in->base));
}

#if !defined(LIBXML_ICONV_ENABLED) && !defined(LIBXML_ICU_ENABLED)
#ifdef LIBXML_ISO8859X_ENABLED

/**
 * UTF8ToISO8859x:
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @in:  a pointer to an array of UTF-8 chars
 * @inlen:  the length of @in
 * @xlattable: the 2-level transcoding table
 *
 * Take a block of UTF-8 chars in and try to convert it to an ISO 8859-*
 * block of chars out.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 *
 * The value of @inlen after return is the number of octets consumed
 * as the return value is positive, else unpredictable.
 * The value of @outlen after return is the number of octets consumed.
 */
static int
UTF8ToISO8859x(unsigned char* out, int *outlen,
              const unsigned char* in, int *inlen,
              const unsigned char* const xlattable) {
    const unsigned char* outstart = out;
    const unsigned char* inend;
    const unsigned char* instart = in;
    const unsigned char* processed = in;

    if ((out == NULL) || (outlen == NULL) || (inlen == NULL) ||
        (xlattable == NULL))
    return(XML_ENC_ERR_INTERNAL);
    if (in == NULL) {
        /*
        * initialization nothing to do
        */
        *outlen = 0;
        *inlen = 0;
        return(0);
    }
    inend = in + (*inlen);
    while (in < inend) {
        unsigned char d = *in++;
        if  (d < 0x80)  {
            *out++ = d;
        } else if (d < 0xC0) {
            /* trailing byte in leading position */
            *outlen = out - outstart;
            *inlen = processed - instart;
            return(XML_ENC_ERR_INPUT);
        } else if (d < 0xE0) {
            unsigned char c;
            if (!(in < inend)) {
                /* trailing byte not in input buffer */
                *outlen = out - outstart;
                *inlen = processed - instart;
                return(XML_ENC_ERR_PARTIAL);
            }
            c = *in++;
            if ((c & 0xC0) != 0x80) {
                /* not a trailing byte */
                *outlen = out - outstart;
                *inlen = processed - instart;
                return(XML_ENC_ERR_INPUT);
            }
            c = c & 0x3F;
            d = d & 0x1F;
            d = xlattable [48 + c + xlattable [d] * 64];
            if (d == 0) {
                /* not in character set */
                *outlen = out - outstart;
                *inlen = processed - instart;
                return(XML_ENC_ERR_INPUT);
            }
            *out++ = d;
        } else if (d < 0xF0) {
            unsigned char c1;
            unsigned char c2;
            if (!(in < inend - 1)) {
                /* trailing bytes not in input buffer */
                *outlen = out - outstart;
                *inlen = processed - instart;
                return(XML_ENC_ERR_PARTIAL);
            }
            c1 = *in++;
            if ((c1 & 0xC0) != 0x80) {
                /* not a trailing byte (c1) */
                *outlen = out - outstart;
                *inlen = processed - instart;
                return(XML_ENC_ERR_INPUT);
            }
            c2 = *in++;
            if ((c2 & 0xC0) != 0x80) {
                /* not a trailing byte (c2) */
                *outlen = out - outstart;
                *inlen = processed - instart;
                return(XML_ENC_ERR_INPUT);
            }
            c1 = c1 & 0x3F;
            c2 = c2 & 0x3F;
        d = d & 0x0F;
        d = xlattable [48 + c2 + xlattable [48 + c1 +
            xlattable [32 + d] * 64] * 64];
            if (d == 0) {
                /* not in character set */
                *outlen = out - outstart;
                *inlen = processed - instart;
                return(XML_ENC_ERR_INPUT);
            }
            *out++ = d;
        } else {
            /* cannot transcode >= U+010000 */
            *outlen = out - outstart;
            *inlen = processed - instart;
            return(XML_ENC_ERR_INPUT);
        }
        processed = in;
    }
    *outlen = out - outstart;
    *inlen = processed - instart;
    return(*outlen);
}

/**
 * ISO8859xToUTF8
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @in:  a pointer to an array of ISO Latin 1 chars
 * @inlen:  the length of @in
 *
 * Take a block of ISO 8859-* chars in and try to convert it to an UTF-8
 * block of chars out.
 *
 * Returns the number of bytes written or an XML_ENC_ERR code.
 *
 * The value of @inlen after return is the number of octets consumed
 * The value of @outlen after return is the number of octets produced.
 */
static int
ISO8859xToUTF8(unsigned char* out, int *outlen,
              const unsigned char* in, int *inlen,
              unsigned short const *unicodetable) {
    unsigned char* outstart = out;
    unsigned char* outend;
    const unsigned char* instart = in;
    const unsigned char* inend;
    const unsigned char* instop;
    unsigned int c;

    if ((out == NULL) || (outlen == NULL) || (inlen == NULL) ||
        (in == NULL) || (unicodetable == NULL))
    return(XML_ENC_ERR_INTERNAL);
    outend = out + *outlen;
    inend = in + *inlen;
    instop = inend;

    while ((in < inend) && (out < outend - 2)) {
        if (*in >= 0x80) {
            c = unicodetable [*in - 0x80];
            if (c == 0) {
                /* undefined code point */
                *outlen = out - outstart;
                *inlen = in - instart;
                return(XML_ENC_ERR_INPUT);
            }
            if (c < 0x800) {
                *out++ = ((c >>  6) & 0x1F) | 0xC0;
                *out++ = (c & 0x3F) | 0x80;
            } else {
                *out++ = ((c >>  12) & 0x0F) | 0xE0;
                *out++ = ((c >>  6) & 0x3F) | 0x80;
                *out++ = (c & 0x3F) | 0x80;
            }
            ++in;
        }
        if (instop - in > outend - out) instop = in + (outend - out);
        while ((*in < 0x80) && (in < instop)) {
            *out++ = *in++;
        }
    }
    if ((in < inend) && (out < outend) && (*in < 0x80)) {
        *out++ =  *in++;
    }
    if ((in < inend) && (out < outend) && (*in < 0x80)) {
        *out++ =  *in++;
    }
    *outlen = out - outstart;
    *inlen = in - instart;
    return (*outlen);
}


/************************************************************************
 * Lookup tables for ISO-8859-2..ISO-8859-16 transcoding                *
 ************************************************************************/

static const unsigned short xmlunicodetable_ISO8859_2 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x0104, 0x02d8, 0x0141, 0x00a4, 0x013d, 0x015a, 0x00a7,
    0x00a8, 0x0160, 0x015e, 0x0164, 0x0179, 0x00ad, 0x017d, 0x017b,
    0x00b0, 0x0105, 0x02db, 0x0142, 0x00b4, 0x013e, 0x015b, 0x02c7,
    0x00b8, 0x0161, 0x015f, 0x0165, 0x017a, 0x02dd, 0x017e, 0x017c,
    0x0154, 0x00c1, 0x00c2, 0x0102, 0x00c4, 0x0139, 0x0106, 0x00c7,
    0x010c, 0x00c9, 0x0118, 0x00cb, 0x011a, 0x00cd, 0x00ce, 0x010e,
    0x0110, 0x0143, 0x0147, 0x00d3, 0x00d4, 0x0150, 0x00d6, 0x00d7,
    0x0158, 0x016e, 0x00da, 0x0170, 0x00dc, 0x00dd, 0x0162, 0x00df,
    0x0155, 0x00e1, 0x00e2, 0x0103, 0x00e4, 0x013a, 0x0107, 0x00e7,
    0x010d, 0x00e9, 0x0119, 0x00eb, 0x011b, 0x00ed, 0x00ee, 0x010f,
    0x0111, 0x0144, 0x0148, 0x00f3, 0x00f4, 0x0151, 0x00f6, 0x00f7,
    0x0159, 0x016f, 0x00fa, 0x0171, 0x00fc, 0x00fd, 0x0163, 0x02d9,
};

static const unsigned char xmltranscodetable_ISO8859_2 [48 + 6 * 64] = {
    "\x00\x00\x01\x05\x02\x04\x00\x00\x00\x00\x00\x03\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\x00\x00\x00\xa4\x00\x00\xa7\xa8\x00\x00\x00\x00\xad\x00\x00"
    "\xb0\x00\x00\x00\xb4\x00\x00\x00\xb8\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\xc3\xe3\xa1\xb1\xc6\xe6\x00\x00\x00\x00\xc8\xe8\xcf\xef"
    "\xd0\xf0\x00\x00\x00\x00\x00\x00\xca\xea\xcc\xec\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\xc5\xe5\x00\x00\xa5\xb5\x00"
    "\x00\x00\x00\x00\x00\x00\x00\xb7\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\xa2\xff\x00\xb2\x00\xbd\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\xa3\xb3\xd1\xf1\x00\x00\xd2\xf2\x00\x00\x00\x00\x00\x00\x00"
    "\xd5\xf5\x00\x00\xc0\xe0\x00\x00\xd8\xf8\xa6\xb6\x00\x00\xaa\xba"
    "\xa9\xb9\xde\xfe\xab\xbb\x00\x00\x00\x00\x00\x00\x00\x00\xd9\xf9"
    "\xdb\xfb\x00\x00\x00\x00\x00\x00\x00\xac\xbc\xaf\xbf\xae\xbe\x00"
    "\x00\xc1\xc2\x00\xc4\x00\x00\xc7\x00\xc9\x00\xcb\x00\xcd\xce\x00"
    "\x00\x00\x00\xd3\xd4\x00\xd6\xd7\x00\x00\xda\x00\xdc\xdd\x00\xdf"
    "\x00\xe1\xe2\x00\xe4\x00\x00\xe7\x00\xe9\x00\xeb\x00\xed\xee\x00"
    "\x00\x00\x00\xf3\xf4\x00\xf6\xf7\x00\x00\xfa\x00\xfc\xfd\x00\x00"
};

static const unsigned short xmlunicodetable_ISO8859_3 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x0126, 0x02d8, 0x00a3, 0x00a4, 0x0000, 0x0124, 0x00a7,
    0x00a8, 0x0130, 0x015e, 0x011e, 0x0134, 0x00ad, 0x0000, 0x017b,
    0x00b0, 0x0127, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x0125, 0x00b7,
    0x00b8, 0x0131, 0x015f, 0x011f, 0x0135, 0x00bd, 0x0000, 0x017c,
    0x00c0, 0x00c1, 0x00c2, 0x0000, 0x00c4, 0x010a, 0x0108, 0x00c7,
    0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
    0x0000, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x0120, 0x00d6, 0x00d7,
    0x011c, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x016c, 0x015c, 0x00df,
    0x00e0, 0x00e1, 0x00e2, 0x0000, 0x00e4, 0x010b, 0x0109, 0x00e7,
    0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
    0x0000, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x0121, 0x00f6, 0x00f7,
    0x011d, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x016d, 0x015d, 0x02d9,
};

static const unsigned char xmltranscodetable_ISO8859_3 [48 + 7 * 64] = {
    "\x04\x00\x01\x06\x02\x05\x00\x00\x00\x00\x00\x03\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\x00\x00\xa3\xa4\x00\x00\xa7\xa8\x00\x00\x00\x00\xad\x00\x00"
    "\xb0\x00\xb2\xb3\xb4\xb5\x00\xb7\xb8\x00\x00\x00\x00\xbd\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\xc6\xe6\xc5\xe5\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xd8\xf8\xab\xbb"
    "\xd5\xf5\x00\x00\xa6\xb6\xa1\xb1\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xa9\xb9\x00\x00\xac\xbc\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\xa2\xff\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xf0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xde\xfe\xaa\xba"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xdd\xfd\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xaf\xbf\x00\x00\x00"
    "\xc0\xc1\xc2\x00\xc4\x00\x00\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
    "\x00\xd1\xd2\xd3\xd4\x00\xd6\xd7\x00\xd9\xda\xdb\xdc\x00\x00\xdf"
    "\xe0\xe1\xe2\x00\xe4\x00\x00\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
    "\x00\xf1\xf2\xf3\xf4\x00\xf6\xf7\x00\xf9\xfa\xfb\xfc\x00\x00\x00"
};

static const unsigned short xmlunicodetable_ISO8859_4 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x0104, 0x0138, 0x0156, 0x00a4, 0x0128, 0x013b, 0x00a7,
    0x00a8, 0x0160, 0x0112, 0x0122, 0x0166, 0x00ad, 0x017d, 0x00af,
    0x00b0, 0x0105, 0x02db, 0x0157, 0x00b4, 0x0129, 0x013c, 0x02c7,
    0x00b8, 0x0161, 0x0113, 0x0123, 0x0167, 0x014a, 0x017e, 0x014b,
    0x0100, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x012e,
    0x010c, 0x00c9, 0x0118, 0x00cb, 0x0116, 0x00cd, 0x00ce, 0x012a,
    0x0110, 0x0145, 0x014c, 0x0136, 0x00d4, 0x00d5, 0x00d6, 0x00d7,
    0x00d8, 0x0172, 0x00da, 0x00db, 0x00dc, 0x0168, 0x016a, 0x00df,
    0x0101, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x012f,
    0x010d, 0x00e9, 0x0119, 0x00eb, 0x0117, 0x00ed, 0x00ee, 0x012b,
    0x0111, 0x0146, 0x014d, 0x0137, 0x00f4, 0x00f5, 0x00f6, 0x00f7,
    0x00f8, 0x0173, 0x00fa, 0x00fb, 0x00fc, 0x0169, 0x016b, 0x02d9,
};

static const unsigned char xmltranscodetable_ISO8859_4 [48 + 6 * 64] = {
    "\x00\x00\x01\x05\x02\x03\x00\x00\x00\x00\x00\x04\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\x00\x00\x00\xa4\x00\x00\xa7\xa8\x00\x00\x00\x00\xad\x00\xaf"
    "\xb0\x00\x00\x00\xb4\x00\x00\x00\xb8\x00\x00\x00\x00\x00\x00\x00"
    "\xc0\xe0\x00\x00\xa1\xb1\x00\x00\x00\x00\x00\x00\xc8\xe8\x00\x00"
    "\xd0\xf0\xaa\xba\x00\x00\xcc\xec\xca\xea\x00\x00\x00\x00\x00\x00"
    "\x00\x00\xab\xbb\x00\x00\x00\x00\xa5\xb5\xcf\xef\x00\x00\xc7\xe7"
    "\x00\x00\x00\x00\x00\x00\xd3\xf3\xa2\x00\x00\xa6\xb6\x00\x00\x00"
    "\x00\x00\x00\x00\x00\xd1\xf1\x00\x00\x00\xbd\xbf\xd2\xf2\x00\x00"
    "\x00\x00\x00\x00\x00\x00\xa3\xb3\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xa9\xb9\x00\x00\x00\x00\xac\xbc\xdd\xfd\xde\xfe\x00\x00\x00\x00"
    "\x00\x00\xd9\xf9\x00\x00\x00\x00\x00\x00\x00\x00\x00\xae\xbe\x00"
    "\x00\x00\x00\x00\x00\x00\x00\xb7\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\x00\xb2\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\xc1\xc2\xc3\xc4\xc5\xc6\x00\x00\xc9\x00\xcb\x00\xcd\xce\x00"
    "\x00\x00\x00\x00\xd4\xd5\xd6\xd7\xd8\x00\xda\xdb\xdc\x00\x00\xdf"
    "\x00\xe1\xe2\xe3\xe4\xe5\xe6\x00\x00\xe9\x00\xeb\x00\xed\xee\x00"
    "\x00\x00\x00\x00\xf4\xf5\xf6\xf7\xf8\x00\xfa\xfb\xfc\x00\x00\x00"
};

static const unsigned short xmlunicodetable_ISO8859_5 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x0401, 0x0402, 0x0403, 0x0404, 0x0405, 0x0406, 0x0407,
    0x0408, 0x0409, 0x040a, 0x040b, 0x040c, 0x00ad, 0x040e, 0x040f,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e, 0x041f,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    0x0428, 0x0429, 0x042a, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043a, 0x043b, 0x043c, 0x043d, 0x043e, 0x043f,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044a, 0x044b, 0x044c, 0x044d, 0x044e, 0x044f,
    0x2116, 0x0451, 0x0452, 0x0453, 0x0454, 0x0455, 0x0456, 0x0457,
    0x0458, 0x0459, 0x045a, 0x045b, 0x045c, 0x00a7, 0x045e, 0x045f,
};

static const unsigned char xmltranscodetable_ISO8859_5 [48 + 6 * 64] = {
    "\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x02\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\x00\x00\x00\x00\x00\x00\xfd\x00\x00\x00\x00\x00\xad\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\x00\xae\xaf"
    "\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf"
    "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
    "\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf"
    "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
    "\x00\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\x00\xfe\xff"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\xf0\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
};

static const unsigned short xmlunicodetable_ISO8859_6 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x0000, 0x0000, 0x0000, 0x00a4, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x060c, 0x00ad, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x061b, 0x0000, 0x0000, 0x0000, 0x061f,
    0x0000, 0x0621, 0x0622, 0x0623, 0x0624, 0x0625, 0x0626, 0x0627,
    0x0628, 0x0629, 0x062a, 0x062b, 0x062c, 0x062d, 0x062e, 0x062f,
    0x0630, 0x0631, 0x0632, 0x0633, 0x0634, 0x0635, 0x0636, 0x0637,
    0x0638, 0x0639, 0x063a, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0640, 0x0641, 0x0642, 0x0643, 0x0644, 0x0645, 0x0646, 0x0647,
    0x0648, 0x0649, 0x064a, 0x064b, 0x064c, 0x064d, 0x064e, 0x064f,
    0x0650, 0x0651, 0x0652, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static const unsigned char xmltranscodetable_ISO8859_6 [48 + 5 * 64] = {
    "\x02\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x03\x04\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\x00\x00\x00\xa4\x00\x00\x00\x00\x00\x00\x00\x00\xad\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xac\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xbb\x00\x00\x00\xbf"
    "\x00\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
    "\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\x00\x00\x00\x00\x00"
    "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
    "\xf0\xf1\xf2\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
};

static const unsigned short xmlunicodetable_ISO8859_7 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x2018, 0x2019, 0x00a3, 0x0000, 0x0000, 0x00a6, 0x00a7,
    0x00a8, 0x00a9, 0x0000, 0x00ab, 0x00ac, 0x00ad, 0x0000, 0x2015,
    0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x0384, 0x0385, 0x0386, 0x00b7,
    0x0388, 0x0389, 0x038a, 0x00bb, 0x038c, 0x00bd, 0x038e, 0x038f,
    0x0390, 0x0391, 0x0392, 0x0393, 0x0394, 0x0395, 0x0396, 0x0397,
    0x0398, 0x0399, 0x039a, 0x039b, 0x039c, 0x039d, 0x039e, 0x039f,
    0x03a0, 0x03a1, 0x0000, 0x03a3, 0x03a4, 0x03a5, 0x03a6, 0x03a7,
    0x03a8, 0x03a9, 0x03aa, 0x03ab, 0x03ac, 0x03ad, 0x03ae, 0x03af,
    0x03b0, 0x03b1, 0x03b2, 0x03b3, 0x03b4, 0x03b5, 0x03b6, 0x03b7,
    0x03b8, 0x03b9, 0x03ba, 0x03bb, 0x03bc, 0x03bd, 0x03be, 0x03bf,
    0x03c0, 0x03c1, 0x03c2, 0x03c3, 0x03c4, 0x03c5, 0x03c6, 0x03c7,
    0x03c8, 0x03c9, 0x03ca, 0x03cb, 0x03cc, 0x03cd, 0x03ce, 0x0000,
};

static const unsigned char xmltranscodetable_ISO8859_7 [48 + 7 * 64] = {
    "\x04\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x06"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\x00\x00\xa3\x00\x00\xa6\xa7\xa8\xa9\x00\xab\xac\xad\x00\x00"
    "\xb0\xb1\xb2\xb3\x00\x00\x00\xb7\x00\x00\x00\xbb\x00\xbd\x00\x00"
    "\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\xaf\x00\x00\xa1\xa2\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\xb4\xb5\xb6\x00\xb8\xb9\xba\x00\xbc\x00\xbe\xbf"
    "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
    "\xd0\xd1\x00\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf"
    "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
    "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
};

static const unsigned short xmlunicodetable_ISO8859_8 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x0000, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a7,
    0x00a8, 0x00a9, 0x00d7, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af,
    0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x00b6, 0x00b7,
    0x00b8, 0x00b9, 0x00f7, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2017,
    0x05d0, 0x05d1, 0x05d2, 0x05d3, 0x05d4, 0x05d5, 0x05d6, 0x05d7,
    0x05d8, 0x05d9, 0x05da, 0x05db, 0x05dc, 0x05dd, 0x05de, 0x05df,
    0x05e0, 0x05e1, 0x05e2, 0x05e3, 0x05e4, 0x05e5, 0x05e6, 0x05e7,
    0x05e8, 0x05e9, 0x05ea, 0x0000, 0x0000, 0x200e, 0x200f, 0x0000,
};

static const unsigned char xmltranscodetable_ISO8859_8 [48 + 7 * 64] = {
    "\x02\x00\x01\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x06\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\x00\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\x00\xab\xac\xad\xae\xaf"
    "\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\x00\xbb\xbc\xbd\xbe\x00"
    "\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\xaa\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\xba\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xfd\xfe"
    "\x00\x00\x00\x00\x00\x00\x00\xdf\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
    "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
};

static const unsigned short xmlunicodetable_ISO8859_9 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a7,
    0x00a8, 0x00a9, 0x00aa, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af,
    0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x00b6, 0x00b7,
    0x00b8, 0x00b9, 0x00ba, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf,
    0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7,
    0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
    0x011e, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00d7,
    0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x0130, 0x015e, 0x00df,
    0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7,
    0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
    0x011f, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x00f7,
    0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x0131, 0x015f, 0x00ff,
};

static const unsigned char xmltranscodetable_ISO8859_9 [48 + 5 * 64] = {
    "\x00\x00\x01\x02\x03\x04\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf"
    "\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf"
    "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
    "\x00\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\x00\x00\xdf"
    "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
    "\x00\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\x00\x00\xff"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xd0\xf0"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xdd\xfd\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xde\xfe"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
};

static const unsigned short xmlunicodetable_ISO8859_10 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x0104, 0x0112, 0x0122, 0x012a, 0x0128, 0x0136, 0x00a7,
    0x013b, 0x0110, 0x0160, 0x0166, 0x017d, 0x00ad, 0x016a, 0x014a,
    0x00b0, 0x0105, 0x0113, 0x0123, 0x012b, 0x0129, 0x0137, 0x00b7,
    0x013c, 0x0111, 0x0161, 0x0167, 0x017e, 0x2015, 0x016b, 0x014b,
    0x0100, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x012e,
    0x010c, 0x00c9, 0x0118, 0x00cb, 0x0116, 0x00cd, 0x00ce, 0x00cf,
    0x00d0, 0x0145, 0x014c, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x0168,
    0x00d8, 0x0172, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x00de, 0x00df,
    0x0101, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x012f,
    0x010d, 0x00e9, 0x0119, 0x00eb, 0x0117, 0x00ed, 0x00ee, 0x00ef,
    0x00f0, 0x0146, 0x014d, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x0169,
    0x00f8, 0x0173, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x0138,
};

static const unsigned char xmltranscodetable_ISO8859_10 [48 + 7 * 64] = {
    "\x00\x00\x01\x06\x02\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\x00\x00\x00\x00\x00\x00\xa7\x00\x00\x00\x00\x00\xad\x00\x00"
    "\xb0\x00\x00\x00\x00\x00\x00\xb7\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xc0\xe0\x00\x00\xa1\xb1\x00\x00\x00\x00\x00\x00\xc8\xe8\x00\x00"
    "\xa9\xb9\xa2\xb2\x00\x00\xcc\xec\xca\xea\x00\x00\x00\x00\x00\x00"
    "\x00\x00\xa3\xb3\x00\x00\x00\x00\xa5\xb5\xa4\xb4\x00\x00\xc7\xe7"
    "\x00\x00\x00\x00\x00\x00\xa6\xb6\xff\x00\x00\xa8\xb8\x00\x00\x00"
    "\x00\x00\x00\x00\x00\xd1\xf1\x00\x00\x00\xaf\xbf\xd2\xf2\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xaa\xba\x00\x00\x00\x00\xab\xbb\xd7\xf7\xae\xbe\x00\x00\x00\x00"
    "\x00\x00\xd9\xf9\x00\x00\x00\x00\x00\x00\x00\x00\x00\xac\xbc\x00"
    "\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\xbd\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\xc1\xc2\xc3\xc4\xc5\xc6\x00\x00\xc9\x00\xcb\x00\xcd\xce\xcf"
    "\xd0\x00\x00\xd3\xd4\xd5\xd6\x00\xd8\x00\xda\xdb\xdc\xdd\xde\xdf"
    "\x00\xe1\xe2\xe3\xe4\xe5\xe6\x00\x00\xe9\x00\xeb\x00\xed\xee\xef"
    "\xf0\x00\x00\xf3\xf4\xf5\xf6\x00\xf8\x00\xfa\xfb\xfc\xfd\xfe\x00"
};

static const unsigned short xmlunicodetable_ISO8859_11 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x0e01, 0x0e02, 0x0e03, 0x0e04, 0x0e05, 0x0e06, 0x0e07,
    0x0e08, 0x0e09, 0x0e0a, 0x0e0b, 0x0e0c, 0x0e0d, 0x0e0e, 0x0e0f,
    0x0e10, 0x0e11, 0x0e12, 0x0e13, 0x0e14, 0x0e15, 0x0e16, 0x0e17,
    0x0e18, 0x0e19, 0x0e1a, 0x0e1b, 0x0e1c, 0x0e1d, 0x0e1e, 0x0e1f,
    0x0e20, 0x0e21, 0x0e22, 0x0e23, 0x0e24, 0x0e25, 0x0e26, 0x0e27,
    0x0e28, 0x0e29, 0x0e2a, 0x0e2b, 0x0e2c, 0x0e2d, 0x0e2e, 0x0e2f,
    0x0e30, 0x0e31, 0x0e32, 0x0e33, 0x0e34, 0x0e35, 0x0e36, 0x0e37,
    0x0e38, 0x0e39, 0x0e3a, 0x0000, 0x0000, 0x0000, 0x0000, 0x0e3f,
    0x0e40, 0x0e41, 0x0e42, 0x0e43, 0x0e44, 0x0e45, 0x0e46, 0x0e47,
    0x0e48, 0x0e49, 0x0e4a, 0x0e4b, 0x0e4c, 0x0e4d, 0x0e4e, 0x0e4f,
    0x0e50, 0x0e51, 0x0e52, 0x0e53, 0x0e54, 0x0e55, 0x0e56, 0x0e57,
    0x0e58, 0x0e59, 0x0e5a, 0x0e5b, 0x0000, 0x0000, 0x0000, 0x0000,
};

static const unsigned char xmltranscodetable_ISO8859_11 [48 + 6 * 64] = {
    "\x04\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x03\x05\x00\x00\x00\x00\x00\x00"
    "\x00\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf"
    "\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf"
    "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
    "\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\x00\x00\x00\x00\xdf"
    "\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
    "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
};

static const unsigned short xmlunicodetable_ISO8859_13 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x201d, 0x00a2, 0x00a3, 0x00a4, 0x201e, 0x00a6, 0x00a7,
    0x00d8, 0x00a9, 0x0156, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00c6,
    0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x201c, 0x00b5, 0x00b6, 0x00b7,
    0x00f8, 0x00b9, 0x0157, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00e6,
    0x0104, 0x012e, 0x0100, 0x0106, 0x00c4, 0x00c5, 0x0118, 0x0112,
    0x010c, 0x00c9, 0x0179, 0x0116, 0x0122, 0x0136, 0x012a, 0x013b,
    0x0160, 0x0143, 0x0145, 0x00d3, 0x014c, 0x00d5, 0x00d6, 0x00d7,
    0x0172, 0x0141, 0x015a, 0x016a, 0x00dc, 0x017b, 0x017d, 0x00df,
    0x0105, 0x012f, 0x0101, 0x0107, 0x00e4, 0x00e5, 0x0119, 0x0113,
    0x010d, 0x00e9, 0x017a, 0x0117, 0x0123, 0x0137, 0x012b, 0x013c,
    0x0161, 0x0144, 0x0146, 0x00f3, 0x014d, 0x00f5, 0x00f6, 0x00f7,
    0x0173, 0x0142, 0x015b, 0x016b, 0x00fc, 0x017c, 0x017e, 0x2019,
};

static const unsigned char xmltranscodetable_ISO8859_13 [48 + 7 * 64] = {
    "\x00\x00\x01\x04\x06\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\x00\xa2\xa3\xa4\x00\xa6\xa7\x00\xa9\x00\xab\xac\xad\xae\x00"
    "\xb0\xb1\xb2\xb3\x00\xb5\xb6\xb7\x00\xb9\x00\xbb\xbc\xbd\xbe\x00"
    "\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\x00\x00\xb4\xa1\xa5\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\xc4\xc5\xaf\x00\x00\xc9\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\xd3\x00\xd5\xd6\xd7\xa8\x00\x00\x00\xdc\x00\x00\xdf"
    "\x00\x00\x00\x00\xe4\xe5\xbf\x00\x00\xe9\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\xf3\x00\xf5\xf6\xf7\xb8\x00\x00\x00\xfc\x00\x00\x00"
    "\x00\xd9\xf9\xd1\xf1\xd2\xf2\x00\x00\x00\x00\x00\xd4\xf4\x00\x00"
    "\x00\x00\x00\x00\x00\x00\xaa\xba\x00\x00\xda\xfa\x00\x00\x00\x00"
    "\xd0\xf0\x00\x00\x00\x00\x00\x00\x00\x00\xdb\xfb\x00\x00\x00\x00"
    "\x00\x00\xd8\xf8\x00\x00\x00\x00\x00\xca\xea\xdd\xfd\xde\xfe\x00"
    "\xc2\xe2\x00\x00\xc0\xe0\xc3\xe3\x00\x00\x00\x00\xc8\xe8\x00\x00"
    "\x00\x00\xc7\xe7\x00\x00\xcb\xeb\xc6\xe6\x00\x00\x00\x00\x00\x00"
    "\x00\x00\xcc\xec\x00\x00\x00\x00\x00\x00\xce\xee\x00\x00\xc1\xe1"
    "\x00\x00\x00\x00\x00\x00\xcd\xed\x00\x00\x00\xcf\xef\x00\x00\x00"
};

static const unsigned short xmlunicodetable_ISO8859_14 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x1e02, 0x1e03, 0x00a3, 0x010a, 0x010b, 0x1e0a, 0x00a7,
    0x1e80, 0x00a9, 0x1e82, 0x1e0b, 0x1ef2, 0x00ad, 0x00ae, 0x0178,
    0x1e1e, 0x1e1f, 0x0120, 0x0121, 0x1e40, 0x1e41, 0x00b6, 0x1e56,
    0x1e81, 0x1e57, 0x1e83, 0x1e60, 0x1ef3, 0x1e84, 0x1e85, 0x1e61,
    0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7,
    0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
    0x0174, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x1e6a,
    0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x0176, 0x00df,
    0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7,
    0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
    0x0175, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x1e6b,
    0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x0177, 0x00ff,
};

static const unsigned char xmltranscodetable_ISO8859_14 [48 + 10 * 64] = {
    "\x00\x00\x01\x09\x04\x07\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\x00\x00\xa3\x00\x00\x00\xa7\x00\xa9\x00\x00\x00\xad\xae\x00"
    "\x00\x00\x00\x00\x00\x00\xb6\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x03\x08\x05\x06\x00\x00\x00\x00"
    "\x00\x00\xa1\xa2\x00\x00\x00\x00\x00\x00\xa6\xab\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xb0\xb1"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xa4\xa5\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xb2\xb3\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xa8\xb8\xaa\xba\xbd\xbe\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\xac\xbc\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\xd0\xf0\xde\xfe\xaf\x00\x00\x00\x00\x00\x00\x00"
    "\xb4\xb5\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\xb7\xb9\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xbb\xbf\x00\x00\x00\x00\x00\x00\x00\x00\xd7\xf7\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
    "\x00\xd1\xd2\xd3\xd4\xd5\xd6\x00\xd8\xd9\xda\xdb\xdc\xdd\x00\xdf"
    "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
    "\x00\xf1\xf2\xf3\xf4\xf5\xf6\x00\xf8\xf9\xfa\xfb\xfc\xfd\x00\xff"
};

static const unsigned short xmlunicodetable_ISO8859_15 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x20ac, 0x00a5, 0x0160, 0x00a7,
    0x0161, 0x00a9, 0x00aa, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af,
    0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x017d, 0x00b5, 0x00b6, 0x00b7,
    0x017e, 0x00b9, 0x00ba, 0x00bb, 0x0152, 0x0153, 0x0178, 0x00bf,
    0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7,
    0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
    0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00d7,
    0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x00de, 0x00df,
    0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7,
    0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
    0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x00f7,
    0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x00ff,
};

static const unsigned char xmltranscodetable_ISO8859_15 [48 + 6 * 64] = {
    "\x00\x00\x01\x05\x00\x04\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\xa1\xa2\xa3\x00\xa5\x00\xa7\x00\xa9\xaa\xab\xac\xad\xae\xaf"
    "\xb0\xb1\xb2\xb3\x00\xb5\xb6\xb7\x00\xb9\xba\xbb\x00\x00\x00\xbf"
    "\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xa4\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\xbc\xbd\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xa6\xa8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\xbe\x00\x00\x00\x00\xb4\xb8\x00"
    "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
    "\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf"
    "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
    "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff"
};

static const unsigned short xmlunicodetable_ISO8859_16 [128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x0104, 0x0105, 0x0141, 0x20ac, 0x201e, 0x0160, 0x00a7,
    0x0161, 0x00a9, 0x0218, 0x00ab, 0x0179, 0x00ad, 0x017a, 0x017b,
    0x00b0, 0x00b1, 0x010c, 0x0142, 0x017d, 0x201d, 0x00b6, 0x00b7,
    0x017e, 0x010d, 0x0219, 0x00bb, 0x0152, 0x0153, 0x0178, 0x017c,
    0x00c0, 0x00c1, 0x00c2, 0x0102, 0x00c4, 0x0106, 0x00c6, 0x00c7,
    0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
    0x0110, 0x0143, 0x00d2, 0x00d3, 0x00d4, 0x0150, 0x00d6, 0x015a,
    0x0170, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x0118, 0x021a, 0x00df,
    0x00e0, 0x00e1, 0x00e2, 0x0103, 0x00e4, 0x0107, 0x00e6, 0x00e7,
    0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
    0x0111, 0x0144, 0x00f2, 0x00f3, 0x00f4, 0x0151, 0x00f6, 0x015b,
    0x0171, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x0119, 0x021b, 0x00ff,
};

static const unsigned char xmltranscodetable_ISO8859_16 [48 + 9 * 64] = {
    "\x00\x00\x01\x08\x02\x03\x00\x00\x07\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
    "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
    "\xa0\x00\x00\x00\x00\x00\x00\xa7\x00\xa9\x00\xab\x00\xad\x00\x00"
    "\xb0\xb1\x00\x00\x00\x00\xb6\xb7\x00\x00\x00\xbb\x00\x00\x00\x00"
    "\x00\x00\xc3\xe3\xa1\xa2\xc5\xe5\x00\x00\x00\x00\xb2\xb9\x00\x00"
    "\xd0\xf0\x00\x00\x00\x00\x00\x00\xdd\xfd\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\xa3\xb3\xd1\xf1\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xd5\xf5\xbc\xbd\x00\x00\x00\x00\x00\x00\xd7\xf7\x00\x00\x00\x00"
    "\xa6\xa8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xd8\xf8\x00\x00\x00\x00\x00\x00\xbe\xac\xae\xaf\xbf\xb4\xb8\x00"
    "\x06\x00\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xa4\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xb5\xa5\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\xaa\xba\xde\xfe\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xc0\xc1\xc2\x00\xc4\x00\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
    "\x00\x00\xd2\xd3\xd4\x00\xd6\x00\x00\xd9\xda\xdb\xdc\x00\x00\xdf"
    "\xe0\xe1\xe2\x00\xe4\x00\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
    "\x00\x00\xf2\xf3\xf4\x00\xf6\x00\x00\xf9\xfa\xfb\xfc\x00\x00\xff"
};


/*
 * auto-generated functions for ISO-8859-2 .. ISO-8859-16
 */

static int ISO8859_2ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_2);
}
static int UTF8ToISO8859_2 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_2);
}

static int ISO8859_3ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_3);
}
static int UTF8ToISO8859_3 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_3);
}

static int ISO8859_4ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_4);
}
static int UTF8ToISO8859_4 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_4);
}

static int ISO8859_5ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_5);
}
static int UTF8ToISO8859_5 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_5);
}

static int ISO8859_6ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_6);
}
static int UTF8ToISO8859_6 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_6);
}

static int ISO8859_7ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_7);
}
static int UTF8ToISO8859_7 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_7);
}

static int ISO8859_8ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_8);
}
static int UTF8ToISO8859_8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_8);
}

static int ISO8859_9ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_9);
}
static int UTF8ToISO8859_9 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_9);
}

static int ISO8859_10ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_10);
}
static int UTF8ToISO8859_10 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_10);
}

static int ISO8859_11ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_11);
}
static int UTF8ToISO8859_11 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_11);
}

static int ISO8859_13ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_13);
}
static int UTF8ToISO8859_13 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_13);
}

static int ISO8859_14ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_14);
}
static int UTF8ToISO8859_14 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_14);
}

static int ISO8859_15ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_15);
}
static int UTF8ToISO8859_15 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_15);
}

static int ISO8859_16ToUTF8 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return ISO8859xToUTF8 (out, outlen, in, inlen, xmlunicodetable_ISO8859_16);
}
static int UTF8ToISO8859_16 (unsigned char* out, int *outlen,
    const unsigned char* in, int *inlen) {
    return UTF8ToISO8859x (out, outlen, in, inlen, xmltranscodetable_ISO8859_16);
}

#endif
#endif

