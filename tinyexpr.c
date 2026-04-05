// SPDX-License-Identifier: Zlib
/*
 * TINYEXPR - Tiny recursive descent parser and evaluation engine in C
 *
 * Copyright (c) 2015-2020 Lewis Van Winkle
 *
 * http://CodePlea.com
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgement in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

/* COMPILE TIME OPTIONS */

/* This option only affects the pow() function semantics in examples/tests
and is retained for backward compatibility with older TinyExpr versions. */
/* #define TE_POW_FROM_RIGHT */

/* Logarithms
For log = base 10 log do nothing
For log = natural log uncomment the next line. */
/* #define TE_NAT_LOG */

#include "tinyexpr.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>

#ifndef NAN
#define NAN (0.0/0.0)
#endif

#ifndef INFINITY
#define INFINITY (1.0/0.0)
#endif


typedef double (*te_fun2)(double, double);

enum {
    TOK_NULL = TE_CLOSURE7+1, TOK_ERROR, TOK_END, TOK_SEP,
    TOK_OPEN, TOK_CLOSE, TOK_NUMBER, TOK_VARIABLE, TOK_INFIX
};


enum {TE_CONSTANT = 1};


typedef struct state {
    const char *start;
    const char *next;
    int type;
    union {double value; const double *bound; const void *function;};
    void *context;

    const te_variable *lookup;
    int lookup_len;

    int index_arg;       /* 1 = this CLOSURE1 token has a baked-in constant index */
    double index_value;  /* the constant index value (from numeric suffix in name)  */
} state;


#define TYPE_MASK(TYPE) ((TYPE)&0x0000001F)

#define IS_PURE(TYPE) (((TYPE) & TE_FLAG_PURE) != 0)
#define IS_FUNCTION(TYPE) (((TYPE) & TE_FUNCTION0) != 0)
#define IS_CLOSURE(TYPE) (((TYPE) & TE_CLOSURE0) != 0)
#define ARITY(TYPE) ( ((TYPE) & (TE_FUNCTION0 | TE_CLOSURE0)) ? ((TYPE) & 0x00000007) : 0 )
#define NEW_EXPR(type, ...) new_expr((type), (const te_expr*[]){__VA_ARGS__})
#define CHECK_NULL(ptr, ...) if ((ptr) == NULL) { __VA_ARGS__; return NULL; }

static te_expr *new_expr(const int type, const te_expr *parameters[]) {
    const int arity = ARITY(type);
    const int psize = sizeof(void*) * arity;
    const int size = (sizeof(te_expr) - sizeof(void*)) + psize + (IS_CLOSURE(type) ? sizeof(void*) : 0);
    te_expr *ret = malloc(size);
    CHECK_NULL(ret);

    memset(ret, 0, size);
    if (arity && parameters) {
        memcpy(ret->parameters, parameters, psize);
    }
    ret->type = type;
    ret->bound = 0;
    return ret;
}


void te_free_parameters(te_expr *n) {
    if (!n) return;
    switch (TYPE_MASK(n->type)) {
        case TE_FUNCTION7: case TE_CLOSURE7: te_free(n->parameters[6]);     /* Falls through. */
        case TE_FUNCTION6: case TE_CLOSURE6: te_free(n->parameters[5]);     /* Falls through. */
        case TE_FUNCTION5: case TE_CLOSURE5: te_free(n->parameters[4]);     /* Falls through. */
        case TE_FUNCTION4: case TE_CLOSURE4: te_free(n->parameters[3]);     /* Falls through. */
        case TE_FUNCTION3: case TE_CLOSURE3: te_free(n->parameters[2]);     /* Falls through. */
        case TE_FUNCTION2: case TE_CLOSURE2: te_free(n->parameters[1]);     /* Falls through. */
        case TE_FUNCTION1: case TE_CLOSURE1: te_free(n->parameters[0]);
    }
}


void te_free(te_expr *n) {
    if (!n) return;
    te_free_parameters(n);
    free(n);
}


