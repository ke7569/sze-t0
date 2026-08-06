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

//
// Created by cjiang on 17/8/16.
//

#ifndef LONGFIST_TRANSFER_M_H
#define LONGFIST_TRANSFER_M_H

#include "LFConstants.h"
/*
 * 150006320 -> '15:00:06'
 */
inline void UpdateTimeFromMilSecInt(char* time_str, int time_int)
{
    time_int /= 1000;
    sprintf(time_str, "%02d:%02d:%02d", time_int / 10000, time_int / 100 % 100, time_int % 100);
}

/*
 * 150006320 -> 320
 */
inline void UpdateMillisecFromMilSecInt(int& mil_sec, int time_int)
{
    mil_sec = time_int % 1000;
}

/*
 * YYYYMMDDHHMMSSsss -> 'YYYYMMDD'
 */
inline void TradingDayFromLong(char* trading_day, long datetime)
{
    sprintf(trading_day, "%d", (int)(datetime / 1000000000));
}

/*
 * YYYYMMDDHHMMSSsss -> 'DD:HH:MM'
 */
inline void UpdateTimeFromLong(char* update_time, long datetime)
{
    int time_int = datetime % 1000000000;
    time_int /= 1000;
    sprintf(update_time, "%02d:%02d:%02d", time_int / 10000, time_int / 100 % 100, time_int % 100);
}

/*
 * YYYYMMDDHHMMSSsss -> sss
 */
inline void MillisecFromLong(int& mil_sec, long datetime)
{
    mil_sec = datetime % 1000;
}

#ifdef KUNGFU_LF_XTP
/**************************************************************/
/*                          XTP                               */
/**************************************************************/
inline void FromXtpExchangeId(char* exchangeId, XTP_EXCHANGE_TYPE exId)
{
    if (exId == XTP_EXCHANGE_SH)
        strcpy(exchangeId, EXCHANGE_SSE);
    else if (exId == XTP_EXCHANGE_SZ)
        strcpy(exchangeId, EXCHANGE_SZE);
    else
        strcpy(exchangeId, "None");
}

inline void ToXtpExchangeId(XTP_EXCHANGE_TYPE& exId, const char* exchangeId)
{
    if (!strcmp(exchangeId, EXCHANGE_SSE))
        exId = XTP_EXCHANGE_SH;
    else if (!strcmp(exchangeId, EXCHANGE_SZE))
        exId = XTP_EXCHANGE_SZ;
    else
        exId = XTP_EXCHANGE_UNKNOWN;
}

inline void FromXtpMarket(char* exchangeId, XTP_MARKET_TYPE marketId)
{
    if (marketId == XTP_MKT_SH_A)
        strcpy(exchangeId, EXCHANGE_SSE);
    else if (marketId == XTP_MKT_SZ_A)
        strcpy(exchangeId, EXCHANGE_SZE);
    else
        strcpy(exchangeId, "None");
}

inline void ToXtpMarket(XTP_MARKET_TYPE& marketId, const char* exchangeId)
{
    if (!strcmp(exchangeId, EXCHANGE_SSE))
        marketId = XTP_MKT_SH_A;
    else if (!strcmp(exchangeId, EXCHANGE_SZE))
        marketId = XTP_MKT_SZ_A;
    else
        marketId = XTP_MKT_UNKNOWN;
}

inline void FromXtpPosiDirection(char& posi_direction, XTP_MARKET_TYPE marketId)
{
    posi_direction = LF_CHAR_Net;
}

inline void FromXtpPriceType(char& order_price_type, XTP_PRICE_TYPE xtp_type)
{
    if (xtp_type == XTP_PRICE_LIMIT)
        order_price_type = LF_CHAR_LimitPrice;
    else
        order_price_type = LF_CHAR_BestPrice;
}

inline void ToXtpPriceType(XTP_PRICE_TYPE& xtp_type, char order_price_type, char TimeCondition, char VolumeCondition)
{
    if (order_price_type == LF_CHAR_LimitPrice && TimeCondition == LF_CHAR_GFD && VolumeCondition == LF_CHAR_AV){//limit
        xtp_type = XTP_PRICE_LIMIT;
    }
    else if (order_price_type == LF_CHAR_LimitPrice && TimeCondition == LF_CHAR_IOC && VolumeCondition == LF_CHAR_CV){ //fok
        xtp_type = XTP_PRICE_ALL_OR_CANCEL;
    }
    else if (order_price_type == LF_CHAR_LimitPrice && TimeCondition == LF_CHAR_IOC  && VolumeCondition == LF_CHAR_AV){//fak
        xtp_type = XTP_PRICE_BEST_OR_CANCEL;
    }
    else{
        xtp_type = XTP_PRICE_LIMIT;
    }
}

