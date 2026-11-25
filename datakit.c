#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **values;
    size_t count;
} Row;

typedef struct {
    char **headers;
    size_t columns;
    Row *rows;
    size_t row_count;
    size_t row_capacity;
} Table;

typedef struct {
    char *value;
    size_t count;
} Frequency;

static char *copy_string(const char *text, size_t length)
{
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static int append_char(char **buffer, size_t *length, size_t *capacity, int ch)
{
    if (*length + 1 >= *capacity) {
        size_t next_capacity = *capacity == 0 ? 128 : *capacity * 2;
        char *next = (char *)realloc(*buffer, next_capacity);
        if (next == NULL) {
            return 0;
        }
        *buffer = next;
        *capacity = next_capacity;
    }
    (*buffer)[(*length)++] = (char)ch;
    return 1;
}

/* Reads one logical CSV record, keeping newlines inside quoted fields. */
static int read_record(FILE *file, char **record, size_t *length)
{
    size_t capacity = 0;
    int quoted = 0;
    int ch;

    *record = NULL;
    *length = 0;
    while ((ch = fgetc(file)) != EOF) {
        if (!append_char(record, length, &capacity, ch)) {
            free(*record);
            *record = NULL;
            return -1;
        }

        if (ch == '"') {
            if (quoted) {
                int next = fgetc(file);
                if (next == '"') {
                    if (!append_char(record, length, &capacity, next)) {
                        free(*record);
                        *record = NULL;
                        return -1;
                    }
                } else {
                    quoted = 0;
                    if (next == EOF) {
                        break;
                    }
                    if (next == '\n') {
                        break;
                    }
                    if (!append_char(record, length, &capacity, next)) {
                        free(*record);
                        *record = NULL;
                        return -1;
                    }
                }
            } else {
                quoted = 1;
            }
        } else if (ch == '\n' && !quoted) {
            break;
        }
    }

    if (*length == 0 && ch == EOF) {
        free(*record);
        *record = NULL;
        return 0;
    }
    if (quoted) {
        free(*record);
        *record = NULL;
        return -2;
    }
    if (*length > 0 && (*record)[*length - 1] == '\n') {
        --(*length);
    }
    if (*length > 0 && (*record)[*length - 1] == '\r') {
        --(*length);
    }
    return 1;
}

static void free_values(char **values, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        free(values[i]);
    }
    free(values);
}

static int parse_record(const char *record, size_t length, char ***values_out, size_t *count_out)
{
    size_t pos = 0;
    size_t count = 0;
    size_t capacity = 0;
    char **values = NULL;

    while (pos <= length) {
        char *field = NULL;
        size_t field_length = 0;
        size_t field_capacity = 0;
        int quoted = 0;

        if (pos < length && record[pos] == '"') {
            quoted = 1;
            ++pos;
            while (pos < length) {
                if (record[pos] == '"') {
                    if (pos + 1 < length && record[pos + 1] == '"') {
                        if (!append_char(&field, &field_length, &field_capacity, '"')) {
                            free(field);
                            free_values(values, count);
                            return 0;
                        }
                        pos += 2;
                    } else {
                        ++pos;
                        break;
                    }
                } else {
                    if (!append_char(&field, &field_length, &field_capacity, record[pos++])) {
                        free(field);
                        free_values(values, count);
                        return 0;
                    }
                }
            }
            if (pos > length || (pos == length && record[length - 1] != '"')) {
                free(field);
                free_values(values, count);
                return 0;
            }
            if (pos < length && record[pos] != ',') {
                free(field);
                free_values(values, count);
                return 0;
            }
        } else {
            while (pos < length && record[pos] != ',') {
                if (record[pos] == '"' ||
                    !append_char(&field, &field_length, &field_capacity, record[pos++])) {
                    free(field);
                    free_values(values, count);
                    return 0;
                }
            }
        }

        if (field == NULL) {
            field = copy_string("", 0);
            if (field == NULL) {
                free_values(values, count);
                return 0;
            }
        } else {
            field[field_length] = '\0';
        }
        if (count == capacity) {
            size_t next_capacity = capacity == 0 ? 8 : capacity * 2;
            char **next = (char **)realloc(values, next_capacity * sizeof(*values));
            if (next == NULL) {
                free(field);
                free_values(values, count);
                return 0;
            }
            values = next;
            capacity = next_capacity;
        }
        values[count++] = field;

        if (pos == length) {
            break;
        }
        if (record[pos] != ',') {
            free_values(values, count);
            return 0;
        }
        ++pos;
        if (pos == length) {
            char *empty = copy_string("", 0);
            char **next;
            if (empty == NULL) {
                free_values(values, count);
                return 0;
            }
            if (count == capacity) {
                size_t next_capacity = capacity == 0 ? 8 : capacity * 2;
                next = (char **)realloc(values, next_capacity * sizeof(*values));
                if (next == NULL) {
                    free(empty);
                    free_values(values, count);
                    return 0;
                }
                values = next;
                capacity = next_capacity;
            }
            values[count++] = empty;
            break;
        }
        (void)quoted;
    }

    *values_out = values;
    *count_out = count;
    return 1;
}

