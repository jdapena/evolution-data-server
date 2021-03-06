/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 * Author:
 *  Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-pop3-engine.h"
#include "camel-pop3-stream.h"

/* max 'outstanding' bytes in output stream, so we can't deadlock waiting
 * for the server to accept our data when pipelining */
#define CAMEL_POP3_SEND_LIMIT (1024)

extern CamelServiceAuthType camel_pop3_password_authtype;
extern CamelServiceAuthType camel_pop3_apop_authtype;

#define dd(x) (camel_debug ("pop3")?(x):0)

static gboolean get_capabilities (CamelPOP3Engine *pe, GCancellable *cancellable, GError **error);

G_DEFINE_TYPE (CamelPOP3Engine, camel_pop3_engine, CAMEL_TYPE_OBJECT)

static void
pop3_engine_dispose (GObject *object)
{
	CamelPOP3Engine *engine = CAMEL_POP3_ENGINE (object);

	if (engine->stream != NULL) {
		g_object_unref (engine->stream);
		engine->stream = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_pop3_engine_parent_class)->dispose (object);
}

static void
pop3_engine_finalize (GObject *object)
{
	CamelPOP3Engine *engine = CAMEL_POP3_ENGINE (object);

	/* FIXME: Also flush/free any outstanding requests, etc */

	g_list_free (engine->auth);
	g_free (engine->apop);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_pop3_engine_parent_class)->finalize (object);
}

static void
camel_pop3_engine_class_init (CamelPOP3EngineClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = pop3_engine_dispose;
	object_class->finalize = pop3_engine_finalize;
}

static void
camel_pop3_engine_init (CamelPOP3Engine *engine)
{
	g_queue_init (&engine->active);
	g_queue_init (&engine->queue);
	g_queue_init (&engine->done);
	engine->state = CAMEL_POP3_ENGINE_DISCONNECT;
}

static gint
read_greeting (CamelPOP3Engine *pe,
               GCancellable *cancellable)
{
	guchar *line, *apop, *apopend;
	guint len;

	g_return_val_if_fail (pe != NULL, -1);

	/* first, read the greeting */
	if (camel_pop3_stream_line (pe->stream, &line, &len, cancellable, NULL) == -1
	    || strncmp ((gchar *) line, "+OK", 3) != 0)
		return -1;

	if ((apop = (guchar *) strchr ((gchar *) line + 3, '<'))
	    && (apopend = (guchar *) strchr ((gchar *) apop, '>'))) {
		apopend[1] = 0;
		pe->apop = g_strdup ((gchar *) apop);
		pe->capa = CAMEL_POP3_CAP_APOP;
		pe->auth = g_list_append (pe->auth, &camel_pop3_apop_authtype);
	}

	pe->auth = g_list_prepend (pe->auth, &camel_pop3_password_authtype);

	return 0;
}

/**
 * camel_pop3_engine_new:
 * @source: source stream
 * @flags: engine flags
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: optional #GError, or %NULL
 *
 * Returns a NULL stream.  A null stream is always at eof, and
 * always returns success for all reads and writes.
 *
 * Returns: the stream
 **/
CamelPOP3Engine *
camel_pop3_engine_new (CamelStream *source,
                       guint32 flags,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelPOP3Engine *pe;

	pe = g_object_new (CAMEL_TYPE_POP3_ENGINE, NULL);

	pe->stream = (CamelPOP3Stream *) camel_pop3_stream_new (source);
	pe->state = CAMEL_POP3_ENGINE_AUTH;
	pe->flags = flags;

	if (read_greeting (pe, cancellable) == -1 ||
	    !get_capabilities (pe, cancellable, error)) {
		g_object_unref (pe);
		return NULL;
	}

	return pe;
}

/**
 * camel_pop3_engine_reget_capabilities:
 * @engine: pop3 engine
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: optional #GError, or %NULL
 *
 * Regets server capabilities (needed after a STLS command is issued for example).
 **/
gboolean
camel_pop3_engine_reget_capabilities (CamelPOP3Engine *engine,
                                      GCancellable *cancellable,
                                      GError **error)
{
	g_return_val_if_fail (CAMEL_IS_POP3_ENGINE (engine), FALSE);

	return get_capabilities (engine, cancellable, error);
}

/* TODO: read implementation too?
 * etc? */
static struct {
	const gchar *cap;
	guint32 flag;
} capa[] = {
	{ "APOP" , CAMEL_POP3_CAP_APOP },
	{ "TOP" , CAMEL_POP3_CAP_TOP },
	{ "UIDL", CAMEL_POP3_CAP_UIDL },
	{ "PIPELINING", CAMEL_POP3_CAP_PIPE },
	{ "STLS", CAMEL_POP3_CAP_STLS },  /* STARTTLS */
};

