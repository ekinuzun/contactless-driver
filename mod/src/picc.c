/*
* Name: PICC source file
* Date: 2012/12/04
* Author: Alex Wang
* Version: 1.0
*/



#include <linux/string.h>

#include "common.h"
#include "picc.h"
#include "pn512app.h"
#include "pn512.h"
#include "delay.h"
#include "typeA.h"
#include "typeB.h"
#include "felica.h"
#include "topaz.h"
#include "pcsc.h"
#include "mifare.h"
#include "part4.h"
#include "debug.h"


extern void RunPiccPoll(void);





struct pcdInfo pcd;
struct piccInfo picc;



const UINT8 vendorName[] = {'A', 'C', 'S'};
const UINT8 productName[] = {'A', 'C', 'R', '8', '9', '0'};
const UINT8 driverVersion[] = {'1', '.', '0', '0'};
const UINT8 firmwareVersion[] = {'A', 'C', 'R', '8', '9', '0', ' ', '1', '0', '0'};
const UINT8 IFDVersion[] = {0x01, 0x00, 0x00};

const UINT16 FSCConvertTbl[9] = {16, 24, 32, 40, 48, 64, 96, 128, 256};







/******************************************************************/
//       PICC reset
/******************************************************************/
void PiccReset(void)
{
    RegWrite(REG_TXCONTROL, 0x00);    // Turn off the Antenna
    Delay1ms(9);
    RegWrite(REG_TXCONTROL, 0x83);    // Turn on the Antenna
    picc.states = PICC_IDLE;
}


