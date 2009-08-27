/***
  This file is part of PulseAudio.

  Copyright 2007 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <ctype.h>

#include <pulse/xmalloc.h>
#include <pulse/utf8.h>
#include <pulse/i18n.h>

#include <pulsecore/hashmap.h>
#include <pulsecore/strbuf.h>
#include <pulsecore/core-util.h>

#include "proplist.h"

struct property {
    char *key;
    void *value;
    size_t nbytes;
};

#define MAKE_HASHMAP(p) ((pa_hashmap*) (p))
#define MAKE_PROPLIST(p) ((pa_proplist*) (p))

static pa_bool_t property_name_valid(const char *key) {

    if (!pa_ascii_valid(key))
        return FALSE;

    if (strlen(key) <= 0)
        return FALSE;

    return TRUE;
}

static void property_free(struct property *prop) {
    pa_assert(prop);

    pa_xfree(prop->key);
    pa_xfree(prop->value);
    pa_xfree(prop);
}

pa_proplist* pa_proplist_new(void) {
    return MAKE_PROPLIST(pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func));
}

void pa_proplist_free(pa_proplist* p) {
    pa_assert(p);

    pa_proplist_clear(p);
    pa_hashmap_free(MAKE_HASHMAP(p), NULL, NULL);
}

/** Will accept only valid UTF-8 */
int pa_proplist_sets(pa_proplist *p, const char *key, const char *value) {
    struct property *prop;
    pa_bool_t add = FALSE;

    pa_assert(p);
    pa_assert(key);
    pa_assert(value);

    if (!property_name_valid(key) || !pa_utf8_valid(value))
        return -1;

    if (!(prop = pa_hashmap_get(MAKE_HASHMAP(p), key))) {
        prop = pa_xnew(struct property, 1);
        prop->key = pa_xstrdup(key);
        add = TRUE;
    } else
        pa_xfree(prop->value);

    prop->value = pa_xstrdup(value);
    prop->nbytes = strlen(value)+1;

    if (add)
        pa_hashmap_put(MAKE_HASHMAP(p), prop->key, prop);

    return 0;
}

/** Will accept only valid UTF-8 */
static int proplist_setn(pa_proplist *p, const char *key, size_t key_length, const char *value, size_t value_length) {
    struct property *prop;
    pa_bool_t add = FALSE;
    char *k, *v;

    pa_assert(p);
    pa_assert(key);
    pa_assert(value);

    k = pa_xstrndup(key, key_length);
    v = pa_xstrndup(value, value_length);

    if (!property_name_valid(k) || !pa_utf8_valid(v)) {
        pa_xfree(k);
        pa_xfree(v);
        return -1;
    }

    if (!(prop = pa_hashmap_get(MAKE_HASHMAP(p), k))) {
        prop = pa_xnew(struct property, 1);
        prop->key = k;
        add = TRUE;
    } else {
        pa_xfree(prop->value);
        pa_xfree(k);
    }

    prop->value = v;
    prop->nbytes = strlen(v)+1;

    if (add)
        pa_hashmap_put(MAKE_HASHMAP(p), prop->key, prop);

    return 0;
}

static int proplist_sethex(pa_proplist *p, const char *key, size_t key_length, const char *value, size_t value_length) {
    struct property *prop;
    pa_bool_t add = FALSE;
    char *k, *v;
    uint8_t *d;
    size_t dn;

    pa_assert(p);
    pa_assert(key);
    pa_assert(value);

    k = pa_xstrndup(key, key_length);

    if (!property_name_valid(k)) {
        pa_xfree(k);
        return -1;
    }

    v = pa_xstrndup(value, value_length);
    d = pa_xmalloc(value_length*2+1);

    if ((dn = pa_parsehex(v, d, value_length*2)) == (size_t) -1) {
        pa_xfree(k);
        pa_xfree(v);
        pa_xfree(d);
        return -1;
    }

    pa_xfree(v);

    if (!(prop = pa_hashmap_get(MAKE_HASHMAP(p), k))) {
        prop = pa_xnew(struct property, 1);
        prop->key = k;
        add = TRUE;
    } else {
        pa_xfree(prop->value);
        pa_xfree(k);
    }

    d[dn] = 0;
    prop->value = d;
    prop->nbytes = dn;

    if (add)
        pa_hashmap_put(MAKE_HASHMAP(p), prop->key, prop);

    return 0;
}

