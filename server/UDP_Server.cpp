// Simeon Wuthier
// CS 5220, 11/08/21

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
using namespace std;

#define PORT 2060               // 2 + Student ID
#define MAX_FRAME_LENGTH 4096   // Size in bytes per frame
#define FILENAME_SIZE 256       // File names greater than this are not allowed
#define FRAME_TIMEOUT 100       // Milliseconds to wait until re-sending frame
#define WINDOW_SIZE 12          // Number of frames in the window

int sock; // Socket descriptor
struct sockaddr_in server, client;
socklen_t clientLength;

// Window of received acks
bool *windowReceivedLog;
// Keep the window safe from race conditions when multithreading
mutex windowModificationMutex;
int maxBufferSize, lastAckedFrame, lastFrameSent;
// Track the timestamps for frames and acks
chrono::high_resolution_clock::time_point startTime, *windowSentTimeLog;

// Upon fatal error, gracefully terminate with a message
void fatal(string str) {
    cout << "ERROR: " << str << endl;
    exit(1);
}

// lossProbability = 0% then no frames are dropped
// lossProbability = 100% then all frames are dropped
bool isFrameDropped(int lossProbability) {
    int r = 1 + rand() % 100; // 1 to 100
    return r <= lossProbability;
}

// Listen for acks (needs to be multithreaded with mutex locks over the AckLogs)
void asyncListenForAcksThenRespond();
// Encode an Ack into two bytes
void serializeAck(int seqNum, char *ack, bool error);
// Encode the data for packing into a frame
int serializeFrame(bool endOfTransmission, int seqNum, char *frame, char *data, int dataSize);
// Decode an Ack from two bytes
bool deserializeAck(int *seqNum, bool *error, char *ack);

// Given an array, compute the 4-byte CRC checksum
char computeCrcChecksum(char *buf, int len) {
    int crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= (int) buf[i] & 0xFF;     // XOR byte into least sig. byte of crc
        for (int i = 8; i != 0; i--) {  // Loop over each bit
            if ((crc & 0x0001) != 0) {  // If the LSB is set
                crc >>= 1;              // Shift right and XOR 0xA001
                crc ^= 0xA001;
            } else {                    // Else LSB is not set
                crc >>= 1;              // Just shift right
            }
        }
    }
    // Note, this number has low and high bytes swapped, so use it accordingly (or swap bytes)
    return crc & 0xFFFFFFFF;
}

// Listen for a file name, then send an ACK back: 'K'
void receiveFileName(char (&fileName)[FILENAME_SIZE]) {
    // Receive the file's name
    char fileNameRaw[FILENAME_SIZE];
    int fileNameLen = 0;
    while(fileNameLen <= 0) {
        fileNameLen = recvfrom(sock, (char*) fileNameRaw, FILENAME_SIZE, MSG_WAITALL, (struct sockaddr *) &client, &clientLength);
    }
    cout << "Copying first " << fileNameLen << " byes of \"" << fileNameRaw << "\" into the file name array..." << endl;
    strncpy(fileName, fileNameRaw, fileNameLen);
    fileName[fileNameLen] = '\0';
    char ack[1] = {'K'};
    sendto(sock, ack, 2, 0, (const struct sockaddr *) &client, clientLength);
}

void sendFileNotFound() {
    char ack[1] = {'N'};
    sendto(sock, ack, 2, 0, (const struct sockaddr *) &client, clientLength);
}



