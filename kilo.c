/*************************************************************************
 * Main file for the kilo terminal text editor.
 * Visit http://viewsourcecode.org/snaptoken/kilo/ for the tutorial.
 *
 * FILENAME : kilo.c
 *
 * DESCRIPTION :
 *       I'm up to chapter 5 step 101.
 * 
 * AUTHOR :  Jarrod N. Bakker    START DATE :    06/04/2016
 *
 */

/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include<ctype.h>
#include<errno.h>
#include<stdio.h>
#include<stdarg.h>
#include<stdlib.h>
#include<string.h>
#include<sys/ioctl.h>
#include<sys/types.h>
#include<termios.h>
#include<time.h>
#include<unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/

typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

struct editorConfig {
    int cx, cy;
    int rx;
    int rowOff;
    int colOff;
    int screenRows;
    int screenCols;
    int numRows;
    erow *row;
    char *filename;
    char statusMsg[80];
    time_t statusMsg_time;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

/**
 * Print an error message and exit the program.
 *
 * param msg: String containing an error message.
 */
void die(const char *msg) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(msg);
    exit(1);
}

/**
 * Return terminal settings to its original configuration.
 */
void disableRawMode() {
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("disableRawMode: tcsetattr");
}

/**
 * Set terminal attributes for raw mode.
 */
void enableRawMode() {
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("enableRawMode: tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("enableRawMode: tcsetattr");
}

/**
 * Read a keypress and return it.
 *
 *  return: A char that has been entered by the user, encoded as an int.
 */
int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
        if (nread == -1 && errno != EAGAIN)
            die("editorReadKey: read");
    
    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DEL_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                            return HOME_KEY;
                        case '8':
                            return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A':
                        return ARROW_UP;
                    case 'B': 
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

/**
 * Get the position of the cursor and store it in the parameters.
 *
 * param rows: An int that will store the row number of the cursor.
 * param cols: An int that will store the column number of the cursor.
 * return: 0 if successful, -1 if not.
 */
int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while (i < sizeof(buf) -1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] =  '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;
    
    return 0;
}
    
/**
 * Get the size of the terminal window and store in the parameters.
 *
 * param rows: An int that will store the number of rows.
 * param cols: An int that will store the number of columns.
 * return: 0 if successful, -1 if not.
 */
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** row operations ***/

/**
 * Convert a chars index into a render index.
 *
 * param row: A line in the file.
 * param cx: The number of chars in row.
 */
int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        rx++;
    }
    return rx;
}

/**
 * Copy a line of text into a special buffer for rendering characters
 * such as tabs.
 *
 * param row: Line of text to copy through.
 */
void editorUpdateRow(erow *row) {
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t')
            tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);

    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t'){
            row->render[idx++] = ' ';
            while (idx % KILO_TAB_STOP != 0)
                row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

/**
 * Allocate memory for a row of text.
 *
 * param s: Row of text to add.
 * param len: Length of the row.
 */
void editorAppendRow(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(erow) * (E.numRows + 1));

    int at = E.numRows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.numRows++;
}

/*** file i/o ***/

/**
 * Open and read a file from disk.
 *
 * param filename: Name of a file being opened and read.
 */
void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp)
        die("editorOpen: fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp); 
}

/*** append buffer ***/

struct abuf{
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

/**
 * Append a string onto the end of an existing dynamic string.
 *
 * param ab: The dynamic string to append to.
 * param s: The string being appended.
 * param len: Length of the string being appended.
 */
void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

/**
 * Free a dynamic string.
 *
 * param ab: A dynamic string to free.
 */
void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** output ***/

/**
 * Check if the cursor has moved outside of the visible window. If so,
 * adjust the cursor so that it is in the visible window.
 */
void editorScroll() {
    E.rx = 0;
    if (E.cy < E.numRows)
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);