static void free_table(Table *table)
{
    size_t i;
    free_values(table->headers, table->columns);
    for (i = 0; i < table->row_count; ++i) {
        free_values(table->rows[i].values, table->rows[i].count);
    }
    free(table->rows);
    memset(table, 0, sizeof(*table));
}

static int append_row(Table *table, char **values, size_t count)
{
    if (table->row_count == table->row_capacity) {
        size_t next_capacity = table->row_capacity == 0 ? 32 : table->row_capacity * 2;
        Row *next = (Row *)realloc(table->rows, next_capacity * sizeof(*table->rows));
        if (next == NULL) {
            return 0;
        }
        table->rows = next;
        table->row_capacity = next_capacity;
    }
    table->rows[table->row_count].values = values;
    table->rows[table->row_count].count = count;
    ++table->row_count;
    return 1;
}

static int load_csv(const char *path, Table *table)
{
    FILE *file = fopen(path, "rb");
    char *record = NULL;
    size_t length = 0;
    size_t record_number = 0;
    int status;

    memset(table, 0, sizeof(*table));
    if (file == NULL) {
        fprintf(stderr, "Erro: nao foi possivel abrir '%s'.\n", path);
        return 0;
    }

    while ((status = read_record(file, &record, &length)) > 0) {
        char **values = NULL;
        size_t count = 0;
        ++record_number;
        if (record_number == 1 && length >= 3 &&
            (unsigned char)record[0] == 0xEF &&
            (unsigned char)record[1] == 0xBB &&
            (unsigned char)record[2] == 0xBF) {
            memmove(record, record + 3, length - 3);
            length -= 3;
        }
        if (!parse_record(record, length, &values, &count)) {
            fprintf(stderr, "Erro: CSV malformado na linha %zu.\n", record_number);
            free(record);
            fclose(file);
            free_table(table);
            return 0;
        }
        free(record);
        record = NULL;

        if (record_number == 1) {
            if (count == 0) {
                fprintf(stderr, "Erro: o CSV nao possui cabecalho.\n");
                free_values(values, count);
                fclose(file);
                return 0;
            }
            table->headers = values;
            table->columns = count;
        } else {
            if (count != table->columns) {
                fprintf(stderr, "Erro: linha %zu tem %zu colunas; esperado: %zu.\n",
                        record_number, count, table->columns);
                free_values(values, count);
                fclose(file);
                free_table(table);
                return 0;
            }
            if (!append_row(table, values, count)) {
                fprintf(stderr, "Erro: memoria insuficiente ao carregar os dados.\n");
                free_values(values, count);
                fclose(file);
                free_table(table);
                return 0;
            }
        }
    }
    free(record);
    if (ferror(file)) {
        fprintf(stderr, "Erro: falha ao ler '%s'.\n", path);
        fclose(file);
        free_table(table);
        return 0;
    }
    fclose(file);

    if (status == -1) {
        fprintf(stderr, "Erro: memoria insuficiente ao ler o arquivo.\n");
        free_table(table);
        return 0;
    }
    if (status == -2) {
        fprintf(stderr, "Erro: campo entre aspas nao foi fechado.\n");
        free_table(table);
        return 0;
    }
    if (record_number == 0) {
        fprintf(stderr, "Erro: o arquivo esta vazio.\n");
        return 0;
    }
    return 1;
}

