#ifndef SCANNER_H
#define SCANNER_H

#include <stdio.h>
#include <stdlib.h>

#ifndef SCANNER_MALLOC
#define SCANNER_MALLOC(siz) malloc((siz))
#endif

#ifndef SCANNER_REALLOC
#define SCANNER_REALLOC(ptr,siz) realloc((ptr), (siz))
#endif

#ifndef SCANNER_FREE
#define SCANNER_FREE(ptr) free(ptr)
#endif

#ifndef SCANNER_SKIP
#define SCANNER_SKIP NULL
#endif

#ifndef SCANNER_GETBUF
#define SCANNER_GETBUF 1
#endif

#define __internal_scanner_expand(lexer, option, ...) __internal_scanner_close((lexer), (option))

#define scanner_close(...) __internal_scanner_expand(__VA_ARGS__, 0, 0)

typedef enum {
    SCANNER_MATCH,
    SCANNER_RETURN,
    SCANNER_RECALL,
    SCANNER_FAILURE
} scanner_status;

typedef scanner_status (*scanner_callback)(int c, size_t callnum, void *args);

typedef struct scanner_lexer scanner_lexer;

typedef struct {
    void* id;
    char lxme[];
} scanner_token;

struct scanner_decimal {
    int initialized;
    int last_char_was_dot;
    int dot_found;
};

struct scanner_quoted {
    int initialized;
    const char *lquote;
    const char *rquote;
    size_t llen;
    size_t rlen;
    size_t closing_index;
};

struct scanner_case {
    int initialized;
    const char *text;
    size_t len;
};

scanner_lexer *scanner_create(void *stream, int (*getc)(void*), void *eof, void *err);

char *__internal_scanner_close(scanner_lexer *lexer, int option);

scanner_callback scanner_policy(scanner_lexer *lexer, void *id, scanner_callback callback, void *args);

scanner_token *scanner_next(scanner_lexer *lexer);

scanner_token *scanner_peek(scanner_lexer *lexer, int (*ungetc)(void*, int), size_t (*tell)(void*), void (*seek)(void*, size_t));

int scanner_fgetc(void *file);

int scanner_string(void *strptr);

int scanner_input(void *reset_input);

scanner_status scanner_whitespace(int c, size_t callnum, void *_);

scanner_status scanner_number(int c, size_t callnum, void *_);

scanner_status scanner_decimal(int c, size_t callnum, void *scanner_decimal_struct);

scanner_status scanner_quoted(int c, size_t callnum, void *scanner_quoted_struct);

scanner_status scanner_identifier(int c, size_t callnum, void *_);

scanner_status scanner_sentence(int c, size_t callnum, void *_);

scanner_status scanner_sensitive(int c, size_t callnum, void *scanner_case_struct);

scanner_status scanner_unsensitive(int c, size_t callnum, void *scanner_case_struct);

scanner_status scanner_character(int c, size_t callnum, void *characterp);

#endif
