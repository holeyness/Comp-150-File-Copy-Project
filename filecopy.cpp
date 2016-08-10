/*
 * Jacob Apkon and Yueming Luo
 * filecopy.cpp
 *
 * General purpose functions/definitions used for both client and server
 *
 */

#include "c150grading.h"
#include "c150debug.h"
#include "c150dgmsocket.h"
#include "c150nastydgmsocket.h"
#include "c150nastyfile.h"
#include "filecopy.h"

#include <sys/stat.h>
#include <dirent.h>
#include <openssl/sha.h>
#include <map>

using namespace std;
using namespace C150NETWORK;  // for all the comp150 utilities

void fill_filename(char *filename, char buffer[MAX_FILENAME_SIZE])
{
    int i;
    for (i = 0; i < MAX_FILENAME_SIZE - 1 && filename[i] != '\0'; i++) {
        buffer[i] = filename[i];
    }
    buffer[i] = '\0';
}

// Hashing function that takes in a nastyfile and fills a hash buffer
void hash(NASTYFILE *file, int nastiness, string path, unsigned char hashbuffer[20])
{

    long file_size = get_file_length(file);

    // allocate a buffer to hold our read file
    char *buffer = new char[file_size];

    c150debug->printf(C150APPLICATION,"About to read file: %s \n", path.c_str());

    int len = readfile(file, path, nastiness, buffer);

    if (len != (int) file_size) {
        cerr << "Error reading file " << path << " errno=" << strerror(errno) << endl;
        throw C150FileException("Hash failed to read the file");
    }

    // hash the buffer into the hash struct
    SHA1((const unsigned char *)buffer, file_size, hashbuffer);

    delete[] buffer;
}

// Combine directory and file name
string makeFileName(string dir, string name)
{
    stringstream ss;

    ss << dir;
    // make sure dir name ends in /
    if (dir.substr(dir.length()-1,1) != "/")
        ss << '/';
    ss << name;       // append file name to dir
    return ss.str();  // return dir/name
}

// This function handles disk nastiness, reads the file into the buffer by calling read_byte
int readfile(NASTYFILE *file, string path, int nastiness, char *buffer)
{

    int i = 0;
    long file_size = get_file_length(file);

    //For the size of the file (in bytes)
    for (; i < file_size; i++)
    {
        buffer[i] = read_byte(file, nastiness);
    }

    return i;
}

// this function will repeatedly read the next byte until the same character
// has been read at least 2*nastiness+1 times
char read_byte(NASTYFILE *file, int nastiness)
{
    char byte = 0;
    std::map<char, int> byte_count; //map of byte to # of times they have been read
    byte_count[byte] = 0;

    // only return the byte if it passed the threshold
    while (byte_count[byte] < nastiness * 2 + 1) {
        file->fread(&byte, 1, 1);

        if (byte_count.count(byte)) {
            // the character is currently in the map;
            byte_count[byte]++;
        } else {
            byte_count[byte] = 1;
        }
        file->fseek(-1, SEEK_CUR);
    }

    file->fseek(1, SEEK_CUR);
    return byte;
}

// Write the given byte until we read the same byte from disk
void write_byte(NASTYFILE *file, int nastiness, char byte)
{
    file->fwrite(&byte, 1, 1);
    file->fseek(-1, SEEK_CUR);

    char temp = read_byte(file, nastiness);

    if (byte != temp) {
        file->fseek(-1, SEEK_CUR);
        write_byte(file, nastiness, byte);  //recursive call if write failed
    }
}

long get_file_length(NASTYFILE *file)
{
    long current_location = file->ftell();
    file->fseek(0, SEEK_END);
    long end_of_file = file->ftell();
    file->fseek(0, SEEK_SET);
    long beginning_of_file = file->ftell();
    file->fseek(0, current_location);

    return end_of_file - beginning_of_file;
}

//
//  Utilities provided
// 
void setUpDebugLogging(const char *logname, int argc, char *argv[])
{

     ofstream *outstreamp = new ofstream(logname);
     DebugStream *filestreamp = new DebugStream(outstreamp);
     DebugStream::setDefaultLogger(filestreamp);

     c150debug->setPrefix(argv[0]);
     c150debug->enableTimestamp();

     c150debug->enableLogging(C150APPLICATION | C150NETWORKTRAFFIC |
                              C150NETWORKDELIVERY);
}

// Makes sure a directory exists
void checkDirectory(const char *dirname)
{
    struct stat statbuf;
    if (lstat(dirname, &statbuf) != 0) {
        fprintf(stderr,"Error stating supplied source directory %s\n", dirname);
        exit(8);
    }

    if (!S_ISDIR(statbuf.st_mode)) {
        fprintf(stderr,"File %s exists but is not a directory\n", dirname);
        exit(8);
    }
}

// Checks whether fname is a valid file
bool isFile(string fname)
{
    const char *filename = fname.c_str();
    struct stat statbuf;
    if (lstat(filename, &statbuf) != 0) {
        fprintf(stderr,"isFile: Error stating supplied source file %s\n", filename);
        return false;
    }

    if (!S_ISREG(statbuf.st_mode)) {
        fprintf(stderr,"isFile: %s exists but is not a regular file\n", filename);
        return false;
    }
    return true;
}
