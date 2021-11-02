// Simeon Wuthier
// CS 5220, 11/03/21
#include <chrono>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdlib> // rand()
using namespace std;

#define SERVER_UDP_PORT 2060    // 2 + Student ID
#define MAXLEN 4096             // Size in bytes per frame
#define FILENAME_SIZE 256       // File names greater than this are not allowed
#define WINDOWSIZE 10           // Number of frames in the window
#define FRAMETIMEOUT 100        // Milliseconds to wait until re-sending frame

int sd;
struct sockaddr_in server_addr, client_addr;
socklen_t server_len, client_len;

bool *windowReceivedLog;
int maxBufferSize, lastAckedFrame, lastFrameSent;

chrono::high_resolution_clock::time_point startTime, *windowSentTimeLog;
mutex windowModificationMutex;

void fatal(string str)
{
    cout << "ERROR: " << str << endl;
    exit(1);
}

// lossProbability = 0% then no frames are dropped
// lossProbability = 100% then all frames are dropped
bool isFrameDropped(int lossProbability)
{
    int r = 1 + rand() % 100; // 1 to 100
    return r <= lossProbability;
}

bool readAck(int *seqNum, bool *error, char *ack);
void handleAckMessages();
int createFrame(bool endOfTransmission, int seqNum, char *frame, char *data, int dataSize);

char checksum(char *frame, int count)
{
    u_long sum = 0;
    while (count--)
    {
        sum += *frame++;
        if (sum & 0xFFFF0000)
        {
            sum &= 0xFFFF;
            sum++;
        }
    }
    return (sum & 0xFFFF);
}