void PiccPoll(void)
{
    UINT8 piccCode = 0x00;
    

    PrtMsg(DBGL4, "%s: start, piccType = %02X\n", __FUNCTION__, picc.type);

    if(BITISCLEAR(picc.status, BIT_PRESENT))        //Case 1: No card present before 
    {
        picc.type = PICC_ABSENT;
        AntennaPower(1);

        if(BITISSET(pcd.filterType, BIT_TYPEAPOLL))
        {
            Delay1ms(10);
            PollTypeATags();
        }
        if((picc.type == PICC_ABSENT) && (BITISSET(pcd.filterType, BIT_TYPEBPOLL)))
        {
            Delay1ms(6);
            PollTypeBTags();
        }
        if((picc.type == PICC_ABSENT) && (BITISSET(pcd.filterType, BIT_FEL212POLL)))
        {
            Delay1ms(5);
            PollFeliCaTags(PASSDEPI_212);
        }
        if((picc.type == PICC_ABSENT) && (BITISSET(pcd.filterType, BIT_FEL424POLL)))
        {
            Delay1ms(5);
            PollFeliCaTags(PASSDEPI_424);
        }
        if((picc.type == PICC_ABSENT) && (BITISSET(pcd.filterType, BIT_TOPAZPOLL)))
        {
            Delay1ms(5);
            PollTopazTags();
        }

        
        if(picc.type == PICC_ABSENT)
        {
            //No Tag found
            CLEAR_BIT(picc.status, BIT_PRESENT);
            CLEAR_BIT(picc.status, BIT_FIRSTINSERT);
            CLEAR_BIT(picc.status, BIT_ACTIVATED);
            mifare.keyValid = 0x00;
        }
        else
        {
            //Tag found
            SET_BIT(picc.status, BIT_PRESENT);        // Card Inserted
            SET_BIT(picc.status, BIT_FIRSTINSERT);
            SET_BIT(picc.status, BIT_ACTIVATED);
            SET_BIT(picc.status, BIT_SLOTCHANGE);
            pcd.pollDelay = 1000;
        }
    }
    else            // card present before
    {
        if(picc.type == PICC_MIFARE)
        {
            PiccReset();
            Delay1ms(6);
            if(MifareSelect() != ERROR_NO)
            {
                if(MifareSelect() == ERROR_NO)
                {
                    piccCode = 0x01;
                }
                else
                {
                    piccCode = 0x00;
                }
            }
            else
            {
                piccCode = 0x01;
            }
        }
        else if((picc.type == PICC_TYPEA_TCL) || (picc.type == PICC_TYPEB_TCL))
        {
            // ISO/IEC 14443-4 PICC
            if(picc.states == PICC_ACTIVATED)
            {
                if(TCL_Select(0xB2) != ERROR_NO)        // R-block: ACK
                {
                    if(TCL_Select(0xB2) == ERROR_NO)
                    {
                        piccCode = 0x01;
                    }
                    else
                    {
                        piccCode = 0x00;
                    }
                }
                else
                {
                    piccCode = 0x01;
                }
            }
            else
            {
                if(picc.type == PICC_TYPEA_TCL)
                {
                    if(picc.states == PICC_POWEROFF)
                    {
                        AntennaPower(1);
                        Delay1ms(10);
                    }

                    if(PcdRequestA(PICC_WUPA, picc.ATQA) == ERROR_NOTAG)
                    {
                        if(PcdRequestA(PICC_WUPA, picc.ATQA) == ERROR_NOTAG)
                        {
                            piccCode = 0x00;
                        }
                        else
                        {
                            piccCode = 0x01;
                        }
                    }
                    else
                    {
                        piccCode = 0x01;
                    }

                }
                else
                {
                    if(picc.states == PICC_POWEROFF)
                    {
                        AntennaPower(1);
                        Delay1us(100);
                    }

                    if(PiccRequestB(PICC_WUPB,0) == ERROR_NOTAG)
                    {
                        if(PiccRequestB(PICC_WUPB,0) == ERROR_NOTAG)
                        {
                            piccCode = 0x00;
                        }
                        else
                        {
                            piccCode = 0x01;
                        }
                    }
                    else
                    {
                        piccCode = 0x01;
                    }
                }
            }
        }  
        else if((picc.type == PICC_FELICA212) || (picc.type == PICC_FELICA424))   // add--s
        {
            if(picc.states == PICC_POWEROFF)
            {
                AntennaPower(1);
                Delay1ms(6);
            }
            if(FelReqResponse() == ERROR_NO)
            {
                piccCode = 0x01;
            }
            else
            {
                piccCode = 0x00;
            }
        }
        else if(picc.type == PICC_TOPAZ)
        {
            if(picc.states == PICC_POWEROFF)
            {
                AntennaPower(1);
                Delay1ms(6);
            }
            if(PcdRequestA(PICC_WUPA, picc.ATQA) == ERROR_NOTAG)
            {
                if(PcdRequestA(PICC_WUPA, picc.ATQA) == ERROR_NOTAG)
                {
                    piccCode = 0x00;
                }
                else
                {
                    piccCode = 0x01;
                }
            }
            else
            {
                piccCode = 0x01;
            }
        }

        // Success a tag is still there
        if(piccCode == 0x01)
        {
            SET_BIT(picc.status, BIT_PRESENT);
        }
        else
        {  
            CLEAR_BIT(picc.status, BIT_PRESENT);
            CLEAR_BIT(picc.status, BIT_FIRSTINSERT);
            CLEAR_BIT(picc.status, BIT_ACTIVATED);
            SET_BIT(picc.status, BIT_SLOTCHANGE);

            picc.type = PICC_ABSENT;
            pcd.curSpeed = 0x80;
        }
    }

    PrtMsg(DBGL6, "%s: exit, piccType = %02X\n", __FUNCTION__, picc.type);
    
}


