#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <termios.h>
#include <unistd.h>

#define MAX_VARS 100
#define MAX_NAME 32
#define MAX_LIST_SIZE 10
#define MAX_FILE_SIZE 4096 
#define MAX_HISTORY 100    
#define MAX_FUNCTIONS 20   
#define MAX_FUNC_LINES 50  

#ifdef _WIN32
    #define CLEAR_COMMAND "cls"
#else
    #define CLEAR_COMMAND "clear"
#endif

typedef enum { TYPE_NUMBER, TYPE_STRING, TYPE_LIST, TYPE_NONE } VarType;

typedef struct {
    char name[MAX_NAME];
    VarType type;
    double num_val;
    char str_val[MAX_FILE_SIZE]; 
    double list_val[MAX_LIST_SIZE];
    int list_size;
} Variable;

typedef struct {
    char name[MAX_NAME];
    char body[MAX_FUNC_LINES][256];
    int line_count;
} Function;

// Глобальное состояние интерпретатора
Variable vars[MAX_VARS];
int var_count = 0;

Function functions[MAX_FUNCTIONS];
int func_count = 0;

char history[MAX_HISTORY][4096];
int history_count = 0;

// Состояния для обработки блоков (if/elif/else/def)
int block_skip = 0;       
int if_cond_met = 0;      
int recording_def = 0;    
int active_def_idx = -1;  

Variable* find_var(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) return &vars[i];
    }
    return NULL;
}

void set_var_number(const char *name, double val) {
    Variable *v = find_var(name);
    if (!v && var_count < MAX_VARS) v = &vars[var_count++];
    if (v) {
        strncpy(v->name, name, MAX_NAME);
        v->type = TYPE_NUMBER;
        v->num_val = val;
    }
}

void set_var_string(const char *name, const char *val) {
    Variable *v = find_var(name);
    if (!v && var_count < MAX_VARS) v = &vars[var_count++];
    if (v) {
        strncpy(v->name, name, MAX_NAME);
        v->type = TYPE_STRING;
        strncpy(v->str_val, val, sizeof(v->str_val) - 1);
        v->str_val[sizeof(v->str_val) - 1] = '\0';
    }
}

char* trim(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return str;
}

int read_file_content(const char *filename, char *output, int max_size) {
    FILE *f = fopen(filename, "r");
    if (!f) return 0;
    int len = fread(output, 1, max_size - 1, f);
    output[len] = '\0';
    fclose(f);
    return 1;
}

char* extract_quoted_arg(char *str) {
    str = trim(str);
    if ((str[0] == '"' && str[strlen(str)-1] == '"') || (str[0] == '\'' && str[strlen(str)-1] == '\'')) {
        str[strlen(str)-1] = '\0';
        return str + 1;
    }
    return NULL;
}

void builtin_clear() {
    system(CLEAR_COMMAND);
}

void builtin_help() {
    printf("Welcome to Mini-Python help utility!\n\n");
    printf("Доступные команды и функции:\n");
    printf("  help()         - Показать это окно справки.\n");
    printf("  vars()         - Показать все созданные переменные.\n");
    printf("  clear()        - Очистить экран консоли.\n");
    printf("  title(\"text\")  - Изменить заголовок окна консоли.\n");
    printf("  system(\"cmd\")  - Выполнить системную команду.\n");
    printf("  version()      - Показать версию интерпретатора.\n");
    printf("  credits()      - Показать информацию об авторах.\n");
    printf("  type(var)      - Показать тип переменной.\n");
    printf("  len(var)       - Получить длину строки или списка.\n");
    printf("  abs(x)         - Возвращает абсолютное значение числа.\n");
    printf("  open(\"file\")   - Прочитать текстовый файл.\n");
    printf("  import name    - Импортировать и выполнить скрипт name.py\n");
    printf("  exit()         - Выход из интерпретатора.\n\n");
    printf("Поддержка f-строк:\n");
    printf("  print(f\"Результат: {my_var}\")\n\n");
}

