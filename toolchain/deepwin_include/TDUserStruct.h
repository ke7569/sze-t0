/*****************************************************************************
 * Copyright [2022] [deepwin.ai]
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *****************************************************************************/

/**
 * TDUserStruct: struct of Trade engine internal usage.
 * @Author lizw(lizw@deepwin.ai)
 * @since   September, 2017
 */

#ifndef WINGCHUN_TDENGINESTRUCT_H
#define WINGCHUN_TDENGINESTRUCT_H

#include "WC_DECLARE.h"
#include "constants.h"
#include "json.hpp"
#include "longfist/LFConstants.h"
#include <boost/unordered_map.hpp>
#include <boost/atomic.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>

using json = nlohmann::json;

WC_NAMESPACE_START

#define ORDER_INFO_RAW '\0'
#define ORDER_INFO_TICKER_LIMIT 15

inline bool is_order_end_status(char status){
        if (status == LF_CHAR_AllTraded || (status == LF_CHAR_Canceled 
            || status == LF_CHAR_Error || status == LF_CHAR_PartTradedNotQueueing 
            || status == LF_CHAR_NoTradeNotQueueing )){
            return true;
        }
        return false;
}

inline bool is_order_start_status(char status){
        if ((status == LF_CHAR_Unknown || status == LF_CHAR_Touched 
            || status == LF_CHAR_NotTouched || status == LF_CHAR_OrderInserted 
            || status == LF_CHAR_OrderAccepted))
        {
            return true;
        }
        return false;
}

struct TDOrderInfo
{
    /** request id of the order */
    int order_id;
    /** assigned by TradeEngine */
    long local_id;
    /** LfOrderStatusType, ORDER_INFO_RAW means nothing */
    char status;
    /** InstrumentID name */
    char ticker[ORDER_INFO_TICKER_LIMIT];
    char order_sys_id[30];  // xele add
    char order_no[30];      // da add
    char exchange_code[11]; // da add
    char server_flag;       // yisheng add

    LfPosiDirectionType PosiDirection; //持仓多空方向
    LfOffsetFlagType OffsetFlag;
    LfTimeConditionType TimeCondition; //有效期类型

    // td_order = td_cancel + td_traded
    int td_traded = 0;       // 已今成交量
    int td_order = 0;

    double price = 0.0;
    double change = 0.0;
    int account_index = 0;
    int channel_index = 0;

    TDOrderInfo(){};
    TDOrderInfo(const char *pticker, const char *pOrderSysID, const char *pOrderNo, const char *pExchangeID)
    {
        if (pticker != nullptr && pticker[0] != '\0')
        {
            strncpy(ticker, pticker,30);
        }
        if (pOrderSysID != nullptr && pOrderSysID[0] != '\0')
        {
            strncpy(order_sys_id, pOrderSysID,30);
        }
        if (pOrderNo != nullptr && pOrderNo[0] != '\0')
        {
            strncpy(order_no, pOrderNo,30);
        }
        if (pExchangeID != nullptr && pExchangeID[0] != '\0')
        {
            strncpy(exchange_code, pExchangeID,11);
        }
    };

    inline void update_trade_num(int traded)
    {
        td_traded += traded;
    };

} __attribute__((packed));

/** for one single strategy,
 * how many available orders can be supported.
 * available order */
#define AVAILABLE_ORDER_LIMIT 10000000
/** enough space to store json for pos map */
#define POS_JSON_STR_LENGTH 100000

/**
 * Trade engine user info
 * will contain info for risk management.
 */
#define TD_INFO_NORMAL 'n'
#define TD_INFO_WRITING 'w'
#define TD_INFO_FORCE_QUIT 'e'