static void
cmd_capa (CamelPOP3Engine *pe,
          CamelPOP3Stream *stream,
          GCancellable *cancellable,
          GError **error,
          gpointer data)
{
	guchar *line, *tok, *next;
	guint len;
	gint ret;
	gint i;
	CamelServiceAuthType *auth;

	dd (printf ("cmd_capa\n"));

	g_return_if_fail (pe != NULL);

	do {
		ret = camel_pop3_stream_line (stream, &line, &len, cancellable, error);
		if (ret >= 0) {
			if (strncmp ((gchar *) line, "SASL ", 5) == 0) {
				tok = line + 5;
				dd (printf ("scanning tokens '%s'\n", tok));
				while (tok) {
					next = (guchar *) strchr ((gchar *) tok, ' ');
					if (next)
						*next++ = 0;
					auth = camel_sasl_authtype ((const gchar *) tok);
					if (auth) {
						dd (printf ("got auth type '%s'\n", tok));
						pe->auth = g_list_prepend (pe->auth, auth);
					} else {
						dd (printf ("unsupported auth type '%s'\n", tok));
					}
					tok = next;
				}
			} else {
				for (i = 0; i < G_N_ELEMENTS (capa); i++) {
					if (strcmp ((gchar *) capa[i].cap, (gchar *) line) == 0)
						pe->capa |= capa[i].flag;
				}
			}
		}
	} while (ret > 0);
}