void builtin_vars() {
    if (var_count == 0) {
        printf("{}\n");
        return;
    }
    printf("{\n");
    for (int i = 0; i < var_count; i++) {
        printf("  '%s': ", vars[i].name);
        if (vars[i].type == TYPE_NUMBER) printf("%g", vars[i].num_val);
        else if (vars[i].type == TYPE_STRING) printf("'%s'", vars[i].str_val);
        else if (vars[i].type == TYPE_LIST) {
            printf("[");
            for(int j=0; j<vars[i].list_size; j++) {
                printf("%g%s", vars[i].list_val[j], (j == vars[i].list_size - 1) ? "" : ", ");
            }
            printf("]");
        }
        printf("\n");
    }
    printf("}\n");
}

double evaluate_math(char *expr) {
    expr = trim(expr);
    char buf[1024];
    strncpy(buf, expr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *token = strtok(buf, " \t");
    if (!token) return 0.0;

    double result = 0.0;
    Variable *v = find_var(token);
    if (v && v->type == TYPE_NUMBER) result = v->num_val;
    else result = atof(token);

    while ((token = strtok(NULL, " \t")) != NULL) {
        char op = *token;
        token = strtok(NULL, " \t");
        if (!token) break;

        double val = 0;
        Variable *v2 = find_var(token);
        if (v2 && v2->type == TYPE_NUMBER) val = v2->num_val;
        else val = atof(token);

        if (op == '+') result += val;
        else if (op == '-') result -= val;
        else if (op == '*') result *= val;
        else if (op == '/') result = (val != 0) ? result / val : 0;
    }
    return result;
}

int evaluate_condition(char *cond) {
    cond = trim(cond);
    int len = strlen(cond);
    if (len > 0 && cond[len - 1] == ':') cond[len - 1] = '\0';
    cond = trim(cond);

    char left_str[256] = "", right_str[256] = "";
    char op[3] = "";
    char *p = NULL;

    if ((p = strstr(cond, "=="))) { strcpy(op, "=="); *p = '\0'; strcpy(left_str, cond); strcpy(right_str, p + 2); }
    else if ((p = strstr(cond, "!="))) { strcpy(op, "!="); *p = '\0'; strcpy(left_str, cond); strcpy(right_str, p + 2); }
    else if ((p = strstr(cond, ">="))) { strcpy(op, ">="); *p = '\0'; strcpy(left_str, cond); strcpy(right_str, p + 2); }
    else if ((p = strstr(cond, "<="))) { strcpy(op, "<="); *p = '\0'; strcpy(left_str, cond); strcpy(right_str, p + 2); }
    else if ((p = strstr(cond, ">")))  { strcpy(op, ">");  *p = '\0'; strcpy(left_str, cond); strcpy(right_str, p + 1); }
    else if ((p = strstr(cond, "<")))  { strcpy(op, "<");  *p = '\0'; strcpy(left_str, cond); strcpy(right_str, p + 1); }
    else {
        return evaluate_math(cond) != 0;
    }

    double left_val = evaluate_math(left_str);
    double right_val = evaluate_math(right_str);

    if (strcmp(op, "==") == 0) return left_val == right_val;
    if (strcmp(op, "!=") == 0) return left_val != right_val;
    if (strcmp(op, ">=") == 0) return left_val >= right_val;
    if (strcmp(op, "<=") == 0) return left_val <= right_val;
    if (strcmp(op, ">") == 0) return left_val > right_val;
    if (strcmp(op, "<") == 0) return left_val < right_val;
    return 0;
}

int handle_open_call(char *expr, char *file_buffer) {
    expr = trim(expr);
    if (strncmp(expr, "open(", 5) == 0 && expr[strlen(expr)-1] == ')') {
        expr[strlen(expr)-1] = '\0';
        char *filename = extract_quoted_arg(expr + 5);
        if (!filename) {
            printf("TypeError: open() argument must be a string enclosed in quotes\n");
            return -1;
        }
        if (!read_file_content(filename, file_buffer, MAX_FILE_SIZE)) {
            printf("FileNotFoundError: No such file or directory: '%s'\n", filename);
            return -1;
        }
        return 1;
    }
    return 0;
}

// Функция для обработки f-строк (подстановка переменных из {})
int process_f_string(const char *expr, char *output, int max_size) {
    char str[4096];
    strncpy(str, expr, sizeof(str) - 1);
    str[sizeof(str) - 1] = '\0';
    char *p = trim(str);
    
    int len = strlen(p);
    if (len < 3) return 0;
    
    char quote = '\0';
    if ((p[0] == 'f' || p[0] == 'F') && (p[1] == '"' || p[1] == '\'')) {
        quote = p[1];
        if (p[len - 1] != quote) return 0;
        p[len - 1] = '\0'; 
        p = p + 2; 
    } else {
        return 0; // Это не f-строка
    }
    
    int out_idx = 0;
    int i = 0;
    int src_len = strlen(p);
    
    while (i < src_len && out_idx < max_size - 1) {
        if (p[i] == '{') {
            i++;
            char var_name[256];
            int v_idx = 0;
            while (i < src_len && p[i] != '}' && v_idx < 255) {
                var_name[v_idx++] = p[i++];
            }
            var_name[v_idx] = '\0';
            if (p[i] == '}') i++; 
            
            char *trimmed_var = trim(var_name);
            Variable *v = find_var(trimmed_var);
            char temp[4096] = "";
            
            if (v) {
                if (v->type == TYPE_NUMBER) {
                    snprintf(temp, sizeof(temp), "%g", v->num_val);
                } else if (v->type == TYPE_STRING) {
                    snprintf(temp, sizeof(temp), "%s", v->str_val);
                } else if (v->type == TYPE_LIST) {
                    strcpy(temp, "[");
                    for (int j = 0; j < v->list_size; j++) {
                        char num[32];
                        snprintf(num, sizeof(num), "%g%s", v->list_val[j], (j == v->list_size - 1) ? "" : ", ");
                        strcat(temp, num);
                    }
                    strcat(temp, "]");
                }
            } else {
                // Если переменная не найдена, пробуем вычислить как математику (например {2 + 2} или {a + 5})
                double val = evaluate_math(trimmed_var);
                snprintf(temp, sizeof(temp), "%g", val);
            }
            
            int t_len = strlen(temp);
            if (out_idx + t_len < max_size - 1) {
                strcpy(&output[out_idx], temp);
                out_idx += t_len;
            }
        } else {
            output[out_idx++] = p[i++];
        }
    }
    output[out_idx] = '\0';
    return 1;
}

void execute_line(char *raw_line);

void builtin_import(const char *module_name) {
    char filename[64];
    snprintf(filename, sizeof(filename), "%s.py", module_name);
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("ModuleNotFoundError: No module named '%s'\n", module_name);
        return;
    }
    char file_line[1024];
    while (fgets(file_line, sizeof(file_line), f)) {
        int l = strlen(file_line);
        while (l > 0 && (file_line[l - 1] == '\n' || file_line[l - 1] == '\r')) {
            file_line[l - 1] = '\0';
            l--;
        }
        execute_line(file_line);
    }
    fclose(f);
}

