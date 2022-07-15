/*** includes ***/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h> //get size of terminal
#include <termios.h> //change terminal settings
#include <unistd.h>
#include <errno.h> //error handling

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f) //defining the bitwise operations on a character when ctrl key is pressed

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

struct editorConfig { //contains text editor state
  int cx, cy; //cursor position
  int screenrows; //number of rows in terminal
  int screencols; //number of columns in terminal
  struct termios orig_termios; //stores terminal settings
};

struct editorConfig E;

/*** terminal ***/

void die(const char *s) { //error handling
  //clears the screen and repositions cursor
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s); //prints error message string fed into it
  exit(1); //exits program
}

void disableRawMode() { //resets terminal settings to canonical mode
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

void enableRawMode() { // modifies terminal settings to 'raw mode'
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode); //call to reset settings when program exits

  struct termios raw = E.orig_termios;
  //turning off (IXON) ctrl-S (stop transmission) and ctrl-Q (resume transmission)
  //turning off (ICRNL) ctrl-M
  //also turning off other miscellaneous flags
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  //turning off terminal output processing i.e. "\n" won't get translated to
  //"\r\n"—carriage return followed by newline
  raw.c_oflag &= ~(OPOST);
  //turning off echo: inputs to terminal will not be printed
  //turning off icanon: inputs will be read byte-by-byte, not line-by-line
  //disabling (ISIG) ctrl-C, ctrl-Y and ctrl-Z signals
  //disabling (IEXTEN) ctrl-V
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0; //minimum number of bytes for read() to return
  raw.c_cc[VTIME] = 1; //maximum time to wait before read() returns (in 1/10 of seconds)

  //setting modified termina; attributes
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey() { //waits for a keypress and returns it
  int nread;
  char c;
  while ((nread = (read(STDIN_FILENO, &c, 1)) != 1)) {
    if (nread == -1 && errno != EAGAIN) die("read"); //checks for error
  }

  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
      // PAGE_UP represented as <esc>[5~ and PAGE_DOWN is sent as <esc>[6~.
        switch (seq[1]) {
          //The Home key could be sent as <esc>[1~, <esc>[7~, <esc>[H, or <esc>OH.
          //Similarly, the End key could be sent as <esc>[4~, <esc>[8~, <esc>[F, or <esc>OF
          case '1': return HOME_KEY;
          case '3': return DEL_KEY;
          case '4': return END_KEY;
          case '5': return PAGE_UP;
          case '6': return PAGE_DOWN;
          case '7': return HOME_KEY;
          case '8': return END_KEY;
        }
      }
    } else {
      switch (seq[1]) { //allows controlling with arrow keys, which return escape
      //sequence + 'A', 'B', 'C', 'D'
      case 'A': return ARROW_UP;
      case 'B': return ARROW_DOWN;
      case 'C': return ARROW_RIGHT;
      case 'D': return ARROW_LEFT;
      case 'H': return HOME_KEY;
      case 'F': return END_KEY;
    }
  }
} else if (seq[0] == 'O') {
  switch (seq[1]) {
    case 'H': return HOME_KEY;
    case 'F': return END_KEY;
  }
}
return '\x1b';
} else {
return c;
}
}

int getCursorPosition(int *rows, int *cols) { //prints escape sequence followed by cursor position
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') return -1; //ensures escape sequence
  //sscanf conveys format for parsing string into 2 integers
  //seperated by ; into rows and columns
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) { //gives us no. rows and columns in terminal
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    //C command moves cursor to right, B moves it down, 999 is large enough to ensure
    //bottom right, while also being documented to not go past the edge of the xcreen
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    editorReadKey(); //moving cursor to bottom right
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** append buffer ***/

//includes all tildes to be printed out at once instead of one at a time
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0} //empty buffer, constructor for abuf

//append characters to the buffer
void abAppend(struct abuf *ab, const char *s, int len) {

  //allocates enough memory for new 'string'
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL) return;

  //copy buffer + character to new string
  memcpy(&new[ab->len], s, len);

  //updates values of buffer pointer and length
  ab->b = new;
  ab->len += len;
}

//destructor that deallocates dynamic memory used by a buffer
void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** output ***/

void editorDrawRows(struct abuf *ab) { //draws a column of tildes according to terminal size
  int y;
  for (y = 0; y < E.screenrows; y++) {
    if (y == E.screenrows / 3) {
    //printing welcome message a third of the way down at the center
    char welcome[80];
    int welcomelen = snprintf(welcome, sizeof(welcome),
    "Kilo editor -- version %s", KILO_VERSION);
    if (welcomelen > E.screencols) welcomelen = E.screencols;
    int padding = (E.screencols - welcomelen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
        }
      while (padding--) abAppend(ab, " ", 1);
      abAppend(ab, welcome, welcomelen);
    } else {
    abAppend(ab, "~", 1);
    }
    //clears the part of the line to the right of the cursor
    abAppend(ab, "\x1b[K", 3);

  //avoid printing out blank line if it's the last terminal line
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT; //default buffer constructor
  abAppend(&ab, "\x1b[?25l", 6); //hide cursor
//4 means we are writing out 4 bytes, first is \x1b, the escape character (27) in decimal
//other 3 bytes are [2J
//writing an escape sequence, which always starts with escape + '['
//can be used for things like colouring text, moving stuff around
//the J command clears up the screen (https://vt100.net/docs/vt100-ug/chapter3.html#ED)
//0, 1, and 2 decide which position wrt the cursor to clear
//repositioning cursor to top of the screen, H takes row and column no. as args
//1, 1 by default
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  //repositioning cursor to top of screen
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6); //show cursor

  //printing out entire column of tildes in buffer
  write(STDOUT_FILENO, ab.b, ab.len);

  //freeing up dynamic memory
  abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key) { //determines cursor movement
  switch (key) {
    case ARROW_LEFT:
    if (E.cx != 0) {
      E.cx--;
    }
      break;
    case ARROW_RIGHT:
    if (E.cx != E.screencols - 1) {
      E.cx++;
    }
      break;
    case ARROW_UP:
    if (E.cy != 0) {
      E.cy--;
    }
      break;
    case ARROW_DOWN:
    if (E.cy != E.screenrows - 1) {
       E.cy++;
     }
      break;
  }
}

void editorProcessKeypress() { //waits for a keypress and then handles it
  int c = editorReadKey();
  //if-else statement routing, matches cases and executes corresponding statements
  switch (c) {
    case CTRL_KEY('q'):
    //clearing the screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    case HOME_KEY:
      E.cx = 0; //move to left edge
      break;

    case END_KEY:
      E.cx = E.screencols - 1; //move to right edge
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        int times = E.screenrows;
        while (times--) //moving cursor to top or bottom
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;
    //special cases for moving cursor
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
  }
}

/*** init ***/

void initEditor() { //initialises terminal window size, checks for error
  E.cx = 0;
  E.cy = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
  enableRawMode();
  initEditor();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
