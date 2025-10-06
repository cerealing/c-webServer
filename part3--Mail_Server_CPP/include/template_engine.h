#ifndef TEMPLATE_ENGINE_H
#define TEMPLATE_ENGINE_H

#include <stddef.h>

typedef struct template_engine template_engine_t;

typedef struct {
    const char *key;
    const char *value;
} template_var_t;

template_engine_t *template_engine_create(const char *template_dir);
void template_engine_destroy(template_engine_t *engine);
int template_engine_render(template_engine_t *engine, const char *name,
                           const template_var_t *vars, size_t var_count,
                           char **out_html, size_t *out_len);

#endif // TEMPLATE_ENGINE_H
