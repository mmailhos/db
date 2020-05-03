#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>

#include "main.h"

#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
// same size as virtual memory page in our target computer architecture, no need to break them up
const uint32_t PAGE_SIZE = 4096;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;
const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;

// print_prompt is simply the db prompt
void print_prompt() {
    printf("db > ");
}

void read_input(InputBuffer *input_buffer) {
    // getine reads an entire line from stdin, stores buffer_length bytes in the address from input_buffer->buffer
    // getline will allocate buffer if NULL and n is set to 0 before the call.
    ssize_t bytes_read = getline(&(input_buffer->buffer), &input_buffer->buffer_length, stdin);
    if (bytes_read <= 0) {
        printf("Error reading input\n");
        exit(EXIT_FAILURE);
    }

    input_buffer->buffer_length = bytes_read - 1; // Ignore trailing newline
    input_buffer->buffer[bytes_read - 1] = 0;
}

void close_input_buffer(InputBuffer *input_buffer) {
    // free the buffer, free the struct
    free(input_buffer->buffer);
    free(input_buffer);
}

// new_input_buffer allocates and returns a new InputBuffer
InputBuffer *new_input_buffer() {
    InputBuffer *in = malloc(sizeof(InputBuffer));
    in->buffer = NULL;
    in->buffer_length = 0;
    in->input_length = 0;

    return in;
}

void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
    if (pager->pages[page_num] == NULL) {
        printf("Tried to flush null page\n");
        exit(EXIT_FAILURE);
    }

    off_t offset = lseek(pager->fd, page_num * PAGE_SIZE, SEEK_SET);

    if (offset == -1) {
        printf("Error seeking: %d\n", errno);
        exit(EXIT_FAILURE);
    }
    ssize_t bytes_written = write(pager->fd, pager->pages[page_num], size);

    if (bytes_written == -1) {
        printf("Error writing: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}

void db_close(Table* table) {
    Pager *pager = table->pager;
    uint32_t num_full_pages = table->num_rows / ROWS_PER_PAGE;

    for (uint32_t i = 0; i < num_full_pages; i++) {
        if (pager->pages[i] == NULL) {
            continue;
        }
        pager_flush(pager, i, PAGE_SIZE);
        free(pager->pages[i]);
        pager->pages[i] = NULL;
    }

    // There may be a partial page to write to the end of the file
    uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
    if (num_additional_rows > 0) {
        uint32_t page_num = num_full_pages;
        if (pager->pages[page_num] != NULL) {
            pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
            free(pager->pages[page_num]);
            pager->pages[page_num] = NULL;
        }
    }

    int result = close(pager->fd);
    if (result == -1) {
        printf("Error closing db file.\n");
        exit(EXIT_FAILURE);
    }
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        void* page = pager->pages[i];
        if (page) {
            free(page);
            pager->pages[i] = NULL;
        }
    }
    free(pager);
    free(table);
}


MetaCommandResult do_meta_command(InputBuffer *input_buffer, Table* table) {
    if (strncmp(input_buffer->buffer, ".exit", 5) == 0) {
        close_input_buffer(input_buffer);
        db_close(table);
        exit(EXIT_SUCCESS);
    }
    return META_COMMAND_UNRECOGNIZED;
}

PrepareResult prepare_insert(InputBuffer *input_buffer, Statement *statement) {
    statement->type = STATEMENT_INSERT;

    // calling strtok successively on the input buffer breaks it into substrings by inserting a null character
    // whenever it reaches a delimiter (space). strtok returns a pointer to the start of the substring.
    char *keyword = strtok(input_buffer->buffer, " ");
    char *id_string = strtok(NULL, " ");
    char *username = strtok(NULL, " ");
    char *email = strtok(NULL, " ");

    if (id_string == NULL || username == NULL || email == NULL) {
        return PREPARE_SYNTAX_ERROR;
    }

    int id = atoi(id_string);

    if (id < 0) {
        return PREPARE_NEGATIVE_ID;
    }

    if (strlen(username) > COLUMN_USERNAME_SIZE) {
        return PREPARE_STRING_TOO_LONG;
    }
    if (strlen(email) > COLUMN_EMAIL_SIZE) {
        return PREPARE_STRING_TOO_LONG;
    }

    statement->row_to_insert.id = id;
    strcpy(statement->row_to_insert.username, username);
    strcpy(statement->row_to_insert.email, email);

    return PREPARE_SUCCESS;
}

PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement) {
    if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
        return prepare_insert(input_buffer, statement);
    }

    if (strncmp(input_buffer->buffer, "select", 6) == 0) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED;
}

