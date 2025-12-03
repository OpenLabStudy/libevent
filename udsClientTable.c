#include "udsClientTable.h"
#include <string.h>
#include <stdio.h>

void udsClientTableInit(UDS_CLIENT_TABLE* pstTable)
{
    memset(pstTable, 0, sizeof(*pstTable));
}

UDS_REGISTER_INFO udsClientFindFreeSlot(UDS_CLIENT_TABLE* pstTable, unsigned char uchClientId)
{
    UDS_REGISTER_INFO eFlag = NEED_REGISTER;
    fprintf(stderr,"### %s():%d ###\n", __func__,__LINE__);
    for (int i = 0; i < UDS_MAX_CLIENT; i++)
    {
        if (pstTable->astEntry[i].uchClientId == uchClientId){
            eFlag = REGISTERED;
            break;
        }

    }
    return eFlag;
}

bool udsClientRegister(UDS_CLIENT_TABLE* pstTable,
                       struct bufferevent* pstBev,
                       unsigned char uchClientId)
{
    for (int i = 0; i < UDS_MAX_CLIENT; i++)
    {
        if (!pstTable->astEntry[i].bActive)
        {
            pstTable->astEntry[i].bActive     = true;
            pstTable->astEntry[i].pstBev      = pstBev;
            pstTable->astEntry[i].uchClientId = uchClientId;
            pstTable->iCount++;
            return true;
        }
    }
    return false;
}

struct bufferevent* udsClientGetBev(const UDS_CLIENT_TABLE* pstTable,
                                    unsigned char uchClientId)
{
    for (int i = 0; i < UDS_MAX_CLIENT; i++)
    {
        if (pstTable->astEntry[i].bActive &&
            pstTable->astEntry[i].uchClientId == uchClientId)
        {
            return pstTable->astEntry[i].pstBev;
        }
    }
    return NULL;
}

void udsClientUnregisterByBev(UDS_CLIENT_TABLE* pstTable,
                              struct bufferevent* pstBev)
{
    for (int i = 0; i < UDS_MAX_CLIENT; i++)
    {
        if (pstTable->astEntry[i].bActive &&
            pstTable->astEntry[i].pstBev == pstBev)
        {
            pstTable->astEntry[i].bActive = false;
            pstTable->iCount--;
            return;
        }
    }
}

int udsClientBroadcastMask(const UDS_CLIENT_TABLE* pstTable,
                           unsigned int unMask,
                           const unsigned char* pchData,
                           int iLen)
{
    int iSent = 0;
    for (int i = 0; i < UDS_MAX_CLIENT; i++)
    {
        fprintf(stderr,"### %s():%d Active %d, ID %02x ###\n", __func__,__LINE__, 
            pstTable->astEntry[i].bActive, pstTable->astEntry[i].uchClientId);
        if (pstTable->astEntry[i].bActive)
        {
            unsigned char uchId = pstTable->astEntry[i].uchClientId;
            fprintf(stderr,"### %s():%d Active %d, ID %02x %02x %02x###\n", __func__,__LINE__, 
            pstTable->astEntry[i].bActive, uchId, unMask, (1U << uchId));
            if (unMask & uchId)
            {
                for(int index=0; index<iLen; index++)
                    fprintf(stderr,"%02x ", pchData[index]);
                fprintf(stderr,"\n");
                bufferevent_write(pstTable->astEntry[i].pstBev, pchData, iLen);
                iSent++;
            }
        }
    }

    return iSent;
}
