/*
libfcp2 - freenet client protocol v2 C library
fcp2.c: Low level (ish) FCP communication
*/


/*
Copyright (c) 2006, James Lee (jbit@jbit.net)
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of the listed copyright holders nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* _GNU_SOURCE needed for memmem() function */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <netdb.h>
#include <sys/types.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <fcp2.h>

# define ALIGN(x, align) ((((uintptr_t)x)+((align)-1))&~((align)-1))



/*
FcpServerConnect
	Connect to a freenet server
	This blocks, so keep that in mind.		

	Inputs
		hostname: hostname of freenet node to connect to
		port:     port number of FCP
		ibuf:     Size of input buffer
		obuf:     Size of output buffer 

	Outputs
		return: Server structure or NULL
*/
FcpServer *FcpServerConnect(const char *hostname, int port, int ibuf, int obuf)
{
	FcpServer *serv;

	/* Use local host as default address */
	if (hostname==NULL || strlen(hostname)==0)
		hostname = "127.0.0.1";

	/* Use default port */
	if (port<=0)
		port = 9481;

	int sockfd;
	struct hostent *he;
	struct sockaddr_in addr;

	/* Resolve hostname */
	if ((he = gethostbyname(hostname)) == NULL)
	{ 
		printf("%s: Failed to resolve (%s)!\n", __FUNCTION__, strerror(errno));
        return(NULL);
    }

	/* Snag socket */
	if ((sockfd = socket(PF_INET, SOCK_STREAM, 0))<0)
	{
		printf("%s: Failed to get socket (%s)!\n", __FUNCTION__, strerror(errno));
		return(NULL);
	}

	addr.sin_family = AF_INET;
	addr.sin_port   = htons(port);
	addr.sin_addr   = *((struct in_addr *)he->h_addr);
	memset(&(addr.sin_zero), '\0', 8);

	/* Connect */
	if (connect(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr))<0)
	{
		printf("%s: Failed to connect (%s)!\n", __FUNCTION__, strerror(errno));
		close(sockfd);
		return(NULL);
	}

	/* Allocate accounting and buffers */
	serv = malloc(sizeof(FcpServer)+ibuf+obuf);
	if (serv==NULL)
	{
		printf("%s: Failed to allocate server buffers (%s)!\n", __FUNCTION__, strerror(errno));
		close(sockfd);
		return(NULL);
	}

	/* Setup accounting stuff */
	serv->Name    = NULL;
	serv->Version = NULL;
	serv->InputSize  = ibuf;
	serv->InputUse   = 0;
	serv->Input      = serv->Buffer;
	serv->OutputSize = obuf; 
	serv->OutputUse  = 0;
	serv->Output     = serv->Buffer+ibuf;
	serv->fd = sockfd;

	return(serv);
}



/*
FcpServerDisconnect
	Disconnect from server, and destroy the structure

	Inputs
		serv:  Server to disconnect from
*/
void FcpServerDisconnect(FcpServer *serv)
{
	if (serv->Name!=NULL)
		free(serv->Name);

	if (serv->Version!=NULL)
		free(serv->Version);
	
	close(serv->fd);
	free(serv);
}



/*
FcpServerFlush
	Flush output buffer of server
	NOTE: This won't return until everything is sent!

	Inputs
		serv:  Server to flush

	Outputs
		return: <0=error, 0=nothing to flush, >0=flushed
*/
int FcpServerFlush(FcpServer *serv)
{
	int offset = 0;

	/* Keep looping until the buffer is completly empty */
	while (serv->OutputUse)
	{
		ssize_t ret = send(serv->fd, serv->Output+offset, serv->OutputUse, 0);
		if (ret<0)
			return(-1);

		serv->OutputUse-=ret;
		offset+=ret;
	}

	return(offset);
}