struct TDUserInfo
{
    /** TD_INFO_NORMAL / TD_INFO_WRITING / TD_INFO_FORCE_QUIT, others raw */
    char status;
    /** last start time */
    long start_time;
    /** last end time */
    long end_time;
    /** last order request id (located order index)*/
    int last_order_index;
    /** strategy name */
    char name[JOURNAL_SHORT_NAME_MAX_LENGTH];
    /** folder name */
    char folder[JOURNAL_FOLDER_MAX_LENGTH];
    /** strategy-wise position */
    char pos_str[POS_JSON_STR_LENGTH];
    /** order info hash-map base */
    TDOrderInfo *orders=nullptr;
    TDUserInfo(){
        orders = new TDOrderInfo[AVAILABLE_ORDER_LIMIT];
        memset(orders, '\0', AVAILABLE_ORDER_LIMIT * sizeof(TDOrderInfo));
    }
    ~TDUserInfo(){
        if (orders != nullptr){
            delete orders;
            orders=nullptr;
        }
    }

} __attribute__((packed));

#define TD_USER_INFO_FOLDER KUNGFU_JOURNAL_FOLDER "user/" /**< where we put user info files */
/** user info file length is determined */
const int USER_INFO_SIZE = sizeof(TDUserInfo) + 1024;

struct CSimplePair
{
    long key = 0;
    int  v = 0;
};

class CSimplePairHelper
{
    public:
    CSimplePairHelper(){
        vsp.resize(AVAILABLE_ORDER_LIMIT);
    }

    ~CSimplePairHelper(){
        vsp.clear();
    }

    inline void set(long key,int v){
        if (index >= AVAILABLE_ORDER_LIMIT){
            return ; 
        }        
        auto &sp = vsp[index];
        sp.key = key;
        sp.v=v;
        index++;
    }

    inline int get(long key){
        for (int i = index-1; i>=0; i--){
            auto &kv = vsp[i];
            if (kv.key == key){
                return kv.v;
            }
        }
        return -1;
    }

    private:
    vector<CSimplePair> vsp;
    int index=0;
};

class TDUserInfoHelper
{
    /** all method that may modify data must be private
     * to make it modifiable by ITDEngine and td_pos_tool only */
    friend class ITDEngine;
    friend bool set_pos_tool(const string &user_name, short source, const string &file_name, bool is_csv);

private:
    /** internal map: user_name -> memory */
    map<string, TDUserInfo *> address_book;
    /** source */
    short source;
    /* local_id->rid->TDOrderInfo*/
    CSimplePairHelper vLocalRid;

protected:
    /** internal locate at readable order with order_id, return nullptr if no available */
    TDOrderInfo *locate_readable(const string &user_name, int order_id) const;
    /** internal locate at writable order position with order_id, return nullptr if no available */
    TDOrderInfo *locate_writable(const string &user_name, int order_id);

protected:
    /** constructor with "write" authority */
    TDUserInfoHelper(short source);
    /** load user info of user_name */
    void load(const string &user_name, bool need_write = true);
    /** remove user from list, detach memory */
    void remove(const string &user_name);
    /** necessary cleanup when to a trading day is over */
    void switch_day();
    /** record order info */
    TDOrderInfo *record_order(const string &user_name, long local_id, int order_id, const char *ticker, int order, LfPosiDirectionType PosiDirection, LfOffsetFlagType OffsetFlag, double price);
    /** set position json string */
    void set_pos(const string &user_name, const json &pos);
    /** clean up user info */
    void clean_up(TDUserInfo *info);
    /** get info block address for internal usage only */
    inline TDUserInfo *get_user_info(const string &user_name) const
    {
        auto iter = address_book.find(user_name);
        if (iter == address_book.end())
            return nullptr;
        return iter->second;
    }

public:
    /** constructor with read-ability only */
    TDUserInfoHelper(short source, const string &user_name);
    /** get const user info */
    const TDUserInfo *get_const_user_info(const string &user_name) const;
    /** default destructor */
    ~TDUserInfoHelper();

    /** get order info, return true if local_id and ticker are set properly */
    TDOrderInfo *get_order(const string &user_name, int order_id);
    int get_order_id(long local_id);

    /** set order status when updated by return-order */
    bool set_order(const string &user_name, int order_id, const TDOrderInfo *order);

    /** get position json */
    json get_pos(const string &user_name) const;
    /** get existing orders of this user */
    vector<int> get_existing_orders(const string &user_name) const;

    void erase_order_info(long local_id);
};

DECLARE_PTR(TDUserInfoHelper);

WC_NAMESPACE_END

#endif //WINGCHUN_TDENGINESTRUCT_H
