#include <iostream>
#include <algorithm>
#include <string>
#include <fstream>
#include <streambuf>
#include <set>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <getopt.h>

#define POLL_SIZE 2048

using namespace std;

int set_nonblock(int fd) {
    int flags;
    #if defined (O_NONBLOCK)
        if (-1 == (flags = fcntl(fd, F_GETFL, 0))) flags = 0;
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    #else
        flags = 1;
        return ioctl(fd, FIOBIO, &flags);
    #endif
}

int main(int argc, char *argv[]) {
    
    daemon(1, 1);
    
    // parsing parameters
    int opt;
    uint32_t ip = htonl(INADDR_ANY);
    uint16_t port = htons(80); 
    string dir = ".";
    while ((opt = getopt(argc, argv, "h:p:d:")) != -1) {
        switch (opt) {
            case 'h':
                ip = inet_addr(optarg);
                break;
            case 'p':
                port = htons(atoi(optarg));
                break;
            case 'd':
                dir = optarg;
                break;
            default: /* '?' */
                cerr << "Usage: " << argv[0] << " -h <ip> -p <port> -d <directory>\n";
                exit(EXIT_FAILURE);
        }
    }
                
    // getting socket ready
    int master_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    set<int> slave_sockets;
    struct sockaddr_in sock_addr;
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = port;
    sock_addr.sin_addr.s_addr = ip;
    bind(master_socket, (struct sockaddr *)(&sock_addr), sizeof(sock_addr));
    
    set_nonblock(master_socket);
    listen(master_socket, SOMAXCONN);
    
    struct pollfd poll_set[POLL_SIZE];
    poll_set[0].fd = master_socket;
    poll_set[0].events = POLLIN;
    
    // handling connections
    int pid, status;
    while (true) {
        unsigned int index = 1;
        for (auto it = slave_sockets.begin(); it != slave_sockets.end(); it++) {
            poll_set[index].fd = *it;
            poll_set[index++].events = POLLIN;
        }
        
        unsigned int set_size = 1+slave_sockets.size();
        poll(poll_set, set_size, -1);
        
        for (auto i = 0; i < set_size; i++) {
            if (poll_set[i].revents & POLLIN) {
                if (i) {
                    if ((pid = fork()) == 0) {   
                        close(master_socket);
                        static char buf[1024];
                        int recv_size = recv(poll_set[i].fd, buf, 1024, MSG_NOSIGNAL);
                        if (recv_size == 0 && errno != EAGAIN) {
                            shutdown(poll_set[i].fd, SHUT_RDWR);
                            close(poll_set[i].fd);
                            slave_sockets.erase(poll_set[i].fd);
                        } else if (recv_size > 0){ 
                            // parsing request
                            string path(buf);
                            path = path.substr(path.find(" ")+1);
                            path = dir+path.substr(0, path.find_first_of(" ?\t\r\n"));
                            ifstream fs(path);
                            string res;
                            if (fs.good()) { 
                                // preparing OK answer
                                res = "HTTP/1.0 200 OK\r\n"
                                "Content-length: ";
                                string file((istreambuf_iterator<char>(fs)),         istreambuf_iterator<char>());
                                res += to_string(file.size());
                                res += "\r\nContent-Type: text/html\r\n\r\n";
                                res += file;
                            } else {
                                res = "HTTP/1.0 404 NOT FOUND\r\n"
                                "Content-length: 0\r\n"
                                "Content-Type: text/html\r\n\r\n";
                            }
                            const char *msg = res.c_str();
                            send(poll_set[i].fd, msg, res.size(), MSG_NOSIGNAL);
                        }
                        exit(EXIT_SUCCESS);
                    } else {
                        close(poll_set[i].fd);
                        slave_sockets.erase(poll_set[i].fd);
                        waitpid(pid, &status, 0);
                    }
                } else {
                    int slave_socket = accept(master_socket, 0, 0);
                    set_nonblock(slave_socket);
                    slave_sockets.insert(slave_socket);
                }
            }
        }
    }
    return 0;
}
