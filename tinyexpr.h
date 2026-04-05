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

#ifndef TINYEXPR_H
#define TINYEXPR_H


#ifdef __cplusplus
extern "C" {
#endif



typedef struct te_expr {
    int type;
    union {double value; const double *bound; const void *function;};
    void *parameters[1];
} te_expr;


enum {
    TE_VARIABLE = 0,

    TE_FUNCTION0 = 8, TE_FUNCTION1, TE_FUNCTION2, TE_FUNCTION3,
    TE_FUNCTION4, TE_FUNCTION5, TE_FUNCTION6, TE_FUNCTION7,

    TE_CLOSURE0 = 16, TE_CLOSURE1, TE_CLOSURE2, TE_CLOSURE3,
    TE_CLOSURE4, TE_CLOSURE5, TE_CLOSURE6, TE_CLOSURE7,

    TE_FLAG_PURE = 32
};

typedef struct te_variable {
    const char *name;
    const void *address;
    int type;
    void *context;
} te_variable;



/* Parses the input expression, evaluates it, and frees it. */
/* Returns NaN on error. */
double te_interp(const char *expression, int *error);

/* Parses the input expression and binds variables. */
/* Returns NULL on error. */
te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error);

/* Evaluates the expression. */
double te_eval(const te_expr *n);

/* Prints debugging information on the syntax tree. */
void te_print(const te_expr *n);

/* Frees the expression. */
/* This is safe to call on NULL pointers. */
void te_free(te_expr *n);


/* Expression pool: compile a string pool (NUL-separated, double-NUL terminated)
 * into an array of te_expr*, indexed by sequence number 0,1,2,...
 *
 * String pool format:  "expr0\0expr1\0expr2\0\0"
 *                       ^--- buf                 ^--- buf+len
 */
typedef struct te_pool te_pool;

/* Compile all expressions in the pool.
 * errors: optional int array (caller-allocated, length = number of expressions).
 *         errors[i] == 0 means success; non-zero means parse error position. */
te_pool *te_pool_compile(const char *buf, int len,
                         const te_variable *variables, int var_count,
                         int *errors);

/* Evaluate the i-th expression. Returns NaN on bad index or compile error. */
double te_pool_eval(const te_pool *pool, int index);

/* Number of expressions in the pool. */
int te_pool_count(const te_pool *pool);

/* Free the pool and all compiled expressions. */
void te_pool_free(te_pool *pool);


#ifdef __cplusplus
}
#endif

#endif /*TINYEXPR_H*/
