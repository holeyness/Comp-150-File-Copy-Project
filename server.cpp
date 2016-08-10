/*
 * Jacob Apkon and Yueming Luo
 * server.cpp
 *
 * Implements a server that accepts remote files to a copies them to a target directory
 *
 */

#include "c150grading.h"
#include "c150debug.h"
#include "c150dgmsocket.h"
#include "c150nastydgmsocket.h"
#include "c150nastyfile.h"
#include "filecopy.h"

#include <dirent.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <map>

#define TIMEOUT_TIME 1000
#define NETWORK_NASTINESS_ARG argv[1]
#define FILE_NASTINESS_ARG argv[2]
#define DIRECTORY_ARG argv[3]

using namespace std;
using namespace C150NETWORK;  // for all the comp150 utilities

//global vars
int FILENASTINESS;
int NETWORKNASTINESS;
string TARGET_DIR;
std::map<string, State> filestates; //map of states of files

//function declarations
void run_server();
void handle_file_transfer(transferpacket transfer, C150DgmSocket *sock);
void write_transfer_to_disk(NASTYFILE *file, transferpacket transfer);
void endtoendcheck(endtoendpacket hashrequest, C150DgmSocket *sock);

int main(int argc, char *argv[])
{
    GRADEME(argc, argv);

    setUpDebugLogging("filecopyserver.txt", argc, argv);
    c150debug->setIndent("    ");

    if (argc != 4) {
        cerr << "Error Args: " << argv[0] << " <networknastiness> <filenastiness> <targetdir>\n";
        exit(1);
    }

    try {
        c150debug->printf(C150APPLICATION,"Creating C150DgmSocket");

        NETWORKNASTINESS = atoi(NETWORK_NASTINESS_ARG);
        FILENASTINESS = atoi(FILE_NASTINESS_ARG);
        TARGET_DIR = DIRECTORY_ARG;

        checkDirectory(TARGET_DIR.c_str()); // Ensure the given directory is valid

        run_server();
    }

    catch (C150NetworkException e){
        c150debug->printf(C150ALWAYSLOG,"Caught C150NetworkException: %s\n",
                          e.formattedExplanation().c_str());
    }

    catch (C150FileException e) {
        c150debug->printf(C150ALWAYSLOG,"Caught C150FileException: %s\n",
                          e.formattedExplanation().c_str());
    }
}

// The server's main loop that processes packets
void run_server()
{
    ssize_t readlen;            //amount of data read from socket
    char incomingMessage[MAX_PACKET_SIZE];  // received message data

    // structs to hold our packets
    struct endtoendpacket hashrequest;
    struct transferpacket transfer;

    C150DgmSocket *sock = new C150NastyDgmSocket(NETWORKNASTINESS);

    while (1) {

        readlen = sock -> read(incomingMessage, sizeof(incomingMessage));
        if (readlen == 0) {
            c150debug->printf(C150APPLICATION, "Read zero length message, trying again \n");
            continue;
        }

        if (incomingMessage[0] == TRANSFER) {

            for (unsigned i = 0; i < sizeof(transfer); i++) {
                ((char *)&transfer)[i] = incomingMessage[i];
            }

            handle_file_transfer(transfer, sock);

        } else if (incomingMessage[0] == ENDTOEND) {

            for (unsigned i = 0; i < sizeof(endtoendpacket); i++) {
                ((char *)&hashrequest)[i] = incomingMessage[i];
            }

            if (filestates[string(hashrequest.filename)] != IN_PROGRESS) {
                endtoendcheck(hashrequest, sock);
            }
        }
    }
}

