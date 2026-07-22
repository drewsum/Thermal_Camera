
#include <xc.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "usb_uart/terminal_control.h"

// This function clears the terminal
void terminalClearScreen(void) {
    printf("\033[2J");
}

// This function moves the terminal cursor to top left corner
void terminalSetCursorHome(void) {
    printf("\033[H");
}

// This function clears the line the cursor is currently at on the terminal
void terminalClearLine(void) {
    printf("\033[K");
}

// This function saves the current cursor position on the terminal
void terminalSaveCursor(void) {
    printf("\033[s");
}

// This function returns the cursor to saved position on terminal
void terminalReturnCursor(void) {
    printf("\033[u");
}

// Text attributes function
// See attributes enums in "USB_UART.h"
// Call like so:
/*

    USB_UART_textAttributes(<TEXT COLOR (ALL CAPS)>, 
                            <BACKGROUND COLOR (ALL CAPS)>, 
                            <TEXT EFFECT (ALL CAPS)>);

*/

void terminalTextAttributes(char * foreground_color,
        char * background_color,
        char * input_attribute) {

    char print_string[16];

    snprintf(print_string, sizeof(print_string), "\033[%s;3%s;4%sm",
            input_attribute, foreground_color, background_color);

    printf("%s", print_string);

}

// Reset text attributes to white text, black background, no effects
void terminalTextAttributesReset(void) {
 
    // USB_UART_textAttributes(WHITE, BLACK, NORMAL);
    printf("\033[0;37;40m");
    
}