    if (E.cy < E.rowOff)
        E.rowOff = E.cy;
    if (E.cy >= E.rowOff + E.screenRows)
        E.rowOff = E.cy - E.screenRows + 1;
    if (E.rx < E.colOff)
        E.colOff = E.rx;
    if (E.rx >= E.colOff + E.screenCols)
        E.colOff = E.rx - E.screenCols + 1;
}

/**
 * Draw tildes at the start of any line that is not part of a file.
 *
 * param ab: A dynamic string to append characters to.
 */
void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenRows; y++) {
        int fileRow = y + E.rowOff;
        if (fileRow >= E.numRows) {
            if (E.numRows == 0 && y == E.screenRows/3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Kilo editor -- version %s", KILO_VERSION);
                if (welcomelen > E.screenCols)
                    welcomelen = E.screenCols;
                int padding = (E.screenCols - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[fileRow].rsize - E.colOff;
            if (len < 0)
                len = 0;
            if (len > E.screenCols)
                len = E.screenCols;
            abAppend(ab, &E.row[fileRow].render[E.colOff], len);
        }
        
        abAppend(ab, "\x1b[K", 3); // clear the rest this line
        abAppend(ab, "\r\n", 2);
    }
}

/**
 * Draw a status bar.
 *
 * param ab: A dynamic string to append characters to.
 */
void editorDrawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines",
                       E.filename ? E.filename : "[No Name]", E.numRows);
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
                        E.cy + 1, E.numRows);
    if (len > E.screenCols)
        len = E.screenCols;
    abAppend(ab, status, len);
    while (len < E.screenCols) {
        if (E.screenCols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

/**
 * Draw a Message bar.
 *
 * param ab: A dynamic string to append characters to.
 */
void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msgLen = strlen(E.statusMsg);
    if (msgLen > E.screenCols)
        msgLen = E.screenCols;
    if (msgLen && time(NULL) - E.statusMsg_time < 5)
        abAppend(ab, E.statusMsg, msgLen);
}

/**
 * Refresh the terminal screen.
 */
void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // hide the cursor
    abAppend(&ab, "\x1b[H", 3); // reposition cursor top-left

    editorDrawRows(&ab); // get the number of tildes for the screen
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowOff) + 1,
                                              (E.rx - E.colOff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // show the cursor again

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/**
 * Set the status message for the kilo editor and note the time it
 * was set.
 *
 * param fmt: A format string.
 */
void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusMsg, sizeof(E.statusMsg), fmt, ap);
    va_end(ap);
    E.statusMsg_time = time(NULL);
}

/*** input ***/

/**
 * Move the cursor given a specific keypress. 
 *
 * param key: A key that has been pressed, encoded as an int.
 */
void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];

    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0)
                E.cy--;
            break;
        case ARROW_DOWN:
            if (E.cy < E.numRows)
                E.cy++;
            break;
    }

    row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];
    int rowLen = row ? row->size : 0;
    if (E.cx > rowLen)
        E.cx = rowLen;
}

/**
 * Wait for a keypress then handle it.
 */
void editorProcessKeypress() {
    int c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            if (E.cy < E.numRows)
                E.cx = E.row[E.cy].size;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    E.cy = E.rowOff;
                } else if (c == PAGE_DOWN){
                    E.cy = E.rowOff + E.screenRows - 1;
                    if (E.cy > E.numRows)
                        E.cy = E.numRows;
                }

                int times = E.screenRows;
                while (times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** init ***/

/**
 * Initialise the editor.
 */
void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowOff = 0;
    E.colOff = 0;
    E.numRows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusMsg[0] = '\0';
    E.statusMsg_time = 0;

    if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
        die("initEditor: getWindowSize");
    E.screenRows -= 2;
}

int main(int argc, char **argv) {
    enableRawMode();
    initEditor();
    if (argc >= 2)
        editorOpen(argv[1]);

    editorSetStatusMessage("HELP: Ctrl-Q = quit");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
