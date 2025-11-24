#include "udsClientTable.h"
#include <string.h>

void udsClientTableInit(UDS_CLIENT_TABLE* pstTable)
{
    memset(pstTable, 0, sizeof(*pstTable));
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
        if (pstTable->astEntry[i].bActive)
        {
            unsigned char uchId = pstTable->astEntry[i].uchClientId;

            if (unMask & (1U << uchId))
            {
                bufferevent_write(pstTable->astEntry[i].pstBev, pchData, iLen);
                iSent++;
            }
        }
    }

    return iSent;
}