static double pi(void) {return 3.14159265358979323846;}
static double e(void) {return 2.71828182845904523536;}
static double clip(double value, double min_value, double max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}
static double sign(double value) {
    if (value > 0.0) return 1.0;
    if (value < 0.0) return -1.0;
    return 0.0;
}
static double te_if(double condition, double true_value, double false_value) {
    return condition != 0.0 ? true_value : false_value;
}
static double fac(double a) {/* simplest version of fac */
    if (a < 0.0)
        return NAN;
    if (a > UINT_MAX)
        return INFINITY;
    unsigned int ua = (unsigned int)(a);
    unsigned long int result = 1, i;
    for (i = 1; i <= ua; i++) {
        if (i > ULONG_MAX / result)
            return INFINITY;
        result *= i;
    }
    return (double)result;
}
static double ncr(double n, double r) {
    if (n < 0.0 || r < 0.0 || n < r) return NAN;
    if (n > UINT_MAX || r > UINT_MAX) return INFINITY;
    unsigned long int un = (unsigned int)(n), ur = (unsigned int)(r), i;
    unsigned long int result = 1;
    if (ur > un / 2) ur = un - ur;
    for (i = 1; i <= ur; i++) {
        if (result > ULONG_MAX / (un - ur + i))
            return INFINITY;
        result *= un - ur + i;
        result /= i;
    }
    return result;
}
static double npr(double n, double r) {return ncr(n, r) * fac(r);}
static double bitwise_xor(double a, double b);

#ifdef _MSC_VER
#pragma function (ceil)
#pragma function (floor)
#endif

