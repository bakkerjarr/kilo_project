/*************************************************************************
* Main file for the kilo terminal text editor.
* Visit http://viewsourcecode.org/snaptoken/kilo/ for the tutorial.
*
* FILENAME : kilo.c
*
* DESCRIPTION :
*       I'm up to chapter 3 step 46.
* 
* AUTHOR :  Jarrod N. Bakker    START DATE :    06/04/2016
*
**/

/*** includes ***/
#include<ctype.h>
#include<errno.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/ioctl.h>
#include<termios.h>
#include<unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/

struct editorConfig {
    int cx, cy;
    int screenRows;
    int screenCols;
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
 *  return: A char that has been entered by the user.
 */
char editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
        if (nread == -1 && errno != EAGAIN)
            die("editorReadKey: read");
    return c;
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
 * Draw tildes at the start of any line that is not part of a file.
 *
 * param ab: A dynamic string to append characters to.
 */
void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenRows; y++){
        if (y == E.screenRows/3){
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome),
                "Kilo editor -- version %s", KILO_VERSION);
            if (welcomelen > E.screenCols)
                welcomelen = E.screenCols;
            int padding = (E.screenCols - welcomelen)/2;
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
        
        abAppend(ab, "\x1b[K", 3); // clear the rest this line
        if (y < E.screenRows - 1)
            abAppend(ab, "\r\n", 2);
    }
}

/**
 * Refresh the terminal screen.
 */
void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // hide the cursor
    abAppend(&ab, "\x1b[H", 3); // reposition cursor top-left

    editorDrawRows(&ab); // get the number of tildes for the screen

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy+1, E.cx+1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // show the cursor again

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input ***/

/**
 * Move the cursor given a specific keypress. 
 *
 * param key: A key that has been pressed.
 */
void editorMoveCursor(char key) {
    switch (key) {
        case 'a':
            E.cx--;
            break;
        case 'd':
            E.cx++;
            break;
        case 'w':
            E.cy--;
            break;
        case 's':
            E.cy++;
            break;
    }
}

/**
 * Wait for a keypress then handle it.
 */
void editorProcessKeypress() {
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case 'w':
        case 's':
        case 'a':
        case 'd':
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

    if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
        die("initEditor: getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while(1){
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
