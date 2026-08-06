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
 * ITDEngine: base class of all trade engine.
 * @Author lizw(lizw@deepwin.ai)
 * @since   2022-11
 */

#ifndef WINGCHUN_ITDENGINE_H
#define WINGCHUN_ITDENGINE_H

#include "IEngine.h"
#include "PosHandler.hpp"
#include "Timer.h"
#include "TDUserStruct.h"
#include "TDUserPos.h"
#include "longfist/LFDataStruct.h"
#include <vector>
#include <string>
#include <cstring>
//#include <boost/unordered_set.hpp>
#include <unordered_set>
#include <unordered_map>
#include "filehelper.h"



#define ORDER_MISC_INFO_SIZE 29

#define CODE_MINIMUM_UNIT_CHANGE "MinPoint"

WC_NAMESPACE_START

using namespace std;

using kungfu::yijinjing::PosHandlerPtr;
using kungfu::yijinjing::FeeHandlerPtr;
using kungfu::yijinjing::PosHandler;
using kungfu::yijinjing::FeeHandler;


struct CChannelInfo
{
    long start_time_optional = -1;
    long optional_timeout = -1;
    int i_channel = 0;
    string str_channel = "";

    inline void reset() {
        start_time_optional = -1;
    }

};


struct CChannel
{
    void init_v(vector<int> v_c, long optional_timeout) {
        v_upper_channel.resize(v_c.size());
        v_type = 0;
        max_channel_size = v_c.size();
        for (size_t i = 0; i < v_c.size(); i++) {
            CChannelInfo &info = v_upper_channel[i];
            info.optional_timeout = optional_timeout* 1000000000;
            info.i_channel = v_c[i];
        }
    }

    void init_v(vector<string> v_c, long optional_timeout) {
        v_upper_channel.resize(v_c.size());
        v_type = 1;
        max_channel_size = v_c.size();
        for (size_t i = 0; i < v_c.size(); i++) {
            CChannelInfo &info = v_upper_channel[i];
            info.optional_timeout = optional_timeout* 1000000000;
            info.str_channel = v_c[i];
        }
    }

    bool is_use() {
        return (max_channel_size > 0) ? true : false;
    }

    void do_switch_by_event(long rcv_time, int index);

    int & get_i_channel(long rcv_time);

    string & get_str_channel(long rcv_time);

    /////private
    int v_i = 0;
    int max_channel_size = 0;

    int channel_switch_force();
    int channel_switch_by_timeout(long rcv_time);

    int v_type = 0;
    bool bfirst = true;
    vector<CChannelInfo> v_upper_channel;

};


struct CReqPosFlag
{
    bool flag_long=false;
    bool flag_short=false;
    bool flag_net=false;
    bool is_req_short = false;
    bool is_req_net = false;
    vector<LFRspPositionField> vrsp_long;
    vector<LFRspPositionField> vrsp_short;
    vector<LFRspPositionField> vrsp_net;
    std::unordered_set<string> req_codes;

    CReqPosFlag(bool req_short, bool req_net){
        this->is_req_short = req_short;
        this->is_req_net = req_net;
    }

    inline bool is_done(){
        bool ret = flag_long;
        if (this->is_req_short){
            ret = ret && flag_short;
        }
        if (this->is_req_net){
            ret = ret && flag_net;
        }
        return ret;
    }

    inline void push_vec(vector<LFRspPositionField> &v, LFRspPositionField &res)
    {
        bool bF=false;
        for (auto &c:v){
            if (strcmp(c.InstrumentID,res.InstrumentID) == 0){
                bF=true;
                break;
            }
        }
        if (!bF){
            v.push_back(res);
        }else{
            std::cout <<"push_vec:"<<v.size()<<","<<res.InstrumentID<<std::endl;
        }
    }

    void push(LFRspPositionField &res,bool isLast){
        if (strlen(res.InstrumentID) <= 2){
            if (res.PosiDirection == LF_CHAR_Long){
                flag_long = isLast;
            }
            else if (res.PosiDirection == LF_CHAR_Short){
                flag_short = isLast;
            }
            else if (res.PosiDirection == LF_CHAR_Net){
                flag_net = isLast;
            }                  
            return;
        }
        req_codes.emplace(res.InstrumentID);

        if (res.PosiDirection == LF_CHAR_Long){
            //vrsp_long.push_back(res);
            push_vec(vrsp_long, res);
            flag_long = isLast;
        }
        else if (res.PosiDirection == LF_CHAR_Short){
            //vrsp_short.push_back(res);
            push_vec(vrsp_short, res);
            flag_short = isLast;
        }
        else if (res.PosiDirection == LF_CHAR_Net){
            //vrsp_net.push_back(res);
            push_vec(vrsp_net, res);
            flag_net = isLast;
        }
        //std::cout << "req_codes size="<<req_codes.size()<<","<<flag_long<<","<<flag_short<<","<<res.InstrumentID<<std::endl;
    }

