// Simeon Wuthier
// CS 5220, 11/08/21

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
using namespace std;

#define PORT 2060               // 2 + Student ID
#define MAX_FRAME_LENGTH 4096   // Size in bytes per frame
#define FILENAME_SIZE 256       // File names greater than this are not allowed
#define WINDOW_SIZE 12          // Number of frames in the window

// Socket descriptor
int sock, maxBufferSize;
struct sockaddr_in server, client;
socklen_t serverLength, clientLength;

// Upon fatal error, gracefully terminate with a message
void fatal(string str) {
    cout << "ERROR: " << str << endl;
    exit(1);
}

// Listen for acks (needs to be multithreaded)
void asyncListenForAcksThenRespond();
// Encode an Ack into two bytes
void serializeAck(int seqNum, char *ack, bool error);
// Decode the data within a frame
bool deserializeFrame(int *seqNum, char *data, int *dataSize, bool *endOfTransmission, char *frame);

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

    maxBufferSize = MAX_FRAME_LENGTH * WINDOW_SIZE;

    memset(&server, 0, sizeof(server));
    memset(&client, 0, sizeof(client));

    // Create the tocket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fatal("Socket initialization failed");
    }
    // Init the server data
    bzero((char*) &server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    // Convert the hostname, windom.uccs.edu, into an address
    struct hostent * hp;
    hp = gethostbyname(serverHostname);
    if (hp == NULL) {
        fatal("Unable to get srver IP address");
    }
    bcopy(hp->h_addr, (char*) &server.sin_addr, hp->h_length);

    cout << "Socket connected!" << endl;

    FILE *file = fopen(fileName, "wb");
    char buffer[maxBufferSize];
    int bufferSize;

    bool frameError;                    // True when frame has error due to checksum
    char ack[2];                        // Storage location for crafting ACKs
    char data[MAX_FRAME_LENGTH];        // Stores the file contents ready to be sent
    char frame[MAX_FRAME_LENGTH + 10];  // Stores the frame header + data + 10 bytes for checksum
    int dataSize, frameSize, seqNumCounter, windowMinIndex, windowMaxIndex;
    bool endOfTransmission, isReceiving = true;
    int bufferNum = 0;



    {
        // Set the recvfrom to terminate after 100 milliseconds
        struct timeval readTimeout;
        readTimeout.tv_sec = 0;
        readTimeout.tv_usec = 100 * 1000; // 100 milliseconds to microseconds
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&readTimeout, sizeof readTimeout);


        // Send the file's name
        cout << "Sending file name..." << endl;
        bool receivedFileName = false;
        char ack[2];
        while(!receivedFileName) {
            serverLength = sizeof(server);
            if (sendto(sock, fileName, fileNameLen, 0, (struct sockaddr *) &server, serverLength) <= 0) {
                fatal("Failed to send");
            }
            int ackSize = recvfrom(sock, (char*) ack, 2, MSG_WAITALL, (struct sockaddr *) &client, &clientLength);
            if(ackSize <= 0) continue;
            cout << "Received " << ackSize << " bytes, " << ack[0] << ack[1] << endl;
            if(ack[0] == 'K') receivedFileName = true;
            else fatal("404, file not found.");
        }

        // Set the recvfrom to not terminate, undos the 100ms timeout
        readTimeout.tv_sec = 0;
        readTimeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&readTimeout, sizeof readTimeout);
    }






    /*
     *  ARQ Stop-and-Wait
     */
    if(protocolType == 1) {
        int bytesReceived = 0;
        while (isReceiving) {
            cout << "Listening for frame..." << endl;
            frameSize = recvfrom(sock, (char*) frame, MAX_FRAME_LENGTH + 10, MSG_WAITALL, (struct sockaddr *) &client, &clientLength);
            frameError = !deserializeFrame(&seqNumCounter, data, &dataSize, &endOfTransmission, frame);
            if(!frameError) {
                bytesReceived += frameSize;

                // Write the serialized frame data portion to file if the checksum hash matches
                fwrite(data, 1, dataSize, file);
                bufferNum += 1;

                // frameError is always false, therefore no NACKs exist
                serializeAck(seqNumCounter, ack, frameError);
                cout << "Received frame #" << bufferNum * WINDOW_SIZE + seqNumCounter << (frameError ? "... INVALID" : ".") << endl;
                cout << "    Sending " << (frameError ? "NACK" : "ACK") << " for frame #" << bufferNum * WINDOW_SIZE + seqNumCounter << "..." << endl;
                sendto(sock, ack, 2, 0, (const struct sockaddr *) &client, clientLength);

                if(endOfTransmission) isReceiving = false;
            }

        }
        // cout << bytesReceived << " bytes received!" << endl;
        fclose(file);

        // Now, we want to make sure that the server is certain that we're done, just in case the EOT message was not correupt
        // So continue sending ACKs for 1 second, then we can end
        thread finalAckSenderThread(asyncListenForAcksThenRespond);
        chrono::high_resolution_clock::time_point start_time = chrono::high_resolution_clock::now();
        while (chrono::duration_cast<chrono::milliseconds > (chrono::high_resolution_clock::now() - start_time).count() < 1000) {
            this_thread::sleep_for(chrono::milliseconds(10));
        }
        finalAckSenderThread.detach();





    /*
     *  ARQ Selective Repeat
     */
    } else if(protocolType == 2) {
        while (isReceiving) {
            bufferSize = maxBufferSize;
            memset(buffer, 0, bufferSize);

            int receivedSeqCount = (int) maxBufferSize / MAX_FRAME_LENGTH;
            bool windowReceivedLog[WINDOW_SIZE];
            for (int i = 0; i < WINDOW_SIZE; i++) {
                windowReceivedLog[i] = false;
            }
            windowMinIndex = -1;
            windowMaxIndex = windowMinIndex + WINDOW_SIZE;

            // Receive frames with the sliding window
            while (true) {
                cout << "Listening for frame..." << endl;
                frameSize = recvfrom(sock, (char*) frame, MAX_FRAME_LENGTH + 10, MSG_WAITALL, (struct sockaddr *) &client, &clientLength);
                frameError = !deserializeFrame(&seqNumCounter, data, &dataSize, &endOfTransmission, frame);

                serializeAck(seqNumCounter, ack, frameError);
                cout << "Received window index #" << seqNumCounter << ", frame #" << bufferNum * WINDOW_SIZE + seqNumCounter << (frameError ? "... INVALID" : ".") << endl;
                cout << "    Sending " << (frameError ? "NACK" : "ACK") << " for frame #" << bufferNum * WINDOW_SIZE + seqNumCounter << "..." << endl;
                sendto(sock, ack, 2, 0, (const struct sockaddr *) &client, clientLength);

                if (seqNumCounter <= windowMaxIndex) {
                    if (!frameError) {
                        int bufferOffset = seqNumCounter * MAX_FRAME_LENGTH;

                        if (seqNumCounter == windowMinIndex + 1) {
                            memcpy(buffer + bufferOffset, data, dataSize);

                            // Print the ACK frames that were received
                            cout << "    ACK window status: (";
                            for (int i = 0; i < WINDOW_SIZE; i++) {
                                if (windowReceivedLog[i]) cout << "V ";
                                else cout << "- ";
                            }
                            cout << ")" << endl;

                            // Find the maximum shift number by counting trues from the left
                            int shift = 1;
                            for (int i = 1; i < WINDOW_SIZE; i++) {
                                if (!windowReceivedLog[i]) break;
                                shift += 1;
                            }
                            cout << "        Shifting window right " << shift << " bytes" << endl;
                            // Shift to the left
                            for (int i = 0; i < WINDOW_SIZE - shift; i++) {
                                windowReceivedLog[i] = windowReceivedLog[i + shift];
                            }
                            // Clear the rightmost flags
                            for (int i = WINDOW_SIZE - shift; i < WINDOW_SIZE; i++) {
                                windowReceivedLog[i] = false;
                            }
                            windowMinIndex += shift;
                            windowMaxIndex = windowMinIndex + WINDOW_SIZE;
                        
                        } else if (seqNumCounter > windowMinIndex + 1) {
                            if (!windowReceivedLog[seqNumCounter - (windowMinIndex + 1)]) {
                                memcpy(buffer + bufferOffset, data, dataSize);
                                windowReceivedLog[seqNumCounter - (windowMinIndex + 1)] = true;

                                // Print the ACK frames that were received
                                cout << "    ACK window status: (";
                                for (int i = 0; i < WINDOW_SIZE; i++) {
                                    if (windowReceivedLog[i]) cout << "V ";
                                    else cout << "- ";
                                }
                                cout << ")" << endl;
                            }
                        }

                        /*Set max sequence to sequence of frame with endOfTransmission */
                        if (endOfTransmission) {
                            bufferSize = bufferOffset + dataSize;
                            receivedSeqCount = seqNumCounter + 1;
                            isReceiving = false;
                        }
                    }

                    //  // Print the ACK frames that were received
                    // cout << "    ACK status: (";
                    // for (int i = 0; i < WINDOW_SIZE; i++) {   //     if (windowReceivedLog[i]) cout << "V ";
                    //     else cout << "- ";
                    // }
                    // cout << ")" << endl;
                }

                /*Move to next buffer if all frames in current buffer has been received */
                if (windowMinIndex >= receivedSeqCount - 1) break;
            }
            // cout << bufferNum * maxBufferSize + bufferSize << " bytes received!" << endl;

            // Write the serialized frame data from buffer to file
            fwrite(buffer, 1, bufferSize, file);
            bufferNum += 1;
        }
        fclose(file);

        // Now, we want to make sure that the server is certain that we're done, just in case the EOT message was not correupt
        // So continue sending ACKs for 1 second, then we can end
        thread finalAckSenderThread(asyncListenForAcksThenRespond);
        chrono::high_resolution_clock::time_point start_time = chrono::high_resolution_clock::now();
        while (chrono::duration_cast<chrono::milliseconds > (chrono::high_resolution_clock::now() - start_time).count() < 1000) {
            this_thread::sleep_for(chrono::milliseconds(10));
        }
        finalAckSenderThread.detach();
    }

    cout << "\nTransmission complete: Received \"" << fileName << "\"\n" << endl;
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
    char computedChecksum = computeCrcChecksum(frame, *dataSize + 9);

    //printf("Verifying frame hash: %x == %x\n", receivedChecksum, computedChecksum);
    return receivedChecksum == computedChecksum;
}

// Listen for acks (needs to be multithreaded)
void asyncListenForAcksThenRespond() {
    char frame[MAX_FRAME_LENGTH + 10];
    char data[MAX_FRAME_LENGTH];
    char ack[2];
    int frameSize;
    int dataSize;

    int seqNumCounter;
    bool frameError;
    bool endOfTransmission;

    /*Listen for frames and send ack */
    while (true) {
        frameSize = recvfrom(sock, (char*) frame, MAX_FRAME_LENGTH + 10,
            MSG_WAITALL, (struct sockaddr *) &client, &clientLength);
        frameError = !deserializeFrame(&seqNumCounter, data, &dataSize, &endOfTransmission, frame);

        serializeAck(seqNumCounter, ack, frameError);
        sendto(sock, ack, 2, 0, (const struct sockaddr *) &client, clientLength);
    }
}