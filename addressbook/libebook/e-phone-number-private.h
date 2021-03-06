/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2012,2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Mathias Hasselmann <mathias@openismus.com>
 */

/* NOTE: Keeping API documentation in this header file because gtkdoc-mkdb
 * explicitly only scans .h and .c files, but ignores .cpp files. */

/**
 * SECTION: e-phone-utils
 * @include: libedataserver/libedataserver.h
 * @short_description: Phone number support
 *
 * This modules provides utility functions for parsing and formatting
 * phone numbers. Under the hood it uses Google's libphonenumber.
 **/

#if !defined (__LIBEBOOK_H_INSIDE__) && !defined (LIBEBOOK_COMPILATION)
#error "Only <libebook/libebook.h> should be included directly."
#endif

#ifndef E_PHONE_NUMBER_PRIVATE_H
#define E_PHONE_NUMBER_PRIVATE_H

#include "e-phone-number.h"

G_BEGIN_DECLS

#if __GNUC__ >= 4
#define E_PHONE_NUMBER_LOCAL __attribute__ ((visibility ("hidden")))
#else
#define E_PHONE_NUMBER_LOCAL
#endif

/* defined and used in e-phone-number.c, but also used by e-phone-number-private.cpp */

E_PHONE_NUMBER_LOCAL void		_e_phone_number_set_error		(GError **error,
										 EPhoneNumberError code);

#ifdef ENABLE_PHONENUMBER

/* defined in e-phone-number-private.cpp, and used by by e-phone-number.c */

E_PHONE_NUMBER_LOCAL EPhoneNumber *	_e_phone_number_cxx_from_string		(const gchar *phone_number,
										 const gchar *country_code,
										 GError **error);
E_PHONE_NUMBER_LOCAL gchar *		_e_phone_number_cxx_to_string		(const EPhoneNumber *phone_number,
										 EPhoneNumberFormat format);
E_PHONE_NUMBER_LOCAL EPhoneNumberMatch	_e_phone_number_cxx_compare		(const EPhoneNumber *first_number,
										 const EPhoneNumber *second_number);
E_PHONE_NUMBER_LOCAL EPhoneNumberMatch	_e_phone_number_cxx_compare_strings	(const gchar *first_number,
										 const gchar *second_number,
										 GError **error);
E_PHONE_NUMBER_LOCAL EPhoneNumber *	_e_phone_number_cxx_copy		(const EPhoneNumber *phone_number);
E_PHONE_NUMBER_LOCAL void		_e_phone_number_cxx_free		(EPhoneNumber *phone_number);

#endif /* ENABLE_PHONENUMBER */

G_END_DECLS

#endif /* E_PHONE_NUMBER_PRIVATE_H */
