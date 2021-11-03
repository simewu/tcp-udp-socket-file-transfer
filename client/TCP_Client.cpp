// Simeon Wuthier
// CS 5220, 11/03/21

#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>  // memset
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>  // close
#include <stdlib.h>  // exit
using namespace std;

#define SERVER_PORT 2060  // arbitrary, but client and server must agree
#define BUF_SIZE 4096 // block transfer size

void fatal(char *str) {
  cout << "ERROR: " << str << endl;
  exit(1);
}

// Arg1: Hostname
// Arg2: Filename

int main(int argc, char **argv) {
  char *fileName = argv[2];
  FILE *fp = fopen(fileName, "w");

  int c, s, bytes;
  // buffer for incoming file
  char buf[BUF_SIZE];
  // info about server
  struct hostent * h;
  // holds IP address
  struct sockaddr_in channel;

  if (argc != 3) {
    fatal("Usage: client server-name file-name");
  }

  h = gethostbyname(argv[1]); // look up host's IP address
  if (!h) {
    fatal("gethostbyname failed");
  }

  cout << "Creating socket..." << endl;
  s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s < 0) {
    fatal("Failed to init socket");
  }

  cout << "Creating channel..." << endl;
  memset(&channel, 0, sizeof(channel));
  channel.sin_family = AF_INET;
  memcpy(&channel.sin_addr.s_addr, h->h_addr, h->h_length);
  channel.sin_port = htons(SERVER_PORT);

  cout << "Connecting..." << endl;
  c = connect(s, (struct sockaddr *) &channel, sizeof(channel));
  if (c < 0) fatal("Failed to connect");

  cout << "Sending file name \"" << fileName << "\"..." << endl;
  write(s, argv[2], strlen(fileName) + 1);

  // Go get the file and write it to standard output.
  while (1) {
    bytes = read(s, buf, BUF_SIZE); // read from socket

    cout << "Received " << bytes << " bytes!" << endl;

    if (bytes <= 0) {
      cout << "End of file reached." << endl;
      break;
    }

    // Append the bytes to the file
    fwrite(buf, 1, BUF_SIZE, fp);
  }

  cout << "Goodbye." << endl;
}