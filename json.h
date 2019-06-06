/* See LICENSE file for copyright and license details. */
#ifndef JSON__H
#define JSON__H

/* 
 * https://tools.ietf.org/html/rfc7159
 * Only UTF-8 is supported.
 * Surrogate pairs are not supported.
 * String encoding is not validated.
 */

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>


enum json_h_type {
	JSON_H_NULL,
	JSON_H_BOOLEAN,
	JSON_H_STRING,
	JSON_H_NUMBER,
	/* Only returned by json_h_parse: */
	JSON_H_OBJECT,
	JSON_H_ARRAY,
	/* Only returned by json_h_next: */
	JSON_H_OBJECT_START,
	JSON_H_OBJECT_END,
	JSON_H_ARRAY_START,
	JSON_H_ARRAY_END,
};


struct json_h_boolean {
	enum json_h_type type_;
	int value;
};


struct json_h_string {
	enum json_h_type type_;
	char *s;
	size_t n;
};


struct json_h_number {
	enum json_h_type type_;
	long double value;
};


struct json_h_array {
	enum json_h_type type_;
	union json_h_value *values;
	size_t n;
};


struct json_h_object {
	enum json_h_type type_;
	struct json_h_member *members;
	size_t n;
};


union json_h_value {
	enum   json_h_type    type;
	struct json_h_boolean boolean;
	struct json_h_string  string;
	struct json_h_number  number;
	struct json_h_array   array;
	struct json_h_object  object;
};


struct json_h_member {
	struct json_h_string name;
	union json_h_value value;
};


struct json_h_context {
	size_t r;
	size_t s;
	int need_end;
	int object_state; /* 0 = need name or close, 1 = need colon, 2 = need value */
};


#define JSON_H_CONTEXT_INIT {0, 0, 0, 2}



static void json_h_free(union json_h_value *);


static void
json_h_destroy(union json_h_value *v)
{
	if (v) {
		if (v->type == JSON_H_STRING) {
			free(v->string.s);
		} else if (v->type == JSON_H_OBJECT) {
			while (v->object.n--) {
				free(v->object.members[v->object.n].name.s);
				json_h_destroy(&v->object.members[v->object.n].value);
			}
			free(v->object.members);
		} else if (v->type == JSON_H_ARRAY) {
			while (v->array.n--)
				json_h_destroy(&v->array.values[v->array.n]);
			free(v->array.values);
		}
	}
}


static void
json_h_free(union json_h_value *v)
{
	json_h_destroy(v);
	free(v);
}


static int
json_h_parse_string(char *buf, size_t *n, const char *s)
{
	int esc = 0;
	unsigned long int cp;

	for (;;) {
		if (esc) {
			esc = 0;
			if (*s == '"' || *s == '\\' || *s == '/') {
				buf[*n++] = *s++;
			} else if (*s == 'b') {
				buf[*n++] = '\b';
				s++;
			} else if (*s == 'f') {
				buf[*n++] = '\f';
				s++;
			} else if (*s == 'n') {
				buf[*n++] = '\n';
				s++;
			} else if (*s == 'r') {
				buf[*n++] = '\r';
				s++;
			} else if (*s == 't') {
				buf[*n++] = '\t';
				s++;
			} else if (*s == 'u') {
				if (!isxdigit(s[1]) || !isxdigit(s[2]) || !isxdigit(s[3]) || !isxdigit(s[4]))
					goto einval;
				cp  = ((unsigned long int)(s[1] & 15) + ((s[1] > '9') ? 9 : 0)) << 24;
				cp |= ((unsigned long int)(s[2] & 15) + ((s[2] > '9') ? 9 : 0)) << 16;
				cp |= ((unsigned long int)(s[3] & 15) + ((s[3] > '9') ? 9 : 0)) <<  8;
				cp |= ((unsigned long int)(s[4] & 15) + ((s[4] > '9') ? 9 : 0)) <<  0;
				s += 5;
				if (cp <= 0x007F) {
					buf[*n++] = (char)cp;
				} else if (cp <= 0x07FF) {
					buf[*n++] = (char)((cp >> 6) | 0xC0);
					buf[*n++] = (char)((cp >> 0) & 0x3F);
				} else {
					buf[*n++] = (char)((cp >> 12) | 0xE0);
					buf[*n++] = (char)((cp >> 6) & 0x3F);
					buf[*n++] = (char)((cp >> 0) & 0x3F);
				}
			} else {
				goto einval;
			}
		} else if (*s == '\\') {
			esc = 1;
			s++;
		} else if (*s == '"') {
			buf[*n] = '\0';
			return 0;
		} else if (*(const unsigned char *)s < ' ') {
			goto einval;
		} else {
			buf[*n++] = *s++;
		}
	}

einval:
	errno = EINVAL;
	return -1;
}


