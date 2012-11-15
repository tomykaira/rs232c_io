#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <iostream>
#include <string>

using namespace std;

extern char *optarg;
extern int optind, opterr, optopt;

#define IO_ASCII 0

class Option {
public:
  int baud_rate;
  int io_type;
  bool callback;
  bool blocking;
  string program_file;

  Option() {
    baud_rate = B460800;
    io_type = IO_ASCII;
    callback = false;
    blocking = false;
    program_file = string();
  }

  int read_option(int argc, char* argv[]);

};
