/*
 * Jacob Apkon and Yueming Luo
 * client.cpp
 *
 * Implements a client that sends a source directory to the server
 *
 */

#include "c150grading.h"
#include "c150debug.h"
#include "c150dgmsocket.h"
#include "c150nastydgmsocket.h"
#include "c150nastyfile.h"
#include "filecopy.h"

#include <dirent.h>
#include <math.h>
#include <map>

#define TIMEOUT_TIME 1000
#define SERVER argv[1]
#define NETWORK_NASTINESS_ARG argv[2]
#define FILE_NASTINESS_ARG argv[3]
#define DIRECTORY_ARG argv[4]

using namespace std;          // for C++ std library
using namespace C150NETWORK;  // for all the comp150 utilities

//global var
int FILENASTINESS;
int NETWORKNASTINESS;
string SOURCE_DIR;
std::map<string, State> filestates;       //map of states of files
std::map<string, int> transfer_attempts;  // how many times have we tried a file

//function declarations
void send_directory(DIR *SRC, C150DgmSocket *sock);
void open_file(NASTYFILE *inputFile, string path);
void transfer(NASTYFILE *file, C150DgmSocket *sock, string path, char *filename);
int split_file(NASTYFILE *file, string path, char *filename, transferpacket **packets);
void send_file(C150DgmSocket *sock, transferpacket *packets, unsigned num_packets);
void send_hash_request(NASTYFILE *file, C150DgmSocket *sock, string path, char *filename);
void check_transfer_ack(C150DgmSocket *sock, transferpacket_ack *ack, unsigned seqNum);
void check_end_to_end_ack(C150DgmSocket *sock, endtoendpacket_ack *ack);

int main(int argc, char *argv[])
{
    GRADEME(argc, argv);

    DIR *SRC;

    //set up debugging
    setUpDebugLogging("filecopyclient.txt", argc, argv);

    if (argc != 5) {
        cerr << "Error Args: " << argv[0];
        cerr << " <server> <networknastiness> <filenastiness> <srcdir>\n";
        exit(1);
    }

    try {
        c150debug->printf(C150APPLICATION,"Client creating C150DgmSocket");

        NETWORKNASTINESS = atoi(NETWORK_NASTINESS_ARG);
        FILENASTINESS = atoi(FILE_NASTINESS_ARG);
        SOURCE_DIR = DIRECTORY_ARG;

        // make sure the source dir exists
        checkDirectory(SOURCE_DIR.c_str());

        // open up our source directory
        SRC = opendir(SOURCE_DIR.c_str());
        if (SRC == NULL) {
            cerr << "Error opening source directory " << DIRECTORY_ARG << endl;
            exit(8);
        }

        C150DgmSocket *sock = new C150NastyDgmSocket(NETWORKNASTINESS);
        sock->turnOnTimeouts(TIMEOUT_TIME);
        sock->setServerName(SERVER);

        send_directory(SRC, sock);
    }

    catch (C150NetworkException e) {
        c150debug->printf(C150ALWAYSLOG,
                          "Caught C150NetworkException: %s\n",
                          e.formattedExplanation().c_str());
    }

    catch (C150FileException e) {
        c150debug->printf(C150ALWAYSLOG,
                          "Caught C150FileException: %s\n",
                          e.formattedExplanation().c_str());
    }
}

// Send all legal files in the given SRC directory to the server
void send_directory(DIR *SRC, C150DgmSocket *sock)
{
    struct dirent *sourceFile;

    //  Loop through the files in the src dir
    while ((sourceFile = readdir(SRC)) != NULL) {
        // skip the . and .. names
        if ((strcmp(sourceFile->d_name, ".") == 0) ||
            (strcmp(sourceFile->d_name, "..")  == 0 ))
            continue;      // never copy . or ..

        // skip files with too long names
        if (string(sourceFile->d_name).length() > MAX_FILENAME_SIZE) {
            cerr << sourceFile->d_name << ": filename is too long to send.\n";
            continue;
        }

        // create a full path of the file
        string sourceName = makeFileName(SOURCE_DIR.c_str(), sourceFile->d_name);

        // make sure the file we're copying is not a directory
        if (!isFile(sourceName)) {
            cerr << "Skipping " << sourceName;
            cerr << ", it is a directory or other non-regular file.\n";
            continue;
        }

        c150debug->printf(C150APPLICATION,"Client opening file: %s\n", sourceName.c_str());
        filestates[string(sourceFile->d_name)] = IN_PROGRESS; //start state

        transfer_attempts[string(sourceFile->d_name)] = 0;

        //open and transfer each file in packets
        //send the hash when transfer complete and then close each file
        NASTYFILE currentfile(FILENASTINESS);
        open_file(&currentfile, sourceName);
        transfer(&currentfile, sock, sourceName, sourceFile->d_name);
        currentfile.fclose();
    }

    closedir(SRC);
}