/** Will accept only valid UTF-8 */
int pa_proplist_setf(pa_proplist *p, const char *key, const char *format, ...) {
    struct property *prop;
    pa_bool_t add = FALSE;
    va_list ap;
    char *v;

    pa_assert(p);
    pa_assert(key);
    pa_assert(format);

    if (!property_name_valid(key) || !pa_utf8_valid(format))
        return -1;

    va_start(ap, format);
    v = pa_vsprintf_malloc(format, ap);
    va_end(ap);

    if (!pa_utf8_valid(v))
        goto fail;

    if (!(prop = pa_hashmap_get(MAKE_HASHMAP(p), key))) {
        prop = pa_xnew(struct property, 1);
        prop->key = pa_xstrdup(key);
        add = TRUE;
    } else
        pa_xfree(prop->value);

    prop->value = v;
    prop->nbytes = strlen(v)+1;

    if (add)
        pa_hashmap_put(MAKE_HASHMAP(p), prop->key, prop);

    return 0;

fail:
    pa_xfree(v);
    return -1;
}

int pa_proplist_set(pa_proplist *p, const char *key, const void *data, size_t nbytes) {
    struct property *prop;
    pa_bool_t add = FALSE;

    pa_assert(p);
    pa_assert(key);
    pa_assert(data || nbytes == 0);

    if (!property_name_valid(key))
        return -1;

    if (!(prop = pa_hashmap_get(MAKE_HASHMAP(p), key))) {
        prop = pa_xnew(struct property, 1);
        prop->key = pa_xstrdup(key);
        add = TRUE;
    } else
        pa_xfree(prop->value);

    prop->value = pa_xmalloc(nbytes+1);
    if (nbytes > 0)
        memcpy(prop->value, data, nbytes);
    ((char*) prop->value)[nbytes] = 0;
    prop->nbytes = nbytes;

    if (add)
        pa_hashmap_put(MAKE_HASHMAP(p), prop->key, prop);

    return 0;
}

const char *pa_proplist_gets(pa_proplist *p, const char *key) {
    struct property *prop;

    pa_assert(p);
    pa_assert(key);

    if (!property_name_valid(key))
        return NULL;

    if (!(prop = pa_hashmap_get(MAKE_HASHMAP(p), key)))
        return NULL;

    if (prop->nbytes <= 0)
        return NULL;

    if (((char*) prop->value)[prop->nbytes-1] != 0)
        return NULL;

    if (strlen((char*) prop->value) != prop->nbytes-1)
        return NULL;

    if (!pa_utf8_valid((char*) prop->value))
        return NULL;

    return (char*) prop->value;
}

int pa_proplist_get(pa_proplist *p, const char *key, const void **data, size_t *nbytes) {
    struct property *prop;

    pa_assert(p);
    pa_assert(key);
    pa_assert(data);
    pa_assert(nbytes);

    if (!property_name_valid(key))
        return -1;

    if (!(prop = pa_hashmap_get(MAKE_HASHMAP(p), key)))
        return -1;

    *data = prop->value;
    *nbytes = prop->nbytes;

    return 0;
}

void pa_proplist_update(pa_proplist *p, pa_update_mode_t mode, pa_proplist *other) {
    struct property *prop;
    void *state = NULL;

    pa_assert(p);
    pa_assert(mode == PA_UPDATE_SET || mode == PA_UPDATE_MERGE || mode == PA_UPDATE_REPLACE);
    pa_assert(other);

    if (mode == PA_UPDATE_SET)
        pa_proplist_clear(p);

    while ((prop = pa_hashmap_iterate(MAKE_HASHMAP(other), &state, NULL))) {

        if (mode == PA_UPDATE_MERGE && pa_proplist_contains(p, prop->key))
            continue;

        pa_assert_se(pa_proplist_set(p, prop->key, prop->value, prop->nbytes) == 0);
    }
}