inline void FromXtpDirection(char& direction, XTP_SIDE_TYPE xtp_side)
{
    if (xtp_side == XTP_SIDE_BUY)
        direction = LF_CHAR_Buy;
    else if (xtp_side == XTP_SIDE_SELL)
        direction = LF_CHAR_Sell;
    else
        direction = '\0';
}

inline void ToXtpDirection(XTP_SIDE_TYPE& xtp_side, char direction)
{
    if (direction == LF_CHAR_Buy)
        xtp_side = XTP_SIDE_BUY;
    else if (direction == LF_CHAR_Sell)
        xtp_side = XTP_SIDE_SELL;
    else
        xtp_side = XTP_SIDE_UNKNOWN;
}

inline void FromXtpOrderStatus(char& orderStatus, byte ordStatus)
{
    if (ordStatus == XTP_ORDER_STATUS_INIT)
        orderStatus = LF_CHAR_NotTouched;
    else if (ordStatus == XTP_ORDER_STATUS_UNKNOWN)
        orderStatus = LF_CHAR_Unknown;
    else if (ordStatus == XTP_ORDER_STATUS_PARTTRADEDQUEUEING)
        orderStatus = LF_CHAR_PartTradedQueueing;
    else if (ordStatus == XTP_ORDER_STATUS_NOTRADEQUEUEING)
        orderStatus = LF_CHAR_NoTradeQueueing;
    else if (ordStatus == XTP_ORDER_STATUS_PARTTRADEDNOTQUEUEING)
        orderStatus = LF_CHAR_PartTradedNotQueueing;
    else if (ordStatus == XTP_ORDER_STATUS_CANCELED)
        orderStatus = LF_CHAR_Canceled;
    else if (ordStatus == XTP_ORDER_STATUS_ALLTRADED)
        orderStatus = LF_CHAR_AllTraded;
    else if (ordStatus == XTP_ORDER_STATUS_REJECTED)
        orderStatus = LF_CHAR_Error;
    else
        orderStatus = LF_CHAR_Error;
}

#endif

#ifdef KUNGFU_LF_HXIN
/**************************************************************/
/*                          HXIN                              */
/**************************************************************/
inline void FromHxinExchangeId(char* exchangeId, char exId)
{
    if (exId == '1')
        strcpy(exchangeId, EXCHANGE_SSE);
    else if (exId == '2')
        strcpy(exchangeId, EXCHANGE_SZE);
    else
        strcpy(exchangeId, "None");
}

inline void ToHxinExchangeId(char& exId, const char* exchangeId)
{
    if (strcmp(exchangeId, EXCHANGE_SSE)== 0 || strcmp(exchangeId, EXCHANGE_SSE_A)== 0)
        exId = '1';
    else if (strcmp(exchangeId, EXCHANGE_SZE) == 0 || strcmp(exchangeId, EXCHANGE_SZE_A) == 0)
        exId = '2';
    else
        exId ='0';
}

inline void ToHxinPriceType(const char* exchangeId, char TimeCondition, char VolumeCondition, char OrderPriceType, char& hxin_pricetype)
{
    if (0 == strcmp(exchangeId, EXCHANGE_SZE) || strcmp(exchangeId, EXCHANGE_SSE_A)== 0)
    {
        //fok or fak
        if(TimeCondition==LF_CHAR_IOC && VolumeCondition==LF_CHAR_CV && OrderPriceType==LF_CHAR_LimitPrice)
            hxin_pricetype = '1';//TORA_TSTP_OPT_AnyPrice
        else if(TimeCondition==LF_CHAR_IOC && VolumeCondition==LF_CHAR_AV && OrderPriceType==LF_CHAR_LimitPrice)
            hxin_pricetype = '1';//TORA_TSTP_OPT_AnyPrice
    }
}

inline void FromHxinOrderStatus(char& lfstatus, const char& hxstatus)
{
    if (hxstatus == '4')
        lfstatus = '0';
    else if (hxstatus == '3')
        lfstatus = '1';
    else if (hxstatus == '5')
        lfstatus = '2';
    else if (hxstatus == '2')
        lfstatus = '3';
    else if (hxstatus == '7')
        lfstatus = '4';
    else if (hxstatus == '6')
        lfstatus = '5';
    else
        lfstatus = 'a';
}

#endif

