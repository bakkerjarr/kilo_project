/*************************************************************************
 * Main file for the kilo terminal text editor.
 * Visit http://viewsourcecode.org/snaptoken/kilo/ for the tutorial.
 *
 * FILENAME : kilo.c
 *
 * DESCRIPTION :
 *       A complete implementation of the kilo text editor.
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
#include<fcntl.h>
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
#define KILO_QUIT_TIMES 3

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    BACKSPACE = 127,
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

enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/*** data ***/

struct editorSyntax {
    char *filetype;
    char **filematch;
    char **keywords;
    char *singlelineCommentStart;
    char *multilineCommentStart;
    char *multilineCommentEnd;
    int flags;
};

typedef struct erow {
    int idx;
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *hl;
    int hlOpenComment;
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
    int dirty;
    char *filename;
    char statusMsg[80];
    time_t statusMsg_time;
    struct editorSyntax *syntax;
    struct termios orig_termios;
};

struct editorConfig E;

/*** filetypes ***/

char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",
    
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", NULL
};

struct editorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    }
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

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

/*** syntax highlighting ***/

/**
 * Determine if a character is a separating character.
 *
 * param c: A character to check.
 */
int isSeparator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

/**
 * Determine what characters in a line need to be highlighted.
 *
 * param row: A line of characters.
 */
void editorUpdateSyntax(erow *row) {
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);
    
    if (E.syntax == NULL)
        return;
    
    char **keywords = E.syntax->keywords; 

    char *scs = E.syntax->singlelineCommentStart;
    char *mcs = E.syntax->multilineCommentStart;
    char *mce = E.syntax->multilineCommentEnd;

    int scsLen = scs ? strlen(scs) : 0;
    int mcsLen = mcs ? strlen(mcs) : 0;
    int mceLen = mce ? strlen(mce) : 0;

    int prevSep = 1;
    int inString = 0;
    int inComment = (row->idx > 0 && E.row[row->idx - 1].hlOpenComment);

    int i = 0;
    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prevHl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        if (scsLen && !inString && !inComment) {
            if (!strncmp(&row->render[i], scs, scsLen)){
                memset(&row->hl[i], HL_COMMENT, row->rsize - i);
                break;
            }
        }

        if(mcsLen && mceLen && !inString) {
            if (inComment) {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce, mceLen)){
                    memset(&row->hl[i], HL_MLCOMMENT, mceLen);
                    i += mceLen;
                    inComment = 0;
                    prevSep = 1;
                    continue;
                } else {
                    i++;
                    continue;
                }
            } else if (!strncmp(&row->render[i], mcs, mcsLen)) {
                memset(&row->hl[i], HL_MLCOMMENT, mcsLen);
                i += mcsLen;
                inComment = 1;
                continue;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (inString) {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->rsize) {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }
                if (c == inString)
                    inString = 0;
                i++;
                prevSep = 1;
                continue;
            } else {
                if (c == '"' || c == '\'') {
                    inString = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prevSep || prevHl == HL_NUMBER)) ||
                (c == '.' && prevHl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++;
                prevSep = 0;
                continue;
            }
        }

        if (prevSep) {
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen - 1] == '|';
                if (kw2)
                    klen--;
                if (!strncmp(&row->render[i], keywords[j], klen) &&
                    isSeparator(row->render[i + klen])) {
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prevSep = 0;
                continue;
            }
        }

        prevSep = isSeparator(c);
        i++;
    }

    int changed = (row->hlOpenComment != inComment);
    row->hlOpenComment = inComment;
    if (changed && row->idx + 1 < E.numRows)
        editorUpdateSyntax(&E.row[row->idx + 1]);
}

/**
 * Determine the colour to be used for syntax highlighting.
 *
 * param hl: A character highlight index.
 * return: An integer that represents the desired syntax highlighting 
 *         colour to be fed into an escape  sequence command.
 */
int editorSyntaxToColor(int hl) {
    switch(hl) {
        case HL_COMMENT:
        case HL_MLCOMMENT:
            return 36;
        case HL_KEYWORD1:
            return 33;
        case HL_KEYWORD2:
            return 32;
        case HL_STRING:
            return 35;
        case HL_NUMBER:
            return 31;
        case HL_MATCH:
            return 34;
        default:
            return 37;
    }
}

/**
 * Select the correct syntax highlighting scheme based on the
 * file extension.
 */
