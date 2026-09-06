#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHELL_MAX_TOKENS 128
#define SHELL_MAX_STAGES 16
#define SHELL_MAX_ARGS 32
#define SHELL_LINE_MAX 4096

enum token_type {
    TOKEN_WORD,
    TOKEN_PIPE,
    TOKEN_SEMICOLON,
    TOKEN_INPUT,
    TOKEN_OUTPUT,
    TOKEN_APPEND
};

struct token {
    enum token_type type;
    char *text;
};

struct stage {
    char *argv[SHELL_MAX_ARGS + 1];
    int argc;
    char *input_path;
    char *output_path;
    int append;
};

static int shell_write(const struct cb_api_v1 *api, int descriptor,
                       const char *text)
{
    size_t remaining = strlen(text);
    const char *cursor = text;
    while (remaining > 0) {
        cb_ssize_t count = api->write(descriptor, cursor, remaining);
        if (count <= 0)
            return -1;
        cursor += (size_t)count;
        remaining -= (size_t)count;
    }
    return 0;
}

static void shell_error(const struct cb_api_v1 *api, const char *message)
{
    shell_write(api, 2, "sh: ");
    shell_write(api, 2, message);
    shell_write(api, 2, "\n");
}

static int append_bytes(char **buffer, size_t *used, size_t *capacity,
                        const char *bytes, size_t count)
{
    char *resized;
    size_t needed = *used + count + 1;
    if (needed > *capacity) {
        size_t next = *capacity == 0 ? 32 : *capacity;
        while (next < needed)
            next *= 2;
        resized = realloc(*buffer, next);
        if (resized == NULL)
            return -1;
        *buffer = resized;
        *capacity = next;
    }
    memcpy(*buffer + *used, bytes, count);
    *used += count;
    (*buffer)[*used] = '\0';
    return 0;
}

static int append_expansion(const struct cb_api_v1 *api, const char **cursor,
                            int last_status, char **buffer, size_t *used,
                            size_t *capacity)
{
    const char *start;
    const char *value;
    char name[128];
    size_t length;
    char status[32];
    if (**cursor == '?') {
        int count = snprintf(status, sizeof(status), "%d", last_status);
        ++*cursor;
        return append_bytes(buffer, used, capacity, status, (size_t)count);
    }
    if (**cursor == '{') {
        ++*cursor;
        start = *cursor;
        while (**cursor != '\0' && **cursor != '}')
            ++*cursor;
        if (**cursor != '}')
            return -1;
        length = (size_t)(*cursor - start);
        ++*cursor;
    } else {
        start = *cursor;
        while (isalnum((unsigned char)**cursor) || **cursor == '_')
            ++*cursor;
        length = (size_t)(*cursor - start);
    }
    if (length == 0)
        return append_bytes(buffer, used, capacity, "$", 1);
    if (length >= sizeof(name))
        return -1;
    memcpy(name, start, length);
    name[length] = '\0';
    value = api->getenv(name);
    if (value == NULL)
        value = "";
    return append_bytes(buffer, used, capacity, value, strlen(value));
}

static void free_tokens(struct token *tokens, size_t count)
{
    size_t index;
    for (index = 0; index < count; ++index)
        free(tokens[index].text);
}

