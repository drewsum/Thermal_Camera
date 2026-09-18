/* ************************************************************************** */
/** Terminal Control Functions for serial terminals
 */
/* ************************************************************************** */

#ifndef _TERMINAL_CONTROL_H    /* Guard against multiple inclusion */
#define _TERMINAL_CONTROL_H

#include <stdbool.h>

// these macros define strings for terminal font manipulation
#define NORMAL_FONT          "0"
#define BOLD_FONT            "1"
#define UNDERSCORE_FONT      "4"
#define BLINK_FONT           "5"
#define REVERSE_FONT         "7"
#define CONCEALED_FONT       "8"

#define BLACK_COLOR         "0"
#define RED_COLOR           "1"
#define GREEN_COLOR         "2"
#define YELLOW_COLOR        "3"
#define BLUE_COLOR          "4"
#define MAGENTA_COLOR       "5"
#define CYAN_COLOR          "6"
#define WHITE_COLOR         "7"

// Enumeration holding attributes data for setting text fanciness
//typedef enum {
//    
//    NORMAL_FONT,
//    BOLD_FONT,
//    UNDERSCORE_FONT,
//    BLINK_FONT,
//    REVERSE_FONT,
//    CONCEALED_FONT
//            
//} text_attribute_t;

// Enumeration for setting text color attributes
//typedef enum {
//    
//    BLACK_COLOR,
//    RED_COLOR,
//    GREEN_COLOR,
//    YELLOW_COLOR,
//    BLUE_COLOR,
//    MAGENTA_COLOR,
//    CYAN_COLOR,
//    WHITE_COLOR
//            
//} text_color_t;

// Terminal manipulation functions
void terminalClearScreen(void);  // clears the whole terminal
void terminalSetCursorHome(void);  // Sets cursor to home position (top left)
void terminalClearLine(void);      // clears the current line where the cursor appears
void terminalSaveCursor(void);     // Saves the current position of the cursor
void terminalReturnCursor(void);   // Returns the cursor to saved position

// Text attributes function
void terminalTextAttributes(char * foreground_color,
        char * background_color,
        char * input_attribute);

// Reset to white foreground, black background, no fancy stuff
void terminalTextAttributesReset(void);

// This function tests terminal control
void terminalPrintTestMessage(void);

// this function sets the window title of remote terminal
void terminalSetTitle(char * title_string);

// *****************************************************************************
// Section: Live screen (differential redraw)
// *****************************************************************************
// A "live screen" is a page that gets refreshed on a timer (the live telemetry
// view is the one user of it today). Redrawing one by clearing the terminal and
// re-emitting every line costs ~2kB per refresh, which at this port's baud rate
// is over a tenth of a second of solid traffic -- the terminal visibly flashes
// and lags behind. Instead, build the page out of terminalRow() calls between
// terminalLiveScreenBegin() and terminalLiveScreenEnd(): the screen is kept in
// a shadow buffer and only the rows whose text actually changed since the last
// refresh get rewritten, addressed by row number. In steady state that's a few
// digits of a few rows rather than the whole page, and the terminal never gets
// erased so nothing flickers.

// Compile-time SGR (colour/attribute) escape, taking the same color and font
// macros as terminalTextAttributes(). Background is always black. Rows carry
// their colour as part of the string that gets diffed, so a colour-only change
// still redraws the row.
#define TERMINAL_SGR(foreground_color, input_attribute) \
    "\033[" input_attribute ";3" foreground_color ";40m"

// Green when `ok` is true, red when it isn't -- the colour convention every
// pass/fail status row in this firmware uses. Picking the colour as a string
// (rather than emitting it with terminalTextAttributes()) is what lets a whole
// status row go out as a single terminalRow() call.
#define TERMINAL_SGR_OK_BAD(ok) \
    ((ok) ? TERMINAL_SGR(GREEN_COLOR, NORMAL_FONT) : TERMINAL_SGR(RED_COLOR, NORMAL_FONT))

// Starts a live screen. Pass full_repaint = true to clear the terminal and
// force every row out regardless of the shadow -- needed the first time the
// screen is drawn, or any time something else has printed over it.
void terminalLiveScreenBegin(bool full_repaint);

// Emits one row of the current live screen (or, when called outside of
// Begin/End, just prints the line followed by a newline -- so the same
// rendering code can serve both a live screen and a one-shot dump).
// `sgr` is a TERMINAL_SGR() string, or "" to leave attributes alone.
void terminalRow(const char * sgr, const char * fmt, ...)
        __attribute__((format(printf, 2, 3)));

// terminalRow() for a blank spacer row
void terminalBlankRow(void);

// Finishes a live screen: erases any rows left over from a taller previous
// refresh, parks the cursor below the page and resets text attributes.
void terminalLiveScreenEnd(void);



#endif /* _TERMINAL_CONTROL_H */

/* *****************************************************************************
 End of File
 */
