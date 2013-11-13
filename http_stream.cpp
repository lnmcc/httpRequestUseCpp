#include "http_stream.h"

#include <stdio.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAXRETRY 10

using namespace std;

HttpStream::HttpStream(const string ip, const unsigned int port, const string file) {

    m_ip = ip;
    m_port = port;
    m_file = file;

    m_Going = false;
    m_Recving = false;

    m_sockfd = -1;

	GenHttpRequest();
}

HttpStream::~HttpStream() {

    if(m_sockfd >= 0) {
        close(m_sockfd);
    }
}

void* HttpStream::run0(void *args) {

    HttpStream *p = (HttpStream*)args;
    p->run1();
    return p;
}

void* HttpStream::run1() {

    m_tid = pthread_self();
    run();
    m_tid = 0;
    pthread_exit(NULL);
    return NULL;
}

void HttpStream::run() {

    while(m_Going) {

        m_Recving = true;

        RecvData();

        if(m_sockfd >= 0) {
            close(m_sockfd);
        }

        if(!init()) {
			m_Recving = false;
            m_Going = false;
        }
    }

	if(m_sockfd >= 0) {
		close(m_sockfd);
	}
}

bool HttpStream::start() {

    if(!init()) {
		return false;	
	}

	m_Going = true;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    int ret = pthread_create(&m_tid, &attr, run0, this);
    if(ret != 0) {
        std::cerr << __FILE__ << ":" << __LINE__ << ": Thread start error!" << std::endl;
        pthread_attr_destroy(&attr);
        return false;
    }

    pthread_attr_destroy(&attr);
    return true;
}

void HttpStream::stop() {

    m_Recving = false;
    m_Going = false;
}

bool HttpStream::init() {

    unsigned int retry = 0;

    while(!ConnectServer() && retry < MAXRETRY) {

        sleep(1);
        retry++;
    }

    if(retry == MAXRETRY) {
        return false;
    }

    retry = 0;

    while(!SendHttpRequest() && retry < MAXRETRY) {

        sleep(1);
        retry++;
    }

    if(retry == MAXRETRY) {
        return false;
    }
	
	return true;
}

void HttpStream::GenHttpRequest() {

    stringstream int2str;
    int2str << m_port;
    string port = int2str.str();

    m_httpRequest.append("GET /");
    m_httpRequest.append(m_file);
    m_httpRequest.append(" HTTP/1.1\n");
    m_httpRequest.append("Host: ");
    m_httpRequest.append(m_ip);
    m_httpRequest.append(":");
    m_httpRequest.append(port);
    m_httpRequest.append("\n");
    m_httpRequest.append("User-Agent: Mozilla/5.0 (X11; Ubuntu; Linux i686; rv:23.0) Gecko/20100101 Firefox/23.0\n");
    m_httpRequest.append("Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\n");
    m_httpRequest.append("Accept-Language: en-US,en;q=0.5\n");
    m_httpRequest.append("Accept-Encoding: gzip, deflate\n");
    m_httpRequest.append("Accept-Encoding: gzip, deflate\n");
    m_httpRequest.append("Connection: keep-alive\n\n");

    cerr << "HttpReuest: " << m_httpRequest << endl;
}

bool HttpStream::ConnectServer() {

    struct sockaddr_in servaddr;

    m_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(m_sockfd < 0) {
        return false;
    }

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(m_port);

    if(inet_pton(AF_INET, m_ip.c_str(), &servaddr.sin_addr) <= 0) {
        cerr << __FILE__ << ":" << __LINE__ << "inet_pton error" << endl;
        close(m_sockfd);
        return false;
    }

    if(connect(m_sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        cerr << __FILE__ << ":" << __LINE__ << "connect error" << endl;
        close(m_sockfd);
        return false;
    }

    cerr << "connect to; " << m_ip << ":" << m_port << " OK!!!" << endl;

    return true;
}

bool HttpStream::SendHttpRequest() {

    if(m_sockfd < 0) {
        cerr << __FILE__ << ":" << __LINE__ << "sendHttpRequest failed, Wrong socket!!!" << endl;
        return false;
    }

    int ret = write(m_sockfd, m_httpRequest.c_str(), m_httpRequest.length());
    if(ret < 0) {
        cerr << __FILE__ << ":" << __LINE__ << "write http request error!!!" << endl;
        return false;
    }

	//Send FIN
	//shutdown(m_sockfd, SHUT_WR);

    if(ret < m_httpRequest.length()) {
        cerr << __FILE__ << ":" << __LINE__ << "write http request less than required!!!" << endl;
        return false;
    }

    cerr << "SendHttpRequest OK" << endl;

    return true;
}

bool HttpStream::RecvData() {

    fd_set fdSet;
    int status, readBytes;
    bool httpHead = true;

    struct timeval tv;
    while(m_Recving) {

	    tv.tv_sec = 5;
		tv.tv_usec = 0;

        FD_ZERO(&fdSet);
        FD_SET(m_sockfd, &fdSet);

        status = select(m_sockfd + 1, &fdSet, NULL, NULL, &tv);

        switch(status) {

        case -1:
            cerr << "select: Error!!!" << endl;
            return false;
        case 0:
            cerr << "select: No FD is active!!!" << endl;
            break;
        default:
            if(FD_ISSET(m_sockfd, &fdSet)) {

                memset(m_recvBuf, 0, 4096);

                readBytes = read(m_sockfd, m_recvBuf, 4096);
                if(readBytes > 0) {

                    if(httpHead) {
                        char *ptr = strstr(m_recvBuf, "\r\n\r\n");

                        if(ptr != NULL) {
                            httpHead = false;
                            //fprintf(stderr, "found blank line\n");
                            fprintf(stderr, "line: %s\n", ptr + 4);

                        } else {
                            cerr << __FILE__ << ":" << __LINE__ << "Wrong http header" << endl;
                        }

                    } else {
                        cerr << m_recvBuf;

                    }

                } else if(readBytes == 0){
                    cerr << "receive end!!!" << endl;
                    return  true;
                } else {
                    cerr << "receive error!!!" << endl;
                    return false;
                }
            }
            break;
        }//end switch

    }// end while

    return false;
}

int main(int argc, char **argv) {

	HttpStream httpStream(argv[1], atoi(argv[2]), argv[3]);
	httpStream.start();
	
	sleep(10 * 60 * 60);
	httpStream.stop();

	pause();
	return 0;
}