static const te_variable functions[] = {
    /* must be in alphabetical order */
    {"abs", fabs,     TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"acos", acos,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"asin", asin,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"atan", atan,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"atan2", atan2,  TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"ceil", ceil,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"clip", clip,    TE_FUNCTION3 | TE_FLAG_PURE, 0},
    {"cos", cos,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"cosh", cosh,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"e", e,          TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"exp", exp,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"fac", fac,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"floor", floor,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"if", te_if,     TE_FUNCTION3 | TE_FLAG_PURE, 0},
    {"ln", log,       TE_FUNCTION1 | TE_FLAG_PURE, 0},
#ifdef TE_NAT_LOG
    {"log", log,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
#else
    {"log", log10,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
#endif
    {"log10", log10,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"max", fmax,     TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"min", fmin,     TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"ncr", ncr,      TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"npr", npr,      TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"pi", pi,        TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"pow", pow,      TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"round", round,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sign", sign,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sin", sin,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sinh", sinh,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sqrt", sqrt,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"tan", tan,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"tanh", tanh,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"xor", bitwise_xor, TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {0, 0, 0, 0}
};

static const te_variable *find_builtin(const char *name, int len) {
    int imin = 0;
    int imax = sizeof(functions) / sizeof(te_variable) - 2;

    /*Binary search.*/
    while (imax >= imin) {
        const int i = (imin + ((imax-imin)/2));
        int c = strncmp(name, functions[i].name, len);
        if (!c) c = '\0' - functions[i].name[len];
        if (c == 0) {
            return functions + i;
        } else if (c > 0) {
            imin = i + 1;
        } else {
            imax = i - 1;
        }
    }

    return 0;
}

static const te_variable *find_lookup(const state *s, const char *name, int len) {
    int iters;
    const te_variable *var;
    if (!s->lookup) return 0;

    for (var = s->lookup, iters = s->lookup_len; iters; ++var, --iters) {
        if (strncmp(name, var->name, len) == 0 && var->name[len] == '\0') {
            return var;
        }
    }
    return 0;
}



static double add(double a, double b) {return a + b;}
static double sub(double a, double b) {return a - b;}
static double mul(double a, double b) {return a * b;}
static double divide(double a, double b) {return a / b;}
static double negate(double a) {return -a;}
static double logical_not(double a) {return a == 0.0 ? 1.0 : 0.0;}
static double comma(double a, double b) {(void)a; return b;}
static double less(double a, double b) {return a < b ? 1.0 : 0.0;}
static double less_equal(double a, double b) {return a <= b ? 1.0 : 0.0;}
static double greater(double a, double b) {return a > b ? 1.0 : 0.0;}
static double greater_equal(double a, double b) {return a >= b ? 1.0 : 0.0;}
static double equal(double a, double b) {return a == b ? 1.0 : 0.0;}
static double not_equal(double a, double b) {return a != b ? 1.0 : 0.0;}
static double logical_and(double a, double b) {return (a != 0.0 && b != 0.0) ? 1.0 : 0.0;}
static double logical_or(double a, double b) {return (a != 0.0 || b != 0.0) ? 1.0 : 0.0;}

static long long bitwise_value(double a) {
    return (long long)a;
}

static int shift_count(double a) {
    const long long shift = (long long)a;
    const long long width = (long long)(sizeof(long long) * CHAR_BIT);
    if (shift < 0 || shift >= width) return -1;
    return (int)shift;
}

static double bitwise_not(double a) {return (double)(~bitwise_value(a));}
static double bitwise_and(double a, double b) {return (double)(bitwise_value(a) & bitwise_value(b));}
static double bitwise_or(double a, double b) {return (double)(bitwise_value(a) | bitwise_value(b));}
static double bitwise_xor(double a, double b) {return (double)(bitwise_value(a) ^ bitwise_value(b));}
static double bitwise_lshift(double a, double b) {
    const int shift = shift_count(b);
    if (shift < 0) return NAN;
    return (double)(((unsigned long long)bitwise_value(a)) << shift);
}
static double bitwise_rshift(double a, double b) {
    const int shift = shift_count(b);
    if (shift < 0) return NAN;
    return (double)(bitwise_value(a) >> shift);
}


void next_token(state *s) {
    s->type = TOK_NULL;

    do {

        if (!*s->next){
            s->type = TOK_END;
            return;
        }

        /* Try reading a number. */
        if ((s->next[0] >= '0' && s->next[0] <= '9') || s->next[0] == '.') {
            s->value = strtod(s->next, (char**)&s->next);
            s->type = TOK_NUMBER;
        } else {
            /* Look for a variable or builtin function call. */
            if (isalpha(s->next[0])) {
                const char *start;
                start = s->next;
                while (isalpha(s->next[0]) || isdigit(s->next[0]) || (s->next[0] == '_')) s->next++;
                
                s->index_arg = 0;
                s->index_value = 0.0;

                const te_variable *var = find_lookup(s, start, s->next - start);
                if (!var) var = find_builtin(start, s->next - start);

                /* If not found as a whole name, try splitting "alpha-prefix + digit-suffix".
                 * e.g. "x12" -> prefix "x", index 12 -> treat as x(12) if "x" is a CLOSURE1. */
                if (!var) {
                    const char *p = start;
                    while (p < s->next && isalpha((unsigned char)*p)) p++;
                    /* p now points to the numeric suffix (if any) */
                    if (p > start && p < s->next) {
                        const char *q = p;
                        int all_digits = 1;
                        while (q < s->next) {
                            if (!isdigit((unsigned char)*q)) { all_digits = 0; break; }
                            q++;
                        }
                        if (all_digits) {
                            var = find_lookup(s, start, p - start);
                            if (var && TYPE_MASK(var->type) == TE_CLOSURE1) {
                                s->index_arg = 1;
                                s->index_value = (double)strtol(p, NULL, 10);
                            } else {
                                var = NULL;
                            }
                        }
                    }
                }

                if (!var) {
                    s->type = TOK_ERROR;
                } else {
                    switch(TYPE_MASK(var->type))
                    {
                        case TE_VARIABLE:
                            s->type = TOK_VARIABLE;
                            s->bound = var->address;
                            break;

                        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:         /* Falls through. */
                        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:         /* Falls through. */
                            s->context = var->context;                                                  /* Falls through. */

                        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:     /* Falls through. */
                        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:     /* Falls through. */
                            s->type = var->type;
                            s->function = var->address;
                            break;
                    }

                }

            } else {
                /* Look for an operator or special character. */
                switch (s->next[0]) {
                    case '+': s->type = TOK_INFIX; s->function = add; s->next++; break;
                    case '-': s->type = TOK_INFIX; s->function = sub; s->next++; break;
                    case '*': s->type = TOK_INFIX; s->function = mul; s->next++; break;
                    case '/': s->type = TOK_INFIX; s->function = divide; s->next++; break;
                    case '^': s->type = TOK_INFIX; s->function = bitwise_xor; s->next++; break;
                    case '%': s->type = TOK_INFIX; s->function = fmod; s->next++; break;
                    case '&':
                        s->type = TOK_INFIX;
                        if (s->next[1] == '&') {
                            s->function = logical_and;
                            s->next += 2;
                        } else {
                            s->function = bitwise_and;
                            s->next++;
                        }
                        break;
                    case '|':
                        s->type = TOK_INFIX;
                        if (s->next[1] == '|') {
                            s->function = logical_or;
                            s->next += 2;
                        } else {
                            s->function = bitwise_or;
                            s->next++;
                        }
                        break;
                    case '~': s->type = TOK_INFIX; s->function = bitwise_not; s->next++; break;
                    case '!':
                        s->type = TOK_INFIX;
                        if (s->next[1] == '=') {
                            s->function = not_equal;
                            s->next += 2;
                        } else {
                            s->function = logical_not;
                            s->next++;
                        }
                        break;
                    case '<':
                        s->type = TOK_INFIX;
                        if (s->next[1] == '=') {
                            s->function = less_equal;
                            s->next += 2;
                        } else if (s->next[1] == '<') {
                            s->function = bitwise_lshift;
                            s->next += 2;
                        } else {
                            s->function = less;
                            s->next++;
                        }
                        break;
                    case '>':
                        s->type = TOK_INFIX;
                        if (s->next[1] == '=') {
                            s->function = greater_equal;
                            s->next += 2;
                        } else if (s->next[1] == '>') {
                            s->function = bitwise_rshift;
                            s->next += 2;
                        } else {
                            s->function = greater;
                            s->next++;
                        }
                        break;
                    case '=':
                        if (s->next[1] == '=') {
                            s->type = TOK_INFIX;
                            s->function = equal;
                            s->next += 2;
                        } else {
                            s->type = TOK_ERROR;
                            s->next++;
                        }
                        break;
                    case '(': s->type = TOK_OPEN; s->next++; break;
                    case ')': s->type = TOK_CLOSE; s->next++; break;
                    case ',': s->type = TOK_SEP; s->next++; break;
                    case ' ': case '\t': case '\n': case '\r': s->next++; break;
                    default: s->type = TOK_ERROR; s->next++; break;
                }
            }
        }
    } while (s->type == TOK_NULL);
}


static te_expr *list(state *s);
static te_expr *expr(state *s);
static te_expr *shift(state *s);
static te_expr *relational(state *s);
static te_expr *equality(state *s);
static te_expr *bitwise_and_expr(state *s);
static te_expr *bitwise_xor_expr(state *s);
static te_expr *bitwise_or_expr(state *s);
static te_expr *logical_and_expr(state *s);
static te_expr *logical_or_expr(state *s);
static te_expr *power(state *s);

static te_expr *base(state *s) {
    /* <base>      =    <constant> | <variable> | <function-0> {"(" ")"} | <function-1> <power> | <function-X> "(" <expr> {"," <expr>} ")" | "(" <list> ")" */
    te_expr *ret;
    int arity;

    switch (TYPE_MASK(s->type)) {
        case TOK_NUMBER:
            ret = new_expr(TE_CONSTANT, 0);
            CHECK_NULL(ret);

            ret->value = s->value;
            next_token(s);
            break;

        case TOK_VARIABLE:
            ret = new_expr(TE_VARIABLE, 0);
            CHECK_NULL(ret);

            ret->bound = s->bound;
            next_token(s);
            break;

        case TE_FUNCTION0:
        case TE_CLOSURE0:
            ret = new_expr(s->type, 0);
            CHECK_NULL(ret);

            ret->function = s->function;
            if (IS_CLOSURE(s->type)) ret->parameters[0] = s->context;
            next_token(s);
            if (s->type == TOK_OPEN) {
                next_token(s);
                if (s->type != TOK_CLOSE) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }
            break;

        case TE_FUNCTION1:
        case TE_CLOSURE1:
            ret = new_expr(s->type, 0);
            CHECK_NULL(ret);

            ret->function = s->function;
            if (IS_CLOSURE(s->type)) ret->parameters[1] = s->context;
            {
                /* Read index_arg BEFORE next_token() advances state. */
                int has_index = s->index_arg;
                double index_val = s->index_value;
                next_token(s);
                if (has_index) {
                    /* Baked-in constant index from numeric suffix (e.g. x12 → x(12)). */
                    te_expr *idx = new_expr(TE_CONSTANT, 0);
                    CHECK_NULL(idx, te_free(ret));
                    idx->value = index_val;
                    ret->parameters[0] = idx;
                } else {
                    ret->parameters[0] = power(s);
                    CHECK_NULL(ret->parameters[0], te_free(ret));
                }
            }
            break;

        case TE_FUNCTION2: case TE_FUNCTION3: case TE_FUNCTION4:
        case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
        case TE_CLOSURE2: case TE_CLOSURE3: case TE_CLOSURE4:
        case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            arity = ARITY(s->type);

            ret = new_expr(s->type, 0);
            CHECK_NULL(ret);

            ret->function = s->function;
            if (IS_CLOSURE(s->type)) ret->parameters[arity] = s->context;
            next_token(s);

            if (s->type != TOK_OPEN) {
                s->type = TOK_ERROR;
            } else {
                int i;
                for(i = 0; i < arity; i++) {
                    next_token(s);
                    ret->parameters[i] = logical_or_expr(s);
                    CHECK_NULL(ret->parameters[i], te_free(ret));

                    if(s->type != TOK_SEP) {
                        break;
                    }
                }
                if(s->type != TOK_CLOSE || i != arity - 1) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }

            break;

        case TOK_OPEN:
            next_token(s);
            ret = list(s);
            CHECK_NULL(ret);

            if (s->type != TOK_CLOSE) {
                s->type = TOK_ERROR;
            } else {
                next_token(s);
            }
            break;

        default:
            ret = new_expr(0, 0);
            CHECK_NULL(ret);

            s->type = TOK_ERROR;
            ret->value = NAN;
            break;
    }

    return ret;
}


static te_expr *power(state *s) {
    /* <power>     =    {("-" | "+" | "!" | "~")} <base> */
    int sign = 1;
    int logical_neg = 0;
    int bitwise_neg = 0;
    while (s->type == TOK_INFIX && (s->function == add || s->function == sub || s->function == logical_not || s->function == bitwise_not)) {
        if (s->function == sub) sign = -sign;
        if (s->function == logical_not) logical_neg = !logical_neg;
        if (s->function == bitwise_not) bitwise_neg = !bitwise_neg;
        next_token(s);
    }

    te_expr *ret;

    if (sign == 1) {
        ret = base(s);
    } else {
        te_expr *b = base(s);
        CHECK_NULL(b);

        ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, b);
        CHECK_NULL(ret, te_free(b));

        ret->function = negate;
    }

    if (bitwise_neg) {
        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, ret);
        CHECK_NULL(ret, te_free(prev));

        ret->function = bitwise_not;
    }

    if (logical_neg) {
        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, ret);
        CHECK_NULL(ret, te_free(prev));

        ret->function = logical_not;
    }

    return ret;
}