// Process the transfer packet and ACK it
void handle_file_transfer(struct transferpacket transfer, C150DgmSocket *sock)
{
    struct transferpacket_ack ack;
    ack.code = TRANSFER_ACK;
    fill_filename(transfer.filename, ack.filename);
    ack.seqNum = transfer.seqNum;

    c150debug->printf(C150APPLICATION,
                      "Server received TRANSFER packet %d for file %s\n",
                      transfer.seqNum,
                      transfer.filename);

    State current_state = filestates[string(transfer.filename)];
    if (current_state == COMPLETE) {
        // Delayed transfer packet that we can ignore
        return;
    } else if (current_state == PENDING) {
        // ACK must have gotten lost
        sock -> write((char *)&ack, sizeof(ack));
        return;
    }

    NASTYFILE file(FILENASTINESS);

    // change file state
    filestates[string(transfer.filename)] = IN_PROGRESS;

    // open up the file
    string path = makeFileName(TARGET_DIR.c_str(), transfer.filename);
    path = path + ".tmp";

    if (file.fopen(path.c_str(), "r+b") == NULL) {
        // the file does not yet exist create it
        *GRADING << "File: " <<  transfer.filename << " starting to receive file" << endl;
        if (file.fopen(path.c_str(), "w+b") == NULL) {
            throw C150FileException("Could not create file " + path + "\n");
        }
    }

    write_transfer_to_disk(&file, transfer);
    sock -> write((char *)&ack, sizeof(ack));

    // save the file length to verify progress
    long file_size = get_file_length(&file);
    file.fclose();

    // change file state to pending if the whole file has been received
    if (file_size == transfer.file_size){
        filestates[string(transfer.filename)] = PENDING;
        *GRADING << "File: " <<  transfer.filename;
        *GRADING << " received, beginning end-to-end check" << endl;
    }
}

// Write the given file segment to disk
void write_transfer_to_disk(NASTYFILE *file, struct transferpacket transfer)
{
    for (int i = 0; i < TRANSFER_SIZE; i++) {
        //calculate the location in the file
        unsigned byte_number = transfer.seqNum * TRANSFER_SIZE + i;     
        if (byte_number >= transfer.file_size) return;
        file->fseek(byte_number, SEEK_SET);
        write_byte(file, FILENASTINESS, transfer.file[i]);
    }
}

// Perform the End-To-End Check on a requested file
void endtoendcheck (struct endtoendpacket hashrequest, C150DgmSocket *sock)
{

    // variable declarations
    struct endtoendpacket_ack hashresponse;
    NASTYFILE file(FILENASTINESS);

    // Setup the response paket
    bzero((char *) &hashresponse, sizeof(hashresponse)); //clear everything in hashrequest
    hashresponse.code = ENDTOEND_ACK;
    fill_filename(hashrequest.filename, hashresponse.filename);

    c150debug->printf(C150APPLICATION, "End to end check starting for file %s.\n",
                        hashrequest.filename);

    // resend end to end ack if necessary
    if (filestates[string(hashrequest.filename)] == COMPLETE) {
        hashresponse.success = true;
        //sends the response packet
        c150debug->printf(C150APPLICATION, "Sending hash response for file: %s\n",
                          hashrequest.filename);
        sock -> write((char *)&hashresponse, sizeof(hashresponse));
        return;
    }

    // open up the file
    string path = makeFileName(TARGET_DIR.c_str(), hashrequest.filename) + ".tmp";
    file.fopen(path.c_str(), "rb");  //what happens if fopen fails? do we need the retval

    // hash the file, store the result in the response packet
    c150debug->printf(C150APPLICATION,"Server hashing file: %s \n", hashrequest.filename);
    unsigned char filehash[20];
    hash(&file, FILENASTINESS, path, filehash);
    file.fclose();

    // check our hash
    hashresponse.success = true;
    for (int i = 0; i < 20; i++)
    {
        if (filehash[i] != hashrequest.hash[i]) {
            hashresponse.success = false;
            break;
        }
    }

    if (hashresponse.success) {
        filestates[string(hashresponse.filename)] = COMPLETE;
        string new_name = TARGET_DIR + "/" + string(hashresponse.filename);
        rename(path.c_str(), new_name.c_str());
        c150debug->printf(C150APPLICATION, "File %s correct, renaming to %s.\n",
                          path.c_str(), new_name.c_str());
        *GRADING << "File: " <<  hashresponse.filename << " end-to-end check succeeded" << endl;
    } else {
        filestates[string(hashresponse.filename)] = IN_PROGRESS;
        // hash check failed delete
        remove(path.c_str());
        c150debug->printf(C150APPLICATION,"Deleting file: %s\n", path.c_str());
        *GRADING << "File: " <<  hashresponse.filename << " end-to-end check failed" << endl;
    }

    c150debug->printf(C150APPLICATION,"Sending hash response for file: %s\n", hashrequest.filename);
    sock -> write((char *)&hashresponse, sizeof(hashresponse));
}