UINT8 PiccPowerON(UINT8 *atrBuf, UINT16 *atrLen)
{
    UINT8 ret = SLOT_NO_ERROR;


    PrtMsg(DBGL4, "%s: start\n", __FUNCTION__);

    if(BITISCLEAR(pcd.fgPoll, BIT_AUTOPOLL))
    {
        PiccPoll();
    }

 
//    if(BITISSET(picc.status, BIT_FIRSTINSERT))
//    {
//        CLEAR_BIT(picc.status, BIT_FIRSTINSERT); 
//    }
//    else
//    {
//        AntennaPower(0);
//        Delay1ms(9);
//    }
    if(picc.states == PICC_POWEROFF)
    {
        AntennaPower(1);
        if((picc.type == PICC_MIFARE) || (picc.type == PICC_TYPEA_TCL)) 
        {
            mifare.keyValid = 0x00;
            Delay1ms(10);
            PollTypeATags();
        }
        else if(picc.type == PICC_TYPEB_TCL)
        {
            Delay1ms(6);
            PollTypeBTags();
        }
        else if(picc.type == PICC_FELICA212)
        {
            Delay1ms(5);
            PollFeliCaTags(PASSDEPI_212);   
        }
        else if(picc.type == PICC_FELICA424)
        {
            Delay1ms(5);
            PollFeliCaTags(PASSDEPI_424);
        }
        else if(picc.type == PICC_TOPAZ)
        {
            Delay1ms(5);
            PollTopazTags();
        }  
        else
        { 
            picc.type = PICC_ABSENT;
        }
    }

    if(picc.type == PICC_ABSENT)
    {
        *atrLen = 0;
        CLEAR_BIT(picc.status, BIT_ACTIVATED);
        ret = SLOTERROR_ICC_MUTE;
        AntennaPower(0);
    }
    else  
    {
        SET_BIT(picc.status, BIT_ACTIVATED);       // Card Activate
        PcscAtrBuild(atrBuf, atrLen);
        ret = SLOT_NO_ERROR;
    }

    PrtMsg(DBGL4, "%s: exit, ret = %02X\n", __FUNCTION__, ret);
    
    return(ret);
}


void PiccPowerOff(void)
{
    PrtMsg(DBGL4, "%s: start\n", __FUNCTION__);

    if(BITISCLEAR(picc.status, BIT_FIRSTINSERT))
    {  
        if((picc.type == PICC_TYPEA_TCL) || (picc.type == PICC_TYPEB_TCL))
        {
            if(DeselectRequest() != ERROR_NO)
            {
                if(picc.type == PICC_TYPEA_TCL)
                {
                    PiccHaltA();
                }
                else
                {
                    PiccHaltB(picc.sn);
                }
            }
        }
        else if(picc.type == PICC_MIFARE)
        {
            PiccHaltA();
        }
        CLEAR_BIT(picc.status, BIT_ACTIVATED);

        if(BITISCLEAR(pcd.fgPoll, BIT_AUTOPOLL))
        {
            // if PCD do not auto poll, turn off the antenna
            AntennaPower(0);
        }

    }

    PrtMsg(DBGL4, "%s: exit\n", __FUNCTION__);
}


static UINT8 BsiCmdDispatch(UINT8 *pcmd, UINT16 cmdLen, UINT8 *pres, UINT16 *presLen)
{
    UINT8 recLen;


    if(pcmd[2] == 0x01)
    {
        switch(pcmd[3])
        {
            case 0x01:
                recLen = sizeof(vendorName);
                memcpy(pres, vendorName, recLen);
                break;
                
            case 0x02:
                pres[0] = 0x2F;
                pres[1] = 0x07;
                recLen = 2;
                break;
                
            case 0x03:
                recLen = sizeof(productName);
                memcpy(pres, productName, recLen);
                break;
                
            case 0x04:
                pres[0] = 0x01;
                pres[1] = 0x09;
                recLen = 2;
                break;
                
            case 0x06:
                recLen = sizeof(firmwareVersion);
                memcpy(pres, firmwareVersion, recLen);
                break;
                
            case 0x07:
                recLen = sizeof(driverVersion);
                memcpy(pres, driverVersion, recLen);
                break;
                
            case 0x08:
                pres[0] = 0x00;
                pres[1] = 0x02;
                recLen = 2;
                break;
                
            case 0x09:
                pres[0] = 0x4F;    // 847 Kbps
                pres[1] = 0x03;
                recLen = 2;
                break;
                
            default:
                recLen = 0;
                break;
        }  
        if(recLen == 0)
        {
            pres[0] = 0x6A;
            pres[1] = 0x89;
            *presLen = 2;
        }
        else
        {
            pres[recLen]   = 0x90;
            pres[recLen+1] = 0x00;
            *presLen = 2 + recLen;
        }
    }
    else 
    {
        pres[0] = 0x6A;
        pres[1] = 0x90;
        *presLen = 2;
    }
    
    return(SLOT_NO_ERROR);
}