// tests all the function written for this example
void terminalPrintTestMessage(void) {
    
    // Set starting text color white, background black, no fancy stuff
    // Print COM port settings
    terminalTextAttributesReset();
    terminalClearScreen();
    terminalSetCursorHome();
    printf("USB UART Test\n\r\n\r");
//    printf("COM Port Settings:\n\r");
//    printf("    Baud Rate: %s\n\r", USB_UART_BAUD_RATE_STR);
//    printf("    Data Length: %s\n\r", USB_UART_DATA_LENGTH_STR);
//    printf("    Parity: %\n\r", USB_UART_PARITY_STR);
//    printf("    Stop Bits: %s\n\r", USB_UART_STOP_BITS_STR);
//    printf("    Flow Control: %s\n\r\n\r", USB_UART_FLOW_CONTROL_STR);
        
    // Test text attributes
    printf("Testing text attributes:\n\r");

    // Print some black text
    terminalTextAttributesReset();
    terminalTextAttributes(BLACK_COLOR, WHITE_COLOR, NORMAL_FONT);
    printf("This text is black\n\r");

    terminalTextAttributesReset();
    terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("This text is red\n\r");

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("This text is green\n\r");

    terminalTextAttributesReset();
    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("This text is yellow\n\r");

    terminalTextAttributesReset();
    terminalTextAttributes(BLUE_COLOR, WHITE_COLOR, NORMAL_FONT);
    printf("This text is blue\n\r");

    terminalTextAttributesReset();
    terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("This text is magenta\n\r");

    terminalTextAttributesReset();
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("This text is cyan\n\r");
    
    terminalTextAttributesReset();
    terminalTextAttributes(WHITE_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("This text has a black background\n\r");
    
    terminalTextAttributesReset();
    terminalTextAttributes(BLACK_COLOR, RED_COLOR, NORMAL_FONT);
    printf("This text has a red background\n\r");

    terminalTextAttributesReset();
    terminalTextAttributes(BLACK_COLOR, GREEN_COLOR, NORMAL_FONT);
    printf("This text has a green background\n\r");
    
    terminalTextAttributesReset();
    terminalTextAttributes(BLACK_COLOR, YELLOW_COLOR, NORMAL_FONT);
    printf("This text has a yellow background\n\r");
    
    terminalTextAttributesReset();
    terminalTextAttributes(WHITE_COLOR, BLUE_COLOR, NORMAL_FONT);
    printf("This text has a blue background\n\r");
    
    terminalTextAttributesReset();
    terminalTextAttributes(BLACK_COLOR, MAGENTA_COLOR, NORMAL_FONT);
    printf("This text has a magenta background\n\r");
    
    terminalTextAttributesReset();
    terminalTextAttributes(BLACK_COLOR, CYAN_COLOR, NORMAL_FONT);
    printf("This text has a cyan background\n\r");
    
    terminalTextAttributesReset();
    terminalTextAttributes(BLACK_COLOR, WHITE_COLOR, NORMAL_FONT);
    printf("This text has a white background\n\r");
    
    terminalTextAttributesReset();
    terminalTextAttributes(WHITE_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("This text is bold\n\r");

    terminalTextAttributesReset();
    terminalTextAttributes(WHITE_COLOR, BLACK_COLOR, UNDERSCORE_FONT);
    printf("This text is underscored\n\r");

    terminalTextAttributesReset();
    terminalTextAttributes(WHITE_COLOR, BLACK_COLOR, BLINK_FONT);
    printf("This text is blinking\n\r");

    terminalTextAttributesReset();
    terminalTextAttributes(WHITE_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("This text is reversed\n\r");

    terminalTextAttributesReset();
    printf("This text is normal\n\r\n\r");
    
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Finished test message, type 'Help' for list of commands\n\r\n\r");
    terminalTextAttributesReset();

}// this function sets the window title of remote terminal
void terminalSetTitle(char * title_string) {

    printf("\033]0;%s\007", title_string);

}

// *****************************************************************************
// Section: Live screen (differential redraw)
// *****************************************************************************
// See the header for what this is for. The shadow holds what the terminal is
// currently showing on each row, including the row's leading SGR escape, so a
// row can be skipped entirely whenever its rendered text is byte-identical to
// last refresh.

// Rows/columns the live screen tracks. Rows past TERMINAL_LIVE_ROWS still get
// printed, they just aren't diffed; text past TERMINAL_LIVE_COLS is truncated.
#define TERMINAL_LIVE_ROWS  64
#define TERMINAL_LIVE_COLS  112

static char terminal_live_shadow[TERMINAL_LIVE_ROWS][TERMINAL_LIVE_COLS];

// 1-based row terminalRow() will compose next; 0 means "not inside a live
// screen", which puts terminalRow() into plain scrolling-output mode
static unsigned int terminal_live_row;

// how many rows the previous refresh occupied, so a page that got shorter can
// have its leftover rows erased
static unsigned int terminal_live_prev_rows;

static bool terminal_live_full_repaint;

void terminalLiveScreenBegin(bool full_repaint) {

    terminal_live_full_repaint = full_repaint;
    terminal_live_row = 1;

    if (full_repaint) {

        // Nothing on screen can be trusted, so drop the shadow and start over.
        // This is also the only path that erases the terminal -- ordinary
        // refreshes overwrite in place precisely so they don't have to.
        memset(terminal_live_shadow, 0, sizeof(terminal_live_shadow));
        terminal_live_prev_rows = 0;
        terminalClearScreen();

    }

}

void terminalRow(const char * sgr, const char * fmt, ...) {

    char line[TERMINAL_LIVE_COLS];
    va_list args;
    int used;

    // The SGR escape is part of the row text rather than a separate print, so
    // that a row which only changed colour still compares unequal below
    used = snprintf(line, sizeof(line), "%s", sgr);
    if (used < 0 || (unsigned int) used >= sizeof(line)) used = sizeof(line) - 1;

    va_start(args, fmt);
    vsnprintf(line + used, sizeof(line) - used, fmt, args);
    va_end(args);

    // Called outside a live screen: behave like an ordinary printf() line
    if (terminal_live_row == 0) {

        printf("%s\r\n", line);
        return;

    }

    if (terminal_live_row <= TERMINAL_LIVE_ROWS) {

        char * shadow = terminal_live_shadow[terminal_live_row - 1];

        if (terminal_live_full_repaint || strcmp(line, shadow) != 0) {

            strcpy(shadow, line);

            // Address the row absolutely rather than relying on where the
            // cursor happens to be -- rows that didn't change aren't printed
            // at all, so the cursor doesn't walk down the page on its own.
            // The trailing erase-to-end-of-line trims whatever a longer
            // previous version of this row left behind.
            printf("\033[%u;1H%s\033[K", terminal_live_row, line);

        }

    } else {

        printf("%s\r\n", line);

    }

    terminal_live_row++;

}

void terminalBlankRow(void) {

    terminalRow("", "%s", "");

}

void terminalLiveScreenEnd(void) {

    unsigned int row;

    // Erase rows this refresh didn't use but the previous one did (a status
    // line that stopped applying, say), otherwise they'd sit there stale
    for (row = terminal_live_row;
         row <= terminal_live_prev_rows && row <= TERMINAL_LIVE_ROWS;
         row++) {

        printf("\033[%u;1H\033[K", row);
        terminal_live_shadow[row - 1][0] = '\0';

    }

    terminal_live_prev_rows = terminal_live_row - 1;

    // Park the cursor below the page with default attributes, so anything
    // printed outside the live screen lands somewhere sane and uncoloured
    printf("\033[%u;1H\033[0;37;40m", terminal_live_row);

    terminal_live_row = 0;
    terminal_live_full_repaint = false;

}