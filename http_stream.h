#ifndef HTTP_STREAM_H
#define HTTP_STREAM_H

#include <string>

#define BUFSIZE 4096

class HttpStream {

public:
    HttpStream(const std::string ip, const unsigned int port, const std::string file);
    ~HttpStream();

    bool start();
    void stop();

private:
    bool init();

    void GenHttpRequest();
    bool SendHttpRequest();
    bool RecvData();
    bool ConnectServer();

    void *run1();
    void run();
    static void *run0(void *args);

private:
    bool m_Going;
    bool m_Recving;

    std::string m_httpRequest;
    int m_sockfd;
    std::string m_ip;
    unsigned int m_port;
    //请求的文件名
    std::string m_file;

    char m_recvBuf[BUFSIZE];

    pthread_t m_tid;
};

#endif // HTTP_STREAM_H