static int lex_line(const struct cb_api_v1 *api, const char *line,
                    int last_status, struct token *tokens, size_t *count_out)
{
    const char *cursor = line;
    size_t count = 0;
    while (*cursor != '\0') {
        struct token *token;
        while (isspace((unsigned char)*cursor))
            ++cursor;
        if (*cursor == '\0')
            break;
        if (count == SHELL_MAX_TOKENS)
            goto syntax_error;
        token = &tokens[count];
        token->text = NULL;
        if (*cursor == '|') {
            token->type = TOKEN_PIPE;
            ++cursor;
        } else if (*cursor == ';') {
            token->type = TOKEN_SEMICOLON;
            ++cursor;
        } else if (*cursor == '<') {
            token->type = TOKEN_INPUT;
            ++cursor;
        } else if (*cursor == '>') {
            ++cursor;
            if (*cursor == '>') {
                ++cursor;
                token->type = TOKEN_APPEND;
            } else {
                token->type = TOKEN_OUTPUT;
            }
        } else {
            char *word = NULL;
            size_t used = 0;
            size_t capacity = 0;
            int quote = 0;
            int saw_part = 0;
            token->type = TOKEN_WORD;
            while (*cursor != '\0') {
                char character = *cursor;
                if (quote == 0 && (isspace((unsigned char)character) ||
                    character == '|' || character == ';' ||
                    character == '<' || character == '>'))
                    break;
                if (character == '\'' && quote != '"') {
                    saw_part = 1;
                    quote = quote == '\'' ? 0 : '\'';
                    ++cursor;
                    continue;
                }
                if (character == '"' && quote != '\'') {
                    saw_part = 1;
                    quote = quote == '"' ? 0 : '"';
                    ++cursor;
                    continue;
                }
                if (character == '\\' && quote != '\'') {
                    ++cursor;
                    if (*cursor == '\0') {
                        free(word);
                        goto syntax_error;
                    }
                    character = *cursor++;
                    if (append_bytes(&word, &used, &capacity, &character, 1) < 0)
                        goto memory_error;
                    saw_part = 1;
                    continue;
                }
                if (character == '$' && quote != '\'') {
                    ++cursor;
                    if (append_expansion(api, &cursor, last_status, &word,
                                         &used, &capacity) < 0) {
                        free(word);
                        goto syntax_error;
                    }
                    saw_part = 1;
                    continue;
                }
                ++cursor;
                if (append_bytes(&word, &used, &capacity, &character, 1) < 0)
                    goto memory_error;
                saw_part = 1;
            }
            if (quote != 0 || !saw_part) {
                free(word);
                goto syntax_error;
            }
            if (word == NULL) {
                word = malloc(1);
                if (word == NULL)
                    goto memory_error;
                word[0] = '\0';
            }
            token->text = word;
        }
        ++count;
    }
    *count_out = count;
    return 0;

memory_error:
    free_tokens(tokens, count);
    shell_error(api, "out of memory");
    return -1;
syntax_error:
    free_tokens(tokens, count);
    shell_error(api, "syntax error");
    return -1;
}

static int is_builtin(const char *name)
{
    return strcmp(name, "cd") == 0 || strcmp(name, "pwd") == 0 ||
           strcmp(name, "export") == 0 || strcmp(name, "unset") == 0 ||
           strcmp(name, "exit") == 0;
}

static int run_builtin(const struct cb_api_v1 *api, struct stage *stage,
                       int last_status, int *should_exit)
{
    const char *name = stage->argv[0];
    if (strcmp(name, "cd") == 0) {
        const char *path;
        if (stage->argc > 2) {
            shell_error(api, "cd: too many arguments");
            return 2;
        }
        path = stage->argc == 1 ? api->getenv("HOME") : stage->argv[1];
        if (path == NULL)
            path = "/";
        if (api->chdir(path) < 0) {
            shell_write(api, 2, "sh: cd: ");
            shell_write(api, 2, api->strerror(api->get_errno()));
            shell_write(api, 2, "\n");
            return 1;
        }
        return 0;
    }
    if (strcmp(name, "pwd") == 0) {
        char path[CB_PATH_MAX];
        if (api->getcwd(path, sizeof(path)) == NULL) {
            shell_error(api, api->strerror(api->get_errno()));
            return 1;
        }
        shell_write(api, 1, path);
        shell_write(api, 1, "\n");
        return 0;
    }
    if (strcmp(name, "export") == 0) {
        int index;
        for (index = 1; index < stage->argc; ++index) {
            char *equals = strchr(stage->argv[index], '=');
            if (equals == NULL || equals == stage->argv[index]) {
                shell_error(api, "export: expected NAME=value");
                return 2;
            }
            *equals = '\0';
            if (api->setenv(stage->argv[index], equals + 1, 1) < 0) {
                *equals = '=';
                shell_error(api, "export: invalid name");
                return 2;
            }
            *equals = '=';
        }
        return 0;
    }
    if (strcmp(name, "unset") == 0) {
        int index;
        for (index = 1; index < stage->argc; ++index) {
            if (api->unsetenv(stage->argv[index]) < 0) {
                shell_error(api, "unset: invalid name");
                return 2;
            }
        }
        return 0;
    }
    if (strcmp(name, "exit") == 0) {
        int status = last_status;
        if (stage->argc > 2) {
            shell_error(api, "exit: too many arguments");
            return 2;
        }
        if (stage->argc == 2)
            status = atoi(stage->argv[1]);
        *should_exit = 1;
        return status & 0xff;
    }
    return 127;
}

