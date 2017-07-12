#ifndef MSGDEF_H
#define MSGDEF_H

typedef struct telnet_msg_t
{
    bool  isMalloced;
    int   length;
    char *pSenderTag;
    void *pData;
}telnet_msg_t;

#define TELNET_MSG_SIZE             sizeof(telnet_msg_t)
#define TELNET_MAX_Q_ELEMENTS      (20)

#endif // MSGDEF_H
