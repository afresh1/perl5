/*    dquote.c
 *
 * This file contains functions that are related to
 * parsing double-quotish expressions.
 *
*/

#include "EXTERN.h"
#define PERL_IN_DQUOTE_C
#include "perl.h"

/* XXX Add documentation after final interface and behavior is decided */

bool
Perl_grok_bslash_c(pTHX_ const char   source,
                         U8 *         result,
                         const char** message,
                         U32 *        packed_warn)
{
    PERL_ARGS_ASSERT_GROK_BSLASH_C;

    /* This returns TRUE if the \c? sequence is valid; FALSE otherwise.  If it
     * is valid, the sequence evaluates to a single character, which will be
     * stored into *result.
     *
     * source   is the character immediately after a '\c' sequence.
     * result   points to a char variable into which this function will store
     *          what the sequence evaluates to, if valid; unchanged otherwise.
     * message  A pointer to any warning or error message will be stored into
     *          this pointer; NULL if none.
     * packed_warn if NULL on input asks that this routine display any warning
     *          messages.  Otherwise, if the function found a warning, the
     *          packed warning categories will be stored into *packed_warn (and
     *          the corresponding message text into *message); 0 if none.
     */

    *message = NULL;
    if (packed_warn) *packed_warn = 0;

    if (! isPRINT_A(source)) {
        *message = "Character following \"\\c\" must be printable ASCII";
        return FALSE;
    }

    if (source == '{') {
        const char control = toCTRL('{');
        if (isPRINT_A(control)) {
            /* diag_listed_as: Use "%s" instead of "%s" */
            *message = Perl_form(aTHX_ "Use \"%c\" instead of \"\\c{\"", control);
        }
        else {
            *message = "Sequence \"\\c{\" invalid";
        }
        return FALSE;
    }

    *result = toCTRL(source);
    if (isPRINT_A(*result) && ckWARN(WARN_SYNTAX)) {
        U8 clearer[3];
        U8 i = 0;
        char format[] = "\"\\c%c\" is more clearly written simply as \"%s\"";

        if (! isWORDCHAR(*result)) {
            clearer[i++] = '\\';
        }
        clearer[i++] = *result;
        clearer[i++] = '\0';

        if (packed_warn) {
            *message = Perl_form(aTHX_ format, source, clearer);
            *packed_warn = packWARN(WARN_SYNTAX);
        }
        else {
            Perl_warner(aTHX_ packWARN(WARN_SYNTAX), format, source, clearer);
        }
    }

    return TRUE;
}