/*
FcpServerHandle
	Handle server connection
	This shouldn't block when timeout is 0.
*/
int FcpServerHandle(FcpServer *serv, int timeout)
{
	struct pollfd mypol;
	int retval=0;

	/* Loop until we don't have any more events */
	do
	{
		int pollret;
		struct pollfd mypol;
		mypol.fd = serv->fd;
		mypol.events = POLLERR | POLLHUP;

		/* Only get events that we can use */
		if (serv->OutputUse)
			mypol.events |= POLLOUT;
		if (serv->InputUse<serv->InputSize)
			mypol.events |= POLLIN;
	
		/* Look for some events */
		pollret = poll(&mypol, 1, timeout);
		if (pollret<0)
		{
			printf("%s: poll error (%s)!\n", __FUNCTION__, strerror(errno));
			return(-1);
		}

		if (pollret==0)
			break;

		timeout = 0;

		/* If we have space, send some data */
		if (mypol.revents&POLLOUT)
		{
			ssize_t ret = send(serv->fd, serv->Output, serv->OutputUse, 0);
			if (ret<0)
			{
				printf("%s: send error!\n", __FUNCTION__);
				return(-1);
			}
			if (ret>0)
			{
				serv->OutputUse-=ret;
				memmove(serv->Output, serv->Output+ret, serv->OutputUse);
			}
		}

		/* receive some data if we can */
		if (mypol.revents&POLLIN)
		{
			ssize_t ret = recv(serv->fd, serv->Input+serv->InputUse, serv->InputSize-serv->InputUse, 0);
			if (ret<0)
			{
				printf("%s: recv error!\n", __FUNCTION__);
				return(-1);
			}
			serv->InputUse += ret;
			retval+=ret;
		}

		/* if we get an error, die */
		if (mypol.revents&POLLERR || mypol.revents&POLLHUP)
		{
			return(-1);
		}
	}
	while (1);

	return(retval);
}



/*
FcpMessageField
	Get a message field by name

	Inputs
		msg:  message to get value from
		name: name of field to get

	Outputs
		return: Value of attribute or NULL
*/
const char *FcpMessageField(FcpMessage *msg, const char *name)
{
	/* Just look for the name, and return the value, simple! :) */
	for (int i=0;i<msg->FieldCount;i++)
	{
		if (!strcmp(name, msg->FieldName[i]))
			return(msg->FieldValue[i]);
	}

	return(NULL);
}



/*
FcpMessageClear
	Clear message structure

	Inputs
		msg:  message to clear
*/
void FcpMessageClear(FcpMessage *msg)
{
	/* We don't need to worry about zeroing the Buffer, since nothing should be looking there */
	msg->Name = NULL;
	msg->FieldCount = 0;
	msg->FieldName  = NULL;
	msg->FieldValue = NULL;
}



/*
FcpMessageDecode
	Decode a message into a message structure

	Inputs
		str:    buffer to read from
		buflen: length of valid data in str

	Outputs
		msg:  message to store decoded info to
		return: <0 = error, 0 = message fragment, >0 = success
*/
int FcpMessageDecode(FcpMessage *msg, const char *str, int buflen)
{
	const char *strend = memmem(str, buflen, "\nEndMessage\n", 12);
	if (strend==NULL)
		return(0);

	strend+=12;

	/* Calculate length of message */
	int slen = strend-str;

	/* If the calculated length is bigger than our message buffer, we have a big problem! */
	if (slen>msg->BufferSize)
	{
		return(-1);
	}

	/* memcpy is expensive, i'd like to move this later on */
	memcpy(msg->Buffer, str, slen);


	/* Find out where our pointer list should be, remembering processors arn't magic and need alignment */
	char **ptrs = (void *)ALIGN(msg->Buffer+slen, sizeof(const char*));

	/* calculate maximum number of fields we can support */
	const int maxfield = (msg->BufferSize-((uintptr_t)ptrs-(uintptr_t)msg->Buffer))/(sizeof(const char *)*2);

	/*
		replace all end of lines with nul terminator
		XXX: I'm not sure if FCP allows "\r\n" line termination. If it does, some of this library will have issues.
	*/
	for (int i=0;i<slen;i++)
	{
		if (msg->Buffer[i]=='\n')
			msg->Buffer[i]='\0';
	}

	msg->Name = msg->Buffer;

	const char *end = msg->Buffer + slen;
	char *ptr = msg->Buffer + strlen(msg->Name);
	size_t fields;
	int ended = 0;

	/* find all fields */
	for (fields=0;fields<maxfield&&ptr<end;fields++)
	{
		while (*ptr=='\0'&&ptr<end)
			ptr++;

		if (ptr>=end)
			break;

		if (!strncmp(ptr, "EndMessage", end-ptr))
		{
			ptr += strlen(ptr);
			ended = 1;
			break;
		}

		ptrs[fields] = ptr;
		
		ptr += strlen(ptr);
	}

	/* too many fields in message!!! */
	if (fields>=maxfield)
	{
		printf("%s: Message '%s' has too many fields (>%d)!\n", __FUNCTION__, msg->Name, fields);
		FcpMessageClear(msg);
		return(-ENOMEM);
	}

	/* No ending, but it was detected above, something is up.. */
	if (!ended)
	{
		printf("%s: Message '%s' is malformed!\n", __FUNCTION__, msg->Name);
		FcpMessageClear(msg);
		return(-1);
	}

	/* Take into account the line terminator, for size reasons */
	if (ptr<end)
	{
		int pos = ptr-msg->Buffer;
		if (str[pos]=='\n')
			ptr++;
	}

	msg->FieldName  = (const char**)ptrs;
	msg->FieldValue = (const char**)ptrs+fields;

	/* replace all fields ='s with nuls, and setup value pointers */
	for (size_t i=0;i<fields;i++)
	{
		char *v = strchr(ptrs[i], '=');

		if (v==NULL)
		{
			/* If we get here, we have a line without an =, not sure what to do with it */
			msg->FieldName[i]  = NULL;
			msg->FieldValue[i] = ptrs[i];
		}
		else
		{
			*v = '\0';
			msg->FieldValue[i] = v+1;
		}
	}

	msg->FieldCount = fields;

	return(ptr-msg->Buffer);
}