static te_expr *factor(state *s) {
    /* <factor>    =    <power> */
    te_expr *ret = power(s);
    CHECK_NULL(ret);

    return ret;
}



static te_expr *term(state *s) {
    /* <term>      =    <factor> {("*" | "/" | "%") <factor>} */
    te_expr *ret = factor(s);
    CHECK_NULL(ret);

    while (s->type == TOK_INFIX && (s->function == mul || s->function == divide || s->function == fmod)) {
        te_fun2 t = s->function;
        next_token(s);
        te_expr *f = factor(s);
        CHECK_NULL(f, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, f);
        CHECK_NULL(ret, te_free(f), te_free(prev));

        ret->function = t;
    }

    return ret;
}


static te_expr *expr(state *s) {
    /* <expr>      =    <term> {("+" | "-") <term>} */
    te_expr *ret = term(s);
    CHECK_NULL(ret);

    while (s->type == TOK_INFIX && (s->function == add || s->function == sub)) {
        te_fun2 t = s->function;
        next_token(s);
        te_expr *te = term(s);
        CHECK_NULL(te, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, te);
        CHECK_NULL(ret, te_free(te), te_free(prev));

        ret->function = t;
    }

    return ret;
}


static te_expr *shift(state *s) {
    /* <shift>     =    <expr> {("<<" | ">>") <expr>} */
    te_expr *ret = expr(s);
    CHECK_NULL(ret);

    while (s->type == TOK_INFIX && (s->function == bitwise_lshift || s->function == bitwise_rshift)) {
        te_fun2 t = s->function;
        next_token(s);
        te_expr *rhs = expr(s);
        CHECK_NULL(rhs, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, rhs);
        CHECK_NULL(ret, te_free(rhs), te_free(prev));

        ret->function = t;
    }

    return ret;
}


