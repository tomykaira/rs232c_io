// TODO: ボーレートを設定可能にする
//       入出力形式をえらべるようにする(ASCII, Hex...)
//       window を設定可能にする

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

#define max(x, y) ((x < y) ? y : x)

#define IO_ASCII 0

#define NO_CALLBACK 0
#define DO_CALLBACK 1

#define OPTSTRING "B:ah:c"

extern char *optarg;
extern int optind, opterr, optopt;

typedef struct _options {
  int baud_rate;
  int io_type;
  int callback;
} options;

int init_port(int fd, options *opts) {
  struct termios oldOptions, newOptions;
  int baud_rate = B460800;

  if (opts->baud_rate) {
    baud_rate = opts->baud_rate;
  }

  if (tcgetattr(fd, &oldOptions) < 0){
    close(fd);
    perror("cgetattr");
    return -1;
  }
  bzero(&newOptions, sizeof(newOptions));

  newOptions.c_cflag = baud_rate | CS8 | CLOCAL | CREAD;
  newOptions.c_iflag = IGNPAR;
  newOptions.c_oflag = 0;
  newOptions.c_lflag = 0;
  newOptions.c_cc[VTIME] = 0;
  newOptions.c_cc[VMIN] = 1;

  tcflush(fd, TCIFLUSH);
  if (tcsetattr(fd, TCSANOW, &newOptions) < 0){
    close(fd);
    perror("tcsetattr");
    return -1;
  }
  return 0;
}

int init_stdin() {
  struct termios ttystate;

  //get the terminal state
  tcgetattr(STDIN_FILENO, &ttystate);
  //turn off canonical mode and echo
  ttystate.c_lflag &= ~(ICANON | ECHO);
  //minimum of number input read.
  ttystate.c_cc[VMIN] = 1;

  //set the terminal attributes.
  return tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
}

int watch(int fdw, int fdr, options *opts) {
  fd_set rset, wset;
  int write_done = 0;
  int recv_cnt = 0;
  char io_format[10];
  char line[16];
  int pending = 0, line_ptr = 0;
  int sending_data = 0;

  switch(opts->io_type) {
  case 0:
    sprintf(io_format, "%%c");
    break;
  default:
    sprintf(io_format, "%%0%dx", opts->io_type*2);
    break;
  }

  while (fdw && fdr) {
    int ret = 0;
    unsigned char buf[16];
    int maxfd = 0;

    FD_ZERO(&rset);
    FD_ZERO(&wset);
    FD_SET(fdr, &rset);
    FD_SET(STDIN_FILENO, &rset);
    FD_SET(fdw, &wset);
    maxfd = max(fdr, fdw);

    ret = select(maxfd + 1, &rset, &wset, NULL, NULL);

    if (ret == - 1) {
      if (errno == EINTR)
        continue;
      perror("select");
      return - 1;
    }

    if (FD_ISSET(fdr, &rset)) {
      // 読み込み長は 1 に固定したほうがいい??
      switch ((ret = read(fdr, buf, 16))) {
      case - 1:
        if (errno == EINTR || errno == EAGAIN)
          continue;
        perror("read");
        return - 1;
      case 0:
        if (write_done == 1)
          return 0;
        break;
      default:
        for (int i = 0; i < ret; i++) {
          if (opts->io_type == 0) {
            printf("%c", buf[i]);
          } else {
            printf("%02x", buf[i]);
          }
          recv_cnt++;
          if (opts->io_type != IO_ASCII && (recv_cnt % 4) == 0) {
            printf("\n");
          }
          fflush(stdout);
        }
        break;
      }
    }

    if (! write_done) {
      if (pending) {
        if (FD_ISSET(fdw, &wset)) {
          int send_size = opts->io_type == 0 ? 1 : opts->io_type * sizeof(char);
          char sendbuf[4];
          if (opts->callback == DO_CALLBACK) {
            printf("> ");
            printf(io_format, sending_data);
            printf("(%d)", send_size);
            printf("\n");
          }
          for (int i = 0; i < send_size ; i ++) {
            sendbuf[send_size - 1 - i] = (sending_data >> 8*i) & 0xff;
          }
          write(fdw, sendbuf, send_size);
          pending = 0;
        }
      } else {
        if (FD_ISSET(STDIN_FILENO, &rset)) {
          ret = line[line_ptr++] = fgetc(stdin);
          if (ret == EOF) {
            write_done = 1;
          } else if (ret == '\n') {
            line[line_ptr] = '\0';
            sscanf(line, io_format, &sending_data);
            pending = 1;
            line_ptr = 0;
          }
        }
      }
    }
  }
  printf("\n");
  return 0;
}

int parse_options(int argc, char* argv[], options *opt) {
  int ret;
  opt->callback = NO_CALLBACK;
  while (1) {
    switch ((ret = getopt(argc, argv, OPTSTRING))) {
    case - 1:
      return 0;
    case ':':
      return -1;
    case '?':
      return -1;
    case 'B':
      fprintf(stderr, "baud_rate: %d\n", atoi(optarg));
      switch (atoi(optarg)) {
      case 9600:
        opt->baud_rate = B9600;
        break;
      case 230400:
        opt->baud_rate = B230400;
        break;
      case 460800:
        opt->baud_rate = B460800;
        break;
      case 921600:
        opt->baud_rate = B921600;
        break;
      }
      break;
    case 'a':
      opt->io_type = IO_ASCII;
      fprintf(stderr, "io: ascii\n");
      break;
    case 'h':
      opt->io_type = atoi(optarg);
      fprintf(stderr, "io: hex %d\n", opt->io_type);
      break;
    case 'c':
      opt->callback = DO_CALLBACK;
      fprintf(stderr, "callback: yes\n");
      break;
    default:
      fprintf(stderr, "Unknown option\n");
      return -1;
    }
  }
}

// Bi-directional RS232C communication program
int main(int argc, char* argv[]){
  int fdw = -1, fdr = -1;

  options opts;
  memset(&opts, 0, sizeof(options));

  if (parse_options(argc, argv, &opts) != 0) {
    return 1;
  }

  fprintf(stderr, "opening...\n");
  for (int i = 0; i < 3; ++i){
    char filename[16];
    //使用するUSBポートの選択。選び方が適当なので、正しく動かない可能性もある。
    sprintf(filename, "/dev/ttyUSB%d", i);
    fprintf(stderr, "  %s\n", filename);
    fdw = open(filename, O_WRONLY | O_NOCTTY);
    if (fdw < 0) {
      fprintf(stderr, "Failed to open %s\n", filename);
    } else {
      fdr = open(filename, O_RDONLY | O_NOCTTY);
      if (fdr > 0) {
        fprintf(stderr, "Successfully opened: %s\n", filename);
        break;
      } else {
        fprintf(stderr, "Failed to open %s\n", filename);
      }
    }
  }
  if (fdw < 0 || fdr < 0){
    return 1;
  }

  if (init_port(fdw, &opts) != 0
      || init_port(fdr, &opts) != 0
      || init_stdin() != 0) {
    return 1;
  }

  return watch(fdw, fdr, &opts);
}
