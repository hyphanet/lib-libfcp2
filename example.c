/*
libfcp2 - freenet client protocol v2 C library
example.c: Simple console monitor

Notes:
	This eats alot of bandwidth since it needs to get all the node info every referesh.
	Hopefully in the future FCP will support "GetStatus" or something.
	Doesn't check for any errors on the FCP connection, so will probably screw up in some situations
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcp2.h>

int main(int argc, char *argv[])
{
	FcpMessage *msg;
	FcpServer *serv;
	int port=9481;
	char *host="127.0.0.1";

	if (argc>1)
		host = argv[1];

	if (argc>2)
		port = strtol(argv[2], NULL, 0);


	/* Create a message buffer to use for decoding (you can have more than one if you want) */
	msg = FcpMessageCreate(2*1024);
	if (msg==NULL)
		return(1);

	/* Connect to the server */
	serv = FcpServerConnect(host, port, 64*1024, 64*1024);
	if (serv==NULL)
	{
		printf("Couldn't connect to %s %d!!\n", host, port);
		return(1);
	}

	/* Send the client hello message */
	FcpMessageSend(serv, "ClientHello", 2, "Name", "jbits FCP test", "ExpectedVersion", "2.0");

	int conn=0,  back=0,  dis=0,  nevr=0;
	int heart=0;
	int okay=1;
	while(okay)
	{
		/* Handle the server */
		if (FcpServerHandle(serv, 1000)<0)
		{
			printf("Disconnected!\n");
			okay=0;
			if (serv->InputUse==0)
				break;
		}

		/* If we've got no data in the input buffer, no point in trying to decode it */
		if (serv->InputUse==0)
			continue;

		do
		{
			int ret = FcpMessageRecv(serv, msg);
			if (ret<0)
				return(1);
			if (ret==0)
				break;

			if (!strcmp(msg->Name,"NodeHello")) /* We got a connection!!! */
			{
				printf("Connected to %s %d (%s)\n", host, port, FcpMessageField(msg, "Version"));
				printf("  CONN   BACK   DIS    NEVR\n");

				/* Ask for a list of peers */
				FcpMessageSend(serv, "ListPeers",   2, "WithMetadata", "false", "WithVolatile", "true");
			}
			else if (!strcmp(msg->Name,"Peer")) /* Peer entry */
			{
				const char *status = FcpMessageField(msg, "volatile.status");
				//	printf("%s: %s\n", FcpMessageField(msg, "myName"), status);

				/* Figure out the status of the node, and add it to our accounting */
				if (status!=NULL)
				{
					if (!strcmp(status, "CONNECTED"))
						conn++;
					else if (!strcmp(status, "BACKED OFF"))
						back++;
					else if (!strcmp(status, "DISCONNECTED"))
						dis++;
					else if (!strcmp(status, "NEVER CONNECTED"))
						nevr++;
				}
			}
			else if (!strcmp(msg->Name,"EndListPeers"))
			{
				static int chrs = 0;

				/* remove the last line */
				for (int i=0;i<chrs;i++)
					putchar('\b');

				/* print node status */
				chrs = printf("%c %-4d   %-4d   %-4d   %-4d",  heart?'*':' ', conn, back, dis, nevr);
				fflush(stdout);
		
				heart = 1-heart;

				/* reset stats */
				conn = back = dis = nevr = 0;

				/*  Wait a second, then ask for another list */
				sleep(1);
				FcpMessageSend(serv, "ListPeers",   2, "WithMetadata", "false", "WithVolatile", "true");
			}
			else if (!strcmp(msg->Name,"CloseConnectionDuplicateClientName"))
			{
				printf("\nAnother client stole our connection!\n");
				okay = 0;
			}
			else
				FcpMessageDump(msg);
		} while(1);

	}

	/* Clean up */
	FcpServerDisconnect(serv);
	FcpMessageDestroy(msg);

	return(0);
}
