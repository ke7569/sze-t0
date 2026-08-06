
/**
 * MDSocket: udp & tcp public
 * @Author lzw (lizw@deepwin.ai)
 * @since   2019-07-24
 */

#pragma once

#include <string>
#include <chrono>
#include <ctime>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/unordered_map.hpp>
#include <boost/algorithm/string.hpp>
#include "longfist/LFConstants.h"
#include "longfist/LFDataStruct.h"

#include "json.hpp"
#include <vector>

using json = nlohmann::json;

using namespace boost::posix_time;
using namespace boost::gregorian;
using namespace std;

#define LEN_UDP_HDR			0x2A
#define UDP_DEST_PORT_START 0x24

#define Tranverse16(X) (((X & 0xff00) >> 8) | \
						((X & 0x00ff) << 8))


inline void Tranverse2Byte(void* pDest, void* pSrc)
{
    *(unsigned int*)pDest = Tranverse16(*(unsigned int*)pSrc);
}

struct CTcpData
{
    char *data=nullptr;
    int len=0;
    uint16_t src_port=-1;
    uint16_t dest_port=-1;
    char dest_addr[31]={'\0'};
    char src_addr[31]={'\0'};
};

int public_udp_connect(int &fd, string &mcast_addr, unsigned short port, string &nic_addr, bool isblock);

int SetMiscMode(int &nSocket, const char *if_name);

int set_tcp_mode(int &nSocket, const char *if_name);

int init_socket(int &nSocket, const char *if_name);

void process_packet(unsigned char*, int,struct CTcpData &tcp_data);
void do_ip_header(unsigned char* , int,struct CTcpData &tcp_data);
void do_tcp_packet(unsigned char* , int,struct CTcpData &tcp_data);
void do_udp_packet(unsigned char * , int,struct CTcpData &tcp_data);

ptime unix2ptime(long tick);

string get_short_name(const string &code);

string get_master_name(const string &code);

//bool file2json(const string& filename, json &ret_json);

bool json2file(const json &_json, const string& filename, bool bappend);

bool save_to_json(const string & filename);

bool save_data_to_json(bool &bsave_data, LFMarketDataField &data, bool bSrc);

int compare_diff_by_json(vector<char> &vdiff_price, vector<char> &vdiff_volume, const char *pToday);

inline void arrayReverse(char *buff, int len)
{
    char swap;
    for (int i = 0; i < --len; i++) {
        swap = buff[i];
        buff[i] = buff[len];
        buff[len] = swap;
    }
}

inline double convert_8(char *buff, bool isReverse)
{
    if (isReverse) {
        arrayReverse(buff, 8);
    }
    double ret = 0.0;
    memcpy(&ret, buff, 8);
    return ret;
}

inline int convert_4(char *buff, bool isReverse)
{
    if (isReverse) {
        arrayReverse(buff, 4);
    }
    int ret = 0.0;
    memcpy(&ret, buff, 4);
    return ret;
}

inline void conver_diff_8(char *pPos, bool isReverse, vector<char> &diff_double)
{
    if (isReverse) {
        for (int i = 0; i < 8; i++) {
            pPos[i] = pPos[i] - diff_double[i];
        }
    }
    else {
        for (int i = 0; i < 8; i++) {
            pPos[i] = pPos[i] - diff_double[7 - i];
        }
    }
}

inline void conver_diff_4(char *pPos, bool isReverse, vector<char> &diff_int)
{
    if (isReverse) {
        for (int i = 0; i < 4; i++) {
            pPos[i] = pPos[i] - diff_int[i];
        }
    }
    else {
        for (int i = 0; i < 4; i++) {
            pPos[i] = pPos[i] - diff_int[3 - i];
        }
    }
}


bool get_diff_price_volume(vector<char> &vDiffPrice, vector<char> &vDiffVolume, const char *pDayDate);


json quote_to_json(LFMarketDataField &data);

LFMarketDataField json_quote_to_json(json &j);