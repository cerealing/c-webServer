/*
 * JSMN (Yet another JSON parser) in C
 * https://github.com/zserge/jsmn
 * Public domain
 */
#ifndef JSMN_H
#define JSMN_H

#define JSMN_PARENT_LINKS

typedef enum {
    JSMN_UNDEFINED = 0,
    JSMN_OBJECT = 1,
    JSMN_ARRAY = 2,
    JSMN_STRING = 3,
    JSMN_PRIMITIVE = 4
} jsmntype_t;

typedef struct {
    jsmntype_t type;
    int start;
    int end;
    int size;
#ifdef JSMN_PARENT_LINKS
    int parent;
#endif
} jsmntok_t;

typedef struct {
    unsigned int pos;
    unsigned int toknext;
    int toksuper;
} jsmn_parser;

void jsmn_init(jsmn_parser *parser);
int jsmn_parse(jsmn_parser *parser, const char *js, unsigned int len,
               jsmntok_t *tokens, unsigned int num_tokens);

#endif /* JSMN_H */
