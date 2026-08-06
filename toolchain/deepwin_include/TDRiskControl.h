#pragma once
#include <iostream>
#include <sys/time.h>
#include "WC_DECLARE.h"

using namespace std;

WC_NAMESPACE_START

class TDRiskControl
{
    private:
        // 每秒最大报单数
        int maxorder_persec;
        // 报撤比   order/cancel
        double ordercancel_rate; 

        string ordertime;
        int ordercount;

    public:
        TDRiskControl(int pmaxorder_persec,double pordercancel_rate);
        ~TDRiskControl();
        // 报单前检查
        bool checkinsertorder();
        // 撤单前检查
        bool checkcandelorder(int  ordercount,int  cancelcount);


};

class CQuerySpeedControl
{
    public:
    CQuerySpeedControl(long set_min_time);
    ~CQuerySpeedControl();
    bool check(long cur);
    long get_last_quey(){
        return last_query;
    }
    private:
    long last_query=0;
    long minTime=1000000000;//1sec
};

DECLARE_PTR(TDRiskControl);

DECLARE_PTR(CQuerySpeedControl);

WC_NAMESPACE_END