/*
FcpMessageCreate
	Create and setup a new message structure

	Inputs
		size: Size of messages data buffer (<0 = sensible default)

	Outputs
		return: A new message or NULL
*/
FcpMessage *FcpMessageCreate(int size)
{
	FcpMessage *msg;

	if (size<0)
		size = 8192;

	msg = malloc(sizeof(FcpMessage)+size);
	if (msg==NULL)
		return(NULL);

	msg->BufferSize = size;
	FcpMessageClear(msg);

	return(msg);
}



/*
FcpMessageDestroy
	Destroy a message

	Inputs
		msg: Message to destroy
*/
void FcpMessageDestroy(FcpMessage *msg)
{
	free(msg);
}



/*
FcpMessageDump
	Dump message details to stdout

	Inputs
		msg: Message to dump
*/
void FcpMessageDump(FcpMessage *msg)
{
	printf("<%s>\n", msg->Name);
	for (size_t i=0;i<msg->FieldCount;i++)
		printf("  %3d: [%s]=%s\n", i, msg->FieldName[i], msg->FieldValue[i]);
}



/*
FcpMessageSend
	Send a message, usualy just puts the data on the output queue

	Inputs
		serv:  Server to send to
		name:  The message name
		count: Number of fields
		...:   Alternating (const char*) of field name and field value

	Outputs
		return: <0=error, 0=not sent, >0=sent
*/
int FcpMessageSend(FcpServer *serv, const char *name, int count, ...)
{
	va_list ap;
	char *end = serv->Output+serv->OutputSize;
	char *buf = serv->Output+serv->OutputUse;

	/* Check we have enough space on the output queue for the message name and terminator */
	if (strlen(name)+12>(end-buf))
		return(-1);

	/* Copy name and line terminator into output buffer */
	memcpy(buf, name, strlen(name));
	buf+=strlen(name);
	*(buf++) = '\n';


	va_start(ap, count);


	for (int i=0;i<count;i++)
	{
		const char *n = va_arg(ap, const char *);
		const char *v = va_arg(ap, const char *);

		/* check to make sure we have enough space to add this field and the terminator */
		if (strlen(n)+strlen(v)+13>(end-buf))
		{
			va_end(ap);
			return(-1);
		}

		/* Copy <n>=<v>\n to buffer */
		memcpy(buf, n, strlen(n));
		buf+=strlen(n);
		*(buf++) = '=';
		memcpy(buf, v, strlen(v));
		buf+=strlen(v);
		*(buf++) = '\n';
	}
	va_end(ap);

	/* We shouldn't actually need this check, but do it anyway */
	if (11>(end-buf))
		return(-1);

	memcpy(buf, "EndMessage\n", 11);
	buf+=11;

	serv->OutputUse = buf - serv->Output;

	return(1);
}



/*
FcpMessageRecv
	Decode messsage from server input buffer

	Inputs
		serv:  Server to decode message from
		msg:   Message to store data in

	Outputs
		return: <0=error, 0=not sent, >0=sent
*/
int FcpMessageRecv(FcpServer *serv, FcpMessage *msg)
{
	int size;
	/* Try to decode what's in the buffer */
	size = FcpMessageDecode(msg, serv->Input, serv->InputUse);

	/* If we can't decode, just return */
	if (size<=0)
		return(size);

	/* If we've decoded we can flush the data from the input buffer... */
	memmove(serv->Input, serv->Input+size, serv->InputSize-size);
	serv->InputUse-=size;
	return(1);
}

