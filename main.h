#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
#define TABLE_MAX_PAGES 100

typedef struct {
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

typedef enum {
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED
} MetaCommandResult;

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_UNRECOGNIZED,
    PREPARE_SYNTAX_ERROR,
    PREPARE_NEGATIVE_ID,
    PREPARE_STRING_TOO_LONG
} PrepareResult;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

typedef struct {
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE + 1]; // add null-delimiter
    char email[COLUMN_EMAIL_SIZE + 1]; //add null-delimiter
} Row;

typedef struct {
    StatementType type;
    Row row_to_insert; //only used by insert statement
} Statement;

// the pager accesses the pagge cache adn the file.
typedef struct {
    int fd;
    uint32_t file_length;
    void* pages[TABLE_MAX_PAGES];
} Pager;

// the table makes request for pages through the pager
typedef struct {
    uint32_t num_rows;
    Pager* pager;
} Table;

typedef enum {
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL
} ExecuteResult;

typedef struct {
    Table* table;
    uint32_t row_num;
    bool end_of_table; // indicates a position one past the last element
} Cursor;