// Server-side needs: UDP_Server lossProbability protocolType
// protocolType = 1 for lossProbability (1 to 100)
// protocolType = 2 for protocolType (1 for ARQ stop-and-wait, 2 for ARQ selective repeat)
int main(int argc, char *argv[])
{

    if (argc != 3) fatal("Usage: \"UDP_Server lossProbability protocolType\"");

    int lossProbability = stoi(argv[1]);
    int protocolType = stoi(argv[2]);


    if(protocolType == 1) {
        cout << endl;
        cout << endl;
        cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" << endl;
        cout << "@@@@@            ARQ STOP-AND-WAIT           @@@@@" << endl;
        cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" << endl;
        cout << "@@@@@            by Simeon Wuthier           @@@@@" << endl;
        cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" << endl;
        cout << endl;
    } else if(protocolType == 2) {
        cout << endl;
        cout << endl;
        cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" << endl;
        cout << "@@@@@          ARQ SELECTIVE REPEAT          @@@@@" << endl;
        cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" << endl;
        cout << "@@@@@            by Simeon Wuthier           @@@@@" << endl;
        cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" << endl;
        cout << endl;
    }


    maxBufferSize = MAXLEN * WINDOWSIZE;

    cout << "Creating socket..." << endl;
    if ((sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        fatal("Can't create a socket");
    }

    // Zero out the server
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));
    //bzero((char*)&server_addr, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_UDP_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    cout << "Binding name to socket..." << endl;
    if (bind(sd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1)
    {
        fatal("Can't bind name to socket");
    }










    /*
     *  ARQ Stop-and-Wait
     */
    if(protocolType == 1) {

        while (true)
        {
            client_len = sizeof(client_addr);
            cout << "\nListening..." << endl;
            //windowModificationMutex.lock();
            char fileNameRaw[FILENAME_SIZE];
            int fileNameLen = 0;
            while(fileNameLen <= 0) {
                fileNameLen = recvfrom(sd, (char*) fileNameRaw, FILENAME_SIZE, MSG_WAITALL, (struct sockaddr *) &client_addr, &client_len);
            }

            char fileName[fileNameLen];
            cout << "Copying first " << fileNameLen << " byes of \"" << fileNameRaw << "\" into the file name array..." << endl;
            strncpy(fileName, fileNameRaw, fileNameLen);
            fileName[fileNameLen] = '\0';

            cout << "Received file for reading: \"" << fileName << "\"" << endl;

            if (access(fileName, F_OK) == -1)
            {
                cout << "Error 404, file not found" << endl;
                continue;   // Don't terminate the server
            }

            // Open file to send
            FILE *file = fopen(fileName, "rb");
            

            // Timeout after X milliseconds
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = FRAMETIMEOUT * 1000; // Milliseconds to microseconds
            setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);


            char frame[MAXLEN + 10];    // 10 for the checksum
            char ack[2];
            int seqNum = 0, receivedSeqNum, totalBytes = 0;
            char data[MAXLEN];
            bool ackNeg;

            // Send file
            bool isSending = true;
            while (isSending)
            {
                int bytesReadFromFile = fread(data, 1, MAXLEN, file);

                if (bytesReadFromFile < MAXLEN)
                {
                    isSending = false;
                }
                else if (bytesReadFromFile == MAXLEN)
                {
                    // Read the next byte and check if it's EOF
                    char temp[1];
                    if (fread(temp, 1, 1, file) == 0)
                    {
                        isSending = false;
                    }
                    int error = fseek(file, -1, SEEK_CUR);
                }
                totalBytes += bytesReadFromFile;

                int frame_size = createFrame(!isSending, seqNum, frame, data, bytesReadFromFile);
                
                bool isAwaitingResponse = true;
                while(isAwaitingResponse) {

                    // Handle frame dropping in a lossy network
                    if(!isFrameDropped(lossProbability)) {
                        cout << "Sending frame #" << seqNum << " from bytes:[" << (totalBytes - bytesReadFromFile) << " to " << (totalBytes - 1) << "]" << endl;
                        sendto(sd, frame, frame_size, 0, (const struct sockaddr *) &client_addr, client_len);   //sizeof(client_addr));
                    } else {
                        cout << "Dropping frame #" << seqNum << " from bytes:[" << (totalBytes - bytesReadFromFile) << " to " << (totalBytes - 1) << "]" << endl;
                    }

                    int ackSize = recvfrom(sd, (char*) ack, 2, MSG_WAITALL, (struct sockaddr *) &client_addr, &client_len);
                    bool ackError = true;
                    if(ackSize > 0) {
                        ackError = readAck(&receivedSeqNum, &ackNeg, ack);
                    }
                    if(!ackError && !ackNeg) {
                        // Success!
                        isAwaitingResponse = false;
                    }
                }

                seqNum++;
            }
            cout << totalBytes << " bytes sent" << endl;
            cout << "Transmission complete." << endl;
            
        }












    /*
     *  ARQ Selective Repeat
     */
    } else if(protocolType == 2) {

        startTime = chrono::high_resolution_clock::now();
        while (true)
        {
            client_len = sizeof(client_addr);
            cout << "\nListening..." << endl;
            //windowModificationMutex.lock();
            char fileNameRaw[FILENAME_SIZE];
            int fileNameLen = recvfrom(sd, (char*) fileNameRaw, FILENAME_SIZE, MSG_WAITALL, (struct sockaddr *) &client_addr, &client_len);
            if (fileNameLen <= 0)
            {
                cout << "Got an invalid file name" << endl;
                continue;   // Don't terminate the server
            }

            char fileName[fileNameLen];
            cout << "Copying first " << fileNameLen << " byes of \"" << fileNameRaw << "\" into the file name array..." << endl;
            strncpy(fileName, fileNameRaw, fileNameLen);
            fileName[fileNameLen] = '\0';

            cout << "Received file for reading: \"" << fileName << "\"" << endl;

            if (access(fileName, F_OK) == -1)
            {
                fatal("Error 404, file not found");
                continue;   // Don't terminate the server
            }

            // Open file to send
            FILE *file = fopen(fileName, "rb");
            char buffer[maxBufferSize];
            int bufferSize;

            // Start thread to listen for ack
            thread recv_thread(handleAckMessages);

            char frame[MAXLEN + 10];    // 10 for the checksum
            char data[MAXLEN];
            int frame_size;
            int dataSize;

            // Send file
            bool isSending = true;
            int bufferNum = 0;
            while (isSending)
            {

                cout << "Reading to buffer" << endl;

                // Read part of file to buffer
                bufferSize = fread(buffer, 1, maxBufferSize, file);
                if (bufferSize < maxBufferSize)
                {
                    isSending = false;
                }
                else if (bufferSize == maxBufferSize)
                {
                    // Read the next byte and check if it's EOF
                    char temp[1];
                    if (fread(temp, 1, 1, file) == 0)
                    {
                        isSending = false;
                    }
                    int error = fseek(file, -1, SEEK_CUR);
                }
                windowModificationMutex.lock();

                // Initialize sliding window variables
                int seqCount = bufferSize / MAXLEN + ((bufferSize % MAXLEN == 0) ? 0 : 1);
                int seqNum;
                windowSentTimeLog = new chrono::high_resolution_clock::time_point[WINDOWSIZE];
                windowReceivedLog = new bool[WINDOWSIZE];
                bool windowSentLog[WINDOWSIZE];
                for (int i = 0; i < WINDOWSIZE; i++)
                {
                    windowReceivedLog[i] = false;
                    windowSentLog[i] = false;
                }
                lastAckedFrame = -1;
                lastFrameSent = lastAckedFrame + WINDOWSIZE;
                windowModificationMutex.unlock();

                // Send current buffer with sliding window
                bool isSendDone = false;
                while (!isSendDone)
                {

                    windowModificationMutex.lock();
                    // If the first ack was received, attempt to shift the frame
                    if (windowReceivedLog[0])
                    {
                        int shift = 1;
                        for (int i = 1; i < WINDOWSIZE; i++)
                        {
                            if (!windowReceivedLog[i]) break;
                            shift += 1;
                        }
                        for (int i = 0; i < WINDOWSIZE - shift; i++)
                        {
                            windowSentLog[i] = windowSentLog[i + shift];
                            windowReceivedLog[i] = windowReceivedLog[i + shift];
                            windowSentTimeLog[i] = windowSentTimeLog[i + shift];
                        }
                        for (int i = WINDOWSIZE - shift; i < WINDOWSIZE; i++)
                        {
                            windowSentLog[i] = false;
                            windowReceivedLog[i] = false;
                        }
                        lastAckedFrame += shift;
                        lastFrameSent = lastAckedFrame + WINDOWSIZE;
                    }
                    windowModificationMutex.unlock();
                    //this_thread::sleep_for(chrono::milliseconds(200));

                    // Send frames that has not been sent or has timed out
                    for (int i = 0; i < WINDOWSIZE; i++)
                    {
                        seqNum = lastAckedFrame + i + 1;

                        if (seqNum < seqCount)
                        {
                            windowModificationMutex.lock();

                            int elapsedTime = chrono::duration_cast<chrono::milliseconds > (chrono::high_resolution_clock::now() - windowSentTimeLog[i]).count();
                            if (!windowSentLog[i] || (!windowReceivedLog[i] && (elapsedTime > FRAMETIMEOUT)))
                            {
                                int bufferOffset = seqNum * MAXLEN;
                                dataSize = (bufferSize - bufferOffset < MAXLEN) ? (bufferSize - bufferOffset) : MAXLEN;
                                memcpy(data, buffer + bufferOffset, dataSize);

                                // Determine the end of transmission
                                bool endOfTransmission = (seqNum == seqCount - 1) && (!isSending);


                                frame_size = createFrame(endOfTransmission, seqNum, frame, data, dataSize);
                                
                                // Handle frame dropping in a lossy network
                                if(!isFrameDropped(lossProbability)) {
                                    cout << "Sending frame #" << bufferNum *WINDOWSIZE + seqNum << " from bytes:[" << bufferOffset << " to " << (bufferOffset + dataSize - 1) << "]" << endl;
                                    sendto(sd, frame, frame_size, 0, (const struct sockaddr *) &client_addr, client_len);   //sizeof(client_addr));
                                } else {
                                    cout << "Dropping frame #" << bufferNum *WINDOWSIZE + seqNum << " from bytes:[" << bufferOffset << " to " << (bufferOffset + dataSize - 1) << "]" << endl;
                                }

                                windowSentLog[i] = true;
                                windowSentTimeLog[i] = chrono::high_resolution_clock::now();

                                // Print the ACK frames that were received
                                cout << "    ACK status: (";
                                for (int i = 0; i < WINDOWSIZE; i++)
                                {
                                    if (windowReceivedLog[i]) cout << "V ";
                                    else cout << "- ";
                                }
                                cout << ")" << endl;
                            }

                            windowModificationMutex.unlock();
                        }
                    }

                    // Move to next buffer if all frames in current buffer has been acked
                    if (lastAckedFrame >= seqCount - 1) isSendDone = true;
                }
                bufferNum += 1;
                if (!isSending) break;
            }

            cout << bufferNum *maxBufferSize + bufferSize << " bytes sent" << endl;
            cout << "Transmission complete." << endl;

            fclose(file);
            delete[] windowReceivedLog;
            delete[] windowSentTimeLog;
            recv_thread.detach();
        }
    }

    cout << "Goodbye." << endl;
    return 0;
}