static te_expr *relational(state *s) {
    /* <relational> = <shift> {("<" | "<=" | ">" | ">=") <shift>} */
    te_expr *ret = shift(s);
    CHECK_NULL(ret);

    while (s->type == TOK_INFIX &&
           (s->function == less || s->function == less_equal ||
            s->function == greater || s->function == greater_equal)) {
        te_fun2 t = s->function;
        next_token(s);
        te_expr *rhs = shift(s);
        CHECK_NULL(rhs, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, rhs);
        CHECK_NULL(ret, te_free(rhs), te_free(prev));

        ret->function = t;
    }

    return ret;
}


static te_expr *equality(state *s) {
    /* <equality>  =    <relational> {("==" | "!=") <relational>} */
    te_expr *ret = relational(s);
    CHECK_NULL(ret);

    while (s->type == TOK_INFIX && (s->function == equal || s->function == not_equal)) {
        te_fun2 t = s->function;
        next_token(s);
        te_expr *rhs = relational(s);
        CHECK_NULL(rhs, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, rhs);
        CHECK_NULL(ret, te_free(rhs), te_free(prev));

        ret->function = t;
    }

    return ret;
}


static te_expr *bitwise_and_expr(state *s) {
    /* <bitwise_and> =  <equality> {"&" <equality>} */
    te_expr *ret = equality(s);
    CHECK_NULL(ret);

    while (s->type == TOK_INFIX && s->function == bitwise_and) {
        te_fun2 t = s->function;
        next_token(s);
        te_expr *rhs = equality(s);
        CHECK_NULL(rhs, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, rhs);
        CHECK_NULL(ret, te_free(rhs), te_free(prev));

        ret->function = t;
    }

    return ret;
}


