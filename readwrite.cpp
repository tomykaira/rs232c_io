// TODO: ボーレートを設定可能にする
//       入出力形式をえらべるようにする(ASCII, Hex...)
//       window を設定可能にする
#include "readwrite.h"


#define max(x, y) ((x < y) ? y : x)
#define toggle_endian(data) ((data << 24) | ((data << 8) & 0x00ff0000) | ((data >> 8) & 0x0000ff00) | ((data >> 24) & 0x000000ff))

int init_port(int fd, int baud_rate) {
  struct termios oldOptions, newOptions;

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

int init_async_stdin() {
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

int watch(int fdw, int fdr, Option *opts) {
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
    if (! opts->blocking) {
      FD_SET(STDIN_FILENO, &rset);
    }
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

    if (! opts->blocking && ! write_done) {
      if (pending) {
        if (FD_ISSET(fdw, &wset)) {
          int send_size = opts->io_type == 0 ? 1 : opts->io_type * sizeof(char);
          char sendbuf[4];
          if (opts->callback) {
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

    if (opts->blocking && FD_ISSET(fdw, &wset) && write_done == 0) {
      int val = 0;

      // 標準入力を使用
      // ここで IO 待ちが発生する可能性もある
      ret = scanf(io_format, &val);
      if (ret == EOF) {
        write_done = 1;
      } else {
        if (opts->callback) {
          printf("> ");
          printf(io_format, val);
          printf("\n");
        }
        write(fdw, &val,
              opts->io_type == 0 ? 1 : opts->io_type * sizeof(char));
      }
    }
  }
  printf("\n");
  return 0;
}

int send_program(const char * program_file, int fdw) {
  FILE *program_fp;

  if (!program_file) return 0;

  program_fp = fopen(program_file, "r");
  if (program_fp == NULL) {
    perror("fopen");
    cerr << "Error: failed to read " << program_file << " as the program file" << endl;
    return 1;
  }

  cerr << "Start to send file " << program_file << endl;

  unsigned int inst;
  while(fscanf(program_fp, "%x", &inst) != EOF) {
    // program data is big-endian, only here.
    inst = toggle_endian(inst);
    if (write(fdw, &inst, 4) != 4) {
      perror("write fdw");
      return 1;
    }
  }

  int end_marker = 0xffffffff;
  if (write(fdw, &end_marker, 4) != 4) {
    perror("write fdw");
    return 1;
  }

  cerr << "Send file done" << endl;

  return 0;
}

// Bi-directional RS232C communication program
int main(int argc, char* argv[]){
  int fdw = -1, fdr = -1;

  Option opts = Option();
  if (opts.read_option(argc, argv) != 0) {
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
    } else if (opts.no_read) {
      break;
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
  if (fdw < 0 || (!opts.no_read && fdr < 0)){
    return 1;
  }

  if (init_port(fdw, opts.baud_rate) != 0) {
  }

  if (!opts.no_read && init_port(fdr, opts.baud_rate) != 0) {
    return 1;
  }

  if (send_program(opts.program_file, fdw)) {
    return 1;
  }

  if (opts.no_read) {
    return 0;
  }

  if (! opts.blocking) {
    if (init_async_stdin() != 0) {
      return 1;
    }
  }

  return watch(fdw, fdr, &opts);
}