void execute_line(char *raw_line) {
    int is_indented = (raw_line[0] == ' ' || raw_line[0] == '\t');
    char *line = trim(raw_line);

    if (strlen(line) == 0 || line[0] == '#') return;

    if (recording_def) {
        if (is_indented) {
            if (functions[active_def_idx].line_count < MAX_FUNC_LINES) {
                strcpy(functions[active_def_idx].body[functions[active_def_idx].line_count++], raw_line);
            }
            return;
        } else {
            recording_def = 0;
            active_def_idx = -1;
        }
    }

    if (is_indented) {
        if (block_skip) return; 
    } else {
        if (strncmp(line, "elif ", 5) != 0 && strcmp(line, "else:") != 0) {
            block_skip = 0;
            if_cond_met = 0;
        }
    }

    if (strncmp(line, "if ", 3) == 0) {
        if (evaluate_condition(line + 3)) { if_cond_met = 1; block_skip = 0; } 
        else { if_cond_met = 0; block_skip = 1; }
        return;
    }

    if (strncmp(line, "elif ", 5) == 0) {
        if (if_cond_met) { block_skip = 1; } 
        else {
            if (evaluate_condition(line + 5)) { if_cond_met = 1; block_skip = 0; } 
            else { block_skip = 1; }
        }
        return;
    }

    if (strcmp(line, "else:") == 0) {
        if (if_cond_met) { block_skip = 1; } 
        else { block_skip = 0; if_cond_met = 1; }
        return;
    }

    if (strncmp(line, "def ", 4) == 0) {
        char *open_p = strchr(line, '(');
        if (open_p) {
            *open_p = '\0';
            char *func_name = trim(line + 4);
            if (func_count < MAX_FUNCTIONS) {
                strncpy(functions[func_count].name, func_name, MAX_NAME);
                functions[func_count].line_count = 0;
                recording_def = 1;
                active_def_idx = func_count;
                func_count++;
            }
        }
        return;
    }

    if (strncmp(line, "import ", 7) == 0) {
        char *mod = trim(line + 7);
        builtin_import(mod);
        return;
    }

    if (strcmp(line, "clear()") == 0 || strcmp(line, "cls()") == 0) { builtin_clear(); return; }
    if (strcmp(line, "help()") == 0) { builtin_help(); return; }
    if (strcmp(line, "vars()") == 0) { builtin_vars(); return; }
    if (strcmp(line, "version()") == 0) { printf("Mini-Python 1.7.0 (C-Engine)\n"); return; }
    if (strcmp(line, "credits()") == 0) { printf("Advanced lightweight C-based Python clone.\n"); return; }
    if (strcmp(line, "exit()") == 0) { exit(0); return; }

    if (strncmp(line, "title(", 6) == 0) {
        char *end = strrchr(line, ')');
        if (end) {
            *end = '\0';
            char *title_arg = extract_quoted_arg(line + 6);
            if (title_arg) { printf("\033]0;%s\007", title_arg); fflush(stdout); } 
            else { printf("TypeError: title() argument must be a string enclosed in quotes\n"); }
        }
        return;
    }

    if (strncmp(line, "system(", 7) == 0) {
        char *end = strrchr(line, ')');
        if (end) {
            *end = '\0';
            char *cmd_arg = extract_quoted_arg(line + 7);
            if (cmd_arg) system(cmd_arg);
            else printf("TypeError: system() argument must be a string enclosed in quotes\n");
        }
        return;
    }

    if (strncmp(line, "print(", 6) == 0) {
        char *end = strrchr(line, ')');
        if (end) {
            *end = '\0';
            char *arg = trim(line + 6);
            
            // Проверка 1: Обработка f-строки
            char f_buf[MAX_FILE_SIZE];
            if (process_f_string(arg, f_buf, sizeof(f_buf))) {
                printf("%s\n", f_buf);
                return;
            }

            char file_buf[MAX_FILE_SIZE];
            int open_res = handle_open_call(arg, file_buf);
            if (open_res == 1) { printf("%s\n", file_buf); return; }
            else if (open_res == -1) return;

            char *quoted = extract_quoted_arg(arg);
            if (quoted) { printf("%s\n", quoted); } 
            else {
                Variable *v = find_var(arg);
                if (v) {
                    if (v->type == TYPE_NUMBER) printf("%g\n", v->num_val);
                    else if (v->type == TYPE_STRING) printf("%s\n", v->str_val);
                    else if (v->type == TYPE_LIST) {
                        printf("[");
                        for(int j=0; j<v->list_size; j++) printf("%g%s", v->list_val[j], (j==v->list_size-1)?"":", ");
                        printf("]\n");
                    }
                } else { printf("%g\n", evaluate_math(arg)); }
            }
        } else { printf("SyntaxError: Missing parentheses in call to 'print'\n"); }
        return;
    }

    if (strncmp(line, "type(", 5) == 0) {
        char *end = strrchr(line, ')');
        if (end) {
            *end = '\0';
            Variable *v = find_var(trim(line + 5));
            if (v) {
                if (v->type == TYPE_NUMBER) printf("<class 'int_float'>\n");
                else if (v->type == TYPE_STRING) printf("<class 'str'>\n");
                else if (v->type == TYPE_LIST) printf("<class 'list'>\n");
            } else { printf("<class 'NoneType'>\n"); }
        }
        return;
    }

    if (strncmp(line, "len(", 4) == 0) {
        char *end = strrchr(line, ')');
        if (end) {
            *end = '\0';
            Variable *v = find_var(trim(line + 4));
            if (v) {
                if (v->type == TYPE_STRING) printf("%d\n", (int)strlen(v->str_val));
                else if (v->type == TYPE_LIST) printf("%d\n", v->list_size);
                else printf("TypeError: object of type 'number' has no len()\n");
            } else { printf("NameError: name is not defined\n"); }
        }
        return;
    }

    if (strncmp(line, "abs(", 4) == 0) {
        char *end = strrchr(line, ')');
        if (end) {
            *end = '\0';
            double val = evaluate_math(line + 4);
            printf("%g\n", fabs(val));
        }
        return;
    }

    int len = strlen(line);
    if (len > 2 && line[len - 2] == '(' && line[len - 1] == ')') {
        char f_name[MAX_NAME];
        strncpy(f_name, line, len - 2);
        f_name[len - 2] = '\0';
        char *trimmed_f = trim(f_name);

        for (int i = 0; i < func_count; i++) {
            if (strcmp(functions[i].name, trimmed_f) == 0) {
                for (int j = 0; j < functions[i].line_count; j++) {
                    char temp_line[256];
                    strcpy(temp_line, functions[i].body[j]);
                    execute_line(temp_line);
                }
                return;
            }
        }
    }

    char *eq = strchr(line, '=');
    if (eq) {
        *eq = '\0';
        char *var_name = trim(line);
        char *val_expr = trim(eq + 1);

        // Проверка 2: Присваивание переменной результата f-строки
        char f_buf[MAX_FILE_SIZE];
        if (process_f_string(val_expr, f_buf, sizeof(f_buf))) {
            set_var_string(var_name, f_buf);
            return;
        }

        char file_buf[MAX_FILE_SIZE];
        int open_res = handle_open_call(val_expr, file_buf);
        if (open_res == 1) { set_var_string(var_name, file_buf); return; }
        else if (open_res == -1) return;

        char *quoted = extract_quoted_arg(val_expr);
        if (quoted) { set_var_string(var_name, quoted); return; }

        if (val_expr[0] == '[' && val_expr[strlen(val_expr)-1] == ']') {
            val_expr[strlen(val_expr)-1] = '\0';
            char *list_content = val_expr + 1;
            Variable *v = find_var(var_name);
            if (!v && var_count < MAX_VARS) v = &vars[var_count++];
            if (v) {
                strncpy(v->name, var_name, MAX_NAME);
                v->type = TYPE_LIST; v->list_size = 0;
                char *token = strtok(list_content, ", ");
                while (token && v->list_size < MAX_LIST_SIZE) {
                    v->list_val[v->list_size++] = atof(token);
                    token = strtok(NULL, ", ");
                }
            }
            return;
        }

        double val = evaluate_math(val_expr);
        set_var_number(var_name, val);
        return;
    }

    printf("Unknown command or SyntaxError. Type 'help()' for info.\n");
}

