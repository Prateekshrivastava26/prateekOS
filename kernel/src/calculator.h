#ifndef PRATEEKOS_CALCULATOR_H
#define PRATEEKOS_CALCULATOR_H
#include <stddef.h>
typedef struct {char input[64];char result[64];size_t length;int evaluated;} calculator_t;
void calculator_init(calculator_t *c);
void calculator_key(calculator_t *c,char key);
int calculator_evaluate(const char *input,char out[64]);
#endif
