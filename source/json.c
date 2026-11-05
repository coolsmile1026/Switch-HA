#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define JSON_MAX_DEPTH 32

typedef struct {
    const char *p;
    int depth;
    int bad;
} P;

static void skip_ws(P *s)
{
    while (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r') s->p++;
}

static JNode *node_new(JType t)
{
    JNode *n = (JNode *)calloc(1, sizeof(JNode));
    if (n) n->t = t;
    return n;
}

/* 把 \uXXXX 编码成 UTF-8 */
static char *put_utf8(char *o, unsigned cp)
{
    if (cp < 0x80) {
        *o++ = (char)cp;
    } else if (cp < 0x800) {
        *o++ = (char)(0xC0 | (cp >> 6));
        *o++ = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        *o++ = (char)(0xE0 | (cp >> 12));
        *o++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *o++ = (char)(0x80 | (cp & 0x3F));
    } else {
        *o++ = (char)(0xF0 | (cp >> 18));
        *o++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *o++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *o++ = (char)(0x80 | (cp & 0x3F));
    }
    return o;
}

static int hex4(const char *p, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

/* 解析字符串，返回新分配的解码结果；*adv 指向结束引号之后 */
static char *parse_string(P *s)
{
    if (*s->p != '"') { s->bad = 1; return NULL; }
    s->p++;

    /* 最坏情况：解码后不比原文长，按剩余长度分配 */
    size_t cap = strlen(s->p) + 1;
    char *out = (char *)malloc(cap);
    if (!out) { s->bad = 1; return NULL; }

    char *o = out;
    while (*s->p && *s->p != '"') {
        unsigned char c = (unsigned char)*s->p;
        if (c == '\\') {
            s->p++;
            char e = *s->p;
            if (!e) break;
            s->p++;
            switch (e) {
            case 'n': *o++ = '\n'; break;
            case 't': *o++ = '\t'; break;
            case 'r': *o++ = '\r'; break;
            case 'b': *o++ = '\b'; break;
            case 'f': *o++ = '\f'; break;
            case '/': *o++ = '/';  break;
            case '\\': *o++ = '\\'; break;
            case '"': *o++ = '"';  break;
            case 'u': {
                unsigned cp = 0;
                if (hex4(s->p, &cp) != 0) { s->bad = 1; free(out); return NULL; }
                s->p += 4;
                /* 代理对 */
                if (cp >= 0xD800 && cp <= 0xDBFF && s->p[0] == '\\' && s->p[1] == 'u') {
                    unsigned lo = 0;
                    if (hex4(s->p + 2, &lo) == 0 && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        s->p += 6;
                    }
                }
                o = put_utf8(o, cp);
                break;
            }
            default: *o++ = e; break;
            }
        } else {
            *o++ = (char)c;
            s->p++;
        }
    }
    if (*s->p != '"') { s->bad = 1; free(out); return NULL; }
    s->p++;
    *o = '\0';
    return out;
}

static JNode *parse_value(P *s);

static JNode *parse_array(P *s)
{
    JNode *arr = node_new(J_ARR);
    if (!arr) { s->bad = 1; return NULL; }
    s->p++;   /* [ */
    skip_ws(s);
    if (*s->p == ']') { s->p++; return arr; }

    JNode *tail = NULL;
    for (;;) {
        JNode *v = parse_value(s);
        if (!v) { json_free(arr); return NULL; }
        if (tail) tail->next = v; else arr->child = v;
        tail = v;
        arr->n++;
        skip_ws(s);
        if (*s->p == ',') { s->p++; skip_ws(s); continue; }
        if (*s->p == ']') { s->p++; break; }
        s->bad = 1;
        json_free(arr);
        return NULL;
    }
    return arr;
}

static JNode *parse_object(P *s)
{
    JNode *obj = node_new(J_OBJ);
    if (!obj) { s->bad = 1; return NULL; }
    s->p++;   /* { */
    skip_ws(s);
    if (*s->p == '}') { s->p++; return obj; }

    JNode *tail = NULL;
    for (;;) {
        skip_ws(s);
        char *k = parse_string(s);
        if (!k) { json_free(obj); return NULL; }
        skip_ws(s);
        if (*s->p != ':') { free(k); s->bad = 1; json_free(obj); return NULL; }
        s->p++;
        JNode *v = parse_value(s);
        if (!v) { free(k); json_free(obj); return NULL; }
        v->key = k;
        if (tail) tail->next = v; else obj->child = v;
        tail = v;
        obj->n++;

        skip_ws(s);
        if (*s->p == ',') { s->p++; continue; }
        if (*s->p == '}') { s->p++; break; }
        s->bad = 1;
        json_free(obj);
        return NULL;
    }
    return obj;
}

static JNode *parse_value(P *s)
{
    if (++s->depth > JSON_MAX_DEPTH) { s->bad = 1; return NULL; }
    skip_ws(s);

    JNode *n = NULL;
    char c = *s->p;

    if (c == '{') {
        n = parse_object(s);
    } else if (c == '[') {
        n = parse_array(s);
    } else if (c == '"') {
        char *str = parse_string(s);
        if (str) {
            n = node_new(J_STR);
            if (n) n->str = str; else free(str);
        }
    } else if (strncmp(s->p, "true", 4) == 0) {
        s->p += 4;
        n = node_new(J_BOOL);
        if (n) n->num = 1;
    } else if (strncmp(s->p, "false", 5) == 0) {
        s->p += 5;
        n = node_new(J_BOOL);
    } else if (strncmp(s->p, "null", 4) == 0) {
        s->p += 4;
        n = node_new(J_NULL);
    } else {
        /* 数字（HA 也会返回 nan/inf 作为裸标识符，这里一并吞掉） */
        if (strncmp(s->p, "nan", 3) == 0 || strncmp(s->p, "NaN", 3) == 0) {
            s->p += 3;
            n = node_new(J_NUM);
            return n;
        }
        if (strncmp(s->p, "inf", 3) == 0) { s->p += 3; n = node_new(J_NUM); return n; }

        char *end = NULL;
        double v = strtod(s->p, &end);
        if (end == s->p) { s->bad = 1; return NULL; }
        s->p = end;
        n = node_new(J_NUM);
        if (n) n->num = v;
    }

    s->depth--;
    if (!n) s->bad = 1;
    return n;
}

JNode *json_parse(const char *txt)
{
    if (!txt) return NULL;
    P s;
    s.p = txt;
    s.depth = 0;
    s.bad = 0;

    JNode *n = parse_value(&s);
    if (!n || s.bad) { json_free(n); return NULL; }
    return n;
}

void json_free(JNode *n)
{
    while (n) {
        JNode *next = n->next;
        if (n->child) json_free(n->child);
        free(n->str);
        free(n->key);
        free(n);
        n = next;
    }
}

const JNode *json_get(const JNode *obj, const char *key)
{
    if (!obj || obj->t != J_OBJ) return NULL;
    for (const JNode *c = obj->child; c; c = c->next)
        if (c->key && strcmp(c->key, key) == 0) return c;
    return NULL;
}

const char *json_str(const JNode *obj, const char *key)
{
    const JNode *n = json_get(obj, key);
    return (n && n->t == J_STR && n->str) ? n->str : "";
}

const char *json_str_or(const JNode *obj, const char *key, const char *dflt)
{
    const JNode *n = json_get(obj, key);
    if (n && n->t == J_STR && n->str) return n->str;
    return dflt;
}

double json_num(const JNode *obj, const char *key, double dflt)
{
    const JNode *n = json_get(obj, key);
    if (!n) return dflt;
    if (n->t == J_NUM || n->t == J_BOOL) return n->num;
    return dflt;
}

int json_len(const JNode *n)
{
    return (n && (n->t == J_ARR || n->t == J_OBJ)) ? n->n : 0;
}

const JNode *json_at(const JNode *n, int i)
{
    if (!n || i < 0) return NULL;
    const JNode *c = n->child;
    while (c && i-- > 0) c = c->next;
    return c;
}
