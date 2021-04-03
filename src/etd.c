#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <string.h>

#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

typedef struct Item Item;

struct Item {
    Item* parent;
    uint32_t sibling_index;
    char title[256];
    bool open;
    uint32_t child_count;
    Item* children[];
};

enum Mode {
    Normal,
    Insert
};

#define INDENT_WIDTH 4

uint32_t terminal_size_rows = 0;
uint32_t terminal_size_cols = 0;

uint32_t cursor_position_row = 1; 
uint32_t cursor_position_col = 1; 
uint32_t window_offset_rows = 0;

char* render_buffer;
uint32_t render_buffer_size = 0;
uint32_t render_buffer_index = 0;

enum Mode mode = Normal;

Item* root;
Item* current_item;

void update_term_size() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    terminal_size_rows = w.ws_row;
    terminal_size_cols = w.ws_col;
}

void append_to_render_buffer(char* src, uint32_t src_length) {
    while (render_buffer_index + src_length >= render_buffer_size) {
        render_buffer_size <<= 1;
        render_buffer = realloc(render_buffer, render_buffer_size * sizeof(char));
    }

    for (uint32_t i = 0; i < src_length; i++) {
        render_buffer[render_buffer_index + i] = src[i];
    }

    render_buffer_index += src_length;
    render_buffer[render_buffer_index] = 0;
}

char* item_render(Item* item, uint32_t indent) {
    for (uint32_t i = 0; i < indent * INDENT_WIDTH; i++) {
        append_to_render_buffer(" ", 1);
    }

    if (item == current_item) append_to_render_buffer("\033[1m", 4);
    append_to_render_buffer(item->title, strlen(item->title));
    if (item == current_item) append_to_render_buffer("\033[0m", 4);

    append_to_render_buffer("\012\015", 2);
    if (item->open) {
        for (uint32_t i = 0; i < item->child_count; i++) {
            item_render(item->children[i], indent + 1);
        }
    }
}

void item_free(Item* root) {
    for (uint32_t i = 0; i < root->child_count; i++) {
        item_free(root->children[i]);
    }

    free(root);
}

void item_serialize_to_buffer(Item* root);
void item_parse_from_buffer();

int render(Item* root) {
    // Reset the render buffer
    render_buffer_index = 0;
    render_buffer[0] = 0;

    // Clear screen and move to top-left corner
    append_to_render_buffer("\033[H\033[2J", 7);

    // Render the main content, recusively from the root
    item_render(root, 0);

    uint32_t cursor_mode_col = cursor_position_col;
    uint32_t cursor_mode_row = cursor_position_row;

    switch (mode) {
        case Normal:
            cursor_mode_col = 1;
            break;
        case Insert:
            break;
        default:
            break;
    }

    // Move cursor to desired position
    char set_cursor_position_buffer[16];
    sprintf(set_cursor_position_buffer, "\033[%u;%uH", cursor_mode_row, cursor_mode_col); 
    append_to_render_buffer(set_cursor_position_buffer, strlen(set_cursor_position_buffer));

    write(STDOUT_FILENO, render_buffer, render_buffer_index);
    return 0;
}

int main() {

    // Set the terminal to raw mode and save the old settings
    struct termios old_terminal_settings;
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    old_terminal_settings = term;
    cfmakeraw(&term);
    // Make read non-blocking. Se 'man termios(3)' for details
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSADRAIN, &term);

    // Clear screen by printing newlines
    update_term_size();
    for (uint32_t a = 0; a < terminal_size_rows - 1; a++) {
        printf("\012\015", a);
    }

    // Setup render buffer
    render_buffer_size = 512;
    render_buffer = malloc(render_buffer_size * sizeof(char));


    // DEBUG: Define todo-data, to be read from file later
    root = malloc(sizeof(Item) + 1 * sizeof(Item*));
    current_item = root;

    root->parent = NULL;
    root->sibling_index = 0;

    strcpy(root->title, "Hello");
    root->child_count = 1;
    root->open = true;

    root->children[0] = malloc(sizeof(Item) + 0 * sizeof(Item*));
    root->children[0]->parent = root;
    root->children[0]->sibling_index = 0;
    strcpy(root->children[0]->title, "This is title 2");
    root->children[0]->child_count = 0;


    bool running = true;
    while (running) {
        // Read input
        char input_char;
        input_char = 0;
        uint32_t res;

        do {
            res = read(STDIN_FILENO, &input_char, 1);
            if (res > 0) {
                switch (mode) {
                    case Normal:
                        switch(input_char) {
                            case '.':
                                running = false;
                                break;
                            case 'e':
                                {
                                    bool can_move_cursor = true;

                                    if (current_item->open && current_item->child_count > 0) {
                                        current_item = current_item->children[0];
                                    } else {
                                        if (current_item->parent != NULL && current_item->parent->child_count < current_item->sibling_index + 1) {
                                            current_item = current_item->parent->children[current_item->sibling_index + 1];
                                        } else {
                                            can_move_cursor = false;
                                        }
                                    }

                                    if (can_move_cursor) {
                                        cursor_position_row++;
                                        if (cursor_position_row > terminal_size_rows) {
                                            cursor_position_row = terminal_size_rows;
                                        }
                                    }
                                }
                                break;
                            case 'u':
                                {
                                    bool can_move_cursor = true;

                                    if (current_item->sibling_index == 0) {
                                        if (current_item->parent == NULL) {
                                            can_move_cursor = false;
                                        } else {
                                            current_item = current_item->parent;
                                        }
                                    } else {
                                        current_item = current_item->parent->children[current_item->sibling_index - 1];
                                    }

                                    if (can_move_cursor) {
                                        cursor_position_row--;
                                        if (cursor_position_row <= 0) {
                                            cursor_position_row = 1;
                                        }
                                    }
                                }
                                break;
                            case 'h':
                                cursor_position_col--;
                                if (cursor_position_col <= 0) {
                                    cursor_position_col = 1;
                                }
                                break;
                            case 't':
                                cursor_position_col++;
                                if (cursor_position_col > terminal_size_cols) {
                                    cursor_position_col = terminal_size_cols;
                                }
                                break;
                            case 011:
                                current_item->open = !current_item->open;
                                break;
                            default:
                                // printf("%u\n", (uint32_t)input_char); 
                                // usleep(10000000000);
                                break;
                        }
                        break;
                    case Insert:
                        switch (input_char) {
                            case '.':
                                running = false;
                                break;
                            default:
                                break;
                        }
                        break;
                    default:
                        break;
                }
            }
        } while (res > 0);

        update_term_size();
        render(root);

        // char* str = "\033[1mhej\nhej\n\033[32mHello\033[0m\015Hej      \033(0ajklm\033(A";
        // write(STDOUT_FILENO, str, strlen(str));

        usleep(10000);
    }

    item_free(root);

    // Return the terminal to previous settings/disable raw mode
    char reset_terminal_buffer[] = "\033[0m\033[2J\033[H";
    write(STDOUT_FILENO, reset_terminal_buffer, strlen(reset_terminal_buffer));
    tcsetattr(STDIN_FILENO, TCSADRAIN, &old_terminal_settings);

    return 0;
}
