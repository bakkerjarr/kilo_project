/*************************************************************************
* Main file for the kilo terminal text editor.
* Visit http://viewsourcecode.org/snaptoken/kilo/ for the tutorial.
*
* FILENAME : kilo.c
*
* DESCRIPTION :
*       I'm up to chapter 3 step 27.
* 
* AUTHOR :  Jarrod N. Bakker    START DATE :    06/04/2016
*
**/

/*** includes ***/
#include<ctype.h>
#include<errno.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/ioctl.h>
#include<termios.h>
#include<unistd.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/

struct editorConfig {
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

/*** output ***/

/**
 * Draw tildes at the start of any line that is not part of a file.
 */
void editorDrawRows() {
    int y;
    for (y = 0; y < 24; y++)
        write(STDOUT_FILENO, "~\r\n", 3);
}

/**
 * Refresh the terminal screen.
 */
void editorRefreshScreen() {
    write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen
    write(STDOUT_FILENO, "\x1b[H", 3); // reposition cursor top-left

    editorDrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3); // reposition cursor top-left
}

/*** input ***/

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
    }
}

/*** init ***/

int main() {
    enableRawMode();

    while(1){
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