bool readAck(int *seqNum, bool *error, char *ack)
{
    *error = ack[0] == 0 ? true : false;
    char net_seq_num;
    memcpy(&net_seq_num, ack + 1, 1);
    *seqNum = net_seq_num;  //ntohl(net_seq_num);
    //cout << "ACK: " << (int)ack[0] << (int)ack[1] << endl;
    return false;
}

void handleAckMessages()
{
    char ack[2];
    bool ackError;
    bool ackNeg;
    int ackSeqNum;
    int ackSize;

    // Listen for ack from reciever
    while (true)
    {
        ackSize = recvfrom(sd, (char*) ack, 2, MSG_WAITALL, (struct sockaddr *) &client_addr, &client_len);
        ackError = readAck(&ackSeqNum, &ackNeg, ack);

        windowModificationMutex.lock();
        if (!ackError && ackSeqNum > lastAckedFrame && ackSeqNum <= lastFrameSent)
        {
            if (!ackNeg)
            {
                //cout << "Received ACK" << endl;
                windowReceivedLog[ackSeqNum - (lastAckedFrame + 1)] = true;
            }
            else
            {
                //cout << "Received NACK" << endl;
                windowSentTimeLog[ackSeqNum - (lastAckedFrame + 1)] = startTime;
            }
        }
        windowModificationMutex.unlock();
    }
}

int createFrame(bool endOfTransmission, int seqNum, char *frame, char *data, int dataSize)
{
    frame[0] = endOfTransmission ? 0x0 : 0x1;
    uint32_t net_seq_num = htonl(seqNum);
    uint32_t net_data_size = htonl(dataSize);
    memcpy(frame + 1, &net_seq_num, 4);
    memcpy(frame + 5, &net_data_size, 4);
    memcpy(frame + 9, data, dataSize);
    frame[dataSize + 9] = checksum(frame, dataSize + (int) 9);
    //cout << "EOT: " << endOfTransmission << " Data: " << data << " Data Size: " << dataSize << endl;
    return dataSize + (int) 10;
}