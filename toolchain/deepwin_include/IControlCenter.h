#pragma once

#include "YJJ_DECLARE.h"
#include "longfist/LFDataStruct.h"

YJJ_NAMESPACE_START

class IControlCenter
{
    public:
    IControlCenter(){}
    virtual ~IControlCenter(){}

    //md
    virtual void on_market_data(const LFMarketDataField* data, short source) = 0;

    virtual void on_market_data(const LFL2TradeField* data, short source) = 0;

    virtual void on_market_data(const LFL2OrderField* data, short source) = 0;
    
    virtual void on_market_data(const LFL2MarketDataField* data, short source) = 0;

    virtual void on_market_data(const LFL2IndexField* data, short source) = 0;


    // td, req
    virtual int insert_order(LFInputOrderField *data, short source, int rid, const std::string &name) = 0;
    virtual int cancel_order(LFOrderActionField *data, short source, int rid, const std::string &name) = 0;
    virtual int req_position(LFQryPositionField *data, short source, int rid, const std::string &name) = 0;
    //virtual int req_qry_order_info(short source, const string &instrument_id, const string& sys_id) = 0;
    virtual int req_qry_limit_price(LFQryLimitPrice *data, short source, int rid, const std::string &name) = 0;
    virtual int req_account(LFQryAccountField *data, short source, int rid, const std::string &name) = 0;

    virtual bool subscribe_market_data(short source, vector<string> &vcodes, vector<string> &vmarkets) = 0;
    // td, resp


    virtual void on_rtn_order(const LFRtnOrderField* data, int request_id, short source, long rcv_time) = 0;
    virtual void on_rtn_trade(const LFRtnTradeField* data, int request_id, short source, long rcv_time) = 0;
    virtual void on_rsp_order(const LFInputOrderField* data, int request_id, short source, long rcv_time, int errorId=0, const char* errorMsg=nullptr) = 0;
    //virtual void on_rsp_position(const PosHandlerPtr posMap, int request_id, short source, long rcv_time) = 0;
    virtual void on_rsp_exchange_state(const LFRspExchangeStateField *data, short source, long rcv_time) = 0;
    virtual void on_rsp_order_action(const LFOrderActionField* data, int request_id, short source, long rcv_time, int errorId = 0, const char* errorMsg = nullptr) = 0;

    virtual void on_rsp_limit_price(const LFMarketDataField* data, int request_id, short source, long rcv_time) = 0;

    virtual void on_rtn_trade_all(const LFRtnTradeField* data, int request_id, short source, long rcv_time) = 0;
    
    virtual void on_rsp_account(const LFRspAccountField* data, int request_id, short source, long rcv_time, int errorId = 0, const char* errorMsg = nullptr) = 0;

    virtual void on_rtn_pos_option(const LFRspPositionField* data, bool isLast, int request_id, short source, long rcv_time) = 0;


    // sub
    virtual bool add_md(short source, std::string name) = 0;
    virtual bool add_td(short source, std::string name) = 0;

    virtual IntPair get_rid_pair(std::string name) = 0;

    virtual bool set_str(void *p) = 0;	
};


YJJ_NAMESPACE_END