SV *
Perl_form_alien_digit_msg(pTHX_
        const U8 which,           /* 8 or 16 */
        const STRLEN valids_len,  /* length of input before first bad char */
        const char * const first_bad, /* Ptr to that bad char */
        const char * const send,      /* End of input string */
        const bool UTF,               /* Is it in UTF-8? */
        const bool braced)            /* Is it enclosed in {} */
{
    /* Generate a mortal SV containing an appropriate warning message about
     * alien characters found in an octal or hex constant given by the inputs.
     * The message looks like:
     *
     * Non-hex character '?' terminates \x early.  Resolved as "\x{...}"
     *
     */

    /* The usual worst case scenario: 2 chars to display per byte, plus \x{}
     * (leading zeros could take up more space).  Space for NUL is added by the
     * newSV() function */
    SV * display_char = newSV(2 * UTF8_MAXBYTES + 4);
    SV * message_sv = sv_newmortal();
    char symbol;

    PERL_ARGS_ASSERT_FORM_ALIEN_DIGIT_MSG;
    assert(which == 8 || which == 16);

    /* Calculate the display form of the character */
    if (    UVCHR_IS_INVARIANT(*first_bad)
        || (UTF && isUTF8_CHAR((U8 *) first_bad, (U8 *) send)))
    {
        pv_uni_display(display_char, (U8 *) first_bad, UTF8SKIP(first_bad),
                                                    (STRLEN) -1, UNI_DISPLAY_QQ);
    }
    else {  /* Is not UTF-8, or is illegal UTF-8.  Show just the one byte */

        /* It also isn't a UTF-8 invariant character, so no display shortcuts
         * are available.  Use \\x{...} */
	Perl_sv_setpvf(aTHX_ display_char, "\\x{%02x}", *first_bad);
    }

    /* Ready to start building the message */
    sv_setpvs(message_sv, "Non-");
    if (which == 8) {
        sv_catpvs(message_sv, "octal");
        if (braced) {
            symbol = 'o';
        }
        else {
            symbol = '0';   /* \008, for example */
        }
    }
    else {
        sv_catpvs(message_sv, "hex");
        symbol = 'x';
    }
    sv_catpvs(message_sv, " character ");

    if (isPRINT(*first_bad)) {
        sv_catpvs(message_sv, "'");
    }
    sv_catsv(message_sv, display_char);
    if (isPRINT(*first_bad)) {
        sv_catpvs(message_sv, "'");
    }
    Perl_sv_catpvf(aTHX_ message_sv, " terminates \\%c early.  Resolved as "
                                     "\"\\%c", symbol, symbol);

    if (braced) {
        sv_catpvs(message_sv, "{");
    }

    /* Octal constants have an extra leading 0, but \0 already includes that */
    if (symbol == 'o' && valids_len < 3) {
        sv_catpvs(message_sv, "0");
    }
    if (valids_len == 0) {  /* No legal digits at all */
        sv_catpvs(message_sv, "00");
    }
    else if (valids_len == 1) { /* Just one is legal */
        sv_catpvs(message_sv, "0");
    }
    sv_catpvn(message_sv, first_bad - valids_len, valids_len);

    if (braced) {
        sv_catpvs(message_sv, "}");
    }
    else {
        sv_catsv(message_sv, display_char);
    }
    sv_catpvs(message_sv, "\"");

    SvREFCNT_dec_NN(display_char);

    return message_sv;
}

bool
Perl_grok_bslash_o(pTHX_ char **s, const char * const send, UV *uv,
                      const char** message,
                      U32 *      packed_warn,
                      const bool strict,
                      const bool UTF)
{

/*  Documentation to be supplied when interface nailed down finally
 *  This returns FALSE if there is an error the caller should probably die
 *  from; otherwise TRUE.
 *	s   is the address of a pointer to a string.  **s is 'o', and the
 *	    previous character was a backslash.  At exit, *s will be advanced
 *	    to the byte just after those absorbed by this function.  Hence the
 *	    caller can continue parsing from there.  In the case of an error
 *	    when this function returns FALSE, so continuing to parse is not an
 *	    option, this routine has generally positioned *s to point just to
 *	    the right of the first bad spot, so that a message that has a "<--"
 *	    to mark the spot will be correctly positioned.
 *	send - 1  gives a limit in *s that this function is not permitted to
 *	    look beyond.  That is, the function may look at bytes only in the
 *	    range *s..send-1
 *	uv  points to a UV that will hold the output value, valid only if the
 *	    return from the function is TRUE
 *      message  A pointer to any warning or error message will be stored into
 *          this pointer; NULL if none.
 *      packed_warn if NULL on input asks that this routine display any warning
 *          messages.  Otherwise, if the function found a warning, the packed
 *          warning categories will be stored into *packed_warn (and the
 *          corresponding message text into *message); 0 if none.
 *	strict is true if this should fail instead of warn if there are
 *	    non-octal digits within the braces
 *	UTF is true iff the string *s is encoded in UTF-8.
 */
    char* e;
    STRLEN numbers_len;
    NV overflowed = 0.0;
    I32 flags =   PERL_SCAN_ALLOW_UNDERSCORES
		| PERL_SCAN_DISALLOW_PREFIX
                | PERL_SCAN_SILENT_NON_PORTABLE
	        | PERL_SCAN_SILENT_ILLDIGIT;

    PERL_ARGS_ASSERT_GROK_BSLASH_O;

    assert(*(*s - 1) == '\\');
    assert(* *s       == 'o');

    *message = NULL;
    if (packed_warn) *packed_warn = 0;

    (*s)++;

    if (send <= *s || **s != '{') {
	*message = "Missing braces on \\o{}";
	return FALSE;
    }

    e = (char *) memchr(*s, '}', send - *s);
    if (!e) {
        (*s)++;  /* Move past the '{' */
        while (isOCTAL(**s)) { /* Position beyond the legal digits */
            (*s)++;
        }
        *message = "Missing right brace on \\o{";
	return FALSE;
    }

    (*s)++;    /* Point to expected first digit (could be first byte of utf8
                  sequence if not a digit) */
    numbers_len = e - *s;
    if (numbers_len == 0) {
        (*s)++;    /* Move past the '}' */
	*message = "Empty \\o{}";
	return FALSE;
    }

    *uv = grok_oct(*s, &numbers_len, &flags, &overflowed);
    if (overflowed != 0.0) {
        *s = e;
        *message = Perl_form(aTHX_ "Use of code point %a is not allowed; the"
                                   " permissible max is %a (0%" UVof ")",
                                   overflowed, (NV) MAX_LEGAL_CP, MAX_LEGAL_CP);
        return FALSE;
    }

    /* Note that if has non-octal, will ignore everything starting with that up
     * to the '}' */
    if (numbers_len != (STRLEN) (e - *s)) {
        *s += numbers_len;
        if (strict) {
            *s += (UTF) ? UTF8_SAFE_SKIP(*s, send) : 1;
            *message = "Non-octal character";
            return FALSE;
        }

        if (ckWARN(WARN_DIGIT)) {
            SV* message_sv = form_alien_digit_msg(8, numbers_len, *s, send,
                                                                    UTF, TRUE);
            if (packed_warn) {
                *message = SvPV_nolen(message_sv);
                *packed_warn = packWARN(WARN_DIGIT);
            }
            else {
                Perl_warner(aTHX_ packWARN(WARN_DIGIT), "%s",
                                                        SvPV_nolen(message_sv));
            }
        }
    }

    /* Return past the '}' */
    *s = e + 1;

    return TRUE;
}