static gboolean
get_capabilities (CamelPOP3Engine *pe,
                  GCancellable *cancellable,
                  GError **error)
{
	CamelPOP3Command *pc;
	GError *local_error = NULL;

	g_return_val_if_fail (pe != NULL, FALSE);

	if (!(pe->flags & CAMEL_POP3_ENGINE_DISABLE_EXTENSIONS)) {
		pc = camel_pop3_engine_command_new (pe, CAMEL_POP3_COMMAND_MULTI, cmd_capa, NULL, cancellable, NULL, "CAPA\r\n");
		while (camel_pop3_engine_iterate (pe, pc, cancellable, NULL) > 0)
			;
		camel_pop3_engine_command_free (pe, pc);

		if (pe->state == CAMEL_POP3_ENGINE_TRANSACTION && !(pe->capa & CAMEL_POP3_CAP_UIDL)) {
			/* check for UIDL support manually */
			pc = camel_pop3_engine_command_new (pe, CAMEL_POP3_COMMAND_SIMPLE, NULL, NULL, cancellable, &local_error, "UIDL 1\r\n");
			while (camel_pop3_engine_iterate (pe, pc, cancellable, &local_error) > 0)
				;

			if (pc->state == CAMEL_POP3_COMMAND_OK)
				pe->capa |= CAMEL_POP3_CAP_UIDL;

			camel_pop3_engine_command_free (pe, pc);
		}
	}

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/* returns true if the command was sent, false if it was just queued */
static gboolean
engine_command_queue (CamelPOP3Engine *pe,
                      CamelPOP3Command *pc,
                      GCancellable *cancellable,
                      GError **error)
{
	g_return_val_if_fail (pe != NULL, FALSE);
	g_return_val_if_fail (pc != NULL, FALSE);

	if (((pe->capa & CAMEL_POP3_CAP_PIPE) == 0 || (pe->sentlen + strlen (pc->data)) > CAMEL_POP3_SEND_LIMIT)
	    && pe->current != NULL) {
		g_queue_push_tail (&pe->queue, pc);
		return FALSE;
	}

	/* ??? */
	if (camel_stream_write ((CamelStream *) pe->stream, pc->data, strlen (pc->data), cancellable, error) == -1) {
		g_queue_push_tail (&pe->queue, pc);
		return FALSE;
	}

	pe->sentlen += strlen (pc->data);

	pc->state = CAMEL_POP3_COMMAND_DISPATCHED;

	if (pe->current == NULL)
		pe->current = pc;
	else
		g_queue_push_tail (&pe->active, pc);

	return TRUE;
}

/* returns -1 on error, 0 when no work to do, or >0 if work remaining */
gint
camel_pop3_engine_iterate (CamelPOP3Engine *pe,
                           CamelPOP3Command *pcwait,
                           GCancellable *cancellable,
                           GError **error)
{
	guchar *p;
	guint len;
	CamelPOP3Command *pc;
	GList *link;

	g_return_val_if_fail (pe != NULL, -1);

	if (pcwait && pcwait->state >= CAMEL_POP3_COMMAND_OK)
		return 0;

	pc = pe->current;
	if (pc == NULL)
		return 0;

	/* LOCK */

	if (camel_pop3_stream_line (pe->stream, &pe->line, &pe->linelen, cancellable, error) == -1)
		goto ioerror;

	p = pe->line;
	switch (p[0]) {
	case '+':
		dd (printf ("Got + response\n"));
		if (pc->flags & CAMEL_POP3_COMMAND_MULTI) {
			pc->state = CAMEL_POP3_COMMAND_DATA;
			camel_pop3_stream_set_mode (pe->stream, CAMEL_POP3_STREAM_DATA);

			if (pc->func)
				pc->func (pe, pe->stream, cancellable, error, pc->func_data);

			/* Make sure we get all data before going back to command mode */
			while (camel_pop3_stream_getd (pe->stream, &p, &len, cancellable, error) > 0)
				;
			camel_pop3_stream_set_mode (pe->stream, CAMEL_POP3_STREAM_LINE);
		} else {
			pc->state = CAMEL_POP3_COMMAND_OK;
		}
		break;
	case '-': {
		const gchar *text = (const gchar *) p;

		pc->state = CAMEL_POP3_COMMAND_ERR;
		pc->error_str = g_strdup (g_ascii_strncasecmp (text, "-ERR ", 5) == 0 ? text + 5 : text + 1);
		}
		break;
	default:
		/* what do we do now?  f'knows! */
		g_warning ("Bad server response: %s\n", p);
		pc->state = CAMEL_POP3_COMMAND_ERR;
		pc->error_str = g_strdup ((const gchar *) p + 1);
		break;
	}

	g_queue_push_tail (&pe->done, pc);
	pe->sentlen -= strlen (pc->data);

	/* Set next command */
	pe->current = g_queue_pop_head (&pe->active);

	/* Check the queue for any commands we can now send also. */
	link = g_queue_peek_head_link (&pe->queue);

	while (link != NULL) {
		pc = (CamelPOP3Command *) link->data;

		if (((pe->capa & CAMEL_POP3_CAP_PIPE) == 0 || (pe->sentlen + strlen (pc->data)) > CAMEL_POP3_SEND_LIMIT)
		    && pe->current != NULL)
			break;

		if (camel_stream_write ((CamelStream *) pe->stream, pc->data, strlen (pc->data), cancellable, error) == -1)
			goto ioerror;

		pe->sentlen += strlen (pc->data);
		pc->state = CAMEL_POP3_COMMAND_DISPATCHED;

		if (pe->current == NULL)
			pe->current = pc;
		else
			g_queue_push_tail (&pe->active, pc);

		g_queue_delete_link (&pe->queue, link);
		link = g_queue_peek_head_link (&pe->queue);
	}

	/* UNLOCK */

	if (pcwait && pcwait->state >= CAMEL_POP3_COMMAND_OK)
		return 0;

	return pe->current == NULL ? 0 : 1;

ioerror:
	/* We assume all outstanding commands will fail. */

	while ((pc = g_queue_pop_head (&pe->active)) != NULL) {
		pc->state = CAMEL_POP3_COMMAND_ERR;
		g_queue_push_tail (&pe->done, pc);
	}

	while ((pc = g_queue_pop_head (&pe->queue)) != NULL) {
		pc->state = CAMEL_POP3_COMMAND_ERR;
		g_queue_push_tail (&pe->done, pc);
	}

	if (pe->current != NULL) {
		pe->current->state = CAMEL_POP3_COMMAND_ERR;
		g_queue_push_tail (&pe->done, pe->current);
		pe->current = NULL;
	}

	return -1;
}

CamelPOP3Command *
camel_pop3_engine_command_new (CamelPOP3Engine *pe,
                               guint32 flags,
                               CamelPOP3CommandFunc func,
                               gpointer data,
                               GCancellable *cancellable,
                               GError **error,
                               const gchar *fmt,
                               ...)
{
	CamelPOP3Command *pc;
	va_list ap;

	g_return_val_if_fail (pe != NULL, NULL);

	pc = g_malloc0 (sizeof (*pc));
	pc->func = func;
	pc->func_data = data;
	pc->flags = flags;

	va_start (ap, fmt);
	pc->data = g_strdup_vprintf (fmt, ap);
	va_end (ap);
	pc->state = CAMEL_POP3_COMMAND_IDLE;
	pc->error_str = NULL;

	/* TODO: what about write errors? */
	engine_command_queue (pe, pc, cancellable, error);

	return pc;
}

void
camel_pop3_engine_command_free (CamelPOP3Engine *pe,
                                CamelPOP3Command *pc)
{
	if (pe && pe->current != pc)
		g_queue_remove (&pe->done, pc);
	g_free (pc->error_str);
	g_free (pc->data);
	g_free (pc);
}