// This function opens the file up
void open_file(NASTYFILE *inputFile, string path)
{
       void *fopenretval = inputFile->fopen(path.c_str(), "rb");

       // keep retrying opening the file if it failed
       while (fopenretval == NULL) {
            c150debug->printf(C150APPLICATION,
                              "Client file read failed for: %s, retrying \n",
                              path.c_str());
            fopenretval = inputFile->fopen(path.c_str(), "rb");
       }
 }

// Hash our packet and transfers whether our checks succeeded to the server
void transfer(NASTYFILE *file, C150DgmSocket *sock, string path, char *filename)
{
    *GRADING << "File: " <<  filename << ", beginning transmission, attempt ";
    *GRADING << transfer_attempts[string(filename)] << endl;
    struct endtoendpacket_ack success;

    // send the file
    transferpacket *packets = NULL;
    int num_packets = split_file(file, path, filename, &packets);
    send_file(sock, packets, num_packets);
    delete [] packets;

    // we're done sending the file, time to verify by sending hash
    filestates[string(filename)] = PENDING;
    *GRADING << "File: " << filename;
    *GRADING << " transmission complete, waiting for end-to-end-check, attempt ";
    *GRADING << transfer_attempts[string(filename)] << endl;

    // Setup and send the endtoend check packet containing the hash
    c150debug->printf(C150APPLICATION,
                      "Client preparing to send hash request for file: %s\n",
                      filename);

    bzero((char *) &success, sizeof(success)); //clear everything in hashrequest
    success.code = ENDTOEND_ACK;
    fill_filename(filename, success.filename);
    
    send_hash_request(file, sock, path, filename);

    // Retransfer the file if the end-to-end check failed
    if (filestates[string(filename)] != COMPLETE) {
        transfer_attempts[string(filename)]++;
        transfer(file, sock, path, filename);
    }
}

// Fill an array with transfer packets to send to the server
int split_file(NASTYFILE *file, string path, char *filename, transferpacket **packets)
{
    long file_size = get_file_length(file);

    int num_packets = ceil(file_size / (float)TRANSFER_SIZE);

    *packets = new transferpacket[num_packets];

    int bytes_read = 0;
    for (int i = 0; i < num_packets; i++) {
        bzero((char *)&(*packets)[i], sizeof((*packets)[i]));  //clear everything in each packet

        (*packets)[i].code = TRANSFER;
        (*packets)[i].seqNum = i;

        fill_filename(filename, (*packets)[i].filename);

        (*packets)[i].file_size = file_size;

        // Build each packet's file chunk
        for (int j = 0; j < TRANSFER_SIZE && bytes_read < file_size; j++) {
            (*packets)[i].file[j] = read_byte(file, FILENASTINESS);
            bytes_read++;
        }
    }

    file->rewind();

    return num_packets;
}

// Send all transfer packets that represent a given file
void send_file(C150DgmSocket *sock, transferpacket *packets, unsigned num_packets)
{
    struct transferpacket_ack ack;

    for (unsigned i = 0; i < num_packets; i++)
    {
        sock -> write((char *) &packets[i], sizeof(packets[i]));
        check_transfer_ack(sock, &ack, i);

        if (ack.code == -1) {
            // timed out, resend packet
            c150debug->printf(C150APPLICATION,
                              "Client: packet %d timed out", i);
            i--;
        }
    }
}


