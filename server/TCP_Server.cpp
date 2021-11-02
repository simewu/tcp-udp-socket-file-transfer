// Simeon Wuthier
// CS 5220, 11/03/21

#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h >  // memset
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h >  // close
#include <stdlib.h >  // exit
using namespace std;

#define SERVER_PORT 2060
#define BUF_SIZE 4096 // block transfer size
#define QUEUE_SIZE 10

void fatal(string str)
{
  cout << "ERROR: " << str << endl;
  exit(1);
}

int main(int argc, char *argv[])
{
  int s, b, l, fd, sa, bytes, on = 1;
  // Buffer for outgoing file
  char buf[BUF_SIZE];
  // Hold's IP address
  struct sockaddr_in channel;

  // Build address structure to bind to socket.
  cout << "Creating channel..." << endl;
  memset(&channel, 0, sizeof(channel)); // zero channel
  channel.sin_family = AF_INET;
  channel.sin_addr.s_addr = htonl(INADDR_ANY);
  channel.sin_port = htons(SERVER_PORT);

  // Passive open. Wait for connection.
  cout << "Creating socket..." << endl;
  s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);  // Create socket
  if (s < 0) fatal("Failed to init socket");
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*) &on, sizeof(on));

  cout << "Binding socket..." << endl;
  b = bind(s, (struct sockaddr *) &channel, sizeof(channel));
  if (b < 0) fatal("Failed to bind socket");

  l = listen(s, QUEUE_SIZE);  // Specify queue size
  if (l < 0)
  {
    fatal("Failed to listen");
  }

  // Socket is now set up and bound. Wait for connection and process it.
  while (1)
  {
    cout << "\nListening..." << endl;
    sa = accept(s, 0, 0); // Block for connection request
    if (sa < 0) fatal("Failed to accept");

    read(sa, buf, BUF_SIZE);  // Read file name from socket
    cout << "Received file name request \"" << buf << "\"!" << endl;

    // Get and return the file.
    fd = open(buf, O_RDONLY); // Open the file to be sent back
    if (fd < 0) fatal("Failed to open file");

    while (1)
    {
      bytes = read(fd, buf, BUF_SIZE);  // Read from file
      if (bytes <= 0)
      {
        cout << "End of file reached." << endl;
        break;  // Check for end of file
      }
      write(sa, buf, bytes);  // Write bytes to socket
      cout << "Sent " << bytes << " bytes!" << endl;
    }
    close(fd);  // Close file
    close(sa);  // Close connection
    cout << "Connection completed successfully!" << endl;
  }
}