static int
json_h_next(struct json_h_context *ctx, char *buf, size_t n, union json_h_value *out)
{
	size_t i, m, s;
	int esc;

	while (ctx->r < n) {
		switch (buf[ctx->r++]) {
		case '{':
			if (ctx->need_end || ctx->object_state != 2)
				goto einval;
			buf[ctx->s++] = '{';
			out->type = JSON_H_OBJECT_START;
			ctx->object_state = 0;
			return 1;

		case '}':
			if (ctx->object_state != 0)
				goto einval;
			if (!ctx->s || buf[--ctx->s] != '{')
				goto einval;
			out->type = JSON_H_OBJECT_END;
			ctx->need_end = 1;
			return 1;

		case '[':
			if (ctx->need_end || ctx->object_state != 2)
				goto einval;
			buf[ctx->s++] = '[';
			out->type = JSON_H_ARRAY_START;
			return 1;

		case ']':
			if (!ctx->s || buf[--ctx->s] != '[')
				goto einval;
			out->type = JSON_H_ARRAY_END;
			ctx->need_end = 1;
			return 1;

		case '"':
			if (ctx->need_end || ctx->object_state == 1)
				goto einval;
			for (s = ctx->r, m = 1, esc = 0; ctx->r < n; m++, ctx->r++) {
				if (buf[ctx->r] == '\\')
					esc = 1;
				else if (esc)
					esc = 0;
				else if (buf[ctx->r] == '"')
					break;
			}
			if (ctx->r++ == m)
				goto einval;
			out->type = JSON_H_STRING;
			out->string.s = malloc(m);
			if (!out->string.s)
				return -1;
			out->string.n = 0;
			if (json_h_parse_string(out->string.s, &out->string.n, &buf[s])) {
				free(out->string.s);
				return -1;
			}
			ctx->need_end = 1;
			if (!ctx->object_state)
				ctx->object_state = 1;
			return 1;

		case 'n':
			if (ctx->need_end || ctx->object_state != 2)
				goto einval;
			if (3 > n - ctx->r || strncmp(&buf[ctx->r], "ull", 3))
				goto einval;
			ctx->r += 3;
			out->type = JSON_H_NULL;
			ctx->need_end = 1;
			return 1;

		case 't':
			if (ctx->need_end || ctx->object_state != 2)
				goto einval;
			if (3 > n - ctx->r || strncmp(&buf[ctx->r], "rue", 3))
				goto einval;
			ctx->r += 3;
			out->type = JSON_H_BOOLEAN;
			out->boolean.value = 1;
			ctx->need_end = 1;
			return 1;

		case 'f':
			if (ctx->need_end || ctx->object_state != 2)
				goto einval;
			if (4 > n - ctx->r || strncmp(&buf[ctx->r], "alse", 4))
				goto einval;
			ctx->r += 4;
			out->type = JSON_H_BOOLEAN;
			out->boolean.value = 0;
			ctx->need_end = 1;
			return 1;

		case ',':
			if (ctx->object_state != 2)
				goto einval;
			if (ctx->s && buf[ctx->s - 1] == '{')
				ctx->object_state = 0;
			ctx->need_end = 0;
			break;

		case ':':
			if (ctx->object_state != 1)
				goto einval;
			ctx->object_state = 2;
			ctx->need_end = 0;
			break;

		default:
			if (isspace(buf[ctx->r - 1]))
				break;
			if (ctx->need_end || ctx->object_state != 2)
				goto einval;
			ctx->r--;
			/* TODO number */
			goto einval;
		}
	}

	if (!ctx->s)
		return 0;

einval:
	errno = EINVAL;
	return -1;
}


static int
json_h_parse_(struct json_h_context *ctx, char *buf, size_t n, union json_h_value *value)
{
	union json_h_value *newarray;
	int r, size = 0;

	r = json_h_next(ctx, buf, n, value);
	if (r < 0)
		return -1;
	if (!r) {
		errno = EINVAL;
		return -1;
	}

	if (value->type == JSON_H_OBJECT_START || value->type == JSON_H_ARRAY_START) {
		value->array.values = NULL;
		for (value->array.n = 0;; value->array.n++) {
			if (value->array.n == size) {
				size += 16;
				newarray = realloc(value->array.values, size * sizeof(*value->array.values));
				if (!newarray) {
					json_h_free(value);
					return -1;
				}
				value->array.values = newarray;
			}
			if (json_h_parse_(ctx, buf, n, &value->array.values[value->array.n])) {
				json_h_destroy(value);
				return -1;
			}
			if (value->array.values[value->array.n].type == JSON_H_OBJECT_END) {
				value->array.n /= 2;
				value->type = JSON_H_OBJECT;
				break;
			} else if (value->array.values[value->array.n].type == JSON_H_ARRAY_END) {
				value->type = JSON_H_ARRAY;
				break;
			}
		}
	}

	return 0;
}


static union json_h_value *
json_h_parse(char *buf, size_t n)
{
	struct json_h_context ctx = JSON_H_CONTEXT_INIT;
	union json_h_value value, *ret;
	int r;

	ret = malloc(sizeof(*ret));
	if (!ret)
		return NULL;

	if (json_h_parse_(&ctx, buf, n, ret)) {
		free(ret);
		return NULL;
	}

	r = json_h_next(&ctx, buf, n, &value);
	if (r < 0) {
		json_h_free(ret);
		return NULL;
	}
	if (r > 0) {
		json_h_destroy(&value);
		json_h_free(ret);
		errno = EINVAL;
		return NULL;
	}

	return ret;
}


#endif
