#include "scanner.h"

#include <string.h>
#include <ctype.h>

typedef struct {
    void *id;
    scanner_callback callback;
    void *args;
    uint8_t active;
} scanner_rule;

struct scanner_lexer {
    void *stream;
    int (*getc)(void*);
    void *eof;
    void *err;

    scanner_rule *rules;
    size_t rules_count;
    size_t rules_size;

    char *buffer;
    size_t buffer_length;
    size_t buffer_size;
};

static inline size_t scanner_round_size(size_t size) {
    if(size == 0) return 1;
    size--;

    size |= size >> 1;
    size |= size >> 2;
    size |= size >> 4;
    size |= size >> 8;
    size |= size >> 16;
    size |= size >> 32;

    return size + 1;
}

static int scanner_resize_buffer(scanner_lexer *lexer) {
    size_t new_size = scanner_round_size(lexer->buffer_size + 1) + 1;
    
    char *new_buffer = SCANNER_REALLOC(lexer->buffer, new_size);
    if(!new_buffer) return -1;

    lexer->buffer = new_buffer;
    lexer->buffer_size = new_size;
    
    return 0;
}

static int scanner_fill_buffer(scanner_lexer *lexer) {
    if(lexer->buffer_size == 0) {
        if(scanner_resize_buffer(lexer)) return -1;
    }

    while(lexer->buffer_length < lexer->buffer_size - 1) {
        int c = lexer->getc(lexer->stream);
        if(c == EOF) break;
        lexer->buffer[lexer->buffer_length++] = (char)(c);
    }

    lexer->buffer[lexer->buffer_length] = '\0';
    return 0;
}

static void scanner_slide_buffer(scanner_lexer *lexer, size_t n) {
    size_t left = lexer->buffer_length - n;
    if(left) memmove(lexer->buffer, lexer->buffer + n, left);
    lexer->buffer_length = left;
    lexer->buffer[left] = '\0';
}

static scanner_token *scanner_build_token(const char *lxme, size_t n, void *id) {
    size_t total_size = sizeof(scanner_token) + n + 1;

    scanner_token *token = SCANNER_MALLOC(total_size);
    if(!token) return NULL;

    memcpy(token->lxme, lxme, n);
    token->lxme[n] = '\0';
    token->id = id;

    return token;
}