static int open_redirections(const struct cb_api_v1 *api, struct stage *stage,
                             int *input_fd, int *output_fd)
{
    *input_fd = -1;
    *output_fd = -1;
    if (stage->input_path != NULL) {
        *input_fd = api->open(stage->input_path, CB_O_RDONLY, 0);
        if (*input_fd < 0)
            return -1;
    }
    if (stage->output_path != NULL) {
        int flags = CB_O_WRONLY | CB_O_CREAT |
                    (stage->append ? CB_O_APPEND : CB_O_TRUNC);
        *output_fd = api->open(stage->output_path, flags, 0666);
        if (*output_fd < 0) {
            if (*input_fd >= 0)
                api->close(*input_fd);
            *input_fd = -1;
            return -1;
        }
    }
    return 0;
}

static int run_builtin_redirected(const struct cb_api_v1 *api,
                                  struct stage *stage, int last_status,
                                  int *should_exit)
{
    int input_fd;
    int output_fd;
    int saved_input = -1;
    int saved_output = -1;
    int result;
    if (open_redirections(api, stage, &input_fd, &output_fd) < 0) {
        shell_error(api, api->strerror(api->get_errno()));
        return 1;
    }
    if (input_fd >= 0) {
        saved_input = api->dup(0);
        if (saved_input < 0 || api->dup2(input_fd, 0) < 0)
            goto descriptor_error;
    }
    if (output_fd >= 0) {
        saved_output = api->dup(1);
        if (saved_output < 0 || api->dup2(output_fd, 1) < 0)
            goto descriptor_error;
    }
    result = run_builtin(api, stage, last_status, should_exit);
    if (saved_input >= 0)
        api->dup2(saved_input, 0);
    if (saved_output >= 0)
        api->dup2(saved_output, 1);
    if (saved_input >= 0)
        api->close(saved_input);
    if (saved_output >= 0)
        api->close(saved_output);
    if (input_fd >= 0)
        api->close(input_fd);
    if (output_fd >= 0)
        api->close(output_fd);
    return result;

descriptor_error:
    shell_error(api, "redirection failed");
    if (saved_input >= 0)
        api->close(saved_input);
    if (saved_output >= 0)
        api->close(saved_output);
    if (input_fd >= 0)
        api->close(input_fd);
    if (output_fd >= 0)
        api->close(output_fd);
    return 1;
}

static size_t add_action(struct cb_spawn_action_v1 *actions, size_t count,
                         enum cb_spawn_action_type type, int from, int to)
{
    actions[count].abi_version = CB_ABI_VERSION_V1;
    actions[count].struct_size = sizeof(actions[count]);
    actions[count].type = (uint32_t)type;
    actions[count].from_fd = from;
    actions[count].to_fd = to;
    return count + 1;
}