void editorSelectSyntaxHighlight() {
    E.syntax = NULL;
    if (E.filename == NULL)
        return;

    char *ext = strrchr(E.filename, '.');

    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;
        while(s->filematch[i]) {
            int isExt = (s->filematch [i][0] == '.');
            if ((isExt && ext && !strcmp(ext, s->filematch[i])) ||
                (!isExt && strstr(E.filename, s->filematch[i]))) {
                    E.syntax = s;

                    int filerow;
                    for (filerow = 0; filerow < E.numRows; filerow++) {
                        editorUpdateSyntax(&E.row[filerow]);
                    }

                    return;
                }
            i++;
        }
    }
}

/*** row operations ***/

/**
 * Convert a chars index into a render index.
 *
 * param row: A line in the file.
 * param cx: Chars index.
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
 * Convert a render index into a chars index.
 *
 * param row: A line in the file.
 * param rx: Render index.
 */
int editorRowRxToCx(erow *row, int rx) {
    int curRx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t')
            curRx += (KILO_TAB_STOP - 1) - (curRx % KILO_TAB_STOP);
        curRx++;

        if (curRx > rx)
            return cx;
    }
    return cx;
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

    editorUpdateSyntax(row);
}

/**
 * Allocate memory for a row of text.
 *
 * param at: The row index to insert at.
 * param s: Row of text to add.
 * param len: Length of the row.
 */
void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numRows)
        return;

    E.row = realloc(E.row, sizeof(erow) * (E.numRows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numRows - at));
    for (int j = at + 1; j <= E.numRows; j++)
        E.row[j].idx++;

    E.row[at].idx = at;

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hlOpenComment = 0;
    editorUpdateRow(&E.row[at]);

    E.numRows++;
    E.dirty++;
}

/**
 * Free the memory associated with storing and rendering a line.
 *
 * param row: The line to free.
 */
void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

/**
 * Delete a line of text.
 *
 * param at: The line the cursor is at.
 */
void editorDelRow(int at) {
    if (at < 0 || at >= E.numRows)
        return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1],
            sizeof(erow) * (E.numRows - at - 1));
    for (int j = at; j < E.numRows - 1; j++)
        E.row[j].idx--;
    E.numRows--;
    E.dirty++;
}

/**
 * Insert a single character into a line at the cursor.
 *
 * param row: The line of text.
 * param at: The index we want to insert the character at.
 * param c: The character being inserted.
 */
void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size)
        at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

/**
 * Append a string to the end of a line.
 *
 * param row: The line to append to.
 * param s: The string being appended.
 * param len: The length of the string.
 */
void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

/**
 * Delete a character from a line at the cursor.
 *
 * param row: The line of text.
 * param at: The index we want to delete the character from.
 */
void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size)
        return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

/*** editor operations ***/

/**
 * Prepare to insert a character at the cursor's position.
 *
 * param c: The character being inserted.
 */
