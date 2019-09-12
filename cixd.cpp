// $Id: cixd.cpp,v 1.8 2019-04-05 15:04:28-07 - - $
//Sam Guyette (sguyette@ucsc.edu)
//Ryan McCrory (rmccrory@ucsc.edu)

#include <iostream>
#include <string>
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
//exit
struct cix_exit : public exception {};

void reply_ls (accepted_socket& client_sock, cix_header& header) {
    const char* ls_cmd = "ls -l 2>&1";
    FILE* ls_pipe = popen (ls_cmd, "r");
    if(ls_pipe == NULL){
        outlog << "ls -l: popen failed: " << strerror (errno) << endl;
        header.command = cix_command::NAK;
        header.nbytes = errno;
        send_packet (client_sock, &header, sizeof header);
        return;
    }
    string ls_output;
    char buffer[0x1000];
    for(;;){
        char* rc = fgets (buffer, sizeof buffer, ls_pipe);
        if(rc == nullptr) break;
        ls_output.append (buffer);
    }
    int status = pclose (ls_pipe);
    if(status < 0) outlog << ls_cmd << ": " << strerror (errno) << endl;
    else outlog << ls_cmd << ": exit " << (status >> 8)
                << " signal " << (status & 0x7F)
                << " core " << (status >> 7 & 1) << endl;
    header.command = cix_command::LSOUT;
    header.nbytes = ls_output.size();
    memset (header.filename, 0, FILENAME_SIZE);
    outlog << "sending header " << header << endl;
    send_packet (client_sock, &header, sizeof header);
    send_packet (client_sock, ls_output.c_str(), ls_output.size());
    outlog << "sent " << ls_output.size() << " bytes" << endl;
}

//get
void reply_get (accepted_socket& client_sock, cix_header& header) {
    //open ifstream
    ifstream in(header.filename);
    //if file doesnt exist
    if(in.fail()){
        //set command to NAK
        header.command = cix_command::NAK;
        //send packet
        send_packet (client_sock, &header, sizeof header);
    //if file does exist
    } else {
        //prevent infinite loop
        in.seekg(0, in.end);
        size_t size = in.tellg();
        in.seekg(0, in.beg);
        char buffer[size];
        in.read(buffer, size);
        //set command to FILEOUT
        header.command = cix_command::FILEOUT;
        header.nbytes = size;
        //send packets and print messages
        outlog << "sending header " << header << endl;
        send_packet (client_sock, &header, sizeof header);
        send_packet (client_sock, buffer, size);
        outlog << "received header " << header << endl;
    }
    //close ifstream
    in.close();
}

//put
void reply_put (accepted_socket& client_sock, cix_header& header) {
    char packet[header.nbytes];
    //receive packet
    recv_packet (client_sock, packet, header.nbytes);
    //open ofstream
    ofstream in(header.filename);
    //if file doesnt exist
    if(in.fail()){
        header.command = cix_command::NAK;
    //if it does
    } else {
        header.command = cix_command::ACK;
        in.write(packet, header.nbytes);
    }
    //send header and print message
    outlog << "sending header " << header << endl;
    send_packet (client_sock, &header, sizeof header);
    //close ofstream
    in.close();
}

//rm
void reply_rm (accepted_socket& client_sock, cix_header& header) {
    bool remove = unlink(header.filename);
    //if the file doesntr exist, print message
    if(remove){
        outlog << "RM failed, " << header.filename 
        << " does not exist" << endl;
        //set command to NAK
        header.command = cix_command::NAK;
    //if it does and is removed, print message
    } else {
        outlog << "removed " << header.filename << endl;
        //set command to ACK
        header.command = cix_command::ACK;
    }
    //send packet
    send_packet(client_sock, &header, sizeof header);
}

void run_server (accepted_socket& client_sock) {
    outlog.execname (outlog.execname() + "-server");
    outlog << "connected to " << to_string (client_sock) << endl;
    try {
        for(;;){
            cix_header header;
            recv_packet (client_sock, &header, sizeof header);
            outlog << "received header " << header << endl;
            switch(header.command){
            case cix_command::LS:
                reply_ls (client_sock, header);
                break;
            //add get put and rm
            case cix_command::GET: {
                reply_get(client_sock, header);
                break;
            }
            case cix_command::PUT: {
                reply_put(client_sock, header);
                break;
            }
            case cix_command::RM: {
                reply_rm(client_sock, header);
                break;
            }
            default:
                outlog << "invalid client header:" << header << endl;
                break;
            }
        }
    }catch(socket_error& error){
        outlog << error.what() << endl;
    }catch(cix_exit& error){
        outlog << "caught cix_exit" << endl;
    }
    outlog << "finishing" << endl;
    throw cix_exit();
}

void fork_cixserver (server_socket& server, accepted_socket& accept) {
    pid_t pid = fork();
    if(pid == 0){  // child
        server.close();
        run_server (accept);
        throw cix_exit();
    }else {
        accept.close();
        if(pid < 0){
            outlog << "fork failed: " << strerror (errno) << endl;
        }else {
            outlog << "forked cixserver pid " << pid << endl;
        }
    }
}

void reap_zombies() {
    for(;;){
        int status;
        pid_t child = waitpid (-1, &status, WNOHANG);
        if(child <= 0) break;
        outlog << "child " << child
               << " exit " << (status >> 8)
               << " signal " << (status & 0x7F)
               << " core " << (status >> 7 & 1) << endl;
    }
}

void signal_handler (int signal) {
    outlog << "signal_handler: caught " << strsignal (signal) << endl;
    reap_zombies();
}

void signal_action (int signal, void (*handler)(int)) {
    struct sigaction action;
    action.sa_handler = handler;
    sigfillset (&action.sa_mask);
    action.sa_flags = 0;
    int rc = sigaction (signal, &action, nullptr);
    if(rc < 0) outlog << "sigaction " << strsignal (signal)
                      << " failed: " << strerror (errno) << endl;
}

int main (int argc, char** argv) {
    outlog.execname (basename (argv[0]));
    outlog << "starting" << endl;
    vector<string> args (&argv[1], &argv[argc]);
    signal_action (SIGCHLD, signal_handler);
    in_port_t port = get_cix_server_port (args, 0);
    try {
        server_socket listener (port);
        for(;;){
            outlog << to_string (hostinfo()) << " accepting port "
                   << to_string (port) << endl;
            accepted_socket client_sock;
            for(;;){
                try {
                    listener.accept (client_sock);
                    break;
                }catch(socket_sys_error& error){
                    switch(error.sys_errno){
                    case EINTR:
                        outlog << "listener.accept caught "
                               << strerror (EINTR) << endl;
                        break;
                    default:
                        throw;
                    }
                }
            }
            outlog << "accepted " << to_string (client_sock) << endl;
            try {
                fork_cixserver (listener, client_sock);
                reap_zombies();
            }catch(socket_error& error){
                outlog << error.what() << endl;
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