void get_input(char *buf) {
    struct termios t, raw;
    tcgetattr(0, &t); raw = t;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(0, TCSAFLUSH, &raw);

    int pos = 0; 
    int len = 0; 
    int h_idx = history_count; 
    char current_backup[4096] = ""; 
    
    buf[0] = '\0';
    printf(">>> "); fflush(stdout);

    int c;
    while ((c = getchar()) != '\n') {
        if (c == 27) { 
            if (getchar() == '[') {
                c = getchar();
                if (c == 'A') { 
                    if (history_count > 0 && h_idx > 0) {
                        if (h_idx == history_count) strcpy(current_backup, buf);
                        h_idx--; strcpy(buf, history[h_idx]);
                        len = strlen(buf); pos = len;
                    }
                } else if (c == 'B') { 
                    if (h_idx < history_count) {
                        h_idx++;
                        if (h_idx == history_count) strcpy(buf, current_backup);
                        else strcpy(buf, history[h_idx]);
                        len = strlen(buf); pos = len;
                    }
                } else if (c == 'C') { 
                    if (pos < len) {
                        pos++;
                        while (pos < len && ((unsigned char)buf[pos] & 0xC0) == 0x80) pos++;
                    }
                } else if (c == 'D') { 
                    if (pos > 0) {
                        pos--;
                        while (pos > 0 && ((unsigned char)buf[pos] & 0xC0) == 0x80) pos--;
                    }
                }
            }
        } else if (c == 127 || c == 8) { 
            if (pos > 0) {
                int bytes_to_delete = 1;
                while (pos - bytes_to_delete > 0 && ((unsigned char)buf[pos - bytes_to_delete] & 0xC0) == 0x80) {
                    bytes_to_delete++;
                }
                int start_del = pos - bytes_to_delete;
                for (int i = start_del; i < len - bytes_to_delete; i++) {
                    buf[i] = buf[i + bytes_to_delete];
                }
                pos -= bytes_to_delete;
                len -= bytes_to_delete;
                buf[len] = '\0';
            }
        } else if (c >= 32 && len < 4095) { 
            for (int i = len; i >= pos; i--) buf[i + 1] = buf[i];
            buf[pos] = c;
            pos++; len++;
        }

        printf("\r\033[K>>> %s", buf);

        int move_back_cols = 0;
        for (int i = pos; i < len; i++) {
            if (((unsigned char)buf[i] & 0xC0) != 0x80) {
                move_back_cols++;
            }
        }
        if (move_back_cols > 0) {
            printf("\033[%dD", move_back_cols);
        }
        fflush(stdout);
    }
    printf("\n");
    tcsetattr(0, TCSAFLUSH, &t); 

    if (len > 0) {
        if (history_count == 0 || strcmp(history[history_count - 1], buf) != 0) {
            if (history_count < MAX_HISTORY) {
                strcpy(history[history_count++], buf);
            } else {
                for (int i = 1; i < MAX_HISTORY; i++) strcpy(history[i - 1], history[i]);
                strcpy(history[MAX_HISTORY - 1], buf);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        FILE *f = fopen(argv[1], "r");
        if (!f) {
            printf("Error: Cannot open file '%s'\n", argv[1]);
            return 1;
        }
        char file_line[1024];
        while (fgets(file_line, sizeof(file_line), f)) {
            int l = strlen(file_line);
            while (l > 0 && (file_line[l - 1] == '\n' || file_line[l - 1] == '\r')) {
                file_line[l - 1] = '\0';
                l--;
            }
            execute_line(file_line);
        }
        fclose(f);
        return 0; 
    }

    static char line[4096];
    printf("Mini-Python 1.7.1 (C-Engine). Type 'exit()' to quit.\n");
    while (1) {
        get_input(line);
        if (strcmp(line, "exit()") == 0) break;
        execute_line(line);
    }
    return 0;
}