// This function will set code in ACK to -1 if it times out
// Else it will be the correct ack
void check_transfer_ack(C150DgmSocket *sock, transferpacket_ack *ack, unsigned seqNum)
{
        ssize_t readlen;            //amount of data read from socket
        char incomingMessage[MAX_PACKET_SIZE];  // received message data

        readlen = sock -> read(incomingMessage, sizeof(incomingMessage));

        // If the thing we read was badly formatted
        if (readlen == 0) {
            return;
        }

        // if it times out
        if (sock -> timedout()) {
            ack->code = -1;
            return;
        }

        //check first byte
        if (incomingMessage[0] != TRANSFER_ACK) {
            check_transfer_ack(sock, ack, seqNum);
            return;
        }
        for (unsigned i = 0; i < sizeof(*ack); i++) {
            ((char *)ack)[i] = incomingMessage[i];
        }

        // Out of order packets and check file state
        if (ack->seqNum < seqNum || filestates[string(ack->filename)] != IN_PROGRESS) {
            check_transfer_ack(sock, ack, seqNum);
            return;
        }

        c150debug->printf(C150APPLICATION,
                          "Client: successfully recieved ack for transfer packet %d", seqNum);

}


// Requests a hash from the server and verifies it with local version
// Returns true if hash matched successfully, else false
void send_hash_request(NASTYFILE *file, C150DgmSocket *sock, string path, char *filename)
{
    struct endtoendpacket hashrequest;   // the struct to hold our hash request
    struct endtoendpacket_ack hashresponse;  // the struct to hold the response froms server
    bzero((char *) &hashrequest, sizeof(hashrequest));    //clear everything in hashrequest
    bzero((char *) &hashresponse, sizeof(hashresponse));  //clear everything in hashresponse
    hashrequest.code = ENDTOEND;

    // copy file name into the request packet byte by byte
    fill_filename(filename, hashrequest.filename);

    //hash our file
    c150debug->printf(C150APPLICATION,"Client hashing file: %s\n", path.c_str());
    hash(file, FILENASTINESS, path, hashrequest.hash);


    // send the hash request to the server
    c150debug->printf(C150APPLICATION,
                      "%s: Sending Hash Request: \"%s\"",
                      "client",
                      filename);
    sock -> write((char *)&hashrequest, sizeof(hashrequest));

    c150debug->printf(C150APPLICATION, "Client: Just sent the hash req, waiting for server hash");

    // waiting for the response from server of hash
    check_end_to_end_ack(sock, &hashresponse);
    while (hashresponse.code == -1) {
        //no packet was read in 5 seconds, log it
        c150debug->printf(C150APPLICATION,
                          "Client: Did not receive a hash respone, retrying send.");

        // the hash response timed out
        sock -> write((char *)&hashrequest, sizeof(hashrequest));
        check_end_to_end_ack(sock, &hashresponse);
    }

    if (hashresponse.success) {
        *GRADING << "File: " <<  filename << ", end-to-end check succeeded, attempt ";
        *GRADING << transfer_attempts[string(hashresponse.filename)] << endl;
        filestates[string(hashresponse.filename)] = COMPLETE;
        c150debug->printf(C150APPLICATION,"Client hash check success: %s\n", filename);
    } else {
        *GRADING << "File: " <<  filename << ", end-to-end check failed, attempt ";
        *GRADING << transfer_attempts[string(hashresponse.filename)] << endl;
        filestates[string(hashresponse.filename)] = IN_PROGRESS;
    }
}

// This function will set code in ACK to -1 if it times out
// Else it will be the correct ack
void check_end_to_end_ack(C150DgmSocket *sock, endtoendpacket_ack *ack)
{
        ssize_t readlen;            //amount of data read from socket
        char incomingMessage[MAX_PACKET_SIZE];  // received message data

        readlen = sock -> read(incomingMessage, sizeof(incomingMessage));

        if (readlen == 0) {
            return;
        }

        // if it times out
        if (sock -> timedout()) {
            ack->code = -1;
            return;
        }

        //check first byte
        if (incomingMessage[0] != ENDTOEND_ACK) {
            check_end_to_end_ack(sock, ack);
        }
        for (unsigned i = 0; i < sizeof(*ack); i++) {
            ((char *)ack)[i] = incomingMessage[i];
        }

        // Ignore delayed packets
        if (filestates[string(ack->filename)] != PENDING) {
            check_end_to_end_ack(sock, ack);
        }

        c150debug->printf(C150APPLICATION,
                          "Client: successfully recieved an e2e ack");

}
