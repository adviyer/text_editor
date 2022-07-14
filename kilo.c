/*** includes ***/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

/*** data ***/

struct termios orig_termios; //stores terminal settings

/*** terminal ***/

void die(const char *s) { //error handling
  perror(s); //prints error message string fed into it
  exit(1); //exits program
}

void disableRawMode() { //resets terminal settings to canonical mode
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) die("tcsetattr");
}

void enableRawMode() { // modifies terminal settings to 'raw mode'
  if(tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode); //call to reset settings when program exits

  struct termios raw = orig_termios;
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

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); //setting modified termina; attributes
}

/*** init ***/

int main() {
  enableRawMode();

  while (1) {
    char c = '\0';
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read"); //EAGAIN is an 'error' on some systems
    if (iscntrl(c)) { //checks whether input is a control character, i.e. non-printable
      printf("%d\r\n", c); //%d formats byte as decimal number i.e. ASCII code
    } else {
      printf("%d ('%c')\r\n", c, c); //%c returns byte directly as a character
    }
    if (c == 'q') break; //quits when 'q' is pressed
  }
  return 0;
}
