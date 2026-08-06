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
 * IMDEngine: base class of all market data engine.
 * @Author lizw(lizw@deepwin.ai)
 * @since   2022-11
 */

#ifndef WINGCHUN_IMDENGINE_H
#define WINGCHUN_IMDENGINE_H

#include "IEngine.h"
#include "longfist/LFDataStruct.h"
#include "MDSocket.h"
#include "fast_queue.h"
#include <unordered_set>
#include "filehelper.h"

WC_NAMESPACE_START

typedef std::map<string, int> SubCountMap;

/**
 * Base class of all market data engine
 */
class IMDEngine: public IEngine
{
private:
    /** internal structure for subscription */
    vector<string> subs_tickers;
    /** internal structure for subscription */
    vector<string> subs_markets;
    /** internal structure for auto subscription when re-begin md engine */
    std::map<short, SubCountMap> history_subs; // { msg_type: { ticker@market : sub_count } }

    std::unordered_set<string> all_codes;

protected:
    /** default constructor */
    IMDEngine(short source);
    /** pre-run after login */
    virtual void pre_run();
    /** init reader and writer */
    virtual void init();
    /** subscribe historically-subscribed tickers */
    void subscribeHistorySubs();
public:
    void sub(const char *pcode){
        if (pcode != nullptr){
            all_codes.insert(pcode);
        }
    }
    void sub(const std::string &pcode){
        sub(pcode.c_str());
    }
    
    bool is_sub(const char *pcode){
        auto it = all_codes.find(pcode);
        if (it != all_codes.end()){
            return true;
        }
        return false;
    }

    bool is_sub(const std::string &pcode){
        return is_sub(pcode.c_str());
    }
    /** subscribe market data, should be override by child-class */
    virtual void subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
    {
        KF_LOG_ERROR(logger, "subscribe market data not supported here!");
    }
    /** subscribe l2 data, should be override by child-class */
    virtual void subscribeL2MD(const vector<string>& instruments, const vector<string>& markets)
    {
        KF_LOG_ERROR(logger, "subscribe l2 data not supported here!");
    }
    /** subscribe index data, should be override by child-class */
    virtual void subscribeIndex(const vector<string>& instruments, const vector<string>& markets)
    {
        KF_LOG_ERROR(logger, "subscribe index data not supported here!");
    }
    /** subscribe order and trade data, should be override by child-class */
    virtual void subscribeOrderTrade(const vector<string>& instruments, const vector<string>& markets)
    {
        KF_LOG_ERROR(logger, "subscribe order trade data not supported here!");
    }
    /** on market data, engine (on_data) */
    void on_market_data(const LFMarketDataField* data);

    /** on market data, engine (on_data) */
    void on_market_data(const LFL2TradeField* data);

    void on_market_data(const LFL2OrderField* data);
    
    void on_market_data(const LFL2MarketDataField* data);

    void on_market_data(const LFL2IndexField* data);
     
};

template<typename T>
IMDEngine *md_get_obj(IControlCenter *pcc, std::string name)
{
    T *p = (T *)(new T());
    p->set_cc(pcc);

    std::string json_str;
    bool bok=get_json_config("md", name, json_str);
    std::cout <<"init="<<bok<<","<<json_str<<std::endl;
    p->initialize(json_str);
    p->start();
    std::cout <<json_str<<",end\n";
    return dynamic_cast<IMDEngine *>(p);
}


DECLARE_PTR(IMDEngine);

WC_NAMESPACE_END

#endif //WINGCHUN_IMDENGINE_H