static te_expr *bitwise_xor_expr(state *s) {
    /* <bitwise_xor> =  <bitwise_and> {"^" <bitwise_and>} */
    te_expr *ret = bitwise_and_expr(s);
    CHECK_NULL(ret);

    while (s->type == TOK_INFIX && s->function == bitwise_xor) {
        te_fun2 t = s->function;
        next_token(s);
        te_expr *rhs = bitwise_and_expr(s);
        CHECK_NULL(rhs, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, rhs);
        CHECK_NULL(ret, te_free(rhs), te_free(prev));

        ret->function = t;
    }

    return ret;
}


static te_expr *bitwise_or_expr(state *s) {
    /* <bitwise_or> =   <bitwise_xor> {"|" <bitwise_xor>} */
    te_expr *ret = bitwise_xor_expr(s);
    CHECK_NULL(ret);

    while (s->type == TOK_INFIX && s->function == bitwise_or) {
        te_fun2 t = s->function;
        next_token(s);
        te_expr *rhs = bitwise_xor_expr(s);
        CHECK_NULL(rhs, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, rhs);
        CHECK_NULL(ret, te_free(rhs), te_free(prev));

        ret->function = t;
    }

    return ret;
}


static te_expr *logical_and_expr(state *s) {
    /* <logical_and> = <bitwise> {"&&" <bitwise>} */
    te_expr *ret = bitwise_or_expr(s);
    CHECK_NULL(ret);

    while (s->type == TOK_INFIX && s->function == logical_and) {
        te_fun2 t = s->function;
        next_token(s);
        te_expr *rhs = bitwise_or_expr(s);
        CHECK_NULL(rhs, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, rhs);
        CHECK_NULL(ret, te_free(rhs), te_free(prev));

        ret->function = t;
    }

    return ret;
}


static te_expr *logical_or_expr(state *s) {
    /* <logical_or> = <logical_and> {"||" <logical_and>} */
    te_expr *ret = logical_and_expr(s);
    CHECK_NULL(ret);

    while (s->type == TOK_INFIX && s->function == logical_or) {
        te_fun2 t = s->function;
        next_token(s);
        te_expr *rhs = logical_and_expr(s);
        CHECK_NULL(rhs, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, rhs);
        CHECK_NULL(ret, te_free(rhs), te_free(prev));

        ret->function = t;
    }

    return ret;
}


static te_expr *list(state *s) {
    /* <list>      =    <logical_or> {"," <logical_or>} */
    te_expr *ret = logical_or_expr(s);
    CHECK_NULL(ret);

    while (s->type == TOK_SEP) {
        next_token(s);
        te_expr *e = logical_or_expr(s);
        CHECK_NULL(e, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, e);
        CHECK_NULL(ret, te_free(e), te_free(prev));

        ret->function = comma;
    }

    return ret;
}


#define TE_FUN(...) ((double(*)(__VA_ARGS__))n->function)
#define M(e) te_eval(n->parameters[e])


