#pragma once

/* 极简 JSON 解析器：够用就好。
 * Home Assistant 的响应里有中文名、引号、\u 转义，靠 strstr 抓字符串迟早出错，
 * 所以这里做一个正经的递归下降解析器（约 400 行），只保留读取能力。 */

typedef enum { J_NULL = 0, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JNode {
    JType t;
    double num;            /* J_NUM / J_BOOL */
    char  *str;            /* J_STR（已解码 UTF-8） */
    char  *key;            /* 作为对象成员时的键名 */
    int    n;              /* 子元素个数 */
    struct JNode *child;   /* 第一个子节点 */
    struct JNode *next;    /* 同级下一个 */
} JNode;

JNode *json_parse(const char *txt);
void   json_free(JNode *n);

const JNode *json_get(const JNode *obj, const char *key);
const char  *json_str(const JNode *obj, const char *key);      /* 安全版：空返回 "" */
const char  *json_str_or(const JNode *obj, const char *key, const char *dflt);
double       json_num(const JNode *obj, const char *key, double dflt);

int          json_len(const JNode *n);
const JNode *json_at(const JNode *n, int i);
