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
 * TDUserPos: 
 * @Author lzw (lizw@deepwin.ai)
 * @since   2018-12
 */

#pragma once

#include "WC_DECLARE.h"
#include "longfist/LFDataStruct.h"
#include "TDUserStruct.h"
#include <boost/unordered_map.hpp>
#include <boost/atomic.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <algorithm>
#include <vector>
#include <atomic>
using namespace std;

using json = nlohmann::json;

WC_NAMESPACE_START

#define IS_CHNAGE_AVAILABLE '#'

struct COderCancelCount
{
    COderCancelCount():cancel_count(0){}
    ~COderCancelCount(){}
    COderCancelCount(const COderCancelCount&ori){
        this->order_count = ori.order_count;
        strcpy(this->code, ori.code);
        this->cancel_count = ori.cancel_count.load();
    }

    COderCancelCount& operator=(const COderCancelCount& ori){
        if (this != &ori){
            this->order_count = ori.order_count;
            strcpy(this->code, ori.code);
            this->cancel_count = ori.cancel_count.load();
        }
        return *this;
    }

    char code[16]={'\0'};
    int order_count = 0;

    atomic<int> cancel_count;
};

class CCOderCancelCountHelper
{
    public:
    CCOderCancelCountHelper(){
        vcc.resize(5000);  
    }

    ~CCOderCancelCountHelper(){
        vcc.clear();
    }

    inline COderCancelCount & get(const char *pcode){
        if (index >= 5000){
            return vcc[0];
        }
        for (size_t i=0;i < index;i++){
            if (strcmp(pcode, vcc[i].code) == 0){
                return vcc[i];
            }
        }
        auto &v = vcc[index];
        strcpy(v.code,pcode);
        index++;
        return v;
       
    }

    inline void add(const char *pcode, int order_c, int cancel_c){
        COderCancelCount &cc = get(pcode);
        cc.cancel_count += cancel_c;
        cc.order_count += order_c;
    }

    inline int get_cancel_count(const char *pcode){
        for (size_t i=0;i < index;i++){
            if (strcmp(pcode, vcc[i].code) == 0){
                return vcc[i].cancel_count;
            }
        }
        return 0;
    }


private:
    vector<COderCancelCount> vcc;
    int index=0;
};

class TDUserAvailableHelper
{
private:
    double  available = 0;

    CCOderCancelCountHelper vcount;
    //mutable boost::recursive_mutex the_mutex_;

public:
    TDUserAvailableHelper() {}
    ~TDUserAvailableHelper() {}
public:

    inline void init_available(double available) {
        this->available = available;
    }

    inline double get_available() {
        return available;
    }

    inline int get_cancel_count(const char *ticker) {
        return vcount.get_cancel_count(ticker);
    }

    inline int get_cancel_num(const char *ticker) {
        return vcount.get_cancel_count(ticker);
    }    

    void add_order_cancel_count(const char *pticker, int order_c=1, int cancel_c=1){
        if (order_c == 0 && cancel_c == 0) {
            return;
        }
        vcount.add(pticker, order_c, cancel_c);
    }

    inline int add_cancel_num(const char *pcode, int i = 1) {
        return get_cancel_count(pcode);
    }
    
    void update_order_cancel(const char *pcode, LfPosiDirectionType dir, int oc, int cc){
        add_order_cancel_count(pcode, oc, cc);
    }

    void update_traded(const char *pcode, int v){

    }

    void get_order_cancel_count(const char *pcode, int &oc, int &cc){
        auto &v = vcount.get(pcode);
        oc = v.order_count;
        cc = v.cancel_count;
    }

    double update_available(LfOffsetFlagType offset, double change_value, char isChange);
};

DECLARE_PTR(TDUserAvailableHelper);

/*
���ʵ���߼���
case 1 : open+#  ,�������
case 2 : closeYesterday+#���������
case 3 : closeToday+# , �������
case 4 : closeToday û��#, ������
case 5 : ���������µ�ʧ�ܣ���ԭ�����ӻ��߼��٣��� ��ǰ��������Ե���case1��case2��case3��
*/
class PosManagerType_
{
    public:
    PosManagerType_(){
        vp.resize(max_count);  
        //memset(vp.data(),'\0', max_count*sizeof(LFRspPositionField));
    }

