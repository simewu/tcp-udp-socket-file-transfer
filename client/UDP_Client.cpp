// Simeon Wuthier
// CS 5220, 11/03/21

#include <algorithm>
#include <chrono>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
using namespace std;

#define SERVER_UDP_PORT 2060    // 2 + Student ID
#define MAXLEN 4096             // Size in bytes per frame
#define FILENAME_SIZE 256       // File names greater than this are not allowed
#define WINDOWSIZE 10           // Number of frames in the window

// Socket
int sd, maxBufferSize;
struct sockaddr_in server_addr, client_addr;
socklen_t server_len, client_len;

// Upon fatal error, gracefully terminate with a message
void fatal(string str) {
    cout << "ERROR: " << str << endl;
    exit(1);
}

// Encode an Ack into two bytes
void serializeAck(int seqNum, char *ack, bool error);
// Decode the data within a frame
bool deserializeFrame(int *seqNum, char *data, int *dataSize, bool *endOfTransmission, char *frame);
// Listen for acks (needs to be multithreaded)
void handleAckMessages();

// Given an array, add all the bytes and take the last two-byte output as the hash
char fastHash(char *array, int arrayLen) {
    int merged = 0;
    for(int i = 0; i < arrayLen; i++) {
        merged += array[i];
    }
    return merged & 0xFFFF;
}