    void create_empty_resp(vector<LFRspPositionField> &vrsp, const LfPosiDirectionType &pos_dir){
        //std::cout << "create_empty_resp req_codes size="<<req_codes.size()<<std::endl;
        for (auto &code : req_codes){
            bool bfind=false;
            for (auto &v:vrsp){
                if (code == v.InstrumentID){
                    bfind=true;
                    break;
                }
            }
            //std::cout <<"create_empty_resp:" <<bfind <<","<<code<<","<<vrsp.size() <<","<<(int)pos_dir<<std::endl;
            if (!bfind){
                
                LFRspPositionField res = {};
                strcpy(res.InstrumentID, code.c_str());
                res.PosiDirection = pos_dir;
                vrsp.emplace_back(res);
            }          
        }   
    }

    void create_all_pos(){
        create_empty_resp(vrsp_long, LF_CHAR_Long);
        if (is_req_short){
            create_empty_resp(vrsp_short, LF_CHAR_Short);
        }
        if (is_req_net){
            create_empty_resp(vrsp_net, LF_CHAR_Net);
        }
    }
};

struct CPosRspMana
{
    
    boost::unordered_map<int, CReqPosFlag *> mapPosMana;

    void push_req(bool req_short_flag=true,bool req_net_flag=true, int rid=-1){
        CReqPosFlag *p = new CReqPosFlag(req_short_flag, req_net_flag);
        mapPosMana[rid]=p;
    }

    CReqPosFlag * push_rsp(LFRspPositionField &pos, int rid, bool isLast){
        auto itPos = mapPosMana.find(rid);
        if (itPos == mapPosMana.end()){
            return nullptr;
        }

        CReqPosFlag *p = itPos->second;
        p->push(pos, isLast);  
        return p;
    }

    void clear(int rid){
        auto itPos = mapPosMana.find(rid);
        if (itPos != mapPosMana.end()){
            delete itPos->second;
            itPos->second=nullptr;
            mapPosMana.erase(itPos);  
        }
    }
};

/**
 * client info, store in trade engine
 */
struct ClientInfoUnit
{
    bool is_alive;
    int  journal_index;
    int  account_index;
    int  rid_start;
    int  rid_end;
    PosHandlerPtr pos_handler;
    vector<string> codes;
    TDUserPosHelperPtr td_pos_helper;
};

/**
 * Trade account information,
 * used when connect or login
 */
struct TradeAccount
{
    char BrokerID[19];
    char UserID[16];
    char InvestorID[19];
    char BusinessUnit[21];
    char Password[33];
    char AccountID[13];
    FeeHandlerPtr fee_handler;
    CChannel Channel;
    //for rem
    CChannel ChannelSHFE;
    CChannel ChannelINE;
    CChannel ChannelCFFEX;
    CChannel ChannelDCE;
    CChannel ChannelCZCE;

    CChannel &get_channel(const char *pExchangeID, short source) {
        if (source == 60 || source == 61 || source == 180 || source == 130) {
            if (0 == strcmp(pExchangeID, "SHFE"))
            {
	            return ChannelSHFE;
            }
            else if (0 == strcmp(pExchangeID, "INE"))
            {
	            return ChannelINE;
            }
            else if (0 == strcmp(pExchangeID, "CFFEX"))
            {
	            return ChannelCFFEX;
            }
            else if (0 == strcmp(pExchangeID, "DCE"))
            {
	            return ChannelDCE;
            }
            else if (0 == strcmp(pExchangeID, "CZCE") || (0 == strcmp(pExchangeID, "ZCE")))
            {
	            return ChannelCZCE;
            }
        }

        return Channel;
    }
    // add order & cancel num
    int32_t order_n=0;
    int32_t cancel_n=0;
    int32_t total_n=0;
};


/**
 * Base class of all trade engines
 */
class ITDEngine: public IEngine
{
protected:
    /** information about all existing clients. */
    map<string, ClientInfoUnit > clients; // clientName: (exist, idx)
    map<int, string> clients_name;

    ClientInfoUnit * get_client_info_unit(int rid) {
        auto it = clients_name.find(rid);
        if (it != clients_name.end()) {
            auto iter = clients.find(it->second);
            if (iter != clients.end()) {
                return &(iter->second);
            }
        }
        return nullptr;
    }