static int run_pipeline(const struct cb_api_v1 *api, struct stage *stages,
                        size_t stage_count, int last_status,
                        int *should_exit)
{
    int pipes[SHELL_MAX_STAGES - 1][2];
    int inputs[SHELL_MAX_STAGES];
    int outputs[SHELL_MAX_STAGES];
    cb_pid_t pids[SHELL_MAX_STAGES];
    size_t pipe_count = stage_count > 0 ? stage_count - 1 : 0;
    size_t created_pipes = 0;
    size_t spawned = 0;
    size_t index;
    int status = 0;

    if (stage_count == 1 && is_builtin(stages[0].argv[0]))
        return run_builtin_redirected(api, &stages[0], last_status,
                                      should_exit);
    for (index = 0; index < stage_count; ++index) {
        inputs[index] = -1;
        outputs[index] = -1;
        if (is_builtin(stages[index].argv[0])) {
            shell_error(api, "built-in command cannot be used in a pipeline yet");
            return 2;
        }
        if (open_redirections(api, &stages[index], &inputs[index],
                              &outputs[index]) < 0) {
            shell_error(api, api->strerror(api->get_errno()));
            goto cleanup;
        }
    }
    for (created_pipes = 0; created_pipes < pipe_count; ++created_pipes) {
        if (api->pipe(pipes[created_pipes]) < 0) {
            shell_error(api, "cannot create pipe");
            goto cleanup;
        }
    }
    for (index = 0; index < stage_count; ++index) {
        struct cb_spawn_action_v1 actions[2 + 2 * (SHELL_MAX_STAGES - 1) + 2];
        size_t action_count = 0;
        size_t pipe_index;
        int source_input = inputs[index] >= 0 ? inputs[index] :
                           (index > 0 ? pipes[index - 1][0] : -1);
        int source_output = outputs[index] >= 0 ? outputs[index] :
                            (index + 1 < stage_count ? pipes[index][1] : -1);
        if (source_input >= 0)
            action_count = add_action(actions, action_count, CB_SPAWN_DUP2,
                                      source_input, 0);
        if (source_output >= 0)
            action_count = add_action(actions, action_count, CB_SPAWN_DUP2,
                                      source_output, 1);
        for (pipe_index = 0; pipe_index < pipe_count; ++pipe_index) {
            action_count = add_action(actions, action_count, CB_SPAWN_CLOSE,
                                      pipes[pipe_index][0], -1);
            action_count = add_action(actions, action_count, CB_SPAWN_CLOSE,
                                      pipes[pipe_index][1], -1);
        }
        if (inputs[index] >= 0)
            action_count = add_action(actions, action_count, CB_SPAWN_CLOSE,
                                      inputs[index], -1);
        if (outputs[index] >= 0 && outputs[index] != inputs[index])
            action_count = add_action(actions, action_count, CB_SPAWN_CLOSE,
                                      outputs[index], -1);
        if (api->spawn(stages[index].argv[0], stages[index].argv, NULL,
                       actions, action_count, &pids[index]) < 0) {
            shell_write(api, 2, "sh: ");
            shell_write(api, 2, stages[index].argv[0]);
            shell_write(api, 2, ": ");
            shell_write(api, 2, api->strerror(api->get_errno()));
            shell_write(api, 2, "\n");
            goto cleanup;
        }
        ++spawned;
    }

cleanup:
    for (index = 0; index < created_pipes; ++index) {
        api->close(pipes[index][0]);
        api->close(pipes[index][1]);
    }
    for (index = 0; index < stage_count; ++index) {
        if (inputs[index] >= 0)
            api->close(inputs[index]);
        if (outputs[index] >= 0)
            api->close(outputs[index]);
    }
    for (index = 0; index < spawned; ++index) {
        int child_status = 0;
        if (api->waitpid(pids[index], &child_status) >= 0 &&
            index + 1 == stage_count)
            status = child_status;
    }
    if (spawned != stage_count)
        return 127;
    return status;
}

