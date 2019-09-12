// $Id: cix.cpp,v 1.9 2019-04-05 15:04:28-07 - - $
//Sam Guyette (sguyette@ucsc.edu)
//Ryan McCrory (rmccrory@ucsc.edu)

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <vector>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>

#include "protocol.h"
#include "logstream.h"
#include "sockets.h"

logstream outlog (cout);
//exit command
//Quit the program. An end of file marker or Control/D is equivalent.
struct cix_exit : public exception {};

unordered_map<string,cix_command> command_map {
    {"exit", cix_command::EXIT},
    {"help", cix_command::HELP},
    {"ls", cix_command::LS  },
    //add remaining needed commands
    {"get", cix_command::GET },
    {"put", cix_command::PUT },
    {"rm", cix_command::RM  },
};
 
//done
//help command
void cix_help() {
    static const vector<string> help = {
        //A summary of available commands is printed
        "exit         - Exit the program.  Equivalent to EOF.",
        "get filename - Copy remote file to local host.",
        "help         - Print help summary.",
        "ls           - List names of files on remote server.",
        "put filename - Copy local file to remote host.",
        "rm filename  - Remove file from remote server.",
    };
    for(const auto& line: help){
        cout << line << endl;
    }
}

//ls command
/*
Causes the remote server to execute the command ls -l and
prints the output to the users terminal.
*/
void cix_ls (client_socket& server) {
    cix_header header;
    header.command = cix_command::LS;
    outlog << "sending header " << header << endl;
    send_packet (server, &header, sizeof header);
    recv_packet (server, &header, sizeof header);
    outlog << "received header " << header << endl;
    if(header.command != cix_command::LSOUT){
        outlog << "sent LS, server did not return LSOUT" << endl;
        outlog << "server returned " << header << endl;
    }else {
        auto buffer = make_unique<char[]> (header.nbytes + 1);
        recv_packet (server, buffer.get(), header.nbytes);
        outlog << "received " << header.nbytes << " bytes" << endl;
        buffer[header.nbytes] = '\0';
        cout << buffer.get();
    }
}

//get command
/*
   Copy the file named filename on the remote server and create
   or overwrite a file of the same name in the current directory.
 */
void cix_get (client_socket& server, string filename) {
    //same code and structure of method as ls
    cix_header header;
    header.command = cix_command::GET;
    //format string
    snprintf(header.filename, filename.length() + 1, filename.c_str());
    //send packets and print messages
    outlog << "sending header " << header << endl;
    send_packet (server, &header, sizeof header);
    recv_packet (server, &header, sizeof header);
    outlog << "received header " << header << endl;
    if(header.command != cix_command::FILEOUT){
        outlog << "sent GET, server did not return FILEOUT" << endl;
        outlog << "server returned " << header << endl;
    }else {
        //create a buffer as in ls, but an auto doesnt work
        char buffer[header.nbytes + 1];
        recv_packet (server, buffer, header.nbytes);
        outlog << "received " << header.nbytes << " bytes" << endl;
        buffer[header.nbytes] = '\0';
        ofstream in(filename);
        // write the buffer and header bytes
        in.write(buffer, header.nbytes);
        //close ofstream
        in.close();
    }
}

//put command
/*
   Copies a local file into the socket and causes the
   remote server to create that file in its directory.
 */
void cix_put (client_socket& server, string filename) {
    cix_header header;
    header.command = cix_command::PUT;
    //format string
    snprintf(header.filename, filename.length() + 1, filename.c_str());
    ifstream in (header.filename);
    //check if file exists
    if (in.fail()){
        outlog << "Error: file does not exist" << endl;
        return;
    }
    //prevents infinite loop
    in.seekg(0, in.end);
    int size = in.tellg();
    char buffer[size];
    in.seekg(0, in.beg);
    in.read(buffer, size);
    header.nbytes = size;
    //send packets and print messages
    outlog << "sending header " << header << endl;
    send_packet (server, &header, sizeof header);
    send_packet (server, buffer, size);
    recv_packet (server, &header, sizeof header);
    outlog << "received header " << header << endl;
    //confirm if process worked, and print message accordingly
    if(header.command == cix_command::ACK){
        outlog << "sent PUT, server returned ACK" << endl;
        outlog << "server returned " << header << endl;
    //if it did not work
    } else if(header.command == cix_command::NAK){
        outlog << "sent PUT, server returned NAK" << endl;
        outlog << "server returned " << header << endl;
    }
    //close ifstream
    in.close();
}

//rm command
/*
   Causes the remote server to remove the file.
 */
void cix_rm (client_socket& server, string filename) {
    cix_header header;
    header.command = cix_command::RM;
    //format string
    snprintf(header.filename, filename.length() + 1, filename.c_str());
    //send packets and print messages
    outlog << "sending header " << header << endl;
    send_packet (server, &header, sizeof header);
    recv_packet (server, &header, sizeof header);
    outlog << "received header " << header << endl;
    //confirm if process worked, and print message accordingly
    if(header.command == cix_command::ACK){
        outlog << "sent RM, server returned ACK" << endl;
        outlog << "server returned " << header << endl;
    //if it did not work
    } else if(header.command == cix_command::NAK){
        outlog << "sent RM, server returned NAK" << endl;
        outlog << "server returned " << header << endl;
    }
}

void usage() {
    cerr << "Usage: " << outlog.execname() << " [host] [port]" << endl;
    throw cix_exit();
}

int main (int argc, char** argv) {
    outlog.execname (basename (argv[0]));
    outlog << "starting" << endl;
    vector<string> args (&argv[1], &argv[argc]);
    if(args.size() > 2) usage();
    string host = get_cix_server_host (args, 0);
    in_port_t port = get_cix_server_port (args, 1);
    vector<string> name_container;
    outlog << to_string (hostinfo()) << endl;
    try {
        outlog << "connecting to " << host << " port " << port << endl;
        client_socket server (host, port);
        outlog << "connected to " << to_string (server) << endl;
        for(;;){
            string line;
            getline (cin, line);
            if(cin.eof()) throw cix_exit();
            std::istringstream string_stream(line);
            std::string name;
            while(std::getline(string_stream, name, ' ')){
                name_container.push_back(name);
            }
            outlog << "command " << line << endl;
            const auto& itor = command_map.find (name_container[0]);
            cix_command cmd = itor == command_map.end()
                              ? cix_command::ERROR : itor->second;
            switch(cmd){
            case cix_command::EXIT:
                throw cix_exit();
                break;
            case cix_command::HELP:
                cix_help();
                name_container.clear();
                break;
            case cix_command::LS:
                cix_ls (server);
                name_container.clear();
                break;
            //add get put and rm cases
            case cix_command::GET: {
                cix_get(server, name_container[1]);
                name_container.clear();
                break;
            }
            case cix_command::PUT: {
                cix_put(server, name_container[1]);
                name_container.clear();
                break;
            }
            case cix_command::RM: {
                cix_rm(server, name_container[1]);
                name_container.clear();
                break;
            }
            default:
                outlog << line << ": invalid command" << endl;
                break;
            }
        }
    }catch(socket_error& error){
        outlog << error.what() << endl;
    }catch(cix_exit& error){
        outlog << "caught cix_exit" << endl;
    }
    outlog << "finishing" << endl;
    return 0;
}