void serialize_row(Row *source, void *destination) {
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
    memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

void deserialize_row(void *source, Row *destination) {
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

void *get_page(Pager *pager, uint32_t page_num) {
    if (page_num > TABLE_MAX_PAGES) {
        printf("Tried to fetch page number ouf of bounds. %d > %d\n", page_num, TABLE_MAX_PAGES);
        exit(EXIT_FAILURE);
    }

    if (pager->pages[page_num] == NULL) {
        // cache miss. Allocate memory and load from file.
        void *page = malloc(PAGE_SIZE);
        uint32_t num_pages = pager->file_length / PAGE_SIZE;

        // we might save a partial page at the end of the file
        if (pager->file_length % PAGE_SIZE) {
            num_pages += 1;
        }

        if (page_num <= num_pages) {
            lseek(pager->fd, page_num * PAGE_SIZE, SEEK_SET);
            ssize_t bytes_read = read(pager->fd, page, PAGE_SIZE);
            if (bytes_read == -1) {
                printf("Error reading file: %d\n", errno);
                exit(EXIT_FAILURE);
            }
        }

        pager->pages[page_num] = page;
    }

    return pager->pages[page_num];
}


//cursor_slot returns a pointer to the row.
void *cursor_value(Cursor *cursor) {
    uint32_t page_num = cursor->row_num / ROWS_PER_PAGE;
    void *page = get_page(cursor->table->pager, page_num);
    uint32_t row_offset = cursor->row_num % ROWS_PER_PAGE;
    uint32_t byte_offset = row_offset * ROW_SIZE;

    return page + byte_offset;
}

void print_row(Row *row) {
    printf("(%d %s %s)\n", row->id, row->username, row->email);
}

Cursor* table_start(Table* table){
    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->row_num = 0;
    cursor->end_of_table = (table->num_rows == 0);

    return cursor;
}

Cursor* table_end(Table* table) {
    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->row_num = table->num_rows;
    cursor->end_of_table = true;

    return cursor;
}

ExecuteResult execute_insert(Statement *statement, Table *table) {
    if (table->num_rows >= TABLE_MAX_ROWS) {
        return EXECUTE_TABLE_FULL;
    }

    Row *row_toinsert = &(statement->row_to_insert);
    Cursor* cursor = table_end(table);

    serialize_row(row_toinsert, cursor_value(cursor));
    table->num_rows += 1;

    return EXECUTE_SUCCESS;
}

void cursor_advance(Cursor* cursor) {
    cursor->row_num += 1;
    if (cursor->row_num >= cursor->table->num_rows) {
        cursor->end_of_table = true;
    }
}

ExecuteResult execute_select(Statement *statement, Table *table) {
    Cursor* cursor = table_start(table);
    Row row;

    while (!(cursor->end_of_table)) {
        deserialize_row(cursor_value(cursor), &row);
        cursor_advance(cursor);
    }

    free(cursor);
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement *statement, Table *table) {
    switch (statement->type) {
        case (STATEMENT_INSERT):
            return execute_insert(statement, table);
        case (STATEMENT_SELECT):
            return execute_select(statement, table);
    }
}

Pager *pager_open(const char *filename) {
    //create if does not exists and read/write. Give user write and read permissions.
    int fd = open(filename, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);
    if (fd == -1) {
        printf("Unable to open file\n");
        exit(EXIT_FAILURE);
    }

    off_t file_length = lseek(fd, 0, SEEK_END);

    Pager *pager = malloc(sizeof(Pager));
    pager->fd = fd;
    pager->file_length = file_length;

    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = NULL;
    }

    return pager;
}

// open a database file. initialize a pager data structure. initialize a table data structure.
Table *db_open(const char *filename) {
    Pager *pager = pager_open(filename);
    uint32_t num_rows = pager->file_length / ROW_SIZE;

    Table *table = malloc(sizeof(Table));

    table->pager = pager;
    table->num_rows = num_rows;

    return table;
}
int main(int argc, char *argv[]) {
    InputBuffer *input_buffer = new_input_buffer();

    if (argc < 2) {
        printf("Must supply a database filename.\n");
        exit(EXIT_FAILURE);
    }

    printf("Helper: insert id username email");

    Table *table = db_open(argv[1]);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
    while (true) {
        print_prompt();
        read_input(input_buffer);

        // Meta commands (non-SQL) start with a dot.
        if (input_buffer->buffer[0] == '.') {
            switch (do_meta_command(input_buffer, table)) {
                case (META_COMMAND_SUCCESS):
                    continue;
                case (META_COMMAND_UNRECOGNIZED):
                    printf("Unrecognized meta command '%s'\n", input_buffer->buffer);
                    continue;
            }
        }

        Statement statement;
        // prepare_statement can be considered as the SQL command processor
        // see https://cstack.github.io/db_tutorial/assets/images/arch-part5.gif
        switch (prepare_statement(input_buffer, &statement)) {
            case (PREPARE_SUCCESS):
                break;
            case (PREPARE_STRING_TOO_LONG):
                printf("String is too long.\n");
                continue;
            case (PREPARE_NEGATIVE_ID):
                printf("ID must be positive.\n");
                continue;
            case (PREPARE_SYNTAX_ERROR):
                printf("Syntax error. Could not parse statement\n");
                continue;
            case (PREPARE_UNRECOGNIZED):
                printf("Unrecognized keyword at start of '%s'.\n", input_buffer->buffer);
                continue;
        }

        // execute_statement can be considered as the Virtual Machine. It executes the statement on the table.
        switch (execute_statement(&statement, table)) {
            case (EXECUTE_SUCCESS):
                printf("Executed\n");
                break;
            case (EXECUTE_TABLE_FULL):
                printf("Error: Table full.\n");
                break;
        }
    }
#pragma clang diagnostic pop
}

