/*
 * Jacob Apkon and Yueming Luo
 * filecopy.h
 *
 * General purpose functions/definitions used for both client and server
 *
 */

#include "c150nastyfile.h"

using namespace C150NETWORK;  // for all the comp150 utilities

// codes for transfers
const char TRANSFER     = 0;
const char TRANSFER_ACK = 1;
const char ENDTOEND     = 2;
const char ENDTOEND_ACK = 3;

// codes for states (maintained per program)
enum State
{
    IN_PROGRESS, PENDING, COMPLETE
};

const int MAX_FILENAME_SIZE = 252; // We need to add '.tmp' on the server
const int TRANSFER_SIZE = 250;
const int MAX_PACKET_SIZE = 512;

// the transfer packet
struct __attribute__((__packed__)) transferpacket {
    char code;
    unsigned seqNum;
    char filename[MAX_FILENAME_SIZE];
    unsigned file_size;
    char file[TRANSFER_SIZE]; // Contains part of the file
};

// the transfer ack packet
struct __attribute__((__packed__)) transferpacket_ack {
    char code;
    char filename[MAX_FILENAME_SIZE];
    unsigned seqNum;
};

// the end to end packet, assuming at most 255 bytes of file name
struct __attribute__((__packed__)) endtoendpacket {
    char code;
    char filename[MAX_FILENAME_SIZE];
    unsigned char hash[20];
};

// the end to end ack packet, assuming 255 bytes of file name
struct __attribute__((__packed__)) endtoendpacket_ack {
    char code;
    char filename[MAX_FILENAME_SIZE];
    bool success;   //bool if the hash matches
};

// Fills the buffer with the filename
void fill_filename(char *filename, char buffer[MAX_FILENAME_SIZE]);

// Calculates the SHA1 hash for the entire file and puts it into hashbuffer
void hash(NASTYFILE *file, int nastiness, string path, unsigned char hashbuffer[20]);

// Combines a directory with a file name to get a full path
string makeFileName(string dir, string name);

// Reads the current byte of the file, including retries for disk errors
char read_byte(NASTYFILE *file, int nastiness);

// Writes the byte to the end of the file, including rewrites to handle disk errors
void write_byte(NASTYFILE *file, int nastiness, char byte);

// Loads the entire file into the buffer by calling write_byte
int readfile(NASTYFILE *file, string path, int nastiness, char *buffer);

// Returns the length of the pointed to file
long get_file_length(NASTYFILE *file);

//
//  Methods provided by C150Utils
//
void setUpDebugLogging(const char *logname, int argc, char *argv[]);

void checkDirectory(const char *dirname);

bool isFile(string fname);