UINT8 PiccXfrDataExchange(UINT8 *cmdBuf, UINT16 cmdLen, UINT8 *resBuf, UINT16 *resLen, UINT8 *level)
{
    UINT8 ret = SLOT_NO_ERROR;
    UINT8 tempLe;
    UINT8 i;


    PrtMsg(DBGL2, "%s: start, cmdLen = %02X\n", __FUNCTION__, cmdLen);    

    //Psuedo-APDU Get UID/ATS
    if((cmdBuf[0] == 0xFF) && (cmdLen >= 0x05) && (*level == 0x00))            //Get UID/ATS
    {
        if((cmdBuf[1] == 0x00) && (cmdBuf[2] == 0x00) && (cmdBuf[3] == 0x00) && (cmdLen > 5))
        {
            if((cmdLen != (cmdBuf[4] + 5)) && (cmdLen != (cmdBuf[4] + 6)))
            {
                resBuf[0] = 0x67;
                resBuf[1] = 0x00;
                *resLen = 2;
                return(SLOT_NO_ERROR);
            }
            if((picc.type == PICC_FELICA212) || (picc.type == PICC_FELICA424))
            {
                ret = FelXfrHandle(cmdBuf + 5, cmdLen - 5, resBuf, resLen);
                if(*resLen > 2)
                {
                    resBuf[(*resLen)++] = 0x90;
                    resBuf[(*resLen)++] = 0x00;
                }

            }
            else if(picc.type == PICC_TOPAZ)
            {
                ret = TopazXfrHandle(cmdBuf + 5, cmdLen - 5, resBuf, resLen);
                if(ret == SLOT_NO_ERROR)
                {
                    resBuf[(*resLen)++] = 0x90;
                    resBuf[(*resLen)++] = 0x00;
                }
                ret = SLOT_NO_ERROR;
            }
            else
            {
                ClearRegBit(REG_STATUS2, BIT_MFCRYPTO1ON);    // disable crypto 1 unit    
                pcsc.fgStatus = 0x00;
                FIFOFlush();
                ret = PcdRawExchange(CMD_TRANSCEIVE, &cmdBuf[5], cmdBuf[4], resBuf, &tempLe);
                if (ret == ERROR_NO)
                {
                    *resLen = tempLe;
                    resBuf[(*resLen)++] = 0x90;
                    resBuf[(*resLen)++] = 0x00;
                }
                else
                {
                    resBuf[0] = 0x63;
                    resBuf[1] = 0x00;
                    *resLen   = 0x02; 
                }
                ret = SLOT_NO_ERROR;
            }
        }
        else if((cmdBuf[1] == 0xCA) && (cmdBuf[3] == 0x00) && (cmdLen == 0x05))
        {
            //Get UID, accroding to pcsc part 3
            tempLe = cmdBuf[4];
            if(cmdBuf[2] == 0x00)
            {
                if(tempLe <= picc.snLen)
                {

                    for(i = 0; i < picc.snLen; i++)
                    {
                        resBuf[i] = picc.sn[i];
                    }
                    if((tempLe == 0x00) || (tempLe == picc.snLen))
                    {
                        resBuf[i++] = 0x90;
                        resBuf[i]   = 0x00;
                        *resLen     = picc.snLen + 2;
                    }
                    else
                    {
                        resBuf[tempLe]     = 0x6C;
                        resBuf[tempLe + 1] = picc.snLen;
                        *resLen            = tempLe + 2;
                    }
                }
                else
                {
                    resBuf[0] = 0x62;
                    resBuf[1] = 0x82;
                    *resLen   = 0x02;
                }
            }
            else if (cmdBuf[2] == 0x01)
            {
                //Get ATS, accroding to pcsc part 3
                resBuf[0] = 0x6A;
                resBuf[1] = 0x81;
                *resLen   = 0x02;
                tempLe    = cmdBuf[4];
                if (picc.type == PICC_TYPEA_TCL)
                {
                    for(i = 0; i < picc.ATS[0]; i++)
                    {
                        resBuf[i] = picc.ATS[i];
                    }
                    if(tempLe && (tempLe != picc.ATS[0]))
                    {
                        resBuf[tempLe]     = 0x6C;
                        resBuf[tempLe + 1] = picc.ATS[0];
                        *resLen            = tempLe + 2;
                    }
                    else
                    {
                        resBuf[i++] = 0x90;
                        resBuf[i]   = 0x00;
                        *resLen     = picc.ATS[0] + 2;
                    }
                }
            }
            else
            {
                resBuf[0] = 0x63;
                resBuf[1] = 0x00;
                *resLen   = 0x02; 
            }
            ret = SLOT_NO_ERROR;
        }
        else if(cmdBuf[1] == 0xC2)
        {
            if(cmdLen > 5)
            {
                if((cmdLen != (cmdBuf[4] + 5)) && (cmdLen != (cmdBuf[4] + 6)))
                {
                    resBuf[0] = 0x67;
                    resBuf[1] = 0x00;
                    *resLen   = 2;
                    return(SLOT_NO_ERROR);
                }
            }
            ret = PcscIfdCmdDispatch(cmdBuf[3], cmdBuf + 5, cmdBuf[4], resBuf, resLen);
        }
        else if((cmdBuf[1] == 0x9A) && (cmdLen >= 0x05))
        {

            ret = BsiCmdDispatch(cmdBuf, cmdLen, resBuf, resLen);
        }
        else 
        {
            if(picc.type == PICC_MIFARE)
            {
                ret = MifarePcscCommand(cmdBuf, cmdLen, resBuf, resLen);
            }
            else
            {
                resBuf[0] = 0x63;
                resBuf[1] = 0x00;
                *resLen   = 0x02;
            }
        }
    }
    else            // Standard APDUs
    {
        if(((picc.type == PICC_TYPEA_TCL)||(picc.type == PICC_TYPEB_TCL)) && (picc.states == PICC_ACTIVATED))
        {
            ret = PiccStandardApduTCL(cmdBuf, cmdLen, resBuf, resLen, level);
            if(ret != SLOT_NO_ERROR)
            {
                DeselectRequest();
                CLEAR_BIT(picc.status, BIT_ACTIVATED);
            }
        }
        else if((picc.type == PICC_FELICA212) || (picc.type == PICC_FELICA424))
        {
            ret = FelTransmisionHandle(cmdBuf, cmdLen, resBuf, resLen);
            if(*resLen < 2)
            {
                resBuf[(*resLen)++] = 0x90;
                resBuf[(*resLen)++] = 0x00;
            }

        }
        else if(picc.type == PICC_TOPAZ)
        {
            ret = TopazTransmissionHandle(cmdBuf + 5,cmdLen - 5, resBuf, resLen);
            if(*resLen < 2)
            {
                resBuf[(*resLen)++] = 0x90;
                resBuf[(*resLen)++] = 0x00;
            }
            ret = SLOT_NO_ERROR;
        }
        else
        {
            ret = SLOTERROR_CMD_ABORTED;
        }
    }
    
    return(ret);
}


void PiccInit(void)
{
    pcd.filterType = 0x1F;    // poll all card type
    pcd.fgPoll     = 0x07;    // auto RATS, auto poll, poll card
    pcd.maxSpeed   = 0x1B;
    pcd.FSDI       = 0x08;    // 256 bytes
    pcd.maxSpeed   = 0x1B;
    pcd.curSpeed   = 0x80;
    pcd.pollDelay  = 500;     // poll card interval time: default 500ms

    picc.states   = 0x00;
    picc.fgTCL    = 0x00;
    picc.FSCI     = 0x02;    // 32 bytes
    picc.FWI      = 0x04;    // 4.8ms
    picc.SFGI     = 0x00;    // default value is 0
    picc.speed    = 0x80;

    pcsc.fgStatus = 0x00;

    mifare.keyValid = 0x00;
}