void editorInsertChar(int c) {
    if (E.cy == E.numRows)
        editorInsertRow(E.numRows, "", 0);
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

/**
 * Insert a new line.
 */
void editorInsertNewLine() {
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

/**
 * Prepare to delete a character from the cursor's position.
 */
void editorDelChar() {
    if (E.cy == E.numRows)
        return;
    if (E.cx == 0 && E.cy == 0)
        return;

    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

/*** file i/o ***/

/**
 * Convert the file into a string that can be written to disk.
 *
 * param buflen: Length of the buffer that was written.
 */
char *editorRowsToString(int *buflen){
    int totlen = 0;
    int j;
    for (j = 0; j < E.numRows; j++)
        totlen += E.row[j].size + 1;
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;
    for (j = 0; j < E.numRows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

/**
 * Open and read a file from disk.
 *
 * param filename: Name of a file being opened and read.
 */
void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);

    editorSelectSyntaxHighlight();

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
        editorInsertRow(E.numRows, line, linelen);
    }
    free(line);
    fclose(fp); 
    E.dirty = 0;
}

/**
 * Save the text to a file on the disk.
 */
void editorSave() {
    if (E.filename == NULL) {
        E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        if (E.filename == NULL) {
            editorSetStatusMessage("Save aborted");
            return;
        }
        editorSelectSyntaxHighlight();
    }

    int len;
    char *buf = editorRowsToString(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if(write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                E.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }

    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/

/**
 * A callback function for incremental searching through the file.
 *
 * param query: The searching query.
 * param key: The keypress.
 */
void editorFindCallback(char *query, int key) {
    static int lastMatch = -1;
    static int direction = 1;

    static int savedHlLine;
    static char *savedHl = NULL;

    if (savedHl) {
         memcpy(E.row[savedHlLine].hl, savedHl, E.row[savedHlLine].rsize);
         free(savedHl);
         savedHl = NULL;
    }

    if (key == '\r' || key == '\x1b') {
        lastMatch = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        lastMatch = -1;
        direction = 1;
    }
    
    if (lastMatch == -1)
        direction = 1;
    int current = lastMatch;
    int i;
    for (i = 0; i < E.numRows; i++) {
        current += direction;
        if (current == -1)
            current = E.numRows - 1;
        else if (current == E.numRows)
            current = 0;

        erow *row = &E.row[current];
        char *match = strstr(row->render, query);
        if (match) {
            lastMatch = current;
            E.cy = current;
            E.cx = editorRowRxToCx(row, match - row->render);
            E.rowOff = E.numRows;

            savedHlLine = current;
            savedHl = malloc(row->rsize);
            memcpy(savedHl, row->hl, row->rsize);
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

/**
 * Prompt the user for a string and search through the text.
 */
void editorFind() {
    int savedCx = E.cx;
    int savedCy = E.cy;
    int savedColOff = E.colOff;
    int savedRowOff = E.rowOff;

    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)",
                               editorFindCallback);

    if (query) {
        free(query);
    } else {
        E.cx = savedCx;
        E.cy = savedCy;
        E.colOff = savedColOff;
        E.rowOff = savedRowOff;
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
            char *c = &E.row[fileRow].render[E.colOff];
            unsigned char *hl = &E.row[fileRow].hl[E.colOff];
            int currentColor = -1;
            int j;
            for (j = 0; j < len; j++) {
                if (iscntrl(c[j])) {
                    char sym = (c[j] <= 26 ? '@' + c[j] : '?');
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, &sym, 1);
                    abAppend(ab, "\x1b[m", 3);
                    if (currentColor != -1) {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", currentColor);
                        abAppend(ab, buf, clen);
                    }
                } else if (hl[j] == HL_NORMAL) {
                    if (currentColor != -1) {
                        abAppend(ab, "\x1b[39m", 5);
                        currentColor = -1;
                    }
                    abAppend(ab, &c[j], 1);
                } else {
                    int color = editorSyntaxToColor(hl[j]);
                    if (color != currentColor) {
                        currentColor = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, clen);
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
            abAppend(ab, "\x1b[39m", 5);
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
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                       E.filename ? E.filename : "[No Name]", E.numRows,
                       E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
                        E.syntax ? E.syntax->filetype : "no ft", E.cy + 1,
                        E.numRows);
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
 * Display a prompt to the user.
 *
 * param prompt: Text to display to the user.
 * param callback: A callback function for incremental searching.
 * return: User input.
 */
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while(1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();
        
        int c = editorReadKey();
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0)
                buf[--buflen] = '\0';
        } else if (c == '\x1b'){
            editorSetStatusMessage("");
            if (callback)
                callback(buf, c);
            free(buf);
            return NULL;
         } else if (c == '\r') {
            if (buflen != 0) {
                editorSetStatusMessage("");
                if (callback)
                    callback(buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }

        if (callback)
            callback(buf, c);
    }
}

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
    static int quit_times = KILO_QUIT_TIMES;

    int c = editorReadKey();

    switch (c) {
        case '\r':
            editorInsertNewLine();
            break;

        case CTRL_KEY('q'):
            if (E.dirty && quit_times > 0) {
                editorSetStatusMessage("Warning!!! File has unsaved "
                                       "changes. Press CTRL-Q %d more "
                                       "times to quit.", quit_times);
                quit_times--;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case CTRL_KEY('s'):
         editorSave();
         break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            if (E.cy < E.numRows)
                E.cx = E.row[E.cy].size;
            break;

        case CTRL_KEY('f'):
            editorFind();
            break;
    
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY)
                editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
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

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        default:
            editorInsertChar(c);
            break;
    }

    quit_times = KILO_QUIT_TIMES;
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
    E.dirty = 0;
    E.filename = NULL;
    E.statusMsg[0] = '\0';
    E.statusMsg_time = 0;
    E.syntax = NULL;

    if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
        die("initEditor: getWindowSize");
    E.screenRows -= 2;
}

int main(int argc, char **argv) {
    enableRawMode();
    initEditor();
    if (argc >= 2)
        editorOpen(argv[1]);

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
