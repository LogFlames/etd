#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <time.h>

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

typedef struct Item Item;

struct Item {
    Item *parent;
    uint32_t sibling_index;
    char title[256];
    bool open;
    uint32_t child_count;
    Item *children[];
};

enum Mode { Normal, Insert };

#define INDENT_WIDTH 4
#define NON_BLOCKING_READ 1 // 1 = true, 0 = false

uint32_t terminal_size_rows = 0;
uint32_t terminal_size_cols = 0;

uint32_t cursor_position_row = 1;
uint32_t cursor_position_col = 1;
uint32_t window_offset_rows = 0;

char *render_buffer;
uint32_t render_buffer_size = 0;
uint32_t render_buffer_index = 0;

enum Mode mode = Normal;

Item *root;
Item *current_item;

void update_term_size() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    terminal_size_rows = w.ws_row;
    terminal_size_cols = w.ws_col;
}

void append_to_render_buffer(char *src, uint32_t src_length) {
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

void item_render(Item *item, uint32_t indent) {
    for (uint32_t i = 0; i < indent * INDENT_WIDTH; i++) {
        append_to_render_buffer(" ", 1);
    }

    if (item == current_item)
        append_to_render_buffer("\033[1m", 4);
    append_to_render_buffer(item->title, strlen(item->title));
    if (item == current_item)
        append_to_render_buffer("\033[0m", 4);

    append_to_render_buffer("\012\015", 2);
    if (item->open) {
        for (uint32_t i = 0; i < item->child_count; i++) {
            item_render(item->children[i], indent + 1);
        }
    }
}

void item_free(Item *root) {
    for (uint32_t i = 0; i < root->child_count; i++) {
        item_free(root->children[i]);
    }

    free(root);
}

void item_close_recusively(Item *root) {
    for (uint32_t i = 0; i < root->child_count; i++) {
        item_close_recusively(root->children[i]);
    }
    root->open = false;
}

bool item_open_layers(Item *root, uint32_t layers, bool *reached_max_depth) {
    bool did_open_item = false;
    *reached_max_depth = false;

    if (!root->open && root->child_count > 0) {
        did_open_item = true;
        root->open = true;
    }

    if (layers > 0) {
        bool max_depth = false;
        bool all_reached_max_depth = true;

        for (uint32_t i = 0; i < root->child_count; i++) {
            if (item_open_layers(root->children[i], layers - 1, &max_depth)) {
                did_open_item = true;

                if (!max_depth) {
                    all_reached_max_depth = false;
                }
            }
        }

        *reached_max_depth = (root->child_count == 0) || all_reached_max_depth;
    }

    return did_open_item;
}

void item_serialize_to_buffer(Item *root);
void item_parse_from_buffer();

int render(Item *root) {
    // Reset the render buffer
    render_buffer_index = 0;
    render_buffer[0] = 0;

    // Clear screen and move to top-left corner
    append_to_render_buffer("\033[H\033[2J", 7);

    // Render the main content, recusively from the root
    item_render(root, 0);

    uint32_t cursor_mode_col = cursor_position_col;
    uint32_t cursor_mode_row = cursor_position_row - window_offset_rows;

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
    sprintf(set_cursor_position_buffer, "\033[%u;%uH", cursor_mode_row,
            cursor_mode_col);
    append_to_render_buffer(set_cursor_position_buffer,
                            strlen(set_cursor_position_buffer));

    write(STDOUT_FILENO, render_buffer, render_buffer_index);
    return 0;
}

void move_up() {
    bool can_move_cursor = true;

    if (current_item->sibling_index == 0) {
        if (current_item->parent == NULL) {
            can_move_cursor = false;
        } else {
            current_item = current_item->parent;
        }
    } else {
        current_item =
            current_item->parent->children[current_item->sibling_index - 1];
        while (current_item->open && current_item->child_count > 0) {
            current_item = current_item->children[current_item->child_count - 1];
        }
    }

    if (can_move_cursor) {
        cursor_position_row--;
        if (cursor_position_row <= 0) {
            cursor_position_row = 1;
        }
    }
}

void move_down() {
    bool can_move_cursor = true;

    if (current_item->open && current_item->child_count > 0) {
        current_item = current_item->children[0];
    } else {
        if (current_item->parent != NULL &&
            current_item->parent->child_count < current_item->sibling_index + 1) {
            current_item =
                current_item->parent->children[current_item->sibling_index + 1];
        } else {
            can_move_cursor = false;
        }
    }

    if (can_move_cursor) {
        cursor_position_row++;
        if (cursor_position_row > terminal_size_rows) {
            // TODO: Cursor needs to be global
            cursor_position_row = terminal_size_rows;
        }
    }
}

void move_left() {
    cursor_position_col--;
    if (cursor_position_col <= 0) {
        cursor_position_col = 1;
    }
}

void move_right() {
    cursor_position_col++;
    if (cursor_position_col > terminal_size_cols) {
        cursor_position_col = terminal_size_cols;
    }
}

void open_current() {}

void close_current() {
    if (current_item->open) {
        item_close_recusively(current_item);
    } else {
        if (current_item->parent != NULL) {
            item_close_recusively(current_item->parent);
            current_item = current_item->parent;
            // TODO: move cursor, by what? Arg
        }
    }
}

int main() {

    // Set the terminal to raw mode and save the old settings
    struct termios old_terminal_settings;
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    old_terminal_settings = term;
    cfmakeraw(&term);

    if (NON_BLOCKING_READ) {
        // Make read non-blocking. Se 'man termios(3)' for details
        term.c_cc[VMIN] = 0;
        term.c_cc[VTIME] = 0;
    }
    tcsetattr(STDIN_FILENO, TCSADRAIN, &term);

    // Clear screen by printing newlines
    update_term_size();
    for (uint32_t a = 0; a < terminal_size_rows - 1; a++) {
        printf("\012\015");
    }

    // Setup render buffer
    render_buffer_size = 512;
    render_buffer = malloc(render_buffer_size * sizeof(char));

    // DEBUG: Define todo-data, to be read from file later
    root = malloc(sizeof(Item) + 1 * sizeof(Item *));
    current_item = root;

    root->parent = NULL;
    root->sibling_index = 0;

    strcpy(root->title, "Hello");
    root->child_count = 1;
    root->open = true;

    root->children[0] = malloc(sizeof(Item) + 1 * sizeof(Item *));
    root->children[0]->parent = root;
    root->children[0]->sibling_index = 0;
    strcpy(root->children[0]->title, "This is title 2");
    root->children[0]->child_count = 1;
    root->children[0]->open = true;

    root->children[0]->children[0] = malloc(sizeof(Item) + 0 * sizeof(Item *));
    root->children[0]->children[0]->parent = root->children[0];
    root->children[0]->children[0]->sibling_index = 0;
    strcpy(root->children[0]->children[0]->title, "This is a third subtitle");
    root->children[0]->children[0]->child_count = 0;

    if (!NON_BLOCKING_READ) {
        // Input is before rendering in update loop, render first frame before
        // blocking read
        update_term_size();
        render(root);
    }

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
                    switch (input_char) {
                    case '.':
                        running = false;
                        break;
                    case 'e':
                        move_down();
                        break;
                    case 'u':
                        move_up();
                        break;
                    case 'h':
                        move_left();
                        break;
                    case 't':
                        move_right();
                        break;
                    case 'i':
                        mode = Insert;
                        break;
                    case 011: // Tab
                    {
                        uint32_t layers = 0;
                        bool reached_max_depth = false;
                        while (
                            !item_open_layers(current_item, layers, &reached_max_depth) &&
                            !reached_max_depth) {
                            layers++;
                        }
                    } break;
                    case 033: {
                        // This might only work with blocking read, maybe
                        res = read(STDIN_FILENO, &input_char, 1);
                        if (res > 0) {
                            switch (input_char) {
                            case '[': {
                                res = read(STDIN_FILENO, &input_char, 1);
                                if (res > 0) {
                                    switch (input_char) {
                                    case 'Z': // Shift + tab
                                        close_current();
                                    default:
                                        break;
                                    }
                                }
                            } break;
                            default:
                                break;
                            }
                        }
                    } break;
                    default:
                        break;
                    }
                    break;
                case Insert:
                    switch (input_char) {
                    case 27:
                        mode = Normal;
                        break;
                    case '.':
                        running = false;
                        break;
                    case 127:
                        // TODO: WORK HERE
                        // current_item->title[]
                        break;
                    default:
                        strncat(current_item->title, &input_char, 1);
                        break;
                    }
                    break;
                default:
                    break;
                }
            }
        } while (NON_BLOCKING_READ && res > 0);

        update_term_size();
        render(root);

        // char* str = "\033[1mhej\nhej\n\033[32mHello\033[0m\015Hej
        // \033(0ajklm\033(A"; write(STDOUT_FILENO, str, strlen(str));

        usleep(10000);
    }

    item_free(root);

    // Return the terminal to previous settings/disable raw mode
    char reset_terminal_buffer[] = "\033[0m\033[2J\033[H";
    write(STDOUT_FILENO, reset_terminal_buffer, strlen(reset_terminal_buffer));
    tcsetattr(STDIN_FILENO, TCSADRAIN, &old_terminal_settings);

    return 0;
}