static int is_octal(char c) {
    return (c >= '0' && c <= '7');
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

scanner_lexer *scanner_create(void *stream, int (*getc)(void*), void *eof, void *err) {
    scanner_lexer *lexer = SCANNER_MALLOC(sizeof(scanner_lexer));
    if(!lexer) return NULL;

    lexer->stream = stream;
    lexer->getc = getc;
    lexer->eof = eof;
    lexer->err = err;

    lexer->rules = NULL;
    lexer->rules_count = 0;
    lexer->rules_size = 0;

    lexer->buffer = NULL;
    lexer->buffer_length = 0;
    lexer->buffer_size = 0;

    return lexer;
}

char *__internal_scanner_close(scanner_lexer *lexer, int option) {
    if(lexer->rules) SCANNER_FREE(lexer->rules);
    
    char *out = NULL;
    if(option == SCANNER_GETBUF) out = lexer->buffer;
    else if(lexer->buffer) SCANNER_FREE(lexer->buffer);

    SCANNER_FREE(lexer);
    
    return out;
}

scanner_callback scanner_policy(scanner_lexer *lexer, void *id, scanner_callback callback, void *args) {
    if(lexer->rules_count >= lexer->rules_size) {
        size_t new_size = scanner_round_size(lexer->rules_size + 1);

        scanner_rule *new_rules = SCANNER_REALLOC(lexer->rules, new_size * sizeof(scanner_rule));
        if(!new_rules) return NULL;

        lexer->rules = new_rules;
        lexer->rules_size = new_size;
    }

    lexer->rules[lexer->rules_count].id = id;
    lexer->rules[lexer->rules_count].callback = callback;
    lexer->rules[lexer->rules_count].args = args;
    lexer->rules[lexer->rules_count].active = 1;
    
    lexer->rules_count++;

    return callback;
}

scanner_token *scanner_next(scanner_lexer *lexer) {
    restart_scan:

    if(lexer->buffer_length == 0) {
        if(scanner_fill_buffer(lexer)) return NULL;
        if(lexer->buffer_length == 0) {
            return scanner_build_token("", 0, lexer->eof);
        }
    }

    size_t longest_match = 0;
    void *matched_id = lexer->err;

    for(size_t n = 0; n < lexer->rules_count; n++) {
        lexer->rules[n].active = 1;
    }

    size_t offset = 0;
    size_t active_count = lexer->rules_count;

    while(active_count > 0) {
        int c;

        if(offset >= lexer->buffer_length) {
            size_t old_length = lexer->buffer_length;
            if(scanner_resize_buffer(lexer) || scanner_fill_buffer(lexer)) {
                return NULL;
            }

            if(lexer->buffer_length == old_length) {
                if(offset >lexer->buffer_length) break;
                c = EOF;
            }
            
            else {
                c = (unsigned char)(lexer->buffer[offset]);
            }
        }

        else c = (unsigned char)(lexer->buffer[offset]);

        size_t callnum = offset;
        active_count = 0;

        for(size_t n = 0; n < lexer->rules_count; n++) {
            scanner_rule *rule = &lexer->rules[n];
            if(!rule->active) continue;

            scanner_status status = rule->callback(c, callnum, rule->args);

            if(status == SCANNER_MATCH) {
                if((offset + 1) >= longest_match) {
                    longest_match = offset + 1;
                    matched_id = rule->id;
                }
                rule->active = 0;
            }

            else if(status == SCANNER_RETURN) {
                if(offset >= longest_match) {
                    longest_match = offset;
                    matched_id = rule->id;
                }
                rule->active = 0;
            }

            else if(status == SCANNER_RECALL) {
                rule->active = 1;
                active_count++;
            }

            else if(status == SCANNER_FAILURE) {
                rule->active = 0;
            }
        }

        offset++;
    }

    if(matched_id == lexer->err) longest_match = 1;

    if(matched_id == SCANNER_SKIP) {
        scanner_slide_buffer(lexer, longest_match);
        goto restart_scan;        
    }
    
    scanner_token *token = scanner_build_token(lexer->buffer, longest_match, matched_id);
    if(!token) return NULL;
    
    scanner_slide_buffer(lexer, longest_match);
    return token;
}

scanner_token *scanner_peek(scanner_lexer *lexer, int (*ungetc)(void*, int), size_t (*tell)(void*), void (*seek)(void*, size_t)) {
    while(lexer->buffer_length > 0) {
        ungetc(lexer->stream, lexer->buffer[lexer->buffer_length--]);
    }

    size_t save = tell(lexer->stream);
    scanner_token *token = scanner_next(lexer);

    lexer->buffer[0] = '\0';
    lexer->buffer_length = 0;

    seek(lexer->stream, save);
    return token;
}

int scanner_fgetc(void *file) {
    return fgetc(file);
}

int scanner_string(void *strptr) {
    const char **ptr = strptr;
    int c = **ptr;
    if(c == '\0') return EOF;
    *ptr += 1;
    return c;
}

int scanner_input(void *reset_input) {
    int *ptr = reset_input;
    int reached_eof = !*ptr;
    if(reached_eof) return EOF;

    int c = getchar();
    if(c == '\n') {
        *ptr = 0;
        return EOF;
    }

    return c;
}

void scanner_unescape(char *str) {
    char *dst = str;

    while(*str != '\0') {
        if(*str == '\\' && *(str + 1) != '\0') {
            str++;
            
            switch(*str) {
                case 'a':  *dst++ = '\a'; str++; break;
                case 'b':  *dst++ = '\b'; str++; break;
                case 'f':  *dst++ = '\f'; str++; break;
                case 'n':  *dst++ = '\n'; str++; break;
                case 'r':  *dst++ = '\r'; str++; break;
                case 't':  *dst++ = '\t'; str++; break;
                case 'v':  *dst++ = '\v'; str++; break;
                case '\\': *dst++ = '\\'; str++; break;
                case '\'': *dst++ = '\''; str++; break;
                case '\"': *dst++ = '\"'; str++; break;
                case '?':  *dst++ = '\?'; str++; break;

                case 'x': case 'X': {
                    str++;
                    unsigned int value = 0;
                    int digits = 0;
                    
                    while(hex_val(*str) != -1) {
                        value = (value << 4) | hex_val(*str);
                        str++;
                        digits++;
                    }
                    
                    if(digits == 0) {
                        *dst++ = '\\';
                        *dst++ = *(str - 1); // Scrive la 'x' o 'X'
                    } else {
                        *dst++ = (char)(value & 0xFF);
                    }
                    break;
                }

                default:
                    if(is_octal(*str)) {
                        unsigned int value = 0;
                        int count = 0;
                        
                        while(count < 3 && is_octal(*str)) {
                            value = (value << 3) | (*str - '0');
                            str++;
                            count++;
                        }
                        *dst++ = (char)(value & 0xFF);
                    } else {
                        *dst++ = '\\';
                        *dst++ = *str++;
                    }
                    break;
            }
        } else {
            *dst++ = *str++;
        }
    }
    *dst = '\0';
}

scanner_status scanner_whitespace(int c, size_t callnum, void *_) {
    if(isspace(c)) return SCANNER_RECALL;
    if(callnum == 0) return SCANNER_FAILURE;
    return SCANNER_RETURN;
}

scanner_status scanner_number(int c, size_t callnum, void *_) {
    if(isdigit(c)) return SCANNER_RECALL;
    if(callnum == 0) return SCANNER_FAILURE;
    return SCANNER_RETURN;
}

scanner_status scanner_decimal(int c, size_t callnum, void *scanner_decimal_struct) {
    struct scanner_decimal *args = scanner_decimal_struct;
    if(!args->initialized) {
        args->last_char_was_dot = 0;
        args->dot_found = 0;
        args->initialized = 1;
    }

    if(isdigit(c)) {
        if(args->last_char_was_dot) {
            args->last_char_was_dot = 0;
        }

        return SCANNER_RECALL;
    }

    else if(c == '.') {
        if(args->dot_found) {
            if(args->last_char_was_dot) {
                args->dot_found = 0;
                args->last_char_was_dot = 0;
                return SCANNER_FAILURE;
            }

            return SCANNER_RETURN;
        }

        args->dot_found = 1;
        args->last_char_was_dot = 1;
        return SCANNER_RECALL;
    }

    else return SCANNER_FAILURE;

    if(args->last_char_was_dot) return SCANNER_FAILURE;
    return SCANNER_RETURN;
}

scanner_status scanner_quoted(int c, size_t callnum, void *scanner_quoted_struct) {
    struct scanner_quoted *args = scanner_quoted_struct;
    if(!args->initialized) {
        args->llen = strlen(args->lquote);
        args->rlen = strlen(args->rquote);
        args->closing_index = 0;
        args->initialized = 1;
    }

    if(args->llen == 0) return SCANNER_FAILURE;
    if(args->rlen == 0) return SCANNER_FAILURE;

    if(callnum < args->llen) {
        if(c != args->lquote[callnum]) return SCANNER_FAILURE;
        return SCANNER_RECALL;
    }

    if(c == args->rquote[args->closing_index]) {
        if(args->closing_index + 1 == args->rlen) return SCANNER_MATCH;
        args->closing_index++;
    }

    args->closing_index = 0;
    return SCANNER_RECALL;
}

scanner_status scanner_identifier(int c, size_t callnum, void *_) {
    if(callnum == 0) {
        if(isalpha(c) || c == '_') return SCANNER_RECALL;
        return SCANNER_FAILURE;
    }

    if(isalnum(c) || c == '_') return SCANNER_RECALL;
    return SCANNER_RETURN;
}

scanner_status scanner_sentence(int c, size_t callnum, void *_) {
    if(!isalpha(c) && !isspace(c)) {
        if(callnum == 0) return SCANNER_FAILURE;
        return SCANNER_RETURN;
    }
    return SCANNER_RECALL;
}

scanner_status scanner_sensitive(int c, size_t callnum, void *scanner_case_struct) {
    struct scanner_case *text = scanner_case_struct;
    if(!text->initialized) {
        text->len = strlen(text->text);
        text->initialized = 1;
    }

    if(callnum == text->len) return SCANNER_RETURN;
    if(c != text->text[callnum]) return SCANNER_FAILURE;
    return SCANNER_RECALL;
}

scanner_status scanner_unsensitive(int c, size_t callnum, void *scanner_case_struct) {
    struct scanner_case *text = scanner_case_struct;
    if(!text->initialized) {
        text->len = strlen(text->text);
        text->initialized = 1;
    }

    if(callnum == text->len) return SCANNER_RETURN;
    if(tolower(c) != tolower(text->text[callnum])) return SCANNER_FAILURE;
    return SCANNER_RECALL;
}

scanner_status scanner_character(int c, size_t callnum, void *characterp) {
    int *ptr = characterp;
    if(c != *ptr) return SCANNER_FAILURE;
    return SCANNER_MATCH;
}
