
#if !defined (__LIBEBOOK_H_INSIDE__) && !defined (LIBEBOOK_COMPILATION)
#error "Only <libebook/libebook.h> should be included directly."
#endif

#ifndef __E_BOOK_QUERY_H__
#define __E_BOOK_QUERY_H__

#include <libebook/e-contact.h>

G_BEGIN_DECLS

#define E_TYPE_BOOK_QUERY (e_book_query_get_type ())

typedef struct EBookQuery EBookQuery;

/**
 * EBookQueryTest:
 * @E_BOOK_QUERY_IS: look for exact match of the supplied test value
 * @E_BOOK_QUERY_CONTAINS: check if a field contains the test value
 * @E_BOOK_QUERY_BEGINS_WITH: check if a field starts with the test value
 * @E_BOOK_QUERY_ENDS_WITH: check if a field ends with the test value
 * @E_BOOK_QUERY_EQUALS_PHONE_NUMBER: check that a field and the test value
 * match exactly when interpreted as phone number, that is after stripping
 * formatting like dashes, dots and spaces. See E_PHONE_NUMBER_MATCH_EXACT.
 * @E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER: check that a field and the
 * test value match when interpreted as phone number, except for the
 * (omitted) country code.
 * @E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER: check that a field and the test
 * value match is the sense that both values appear to be phone numbers,
 * and one might be a part (suffix) of the other.
 *
 * The kind of test a query created by e_book_query_field_test() shall perform.
 *
 * See also: E_PHONE_NUMBER_MATCH_EXACT, E_PHONE_NUMBER_MATCH_NATIONAL and
 * E_PHONE_NUMBER_MATCH_SHORT.
 **/
typedef enum {
  E_BOOK_QUERY_IS,
  E_BOOK_QUERY_CONTAINS,
  E_BOOK_QUERY_BEGINS_WITH,
  E_BOOK_QUERY_ENDS_WITH,

  E_BOOK_QUERY_EQUALS_PHONE_NUMBER,
  E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
  E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER

  /*
    Consider these "coming soon".

    E_BOOK_QUERY_LT,
    E_BOOK_QUERY_LE,
    E_BOOK_QUERY_GT,
    E_BOOK_QUERY_GE,
    E_BOOK_QUERY_EQ,
  */
} EBookQueryTest;

EBookQuery * e_book_query_from_string  (const gchar *query_string);
gchar *       e_book_query_to_string    (EBookQuery *q);

EBookQuery * e_book_query_ref          (EBookQuery *q);
void        e_book_query_unref        (EBookQuery *q);

EBookQuery * e_book_query_and          (gint nqs, EBookQuery **qs, gboolean unref);
EBookQuery * e_book_query_andv         (EBookQuery *q, ...) G_GNUC_NULL_TERMINATED;
EBookQuery * e_book_query_or           (gint nqs, EBookQuery **qs, gboolean unref);
EBookQuery * e_book_query_orv          (EBookQuery *q, ...) G_GNUC_NULL_TERMINATED;

EBookQuery * e_book_query_not          (EBookQuery *q, gboolean unref);

EBookQuery * e_book_query_field_exists (EContactField   field);
EBookQuery * e_book_query_vcard_field_exists (const gchar *field);
EBookQuery * e_book_query_field_test   (EContactField   field,
				       EBookQueryTest     test,
				       const gchar        *value);
EBookQuery * e_book_query_vcard_field_test (const gchar    *field,
				       EBookQueryTest     test,
				       const gchar        *value);

/* a special any field contains query */
EBookQuery * e_book_query_any_field_contains (const gchar  *value);

GType       e_book_query_get_type (void);
EBookQuery * e_book_query_copy     (EBookQuery *q);

G_END_DECLS

#endif /* __E_BOOK_QUERY_H__ */