// Client side needs: UDP_Client serverHostname fileName protocolType
int main(int argc, char *argv[]) {

    if (argc != 4) fatal("Usage: \"UDP_Client serverHostname fileName protocolType\"");
    char *serverHostname = argv[1];
    char fileName[FILENAME_SIZE] = { 0 };

    memcpy(fileName, argv[2], FILENAME_SIZE);
    int fileNameLen = std::min((int) strlen(argv[2]), FILENAME_SIZE);

    //char* fileName = argv[2];
    int protocolType = stoi(argv[3]);


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


    cout << endl;
    cout << "Server hostname: " << serverHostname << endl;
    cout << "File name to request: " << fileName << endl;
    cout << "Protocol type: " << protocolType << endl;

    maxBufferSize = MAXLEN * WINDOWSIZE;

    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    /*Fill server address data structure */
    // server_addr.sin_family = AF_INET;
    // server_addr.sin_addr.s_addr = INADDR_ANY; 
    // server_addr.sin_port = htons(SERVER_UDP_PORT);

    // Create the tocket
    sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd < 0) {
        fatal("Socket initialization failed");
    }
    // Init the server data
    bzero((char*) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_UDP_PORT);

    // Convert the hostname, windom.uccs.edu, into an address
    struct hostent * hp;
    hp = gethostbyname(serverHostname);
    if (hp == NULL) {
        fatal("Can't get server's IP address");
    }
    bcopy(hp->h_addr, (char*) &server_addr.sin_addr, hp->h_length);

    cout << "Connected!" << endl;

    FILE *file = fopen(fileName, "wb");
    char buffer[maxBufferSize];
    int bufferSize;

    bool frameError;    // True when frame has error due to checksum
    char ack[2];    // Storage location for crafting ACKs
    char data[MAXLEN];  // Stores the file contents ready to be sent
    char frame[MAXLEN + 10];    // Stores the frame header + data + 10 bytes for checksum
    int dataSize, frameSize, lastFrameReceived, lastAcceptableFrame, numReceivedSeqNums;

    /*Receive frames until end of transmission */
    bool endOfTransmission, isReceiving = true;
    int bufferNum = 0;

    // Send the file name
    cout << "Sending file name..." << endl;
    server_len = sizeof(server_addr);

    if (sendto(sd, fileName, fileNameLen, 0, (struct sockaddr *) &server_addr, server_len) <= 0) {
        fatal("Failed to send");
    }






    /*
     *  ARQ Stop-and-Wait
     */
    if(protocolType == 1) {
        int bytesReceived = 0;
        while (isReceiving) {
            cout << "Listening for frame..." << endl;
            frameSize = recvfrom(sd, (char*) frame, MAXLEN + 10, MSG_WAITALL, (struct sockaddr *) &client_addr, &client_len);
            frameError = !deserializeFrame(&numReceivedSeqNums, data, &dataSize, &endOfTransmission, frame);
            if(!frameError) {
                bytesReceived += frameSize;

                // Write the serialized frame data portion to file if the checksum hash matches
                fwrite(data, 1, dataSize, file);
                bufferNum += 1;

                // frameError is always false, therefore no NACKs exist
                serializeAck(numReceivedSeqNums, ack, frameError);
                cout << "Received frame #" << bufferNum * WINDOWSIZE + numReceivedSeqNums << (frameError ? "... INVALID" : ".") << endl;
                cout << "    Sending " << (frameError ? "NACK" : "ACK") << " for frame #" << bufferNum * WINDOWSIZE + numReceivedSeqNums << "..." << endl;
                sendto(sd, ack, 2, 0, (const struct sockaddr *) &client_addr, client_len);

                if(endOfTransmission) isReceiving = false;
            }

        }
        cout << bytesReceived << " bytes received!" << endl;
        fclose(file);

        // Now, we want to make sure that the server is certain that we're done, just in case the EOT message was not correupt
        // So continue sending ACKs for 1 second, then we can end
        thread stdby_thread(handleAckMessages);
        chrono::high_resolution_clock::time_point start_time = chrono::high_resolution_clock::now();
        while (chrono::duration_cast<chrono::milliseconds > (chrono::high_resolution_clock::now() - start_time).count() < 1000) {
            this_thread::sleep_for(chrono::milliseconds(10));
        }
        stdby_thread.detach();





    /*
     *  ARQ Selective Repeat
     */
    } else if(protocolType == 2) {
        while (isReceiving) {
            bufferSize = maxBufferSize;
            memset(buffer, 0, bufferSize);

            int receivedSeqCount = (int) maxBufferSize / MAXLEN;
            bool windowLog[WINDOWSIZE];
            for (int i = 0; i < WINDOWSIZE; i++) {
                windowLog[i] = false;
            }
            lastFrameReceived = -1;
            lastAcceptableFrame = lastFrameReceived + WINDOWSIZE;

            /*Receive current buffer with sliding window */
            while (true) {
                cout << "Listening for frame..." << endl;
                frameSize = recvfrom(sd, (char*) frame, MAXLEN + 10, MSG_WAITALL, (struct sockaddr *) &client_addr, &client_len);
                frameError = !deserializeFrame(&numReceivedSeqNums, data, &dataSize, &endOfTransmission, frame);

                serializeAck(numReceivedSeqNums, ack, frameError);
                cout << "Received frame #" << bufferNum * WINDOWSIZE + numReceivedSeqNums << (frameError ? "... INVALID" : ".") << endl;
                cout << "    Sending " << (frameError ? "NACK" : "ACK") << " for frame #" << bufferNum * WINDOWSIZE + numReceivedSeqNums << "..." << endl;
                sendto(sd, ack, 2, 0, (const struct sockaddr *) &client_addr, client_len);

                if (numReceivedSeqNums <= lastAcceptableFrame) {
                    if (!frameError) {
                        int bufferOffset = numReceivedSeqNums * MAXLEN;

                        if (numReceivedSeqNums == lastFrameReceived + 1) {
                            memcpy(buffer + bufferOffset, data, dataSize);

                            int shift = 1;
                            for (int i = 1; i < WINDOWSIZE; i++) {
                                if (!windowLog[i]) break;
                                shift += 1;
                            }
                            for (int i = 0; i < WINDOWSIZE - shift; i++) {
                                windowLog[i] = windowLog[i + shift];
                            }
                            for (int i = WINDOWSIZE - shift; i < WINDOWSIZE; i++) {
                                windowLog[i] = false;
                            }
                            lastFrameReceived += shift;
                            lastAcceptableFrame = lastFrameReceived + WINDOWSIZE;
                        
                        } else if (numReceivedSeqNums > lastFrameReceived + 1) {
                            if (!windowLog[numReceivedSeqNums - (lastFrameReceived + 1)]) {
                                memcpy(buffer + bufferOffset, data, dataSize);
                                windowLog[numReceivedSeqNums - (lastFrameReceived + 1)] = true;
                            }
                        }

                        /*Set max sequence to sequence of frame with endOfTransmission */
                        if (endOfTransmission) {
                            bufferSize = bufferOffset + dataSize;
                            receivedSeqCount = numReceivedSeqNums + 1;
                            isReceiving = false;
                        }
                    }

                    //  // Print the ACK frames that were received
                    // cout << "    ACK status: (";
                    // for (int i = 0; i < WINDOWSIZE; i++) {   //     if (windowLog[i]) cout << "V ";
                    //     else cout << "- ";
                    // }
                    // cout << ")" << endl;
                }

                /*Move to next buffer if all frames in current buffer has been received */
                if (lastFrameReceived >= receivedSeqCount - 1) break;
            }
            cout << bufferNum * maxBufferSize + bufferSize << " bytes received!" << endl;

            // Write the serialized frame data from buffer to file
            fwrite(buffer, 1, bufferSize, file);
            bufferNum += 1;
        }
        fclose(file);

        // Now, we want to make sure that the server is certain that we're done, just in case the EOT message was not correupt
        // So continue sending ACKs for 1 second, then we can end
        thread stdby_thread(handleAckMessages);
        chrono::high_resolution_clock::time_point start_time = chrono::high_resolution_clock::now();
        while (chrono::duration_cast<chrono::milliseconds > (chrono::high_resolution_clock::now() - start_time).count() < 1000) {
            this_thread::sleep_for(chrono::milliseconds(10));
        }
        stdby_thread.detach();
    }

    cout << "Transmission complete." << endl;
    return 0;
}