    /** trade account */
    vector<TradeAccount> accounts;
    /** -1 if do not accept unregistered account, else return index of accounts */
    int default_account_index;
    /** default handler, will be utilized if user do not assign fee structure in account info */
    FeeHandlerPtr default_fee_handler;
    /** trade engine user info helper */
    TDUserInfoHelperPtr user_helper;
    TDOrderPriceHelperPtr order_price_helper;

    // pos long,short,net
    CPosRspMana pos_rsp_mana;

    void push_pos_req(bool req_short_flag=true,bool req_net_flag=true, int rid=-1){
        pos_rsp_mana.push_req(req_short_flag, req_net_flag, rid);
    }

    bool push_pos_resp(LFRspPositionField &pos, int rid, bool isLast)
    {
        CReqPosFlag *p = pos_rsp_mana.push_rsp(pos, rid, isLast);
        if (p == nullptr){
            return false;
        }
        if (p->is_done()){
            push_all_pos_(p, rid);
        }
        return true;
    }

    void push_all_pos_(CReqPosFlag *p,int rid){
        p->create_all_pos();
        //std::cout <<"l:"<<p->vrsp_long.size()<<","<<p->vrsp_short.size()<<","<<p->vrsp_net.size()<<std::endl;
        push_vpos_(p->vrsp_long, rid);
        if (p->is_req_short){
            push_vpos_(p->vrsp_short, rid);
        }
        if (p->is_req_net){
            push_vpos_(p->vrsp_net, rid);
        }

        pos_rsp_mana.clear(rid);  
    }

    void push_vpos_(vector<LFRspPositionField> &vesp, int rid)
    {
        for (size_t i=0;i<vesp.size();i++){
            auto &push_res=vesp[i];
            bool bIsLast=false;
            if ((i+1) == vesp.size()){
                bIsLast=true;
            }
            on_rsp_position(&push_res, bIsLast, rid, 0, nullptr);
        }
    }

    int max_cancel_num = 485;
    /** request_id, incremental*/
    int request_id;
    /** local id, incremental */
    long local_id;
    /** current nano time */
    long cur_time;

	vector<TDUserAvailableHelperPtr> td_available_helpers; // size == accounts // todo multi

    TDOrderInfo *get_order(long local_id);
    
    void on_rsp_position_local(const LFQryPositionField* data, int account_index, int requestId, TDUserPosHelperPtr &td_pos_helper);
    void on_rsp_positon_push(TDUserPosHelperPtr &td_pos_helper, TDOrderInfo *order_info, short source_id);

    void update_cancel_push(TDUserPosHelperPtr &td_pos_helper, TDOrderInfo *order_info, int tdorder = 0, int tdcancel = 0);

    inline double change_funds(const string& ticker, double price, double volume) {
        return default_fee_handler->get_contract_rate(ticker, false) * price * volume;
    }

	int get_account_index(const string &pInvestorID);

    inline void sub_available(TDOrderInfo *order_info){
        if (order_info->change < 0.0001){
            return;
        }
        td_available_helpers[order_info->account_index]->update_available(order_info->OffsetFlag, order_info->change, IS_CHNAGE_AVAILABLE);
    }

    inline void modify_available(TDOrderInfo *order_info){
        if (order_info->change < 0.0001){
            return;
        }
        if ((order_info->status == LF_CHAR_Canceled) || (order_info->status == LF_CHAR_PartTradedNotQueueing) 
            || (order_info->status == LF_CHAR_Error) || (order_info->status == LF_CHAR_NoTradeNotQueueing)){
            double change = 0 - order_info->change;
            if (order_info->td_traded > 0 && order_info->td_order > 0){
                change *= (double)(order_info->td_order - order_info->td_traded) / (double)order_info->td_order;
            }
            td_available_helpers[order_info->account_index]->update_available(order_info->OffsetFlag, change, IS_CHNAGE_AVAILABLE);
        }
    }

    inline void modify_order_cancel_count(int idx, const char *ticker, int order_c=1, int cancel_c=1){
        td_available_helpers[idx]->add_order_cancel_count(ticker, order_c, cancel_c); 
    }

    void disconnected(){
        LFRspExchangeStateField data = {};
        data.State = 999;// 链接断开
        //writer->write_frame(&data, sizeof(LFRspExchangeStateField), source_id, MSG_TYPE_LF_RSP_STATE, true, -1);   
    }
    
    void login_ok(){
        LFRspExchangeStateField data = {};
        data.State = 888;// 登录成功
        //writer->write_frame(&data, sizeof(LFRspExchangeStateField), source_id, MSG_TYPE_LF_RSP_STATE, true, -1);
    }

