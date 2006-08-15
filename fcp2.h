#ifndef __FCP2_H__
#define __FCP2_H__ 1

/* stddef.h for size_t */

#include <stddef.h>
/*
FcpMessage
	All pointers here are within the Buffer (including the Field pointer array)
	All strings are nul terminated.
	Buffer layout:
		original message (with line feeds and field ='s replaced with 0's)
		-- pad to pointer alignment --
		FieldName pointer array
		FieldValue pointer array
*/
typedef struct
{
	const char  *Name;        /* Name of message */
	size_t       FieldCount;  /* How many fields */
	const char **FieldName;   /* Names of fields */
	const char **FieldValue;  /* Names of fields */
	size_t       BufferSize;  /* Size (in bytes) of below Buffer */
	char         Buffer[];
} FcpMessage;

/*
FcpServer
	Name and version are strdup()'ed
	Input and Output point to memory inside Buffer
*/

typedef struct
{
	char   *Name;                   /* Name of server (FBI node) */
	char   *Version;                /* Version of server (Fred,0.7,1.0,941) */
	size_t  InputSize, InputUse;    /* Size of input buffer, and buffer usage (in bytes) */
	char   *Input;                  /* Pointer to input buffer */
	size_t  OutputSize, OutputUse;  /* Size of output buffer, and buffer usage (in bytes)  */
	char   *Output;                 /* Pointer to output buffer */

	int fd;                         /* Socket of server */
	char Buffer[];
} FcpServer;

/* Low levelish functions */
FcpServer *FcpServerConnect   (const char *hostname, int port, int ibuf, int obuf);
void       FcpServerDisconnect(FcpServer *s);
int        FcpServerHandle    (FcpServer *serv, int timeout);

FcpMessage *FcpMessageCreate(int size);
const char *FcpMessageField(FcpMessage *msg, const char *name);
void FcpMessageClear  (FcpMessage *msg);
int  FcpMessageDecode (FcpMessage *msg, const char *str, int buflen);
void FcpMessageDump   (FcpMessage *msg);
void FcpMessageDestroy(FcpMessage *msg);
int  FcpMessageSend   (FcpServer *serv, const char *name, int count, ...);
int  FcpMessageRecv   (FcpServer *serv, FcpMessage *msg);


#endif /* __FCP2_H__ */