// Server-side needs: UDP_Server lossProbability protocolType
// protocolType = 1 for lossProbability (1 to 100)
// protocolType = 2 for protocolType (1 for ARQ stop-and-wait, 2 for ARQ selective repeat)
int main(int argc, char *argv[]) {
    int numFramesTotal = 0;
    int numFramesDropped = 0;

    if (argc != 3) fatal("Usage: \"UDP_Server lossProbability protocolType\"");

    int lossProbability = stoi(argv[1]);
    int protocolType = stoi(argv[2]);


    if(protocolType == 1) {
        cout << endl;
        cout << endl;
        cout << "==================================================" << endl;
        cout << "=====            ARQ STOP-AND-WAIT           =====" << endl;
        cout << "==================================================" << endl;
        cout << "=====            by Simeon Wuthier           =====" << endl;
        cout << "==================================================" << endl;
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


    maxBufferSize = MAX_FRAME_LENGTH * WINDOW_SIZE;

    cout << "Creating socket..." << endl;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        fatal("Can't create a socket");
    }

    // Zero out the server
    memset(&server, 0, sizeof(server));
    memset(&client, 0, sizeof(client));
    //bzero((char*)&server, sizeof(server));

    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = htonl(INADDR_ANY);

    cout << "Binding name to socket..." << endl;
    if (bind(sock, (struct sockaddr *) &server, sizeof(server)) == -1) {
        fatal("Can't bind name to socket");
    }
    // Connection established








    /*
     *  ARQ Stop-and-Wait
     */
    if(protocolType == 1) {

        while (true) {
            // Clear the client to allow new requests
            memset(&client, 0, sizeof(client));
            clientLength = sizeof(client);
            cout << "\nListening..." << endl;

            char fileName[FILENAME_SIZE];
            receiveFileName(fileName);
            cout << "Received file name for reading: \"" << fileName << "\"" << endl;

            if (access(fileName, F_OK) == -1) {
                cout << "Error 404, file not found" << endl;
                sendFileNotFound();
                continue; // Don't terminate the server
            }

            // Open file to send
            FILE *file = fopen(fileName, "rb");
            

            // Force recvfrom to terminate after FRAME_TIMEOUT milliseconds
            struct timeval readTimeout;
            readTimeout.tv_sec = 0;
            readTimeout.tv_usec = FRAME_TIMEOUT * 1000; // Milliseconds to microseconds
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&readTimeout, sizeof readTimeout);


            char frame[MAX_FRAME_LENGTH + 10]; // 10 for the checksum
            char ack[2];
            int seqNum = 0, receivedSeqNum, totalBytes = 0;
            char data[MAX_FRAME_LENGTH];
            bool ackNeg;

            // Send file
            bool isSending = true;
            while (isSending) {
                int bytesReadFromFile = fread(data, 1, MAX_FRAME_LENGTH, file);

                if (bytesReadFromFile < MAX_FRAME_LENGTH) {
                    isSending = false;
                } else if (bytesReadFromFile == MAX_FRAME_LENGTH) {
                    // Read the next byte and check if it's EOF
                    char temp[1];
                    if (fread(temp, 1, 1, file) == 0) {
                        isSending = false;
                    }
                    int error = fseek(file, -1, SEEK_CUR);
                }
                totalBytes += bytesReadFromFile;

                int frameSize = serializeFrame(!isSending, seqNum, frame, data, bytesReadFromFile);
                
                bool isAwaitingResponse = true;
                while(isAwaitingResponse) {

                    // Handle frame dropping in a lossy network
                    if(!isFrameDropped(lossProbability)) {
                        cout << "Sending frame #" << seqNum << " from bytes [" << (totalBytes - bytesReadFromFile) << " to " << (totalBytes - 1) << "]" << endl;
                        sendto(sock, frame, frameSize, 0, (const struct sockaddr *) &client, clientLength);   //sizeof(client));
                    } else {
                        cout << "Dropping frame #" << seqNum << " from bytes [" << (totalBytes - bytesReadFromFile) << " to " << (totalBytes - 1) << "]" << endl;
                        numFramesDropped++;
                    }
                    numFramesTotal++;

                    int ackSize = recvfrom(sock, (char*) ack, 2, MSG_WAITALL, (struct sockaddr *) &client, &clientLength);
                    bool ackError = true;
                    if(ackSize > 0) {
                        ackError = !deserializeAck(&receivedSeqNum, &ackNeg, ack);
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
        while (true) {
            // Clear the client to allow new requests
            memset(&client, 0, sizeof(client));
            clientLength = sizeof(client);
            cout << "\nListening..." << endl;
            
            char fileName[FILENAME_SIZE];
            receiveFileName(fileName);
            cout << "Received file name for reading: \"" << fileName << "\"" << endl;

            if (access(fileName, F_OK) == -1) {
                fatal("Error 404, file not found");
                continue;   // Don't terminate the server
            }

            // Open file to send
            FILE *file = fopen(fileName, "rb");

            char buffer[maxBufferSize];
            int bufferSize;

            // Start thread to listen for ack
            thread ackReceiver(asyncListenForAcksThenRespond);

            char frame[MAX_FRAME_LENGTH + 10];    // 10 for the checksum
            char data[MAX_FRAME_LENGTH];
            int frameSize;
            int dataSize;

            // Send file
            bool isSending = true;
            int bufferNum = 0;
            while (isSending) {

                // Read part of file to buffer
                bufferSize = fread(buffer, 1, maxBufferSize, file);
                cout << "Read " << bufferSize << " bytes from file.";
                if (bufferSize < maxBufferSize) {
                    isSending = false;
                } else if (bufferSize == maxBufferSize) {
                    // Read the next byte and check if it's EOF
                    char temp[1];
                    if (fread(temp, 1, 1, file) == 0) {
                        isSending = false;
                    }
                    int error = fseek(file, -1, SEEK_CUR);
                }
                windowModificationMutex.lock();

                // Initialize sliding window variables
                int seqCount = bufferSize / MAX_FRAME_LENGTH + ((bufferSize % MAX_FRAME_LENGTH == 0) ? 0 : 1);
                int seqNum;
                windowSentTimeLog = new chrono::high_resolution_clock::time_point[WINDOW_SIZE];
                windowReceivedLog = new bool[WINDOW_SIZE];
                bool windowSentLog[WINDOW_SIZE];
                for (int i = 0; i < WINDOW_SIZE; i++) {
                    windowReceivedLog[i] = false;
                    windowSentLog[i] = false;
                }
                lastAckedFrame = -1;
                lastFrameSent = lastAckedFrame + WINDOW_SIZE;
                windowModificationMutex.unlock();

                // Send current buffer with sliding window
                bool isSendDone = false;
                while (!isSendDone) {

                    windowModificationMutex.lock();
                    // If the first ack was received, attempt to shift the frame
                    if (windowReceivedLog[0]) {
                        // Print the ACK frames that were received
                        cout << "    ACK window status: (";
                        for (int i = 0; i < WINDOW_SIZE; i++) {
                            if (windowReceivedLog[i]) cout << "V ";
                            else cout << "- ";
                        }
                        cout << ")" << endl;


                        int shift = 1;
                        for (int i = 1; i < WINDOW_SIZE; i++) {
                            if (!windowReceivedLog[i]) break;
                            shift += 1;
                        }
                        cout << "        Shifting window right " << shift << " bytes" << endl;
                        for (int i = 0; i < WINDOW_SIZE - shift; i++) {
                            windowSentLog[i] = windowSentLog[i + shift];
                            windowReceivedLog[i] = windowReceivedLog[i + shift];
                            windowSentTimeLog[i] = windowSentTimeLog[i + shift];
                        }
                        for (int i = WINDOW_SIZE - shift; i < WINDOW_SIZE; i++) {
                            windowSentLog[i] = false;
                            windowReceivedLog[i] = false;
                        }
                        lastAckedFrame += shift;
                        lastFrameSent = lastAckedFrame + WINDOW_SIZE;
                    }
                    windowModificationMutex.unlock();
                    
                    //this_thread::sleep_for(chrono::milliseconds(200));

                    // Send frames that has not been sent or has timed out
                    for (int i = 0; i < WINDOW_SIZE; i++) {
                        seqNum = lastAckedFrame + i + 1;

                        if (seqNum < seqCount) {
                            windowModificationMutex.lock();

                            int elapsedTime = chrono::duration_cast<chrono::milliseconds > (chrono::high_resolution_clock::now() - windowSentTimeLog[i]).count();
                            if (!windowSentLog[i] || (!windowReceivedLog[i] && (elapsedTime > FRAME_TIMEOUT))) {
                                int bufferOffset = seqNum * MAX_FRAME_LENGTH;
                                dataSize = (bufferSize - bufferOffset < MAX_FRAME_LENGTH) ? (bufferSize - bufferOffset) : MAX_FRAME_LENGTH;
                                memcpy(data, buffer + bufferOffset, dataSize);

                                // Determine the end of transmission
                                bool endOfTransmission = (seqNum == seqCount - 1) && (!isSending);

                                frameSize = serializeFrame(endOfTransmission, seqNum, frame, data, dataSize);
                                
                                // Handle frame dropping in a lossy network
                                if(!isFrameDropped(lossProbability)) {
                                    cout << "Sending Frame #" << bufferNum *WINDOW_SIZE + seqNum << " from bytes:[" << bufferOffset << " to " << (bufferOffset + dataSize - 1) << "]" << endl;
                                    sendto(sock, frame, frameSize, 0, (const struct sockaddr *) &client, clientLength);   //sizeof(client));
                                } else {
                                    cout << "Dropping Frame #" << bufferNum *WINDOW_SIZE + seqNum << " from bytes:[" << bufferOffset << " to " << (bufferOffset + dataSize - 1) << "]" << endl;
                                    numFramesDropped++;
                                }
                                numFramesTotal++;

                                windowSentLog[i] = true;
                                windowSentTimeLog[i] = chrono::high_resolution_clock::now();

                                // Print the ACK frames that were received
                                cout << "    ACK window status: (";
                                for (int i = 0; i < WINDOW_SIZE; i++) {
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

            // cout << bufferNum *maxBufferSize + bufferSize << " bytes successfully sent." << endl;
            cout << "\nTransmission complete: Sent \"" << fileName << "\"" << endl;
            cout << numFramesDropped << " / " << numFramesTotal << " frames were dropped." << endl;

            fclose(file);
            delete[] windowReceivedLog;
            delete[] windowSentTimeLog;
            ackReceiver.detach();
        }
    }

    cout << "Goodbye." << endl;
    return 0;
}

// Encode an Ack into two bytes
void serializeAck(int seqNum, char *ack, bool error) {
    ack[0] = error ? 0 : 1; // Negated ACK (NACK)
    memcpy(ack + 1, &seqNum, 1);
}

// Encode the data for packing into a frame
int serializeFrame(bool endOfTransmission, int seqNum, char *frame, char *data, int dataSize) {
    frame[0] = endOfTransmission ? 0 : 1;
    uint32_t _seqNum = htonl(seqNum);
    uint32_t _dataSize = htonl(dataSize);
    memcpy(frame + 1, &_seqNum, 4);
    memcpy(frame + 5, &_dataSize, 4);
    memcpy(frame + 9, data, dataSize);
    frame[dataSize + 9] = computeCrcChecksum(frame, dataSize + (int) 9);
    //cout << "EOT: " << endOfTransmission << " Data: " << data << " Data Size: " << dataSize << endl;
    return dataSize + (int) 10;
}

// Decode an Ack from two bytes
// Returns true if valid, false if invalid
bool deserializeAck(int *seqNum, bool *error, char *ack) {
    *error = ack[0] == 0;
    char _seqNum;
    memcpy(&_seqNum, ack + 1, 1);
    *seqNum = _seqNum;
    //cout << "ACK: " << (int)ack[0] << (int)ack[1] << endl;
    return true;
}

// Listen for acks (needs to be multithreaded with mutex locks over the AckLogs)
void asyncListenForAcksThenRespond() {
    char ack[2];
    bool ackError;
    bool ackNeg;
    int ackSeqNum;
    int ackSize;

    // Listen for ack from reciever
    while (true) {
        ackSize = recvfrom(sock, (char*) ack, 2, MSG_WAITALL, (struct sockaddr *) &client, &clientLength);
        ackError = !deserializeAck(&ackSeqNum, &ackNeg, ack);

        windowModificationMutex.lock();
        if (!ackError && ackSeqNum > lastAckedFrame && ackSeqNum <= lastFrameSent) {
            if (!ackNeg) {
                cout << "Received ACK for Frame #" << ackSeqNum << endl;
                windowReceivedLog[ackSeqNum - (lastAckedFrame + 1)] = true;
            } else {
                cout << "Received NACK for Frame #" << ackSeqNum << endl;
                windowSentTimeLog[ackSeqNum - (lastAckedFrame + 1)] = startTime;
            }
        }
        windowModificationMutex.unlock();
    }
}