static int find_column(const Table *table, const char *name)
{
    size_t i;
    for (i = 0; i < table->columns; ++i) {
        if (strcmp(table->headers[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int parse_number(const char *text, double *value)
{
    char *end;
    double parsed;
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text == '\0') {
        return 0;
    }
    errno = 0;
    parsed = strtod(text, &end);
    if (text == end || errno == ERANGE || !isfinite(parsed)) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        ++end;
    }
    if (*end != '\0') {
        return 0;
    }
    *value = parsed;
    return 1;
}

static int is_missing(const char *value)
{
    return value[0] == '\0';
}

static void command_summary(const Table *table)
{
    size_t column;
    printf("Arquivo carregado com sucesso\n");
    printf("Registros: %zu\nColunas: %zu\n\n", table->row_count, table->columns);
    printf("%-24s %-12s %10s %10s\n", "COLUNA", "TIPO", "PREENCHIDOS", "VAZIOS");
    printf("---------------------------------------------------------------------\n");

    for (column = 0; column < table->columns; ++column) {
        size_t row;
        size_t filled = 0;
        size_t numeric = 0;
        size_t nonnumeric = 0;
        int all_integer = 1;
        for (row = 0; row < table->row_count; ++row) {
            const char *value = table->rows[row].values[column];
            double number;
            if (is_missing(value)) {
                continue;
            }
            ++filled;
            if (parse_number(value, &number)) {
                char *end;
                (void)strtoll(value, &end, 10);
                while (isspace((unsigned char)*end)) {
                    ++end;
                }
                if (*end != '\0') {
                    all_integer = 0;
                }
                ++numeric;
            } else {
                ++nonnumeric;
            }
        }

        {
            const char *type = "texto";
            if (numeric > 0 && nonnumeric == 0) {
                type = all_integer ? "inteiro" : "decimal";
            } else if (numeric > 0 && nonnumeric > 0) {
                type = "misto";
            }
            printf("%-24.24s %-12s %10zu %10zu\n", table->headers[column], type,
                   filled, table->row_count - filled);
        }
    }
}

static int compare_doubles(const void *left, const void *right)
{
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

static int command_statistics(const Table *table, int column)
{
    size_t row;
    size_t count = 0;
    size_t missing = 0;
    double *values = (double *)malloc(table->row_count * sizeof(*values));
    double mean = 0.0;
    double sum_squares = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;

    if (values == NULL && table->row_count > 0) {
        fprintf(stderr, "Erro: memoria insuficiente para calcular estatisticas.\n");
        return 0;
    }
    for (row = 0; row < table->row_count; ++row) {
        const char *text = table->rows[row].values[column];
        double value;
        if (is_missing(text)) {
            ++missing;
            continue;
        }
        if (!parse_number(text, &value)) {
            fprintf(stderr, "Erro: a coluna '%s' contem valores nao numericos.\n",
                    table->headers[column]);
            free(values);
            return 0;
        }
        values[count] = value;
        if (count == 0) {
            minimum = maximum = value;
        } else {
            if (value < minimum) minimum = value;
            if (value > maximum) maximum = value;
        }
        ++count;
        {
            double delta = value - mean;
            mean += delta / (double)count;
            sum_squares += delta * (value - mean);
        }
    }
    if (count == 0) {
        fprintf(stderr, "Erro: a coluna '%s' nao possui valores numericos.\n",
                table->headers[column]);
        free(values);
        return 0;
    }

    qsort(values, count, sizeof(*values), compare_doubles);
    printf("Estatisticas: %s\n", table->headers[column]);
    printf("Valores validos: %zu\nValores vazios: %zu\n", count, missing);
    printf("Minimo: %.6g\nMaximo: %.6g\nMedia: %.6g\n", minimum, maximum, mean);
    if (count % 2 == 0) {
        printf("Mediana: %.6g\n", (values[count / 2 - 1] + values[count / 2]) / 2.0);
    } else {
        printf("Mediana: %.6g\n", values[count / 2]);
    }
    printf("Desvio padrao populacional: %.6g\n", sqrt(sum_squares / (double)count));
    free(values);
    return 1;
}

static int compare_frequencies(const void *left, const void *right)
{
    const Frequency *a = (const Frequency *)left;
    const Frequency *b = (const Frequency *)right;
    if (a->count != b->count) {
        return a->count < b->count ? 1 : -1;
    }
    return strcmp(a->value, b->value);
}

static int command_frequency(const Table *table, int column, size_t limit)
{
    Frequency *frequencies = NULL;
    size_t count = 0;
    size_t capacity = 0;
    size_t row;
    int success = 1;

    for (row = 0; row < table->row_count; ++row) {
        const char *text = table->rows[row].values[column];
        const char *key = is_missing(text) ? "(vazio)" : text;
        size_t i;
        for (i = 0; i < count; ++i) {
            if (strcmp(frequencies[i].value, key) == 0) {
                ++frequencies[i].count;
                break;
            }
        }
        if (i == count) {
            char *value = copy_string(key, strlen(key));
            if (value == NULL) {
                fprintf(stderr, "Erro: memoria insuficiente ao contar frequencias.\n");
                success = 0;
                goto cleanup;
            }
            if (count == capacity) {
                size_t next_capacity = capacity == 0 ? 16 : capacity * 2;
                Frequency *next =
                    (Frequency *)realloc(frequencies, next_capacity * sizeof(*frequencies));
                if (next == NULL) {
                    free(value);
                    fprintf(stderr, "Erro: memoria insuficiente ao contar frequencias.\n");
                    success = 0;
                    goto cleanup;
                }
                frequencies = next;
                capacity = next_capacity;
            }
            frequencies[count].value = value;
            frequencies[count].count = 1;
            ++count;
        }
    }

    if (count > 1) {
        qsort(frequencies, count, sizeof(*frequencies), compare_frequencies);
    }
    if (limit > count) {
        limit = count;
    }
    printf("Frequencia: %s (mostrando %zu de %zu categorias)\n",
           table->headers[column], limit, count);
    printf("%-40s %10s\n", "VALOR", "CONTAGEM");
    printf("----------------------------------------------------\n");
    for (row = 0; row < limit; ++row) {
        printf("%-40.40s %10zu\n", frequencies[row].value, frequencies[row].count);
    }

cleanup:
    for (row = 0; row < count; ++row) {
        free(frequencies[row].value);
    }
    free(frequencies);
    return success;
}

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Uso:\n"
            "  %s resumo <arquivo.csv>\n"
            "  %s estatisticas <arquivo.csv> <coluna>\n"
            "  %s frequencia <arquivo.csv> <coluna> [limite]\n",
            program, program, program);
}

int main(int argc, char **argv)
{
    Table table;
    int column;
    int exit_code = EXIT_SUCCESS;

    if (argc < 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "resumo") != 0 &&
        strcmp(argv[1], "estatisticas") != 0 &&
        strcmp(argv[1], "frequencia") != 0) {
        fprintf(stderr, "Erro: comando desconhecido '%s'.\n", argv[1]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if ((strcmp(argv[1], "estatisticas") == 0 && argc != 4) ||
        (strcmp(argv[1], "frequencia") == 0 && (argc < 4 || argc > 5)) ||
        (strcmp(argv[1], "resumo") == 0 && argc != 3)) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (!load_csv(argv[2], &table)) {
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "resumo") == 0) {
        command_summary(&table);
    } else {
        column = find_column(&table, argv[3]);
        if (column < 0) {
            fprintf(stderr, "Erro: coluna '%s' nao encontrada.\n", argv[3]);
            exit_code = EXIT_FAILURE;
        } else if (strcmp(argv[1], "estatisticas") == 0) {
            if (!command_statistics(&table, column)) {
                exit_code = EXIT_FAILURE;
            }
        } else {
            size_t limit = 10;
            if (argc == 5) {
                char *end;
                unsigned long parsed;
                errno = 0;
                parsed = strtoul(argv[4], &end, 10);
                if (errno != 0 || *argv[4] == '\0' || *end != '\0' || parsed == 0 ||
                    parsed > (unsigned long)((size_t)-1)) {
                    fprintf(stderr, "Erro: limite deve ser um inteiro positivo.\n");
                    exit_code = EXIT_FAILURE;
                } else {
                    limit = (size_t)parsed;
                }
            }
            if (exit_code == EXIT_SUCCESS) {
                if (!command_frequency(&table, column, limit)) {
                    exit_code = EXIT_FAILURE;
                }
            }
        }
    }
    free_table(&table);
    return exit_code;
}