    ~PosManagerType_(){
        vp.clear();
    }

    inline LFRspPositionField & get(const char *pcode){
        if (index >= max_count){
            std::cout <<"[PosManagerType_] error:"<<index<<","<<pcode<<std::endl;
            return vp[0];
        }
        for (size_t i=0;i < index;i++){
            if (strcmp(pcode, vp[i].InstrumentID) == 0){
                return vp[i];
            }
        }
        auto &v = vp[index];
        strcpy(v.InstrumentID,pcode);
        index++;
        return v;
    }

    inline LFRspPositionField &add(const LFRspPositionField &ori){
        auto &dest = get(ori.InstrumentID);
        dest = ori;
        return dest;
    }

    inline vector<LFRspPositionField> get_all(){
        vector<LFRspPositionField> vret;
        if (index > 0){
            vret.reserve(index);
        }
        for (size_t i=0;i < index;i++){
            vret.emplace_back(vp[i]);
        }
        return vret;
    }


private:
    const int max_count=5000;
    vector<LFRspPositionField> vp;
    int index=0;
};

class TDUserPosHelper
{
private:
    /** internal map: InstrumentID -> TDUserPos */
    PosManagerType_ buy_pm;
    PosManagerType_ sell_pm;
    PosManagerType_ net_pm;//for stock

    PosManagerType_ &getPosManger(const LfPosiDirectionType &dir);

public:
    /** constructor with "write" authority */
    TDUserPosHelper();

    /** default destructor */
    ~TDUserPosHelper();
public:
    vector<LFRspPositionField> get_pos_by_code(const char * strInstrumentID);

    vector<LFRspPositionField> get_pos_all();

    LFRspPositionField *get_pos(const char *pticker,LfPosiDirectionType direction, LfOffsetFlagType offset);

public: 

    LFRspPositionField init(const LFRspPositionField *pos, bool bIsLast); 

    LFRspPositionField *update_pos(const char *pticker,LfPosiDirectionType direction, LfOffsetFlagType offset, int volume);
    
    LfPosiDirectionType  get_direction(LfPosiDirectionType direction, LfOffsetFlagType offset, bool isStock=false);
private:

    bool update_pos_(LFRspPositionField &pos, LfOffsetFlagType offset, int volume);
    bool update_pos_stock(LFRspPositionField &pos, LfOffsetFlagType offset, int volume);
};

DECLARE_PTR(TDUserPosHelper);



struct OrderPrice
{
    long local_id = -1;
    double price = -1.0;
    explicit OrderPrice(long id, double p) :local_id(id), price(p) {};
};

class TDOrderPriceHelper
{
public:
    TDOrderPriceHelper(){}
    ~TDOrderPriceHelper() {};
    //typedef std::vector<OrderPrice> vOrderPriceType_;
    //typedef std::vector<double> vOrderPriceType_;
    typedef boost::unordered_map<long, double> vOrderPriceType_;
    typedef boost::unordered_map<string, vOrderPriceType_ > mapOrderPriceType_;

private:
    //mutable boost::recursive_mutex the_mutex_;
    mapOrderPriceType_ map_buy_queue;
    mapOrderPriceType_ map_sell_queue;

    boost::unordered_map<string, double> mapMinPoint;
    double get_min_point(const char *pticker);

    mutable boost::recursive_mutex the_mutex_;
    bool b_self_trade=true;

public:
    void set_self_trade_flag(bool bflag){
        b_self_trade = bflag;
    }
    void init_min_point(const json &j_config);
    double get_buy_max(const char *pticker);
    double get_sell_min(const char *pticker);

    double record(char dir, const char *pticker, long local_id, double price);
    int remove(char dir, const char *pticker, long local_id);


};


DECLARE_PTR(TDOrderPriceHelper);

WC_NAMESPACE_END