int pa_proplist_unset(pa_proplist *p, const char *key) {
    struct property *prop;

    pa_assert(p);
    pa_assert(key);

    if (!property_name_valid(key))
        return -1;

    if (!(prop = pa_hashmap_remove(MAKE_HASHMAP(p), key)))
        return -2;

    property_free(prop);
    return 0;
}

int pa_proplist_unset_many(pa_proplist *p, const char * const keys[]) {
    const char * const * k;
    int n = 0;

    pa_assert(p);
    pa_assert(keys);

    for (k = keys; *k; k++)
        if (!property_name_valid(*k))
            return -1;

    for (k = keys; *k; k++)
        if (pa_proplist_unset(p, *k) >= 0)
            n++;

    return n;
}

const char *pa_proplist_iterate(pa_proplist *p, void **state) {
    struct property *prop;

    if (!(prop = pa_hashmap_iterate(MAKE_HASHMAP(p), state, NULL)))
        return NULL;

    return prop->key;
}

char *pa_proplist_to_string_sep(pa_proplist *p, const char *sep) {
    const char *key;
    void *state = NULL;
    pa_strbuf *buf;

    pa_assert(p);
    pa_assert(sep);

    buf = pa_strbuf_new();

    while ((key = pa_proplist_iterate(p, &state))) {
        const char *v;

        if (!pa_strbuf_isempty(buf))
            pa_strbuf_puts(buf, sep);

        if ((v = pa_proplist_gets(p, key))) {
            const char *t;

            pa_strbuf_printf(buf, "%s = \"", key);

            for (t = v;;) {
                size_t h;

                h = strcspn(t, "\"");

                if (h > 0)
                    pa_strbuf_putsn(buf, t, h);

                t += h;

                if (*t == 0)
                    break;

                pa_assert(*t == '"');
                pa_strbuf_puts(buf, "\\\"");

                t++;
            }

            pa_strbuf_puts(buf, "\"");
        } else {
            const void *value;
            size_t nbytes;
            char *c;

            pa_assert_se(pa_proplist_get(p, key, &value, &nbytes) == 0);
            c = pa_xmalloc(nbytes*2+1);
            pa_hexstr((const uint8_t*) value, nbytes, c, nbytes*2+1);

            pa_strbuf_printf(buf, "%s = hex:%s", key, c);
            pa_xfree(c);
        }
    }

    return pa_strbuf_tostring_free(buf);
}

char *pa_proplist_to_string(pa_proplist *p) {
    char *s, *t;

    s = pa_proplist_to_string_sep(p, "\n");
    t = pa_sprintf_malloc("%s\n", s);
    pa_xfree(s);

    return t;
}