static int execute_segment(const struct cb_api_v1 *api, const char *line,
                           int last_status, int *should_exit)
{
    struct token tokens[SHELL_MAX_TOKENS];
    size_t token_count;
    size_t cursor = 0;
    int status = last_status;
    if (lex_line(api, line, last_status, tokens, &token_count) < 0)
        return 2;
    while (cursor < token_count) {
        struct stage stages[SHELL_MAX_STAGES];
        size_t stage_count = 1;
        int expecting_command = 1;
        memset(stages, 0, sizeof(stages));
        while (cursor < token_count &&
               tokens[cursor].type != TOKEN_SEMICOLON) {
            struct stage *stage = &stages[stage_count - 1];
            enum token_type type = tokens[cursor].type;
            if (type == TOKEN_WORD) {
                if (stage->argc == SHELL_MAX_ARGS) {
                    shell_error(api, "too many arguments");
                    status = 2;
                    goto done;
                }
                stage->argv[stage->argc++] = tokens[cursor].text;
                stage->argv[stage->argc] = NULL;
                expecting_command = 0;
                ++cursor;
            } else if (type == TOKEN_INPUT || type == TOKEN_OUTPUT ||
                       type == TOKEN_APPEND) {
                if (++cursor == token_count ||
                    tokens[cursor].type != TOKEN_WORD) {
                    shell_error(api, "redirection requires a path");
                    status = 2;
                    goto done;
                }
                if (type == TOKEN_INPUT)
                    stage->input_path = tokens[cursor].text;
                else {
                    stage->output_path = tokens[cursor].text;
                    stage->append = type == TOKEN_APPEND;
                }
                ++cursor;
            } else if (type == TOKEN_PIPE) {
                if (expecting_command || stage->argc == 0 ||
                    stage_count == SHELL_MAX_STAGES) {
                    shell_error(api, "invalid pipeline");
                    status = 2;
                    goto done;
                }
                ++stage_count;
                expecting_command = 1;
                ++cursor;
            } else {
                shell_error(api, "syntax error");
                status = 2;
                goto done;
            }
        }
        if (expecting_command || stages[stage_count - 1].argc == 0) {
            shell_error(api, "missing command");
            status = 2;
            goto done;
        }
        status = run_pipeline(api, stages, stage_count, status, should_exit);
        if (*should_exit)
            goto done;
        if (cursor < token_count && tokens[cursor].type == TOKEN_SEMICOLON)
            ++cursor;
    }

done:
    free_tokens(tokens, token_count);
    return status;
}

static int execute_line(const struct cb_api_v1 *api, const char *line,
                        int last_status, int *should_exit)
{
    const char *start = line;
    const char *cursor = line;
    int quote = 0;
    int escaped = 0;
    int status = last_status;
    for (;;) {
        int at_end = *cursor == '\0';
        int separator = !at_end && !escaped && quote == 0 && *cursor == ';';
        if (at_end || separator) {
            size_t length = (size_t)(cursor - start);
            char *segment = malloc(length + 1);
            if (segment == NULL) {
                shell_error(api, "out of memory");
                return 2;
            }
            memcpy(segment, start, length);
            segment[length] = '\0';
            if (strspn(segment, " \t\r\n") != length)
                status = execute_segment(api, segment, status, should_exit);
            free(segment);
            if (*should_exit || at_end)
                return status;
            start = cursor + 1;
            quote = 0;
            escaped = 0;
            ++cursor;
            continue;
        }
        if (escaped) {
            escaped = 0;
        } else if (*cursor == '\\' && quote != '\'') {
            escaped = 1;
        } else if (*cursor == '\'' && quote != '"') {
            quote = quote == '\'' ? 0 : '\'';
        } else if (*cursor == '"' && quote != '\'') {
            quote = quote == '"' ? 0 : '"';
        }
        ++cursor;
    }
}

static int read_interactive_line(const struct cb_api_v1 *api, char *line,
                                 size_t size)
{
    size_t used = 0;
    shell_write(api, 1, "cannedBSD$ ");
    while (used + 1 < size) {
        char character;
        cb_ssize_t count = api->read(0, &character, 1);
        if (count < 0)
            return -1;
        if (count == 0) {
            if (used == 0)
                return 0;
            break;
        }
        if (character == '\n')
            break;
        line[used++] = character;
    }
    line[used] = '\0';
    return 1;
}

static int shell_main(const struct cb_api_v1 *api, int argc,
                      char *const argv[], char *const envp[])
{
    int status = 0;
    int should_exit = 0;
    (void)envp;
    if (argc >= 3 && strcmp(argv[1], "-c") == 0)
        return execute_line(api, argv[2], status, &should_exit);
    if (argc != 1) {
        shell_error(api, "usage: sh [-c command]");
        return 2;
    }
    while (!should_exit) {
        char line[SHELL_LINE_MAX];
        int result = read_interactive_line(api, line, sizeof(line));
        if (result <= 0)
            break;
        status = execute_line(api, line, status, &should_exit);
    }
    return status;
}

const struct cb_program_v1 cb_shell_program = {
    CB_ABI_VERSION_V1,
    sizeof(struct cb_program_v1),
    "sh",
    0,
    128 * 1024,
    shell_main
};