double te_eval(const te_expr *n) {
    if (!n) return NAN;

    switch(TYPE_MASK(n->type)) {
        case TE_CONSTANT: return n->value;
        case TE_VARIABLE: return *n->bound;

        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
            switch(ARITY(n->type)) {
                case 0: return TE_FUN(void)();
                case 1: return TE_FUN(double)(M(0));
                case 2:
                    if (n->function == logical_and) {
                        const double left = M(0);
                        return left != 0.0 ? (M(1) != 0.0 ? 1.0 : 0.0) : 0.0;
                    }
                    if (n->function == logical_or) {
                        const double left = M(0);
                        return left != 0.0 ? 1.0 : (M(1) != 0.0 ? 1.0 : 0.0);
                    }
                    return TE_FUN(double, double)(M(0), M(1));
                case 3:
                    if (n->function == te_if) {
                        const double condition = M(0);
                        return condition != 0.0 ? M(1) : M(2);
                    }
                    return TE_FUN(double, double, double)(M(0), M(1), M(2));
                case 4: return TE_FUN(double, double, double, double)(M(0), M(1), M(2), M(3));
                case 5: return TE_FUN(double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4));
                case 6: return TE_FUN(double, double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4), M(5));
                case 7: return TE_FUN(double, double, double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4), M(5), M(6));
                default: return NAN;
            }

        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            switch(ARITY(n->type)) {
                case 0: return TE_FUN(void*)(n->parameters[0]);
                case 1: return TE_FUN(void*, double)(n->parameters[1], M(0));
                case 2: return TE_FUN(void*, double, double)(n->parameters[2], M(0), M(1));
                case 3: return TE_FUN(void*, double, double, double)(n->parameters[3], M(0), M(1), M(2));
                case 4: return TE_FUN(void*, double, double, double, double)(n->parameters[4], M(0), M(1), M(2), M(3));
                case 5: return TE_FUN(void*, double, double, double, double, double)(n->parameters[5], M(0), M(1), M(2), M(3), M(4));
                case 6: return TE_FUN(void*, double, double, double, double, double, double)(n->parameters[6], M(0), M(1), M(2), M(3), M(4), M(5));
                case 7: return TE_FUN(void*, double, double, double, double, double, double, double)(n->parameters[7], M(0), M(1), M(2), M(3), M(4), M(5), M(6));
                default: return NAN;
            }

        default: return NAN;
    }

}

#undef TE_FUN
#undef M

static void optimize(te_expr *n) {
    /* Evaluates as much as possible. */
    if (n->type == TE_CONSTANT) return;
    if (n->type == TE_VARIABLE) return;

    /* Only optimize out functions flagged as pure. */
    if (IS_PURE(n->type)) {
        const int arity = ARITY(n->type);
        int known = 1;
        int i;
        for (i = 0; i < arity; ++i) {
            optimize(n->parameters[i]);
            if (((te_expr*)(n->parameters[i]))->type != TE_CONSTANT) {
                known = 0;
            }
        }
        if (known) {
            const double value = te_eval(n);
            te_free_parameters(n);
            n->type = TE_CONSTANT;
            n->value = value;
        }
    }
}


te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error) {
    state s;
    s.start = s.next = expression;
    s.lookup = variables;
    s.lookup_len = var_count;
    s.index_arg = 0;
    s.index_value = 0.0;

    next_token(&s);
    te_expr *root = list(&s);
    if (root == NULL) {
        if (error) *error = -1;
        return NULL;
    }

    if (s.type != TOK_END) {
        te_free(root);
        if (error) {
            *error = (s.next - s.start);
            if (*error == 0) *error = 1;
        }
        return 0;
    } else {
        optimize(root);
        if (error) *error = 0;
        return root;
    }
}


double te_interp(const char *expression, int *error) {
    te_expr *n = te_compile(expression, 0, 0, error);

    double ret;
    if (n) {
        ret = te_eval(n);
        te_free(n);
    } else {
        ret = NAN;
    }
    return ret;
}

static void pn (const te_expr *n, int depth) {
    int i, arity;
    printf("%*s", depth, "");

    switch(TYPE_MASK(n->type)) {
    case TE_CONSTANT: printf("%f\n", n->value); break;
    case TE_VARIABLE: printf("bound %p\n", n->bound); break;

    case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
    case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
    case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
    case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
         arity = ARITY(n->type);
         printf("f%d", arity);
         for(i = 0; i < arity; i++) {
             printf(" %p", n->parameters[i]);
         }
         printf("\n");
         for(i = 0; i < arity; i++) {
             pn(n->parameters[i], depth + 1);
         }
         break;
    }
}


void te_print(const te_expr *n) {
    pn(n, 0);
}