pa_proplist *pa_proplist_from_string(const char *s) {
    enum {
        WHITESPACE,
        KEY,
        AFTER_KEY,
        VALUE_START,
        VALUE_SIMPLE,
        VALUE_DOUBLE_QUOTES,
        VALUE_DOUBLE_QUOTES_ESCAPE,
        VALUE_TICKS,
        VALUE_TICKS_ESCAPED,
        VALUE_HEX
    } state;

    pa_proplist *pl;
    const char *p, *key = NULL, *value = NULL;
    size_t key_len = 0, value_len = 0;

    pa_assert(s);

    pl = pa_proplist_new();

    state = WHITESPACE;

    for (p = s;; p++) {
        switch (state) {

            case WHITESPACE:
                if (*p == 0)
                    goto success;
                else if (*p == '=')
                    goto fail;
                else if (!isspace(*p)) {
                    key = p;
                    state = KEY;
                    key_len = 1;
                }
                break;

            case KEY:
                if (*p == 0)
                    goto fail;
                else if (*p == '=')
                    state = VALUE_START;
                else if (isspace(*p))
                    state = AFTER_KEY;
                else
                    key_len++;
                break;

            case AFTER_KEY:
                if (*p == 0)
                    goto fail;
                else if (*p == '=')
                    state = VALUE_START;
                else if (!isspace(*p))
                    goto fail;
                break;

            case VALUE_START:
                if (*p == 0)
                    goto fail;
                else if (strncmp(p, "hex:", 4) == 0) {
                    state = VALUE_HEX;
                    value = p+4;
                    value_len = 0;
                    p += 3;
                } else if (*p == '\'') {
                    state = VALUE_TICKS;
                    value = p+1;
                    value_len = 0;
                } else if (*p == '"') {
                    state = VALUE_DOUBLE_QUOTES;
                    value = p+1;
                    value_len = 0;
                } else if (!isspace(*p)) {
                    state = VALUE_SIMPLE;
                    value = p;
                    value_len = 1;
                }
                break;

            case VALUE_SIMPLE:
                if (*p == 0 || isspace(*p)) {
                    if (proplist_setn(pl, key, key_len, value, value_len) < 0)
                        goto fail;

                    if (*p == 0)
                        goto success;

                    state = WHITESPACE;
                } else
                    value_len++;
                break;

            case VALUE_DOUBLE_QUOTES:
                if (*p == 0)
                    goto fail;
                else if (*p == '"') {
                    char *v;

                    v = pa_unescape(pa_xstrndup(value, value_len));

                    if (proplist_setn(pl, key, key_len, v, strlen(v)) < 0) {
                        pa_xfree(v);
                        goto fail;
                    }

                    pa_xfree(v);
                    state = WHITESPACE;
                } else if (*p == '\\') {
                    state = VALUE_DOUBLE_QUOTES_ESCAPE;
                    value_len++;
                } else
                    value_len++;
                break;

            case VALUE_DOUBLE_QUOTES_ESCAPE:
                if (*p == 0)
                    goto fail;
                else {
                    state = VALUE_DOUBLE_QUOTES;
                    value_len++;
                }
                break;

            case VALUE_TICKS:
                if (*p == 0)
                    goto fail;
                else if (*p == '\'') {
                    char *v;

                    v = pa_unescape(pa_xstrndup(value, value_len));

                    if (proplist_setn(pl, key, key_len, v, strlen(v)) < 0) {
                        pa_xfree(v);
                        goto fail;
                    }

                    pa_xfree(v);
                    state = WHITESPACE;
                } else if (*p == '\\') {
                    state = VALUE_TICKS_ESCAPED;
                    value_len++;
                } else
                    value_len++;
                break;

            case VALUE_TICKS_ESCAPED:
                if (*p == 0)
                    goto fail;
                else {
                    state = VALUE_TICKS;
                    value_len++;
                }
                break;

            case VALUE_HEX:
                if ((*p >= '0' && *p <= '9') ||
                    (*p >= 'A' && *p <= 'F') ||
                    (*p >= 'a' && *p <= 'f')) {
                    value_len++;
                } else if (*p == 0 || isspace(*p)) {

                    if (proplist_sethex(pl, key, key_len, value, value_len) < 0)
                        goto fail;

                    if (*p == 0)
                        goto success;

                    state = WHITESPACE;
                } else
                    goto fail;
                break;
        }
    }

success:
    return MAKE_PROPLIST(pl);

fail:
    pa_proplist_free(pl);
    return NULL;
}

int pa_proplist_contains(pa_proplist *p, const char *key) {
    pa_assert(p);
    pa_assert(key);

    if (!property_name_valid(key))
        return -1;

    if (!(pa_hashmap_get(MAKE_HASHMAP(p), key)))
        return 0;

    return 1;
}

void pa_proplist_clear(pa_proplist *p) {
    struct property *prop;
    pa_assert(p);

    while ((prop = pa_hashmap_steal_first(MAKE_HASHMAP(p))))
        property_free(prop);
}

pa_proplist* pa_proplist_copy(pa_proplist *template) {
    pa_proplist *p;

    pa_assert_se(p = pa_proplist_new());

    if (template)
        pa_proplist_update(p, PA_UPDATE_REPLACE, template);

    return p;
}

unsigned pa_proplist_size(pa_proplist *p) {
    pa_assert(p);

    return pa_hashmap_size(MAKE_HASHMAP(p));
}

int pa_proplist_isempty(pa_proplist *p) {
    pa_assert(p);

    return pa_hashmap_isempty(MAKE_HASHMAP(p));
}
