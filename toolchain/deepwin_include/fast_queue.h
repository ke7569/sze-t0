#pragma once
#include <thread>
#include <list>
#include <mutex>
#include <functional>
#include <cstring>

using namespace std;


#define MAX_SOCKET_BUFF_LEN (1024 * 1024 * 96 * 3)

#pragma pack(push)
#pragma pack(1)
struct CFastQueueHead
{
    int8_t msg_type=0;
    int32_t msg_len = 0;
    int32_t push_time=0;
};
#pragma pack(pop)

using handle_single_type = void (*)(char *, void *);


class CFastQueue
{
    public:
    CFastQueue(bool bs=false, bool is_time_flag=true):bsleep(bs),is_time_flag(is_time_flag){
        recvBuffer = new char[MAX_SOCKET_BUFF_LEN];
        memset(recvBuffer, '\0', MAX_SOCKET_BUFF_LEN);
    }
    ~CFastQueue(){
        handle_thread->join();
        if (recvBuffer != nullptr){
            delete recvBuffer;
            recvBuffer = nullptr;
        }
    }

    void init(handle_single_type func, void *pUser){
        isRunning = true;
        handle_single_t = func;
        this->pUser = pUser;
        handle_thread = std::shared_ptr<std::thread>(new std::thread(std::bind(&CFastQueue::handle_msg_thread, this)));
    }

   void move(const char *buff, int32_t bytes, int8_t type=0);

   void stop(){
       isRunning = false;
   }

private:
    int handle_msg();
    void handle_msg_thread();

    void *pUser=nullptr;
    handle_single_type handle_single_t;
    char *recvBuffer = nullptr;
    bool handle_error = false;

    int recvBufferLen = 0;
    int handle_recvBufferLen = 0;

    CFastQueueHead head;
    const int headLen = sizeof(CFastQueueHead);
    bool isRunning=false;
    bool bsleep=false;
    bool is_time_flag=true;
    
    std::mutex mtx;
    std::shared_ptr<std::thread> handle_thread;
};