    inline void re_login(int sec=30){
        long cc = 0;
        while (!is_logged_in()) {
            if (cc == 0){
                cc = yijinjing::getNanoTime()/1000000000;
                KF_LOG_INFO(logger, "[ITDEngine::listening] try_login start:"<<cc);
                try_login();
                KF_LOG_INFO(logger, "[ITDEngine::listening] try_login end");
            }
            if ((yijinjing::getNanoTime()/1000000000 - cc) < sec){
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }else{
                cc = 0;
            }
        }
    }

public:
    // add strategy, return true if added successfully.
    virtual bool add_client(const string& name, int rid_s, int rid_e);
    // remove strategy
    virtual bool remove_client(const string& name);

protected:
    /** default constructor */
    ITDEngine(short source);

    /** load config information */
    virtual void load(const json& j_config);
    /** init reader and writer */
    virtual void init();
    /** load TradeAccount from json */
    virtual TradeAccount load_account(int idx, const json& j_account);

public:
    // config and account related
    /** resize account number */
    virtual void resize_accounts(int account_num) = 0;
    /** connect each account and api with timeout as wait interval*/
    virtual void connect(long timeout_nsec) = 0;
    /** login all account and api with timeout as wait interval */
    virtual void login(long timeout_nsec) = 0;
    /** send logout request for each api */
    virtual void logout() = 0;
    /** release api items */
    virtual void release_api() = 0;
    /** is every accounts connected? */
    virtual bool is_connected() const = 0;
    /** is every accounts logged in? */
    virtual bool is_logged_in() const = 0;

    /** after login pre-run something before start service*/
    virtual void pre_run() {};

protected:
    int pre_run_rid = 1;
    bool b_req_investor_position = false;
    bool b_req_qry_order = false;
    bool b_req_qry_trade = false;
    bool b_req_qry_account = false;

    void req_sleep(bool &flag);

public:
    /** some clean up when day switch */
    virtual void on_switch_day() {};
    /** on market data */
    virtual void on_market_data(const LFMarketDataField* data, long rcv_time) {};

    // virtual interfaces for all trade engines
    /** request investor's existing position */
    virtual void req_investor_position(const LFQryPositionField* data, int account_index, int requestId) = 0;
    /** request account info */
    virtual void req_qry_account(const LFQryAccountField* data, int account_index, int requestId) = 0;
    /** insert order */
    virtual int req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time) = 0;
    /** request order action (only cancel is accomplished) */
    virtual void req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time) = 0;

    virtual void req_qry_order_info(const LFQryOrderField* data, int account_index, int requestId) {};

    virtual void req_qry_limit_price(const LFQryLimitPrice* data, int account_index, int requestId) {};


    /** on investor position, engine (on_data) */
    void on_rsp_position(const LFRspPositionField* pos, bool isLast, int requestId, int errorId=0, const char* errorMsg=nullptr);
    /** on rsp order insert, engine (on_data) */
    void on_rsp_order_insert(LFInputOrderField* order, int &requestId,int errorId=0, const char* errorMsg=nullptr);
    /** on rsp order action, engine (on_data) */
    void on_rsp_order_action(LFOrderActionField* action, int requestId, int errorId=0, const char* errorMsg=nullptr);
    /** on rsp account info, engine (on_data) */
    void on_rsp_account(const LFRspAccountField* account, bool isLast, int requestId, int errorId=0, const char* errorMsg=nullptr);
 
    /** on rtn order, engine (on_data) */
    void on_rtn_order(LFRtnOrderField* rtn_order, int &rid);
    /** on rtn trade, engine (on_data) */
    void on_rtn_trade(LFRtnTradeField* rtn_trade, int &rid);


    virtual int insert_order(LFInputOrderField *order,int requestId, const std::string &name);
    virtual int cancel_order(LFOrderActionField *data, int requestId, const std::string &name);
    virtual int req_position(LFQryPositionField *data, int requestId, const std::string &name);
    virtual int req_qry_limit_price(LFQryLimitPrice *data, int requestId, const std::string &name);
    virtual int req_account(LFQryAccountField *data, int requestId, const std::string &name);
      
};

DECLARE_PTR(ITDEngine);


template<typename T>
ITDEngine *td_get_obj(IControlCenter *pcc, std::string name)
{
    T *p = (T *)(new T());
    p->set_cc(pcc);

    std::string json_str;
    bool bok=get_json_config("td", name, json_str);
    std::cout <<"init="<<bok<<","<<json_str<<std::endl;
    p->initialize(json_str);
    p->start();
    std::cout <<json_str<<",end\n";
    return dynamic_cast<ITDEngine *>(p);
}

WC_NAMESPACE_END

#endif //PROJECT_ITDENGINE_H