bool
Perl_grok_bslash_x(pTHX_ char ** s, const char * const send, UV *uv,
                      const char** message,
                      U32 *      packed_warn,
                      const bool strict,
                      const bool UTF)
{

/*  Documentation to be supplied when interface nailed down finally
 *  This returns FALSE if there is an error which the caller need not recover
 *  from; otherwise TRUE.
 *  It guarantees that the returned codepoint, *uv, when expressed as
 *  utf8 bytes, would fit within the skipped "\x{...}" bytes.
 *
 *  On input:
 *	s   is the address of a pointer to a string.  **s is 'x', and the
 *	    previous character was a backslash.  At exit, *s will be advanced
 *	    to the byte just after those absorbed by this function.  Hence the
 *	    caller can continue parsing from there.  In the case of an error,
 *	    this routine has generally positioned *s to point just to the right
 *	    of the first bad spot, so that a message that has a "<--" to mark
 *	    the spot will be correctly positioned.
 *	send - 1  gives a limit in *s that this function is not permitted to
 *	    look beyond.  That is, the function may look at bytes only in the
 *	    range *s..send-1
 *	uv  points to a UV that will hold the output value, valid only if the
 *	    return from the function is TRUE
 *      message  A pointer to any warning or error message will be stored into
 *          this pointer; NULL if none.
 *      packed_warn if NULL on input asks that this routine display any warning
 *          messages.  Otherwise, if the function found a warning, the packed
 *          warning categories will be stored into *packed_warn (and the
 *          corresponding message text into *message); 0 if none.
 *          will be stored into this pointer; 0 if none.
 *	strict is true if anything out of the ordinary should cause this to
 *	    fail instead of warn or be silent.  For example, it requires
 *	    exactly 2 digits following the \x (when there are no braces).
 *	    3 digits could be a mistake, so is forbidden in this mode.
 *	UTF is true iff the string *s is encoded in UTF-8.
 */
    char* e;
    STRLEN numbers_len;
    NV overflowed = 0.0;
    I32 flags = PERL_SCAN_DISALLOW_PREFIX
	      | PERL_SCAN_SILENT_ILLDIGIT
              | PERL_SCAN_NOTIFY_ILLDIGIT
              | PERL_SCAN_SILENT_NON_PORTABLE;

    PERL_ARGS_ASSERT_GROK_BSLASH_X;

    assert(*(*s - 1) == '\\');
    assert(* *s      == 'x');

    *message = NULL;
    if (packed_warn) *packed_warn = 0;

    (*s)++;

    if (send <= *s) {
        if (strict) {
            *message = "Empty \\x";
            return FALSE;
        }

        /* Sadly, to preserve backcompat, an empty \x at the end of string is
         * interpreted as a NUL */
        *uv = 0;
        return TRUE;
    }

    if (**s != '{') {
        numbers_len = (strict) ? 3 : 2;

	*uv = grok_hex(*s, &numbers_len, &flags, NULL);
	*s += numbers_len;

        if (numbers_len != 2 && (strict || (flags & PERL_SCAN_NOTIFY_ILLDIGIT))) {
            if (numbers_len == 3) { /* numbers_len 3 only happens with strict */
                *message = "Use \\x{...} for more than two hex characters";
                return FALSE;
            }
            else if (strict) {
                    *s += (UTF) ? UTF8_SAFE_SKIP(*s, send) : 1;
                    *message = "Non-hex character";
                    return FALSE;
            }
            else if (ckWARN(WARN_DIGIT)) {
                SV* message_sv = form_alien_digit_msg(16, numbers_len, *s, send,
                                                                    UTF, FALSE);

                if (! packed_warn) {
                    Perl_warner(aTHX_ packWARN(WARN_DIGIT), "%s",
                                                        SvPV_nolen(message_sv));
                }
                else {
                    *message = SvPV_nolen(message_sv);
                    *packed_warn = packWARN(WARN_DIGIT);
                }
            }
        }
	return TRUE;
    }

    e = (char *) memchr(*s, '}', send - *s);
    if (!e) {
        (*s)++;  /* Move past the '{' */
        while (*s < send && isXDIGIT(**s)) { /* Position beyond legal digits */
            (*s)++;
        }
        /* XXX The corresponding message above for \o is just '\\o{'; other
         * messages for other constructs include the '}', so are inconsistent.
         */
	*message = "Missing right brace on \\x{}";
	return FALSE;
    }

    (*s)++;    /* Point to expected first digit (could be first byte of utf8
                  sequence if not a digit) */
    numbers_len = e - *s;
    if (numbers_len == 0) {
        if (strict) {
            (*s)++;    /* Move past the } */
            *message = "Empty \\x{}";
            return FALSE;
        }
        *s = e + 1;
        *uv = 0;
        return TRUE;
    }

    flags |= PERL_SCAN_ALLOW_UNDERSCORES;

    *uv = grok_hex(*s, &numbers_len, &flags, &overflowed);
    if (overflowed != 0.0) {
        *s = e;
        *message = Perl_form(aTHX_ "Use of code point %a is not allowed; the"
                                   " permissible max is %a (0x%" UVXf ")",
                                   overflowed, (NV) MAX_LEGAL_CP, MAX_LEGAL_CP);
        return FALSE;
    }

    if (numbers_len != (STRLEN) (e - *s)) {
        *s += numbers_len;
        if (strict) {
            *s += (UTF) ? UTF8_SAFE_SKIP(*s, send) : 1;
            *message = "Non-hex character";
            return FALSE;
        }

        if (ckWARN(WARN_DIGIT)) {
            SV* message_sv = form_alien_digit_msg(16, numbers_len, *s, send,
                                                                    UTF, TRUE);
            if (! packed_warn) {
                Perl_warner(aTHX_ packWARN(WARN_DIGIT), "%s",
                                                        SvPV_nolen(message_sv));
            }
            else {
                *message = SvPV_nolen(message_sv);
                *packed_warn = packWARN(WARN_DIGIT);
            }
        }
    }

    /* Return past the '}' */
    *s = e + 1;

    return TRUE;
}

/*
 * ex: set ts=8 sts=4 sw=4 et:
 */