// Encode an Ack into two bytes
void serializeAck(int seqNum, char *ack, bool error) {
    ack[0] = error ? 0 : 1; // Negated ACK (NACK)
    memcpy(ack + 1, &seqNum, 1);
}

// Decode the data within a frame
// Returns true if valid, false if invalid
bool deserializeFrame(int *seqNum, char *data, int *dataSize, bool *endOfTransmission, char *frame) {
    *endOfTransmission = frame[0] == 0 ? true : false;
    uint32_t _seqNum;
    memcpy(&_seqNum, frame + 1, 4);
    *seqNum = ntohl(_seqNum);

    uint32_t _dataSize;
    memcpy(&_dataSize, frame + 5, 4);
    *dataSize = ntohl(_dataSize);

    memcpy(data, frame + 9, *dataSize);
    char receivedChecksum = frame[*dataSize + 9];
    char computedChecksum = fastHash(frame, *dataSize + 9);

    //printf("Verifying frame hash: %x == %x\n", receivedChecksum, computedChecksum);
    return receivedChecksum == computedChecksum;
}

// Listen for acks (needs to be multithreaded)
void handleAckMessages() {
    char frame[MAXLEN + 10];
    char data[MAXLEN];
    char ack[2];
    int frameSize;
    int dataSize;

    int numReceivedSeqNums;
    bool frameError;
    bool endOfTransmission;

    /*Listen for frames and send ack */
    while (true) {
        frameSize = recvfrom(sd, (char*) frame, MAXLEN + 10,
            MSG_WAITALL, (struct sockaddr *) &client_addr, &client_len);
        frameError = !deserializeFrame(&numReceivedSeqNums, data, &dataSize, &endOfTransmission, frame);

        serializeAck(numReceivedSeqNums, ack, frameError);
        sendto(sd, ack, 2, 0, (const struct sockaddr *) &client_addr, client_len);
    }
}