#ifdef KUNGFU_LF_TAC
/**************************************************************/
/*                          TAC                              */
/**************************************************************/
inline void FromTacExchangeId(char* exchangeId, char exId)
{
    if (exId == '5')
        strcpy(exchangeId, EXCHANGE_SSE);
    else if (exId == '4')
        strcpy(exchangeId, EXCHANGE_SZE);
    else
        strcpy(exchangeId, "None");
}

inline void ToTacExchangeId(char& exId, const char* exchangeId)
{
    if (!strcmp(exchangeId, EXCHANGE_SSE))
        exId = '5';
    else if (!strcmp(exchangeId, EXCHANGE_SZE))
        exId = '4';
    else
        exId ='5';
}


inline void FromTacOrderPriceType(char& pricetype, const char& orderptype)
{
    if (orderptype == '1' || orderptype == '2')
        pricetype = LF_CHAR_AnyPrice;
    else if (orderptype == '0' || orderptype == '7')
        pricetype = LF_CHAR_LimitPrice;
    else if (orderptype == '4')
        pricetype = LF_CHAR_BestPrice;
    else
       pricetype = '3';
}

inline void ToTacOrderPriceType(char& orderptype, const char& pricetype)
{
    if (pricetype == LF_CHAR_LimitPrice)
        orderptype = '0';
    else if (pricetype == LF_CHAR_BestPrice)
        orderptype = '6';
    else if (pricetype == LF_CHAR_AnyPrice)
        orderptype = '1';
    else
        orderptype ='4';    
}

inline void FromTacOrderStatus(char& lfstatus, const char& hxstatus)
{
    if (hxstatus == '0')
        lfstatus = '0';
    else if (hxstatus == '1')
        lfstatus = '1';
    else if (hxstatus == '2')
        lfstatus = '2';
    else if (hxstatus == '3')
        lfstatus = '3';
    else if (hxstatus == '4')
        lfstatus = '4';
    else if (hxstatus == '5')
        lfstatus = '5';
    else
        lfstatus = 'd';
}

#endif

//#define KUNGFU_LF_REM
#ifdef KUNGFU_LF_REM
#include <string>

inline EES_ExchangeID getExchangeIdREM(std::string exchange_name)
{
    if (exchange_name.compare("SZE") == 0 || exchange_name.compare("sze") == 0) //深圳证券交易所
        return EES_ExchangeID_sz_cs;
    else if (exchange_name.compare("CFFEX") == 0 || exchange_name.compare("cffex") == 0) //中国金融期货交易所
        return EES_ExchangeID_cffex;
    else if (exchange_name.compare("SSE") == 0 || exchange_name.compare("sse") == 0) //上海证券交易所
        return EES_ExchangeID_sh_cs;
    else if (exchange_name.compare("DCE") == 0 || exchange_name.compare("dce") == 0) //大连商品交易所
        return EES_ExchangeID_dce;
    else if (exchange_name.compare("SHFE") == 0 || exchange_name.compare("shfe") == 0) //上海期货交易所
        return EES_ExchangeID_shfe;
    else if (exchange_name.compare("CZCE") == 0 || exchange_name.compare("czce") == 0) //郑州商品交易所
        return EES_ExchangeID_zcze;
    else if (exchange_name.compare("INE") == 0 || exchange_name.compare("ine") == 0) //能源中心
        return EES_ExchangeID_ine;
    else if (exchange_name.compare("SGE") == 0 || exchange_name.compare("sge") == 0) //上海金交所
        return EES_ExchangeID_sge;
    return EES_ExchangeID_done_away;
}

inline EES_SecType getSecTypeByExchangeIDREM(EES_ExchangeID id)
{
    if (EES_ExchangeID_sh_cs == id || EES_ExchangeID_sz_cs == id) {
        return EES_SecType_cs;
    }
    else if (id > EES_ExchangeID_sge){
        return EES_SecType_options;
    }
    return EES_SecType_fut;
}

inline const char * getExchangeNameByIdREM(EES_ExchangeID id)
{
    if (id == EES_ExchangeID_sz_cs) {
        return "SZE";
    }
    else if (id == EES_ExchangeID_cffex) {
        return "CFFEX";
    }
    else if (id == EES_ExchangeID_sh_cs) {
        return "SSE";
    }
    else if (id == EES_ExchangeID_dce) {
        return "DCE";
    }
    else if (id == EES_ExchangeID_shfe) {
        return "SHFE";
    }
    else if (id == EES_ExchangeID_zcze) {
        return "CZCE";
    }
    else if (id == EES_ExchangeID_ine) {
        return "INE";
    }
    else if (id == EES_ExchangeID_sge) {
        return "SGE";
    }
    return "";
}

#endif 

#endif //LONGFIST_TRANSFER_M_H
