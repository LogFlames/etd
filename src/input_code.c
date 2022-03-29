#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <unistd.h>
#include <termios.h>

int main() {

    // Set the terminal to raw mode and save the old settings
    struct termios old_terminal_settings;
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    old_terminal_settings = term;
    cfmakeraw(&term);
    tcsetattr(STDIN_FILENO, TCSADRAIN, &term);

    bool running = true;
    while (running) {
        // Read input
        char input_char;
        input_char = 0;
        uint32_t res;

        read(STDIN_FILENO, &input_char, 1);
        printf("%u\12\15", (uint32_t)input_char);

        if (input_char == 'q') {
            running = false;
        }
    }

    tcsetattr(STDIN_FILENO, TCSADRAIN, &old_terminal_settings);

    return 0;
}
