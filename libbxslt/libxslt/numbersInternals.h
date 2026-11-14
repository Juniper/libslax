/*
 * Summary: Implementation of the XSLT number functions
 * Description: Implementation of the XSLT number functions
 *
 * Copy: See Copyright for the status of this software.
 *
 * Author: Bjorn Reese <breese@users.sourceforge.net> and Daniel Veillard
 */

#ifndef __XML_XSLT_NUMBERSINTERNALS_H__
#define __XML_XSLT_NUMBERSINTERNALS_H__

#include <libxml/tree.h>
#include "xsltexports.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _xsltCompMatch;

/**
 * xsltNumberData:
 *
 * This data structure is just a wrapper to pass xsl:number data in.
 */
typedef struct _xsltNumberData xsltNumberData;
typedef xsltNumberData *xsltNumberDataPtr;

struct _xsltNumberData {
    const xmlChar *level;
    const xmlChar *count;
    const xmlChar *from;
    const xmlChar *value;
    const xmlChar *format;
    int has_format;
    int digitsPerGroup;
    int groupingCharacter;
    int groupingCharacterLen;
    const xmlChar *lang;	/* language for alphabet selection, if any */
    int letterValue;	/* XSLT_NUMBER_LV_* ; disambiguates 'i'/'I' */
    xmlDocPtr doc;
    xmlNodePtr node;
    struct _xsltCompMatch *countPat;
    struct _xsltCompMatch *fromPat;

    /*
     * accelerators
     */
};

/**
 * xsltNumberLetterValue:
 *
 * Values for xsltNumberData.letterValue, from xsl:number's
 * letter-value attribute.  This only affects the 'i'/'I' format
 * token: traditional keeps the current roman-numeral interpretation,
 * while alphabetic switches it to the plain alphabetic sequence (the
 * same one already used for 'a'/'A').
 */
#define XSLT_NUMBER_LV_DEFAULT     0
#define XSLT_NUMBER_LV_TRADITIONAL 1
#define XSLT_NUMBER_LV_ALPHABETIC  2

/**
 * xsltFormatNumberInfo,:
 *
 * This data structure lists the various parameters needed to format numbers.
 */
typedef struct _xsltFormatNumberInfo xsltFormatNumberInfo;
typedef xsltFormatNumberInfo *xsltFormatNumberInfoPtr;

struct _xsltFormatNumberInfo {
    int integer_hash;   /* Number of '#' in integer part */
    int integer_digits; /* Number of '0' in integer part */
    int frac_digits;    /* Number of '0' in fractional part */
    int frac_hash;      /* Number of '#' in fractional part */
    int group;          /* Number of chars per display 'group' */
    int multiplier;     /* Scaling for percent or permille */
    char add_decimal;   /* Flag for whether decimal point appears in pattern */
    char
        is_multiplier_set; /* Flag to catch multiple occurences of percent/permille */
    char is_negative_pattern; /* Flag for processing -ve prefix/suffix */
};

#ifdef __cplusplus
}
#endif
#endif /* __XML_XSLT_NUMBERSINTERNALS_H__ */
