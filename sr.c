#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Go Back N protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2  

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications: 
   - removed bidirectional GBN code and other code not used by prac. 
   - fixed C style to adhere to current programming style
   - added GBN implementation
**********************************************************************/
#define RTT 16.0 /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define NOTINUSE (-1)
#define WINDOWSIZE 6
#define SEQSPACE (2*WINDOWSIZE)   

/* Compute checksum */
int ComputeChecksum(struct pkt packet) {
  int checksum = packet.seqnum + packet.acknum;
  int i;
  for (i = 0; i < 20; i++) {
    checksum += (int)(packet.payload[i]);
  }
  return checksum;
}

/* Check if packet is corrupted */
bool IsCorrupted(struct pkt packet) {
  if (packet.checksum == ComputeChecksum(packet)) {
    return false;
  } else {
    return true;
  }
}


/********* Sender (A) variables and functions ************/
static struct pkt buffer[WINDOWSIZE];  /* array for storing packets waiting for ACK */
static int windowfirst, windowlast;    /* array indexes of the first/last packet awaiting ACK */
static int windowcount;                /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;               /* the next sequence number to be used by the sender */

void A_output(struct msg message) {
  struct pkt sendpkt;

  /*  if no block */
  if (windowcount < WINDOWSIZE) {
      if (TRACE > 1) {
        printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");
      }
      
      sendpkt.seqnum = A_nextseqnum;
      sendpkt.acknum = NOTINUSE; /* Not used for data packets */
      memcpy(sendpkt.payload, message.data, 20);
      sendpkt.checksum = ComputeChecksum(sendpkt);

      windowlast = (windowlast + 1) % WINDOWSIZE;
      buffer[windowlast] = sendpkt;
      windowcount++;
      
      /* Send packet */
      if (TRACE > 0) {
        printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
      }
        
      tolayer3 (A, sendpkt);

      /* Start the time */
      if (windowcount == 1) {
        starttimer(A, RTT);
      }

      /* next sequence run from 0 to SEQSPACE - 1 then back to 0 */
      A_nextseqnum = (A_nextseqnum + 1) %SEQSPACE;

  } else {
    if (TRACE > 0) {
      printf("----A: New message arrives, send window is full\n");
    }
    window_full++;

  }

}

/*
When packet arrive in layer 4, layer 3 call this method
*/
void A_input(struct pkt packet) {
  
  /* if packet not corrupted */
  if (!IsCorrupted(packet)) {
    if (TRACE > 0) {
      printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
    }
    
    total_ACKs_received++;

    /* check if new ACK or duplicate */
    if (windowcount != 0) {
      int first_sequence = buffer[windowfirst].seqnum;
      int last_sequence = buffer[windowlast].seqnum;
      int index;
      /* check if  */
      if (((first_sequence <= last_sequence) && (packet.acknum >= first_sequence && packet.acknum <= last_sequence)) ||
          ((first_sequence > last_sequence) && (packet.acknum >= first_sequence || packet.acknum <= last_sequence))) 
      {

        /* packet is a new ACK */
        if (TRACE > 0)
          printf("----A: ACK %d is not a duplicate\n",packet.acknum);
        new_ACKs++;
        
        

        for (index = 0; index < WINDOWSIZE; index++) {
          if (buffer[index].seqnum == packet.acknum) {
            buffer[index].acknum = 5;
            break;
          }
        }

        if (first_sequence == packet.acknum) {
          /* Shift the window */
          while (buffer[windowfirst].acknum != -1 && windowcount != 0) {
            /* Increase window first */
            windowfirst = (windowfirst + 1) % WINDOWSIZE;
            /*  Delete ack package */
            windowcount--;             
          }
          stoptimer(A);
          if (windowcount > 0) {
            starttimer(A, RTT);
          }
        } 
      } else {
        if (TRACE > 0) {
          printf ("----A: duplicate ACK received, do nothing!\n");
        }          
      }
 
    } 
    
  } else {
    /* Corrupted packet */
    if (TRACE > 0)
    {
      printf ("----A: corrupted ACK is received, do nothing!\n");
    }
    
  }  
}

void A_timerinterrupt(void) {
  int i;

  if (TRACE > 0)
  {
    printf("----A: time out,resend packets!\n");
  }

  for (i = 0; i < WINDOWSIZE; i++) {
    int tmp = (windowfirst + i) % WINDOWSIZE;
    if (buffer[tmp].acknum == -1) {
      /* Didn't receive ACK before timeout */
      if (TRACE > 0)
      {
        printf ("---A: resending packet %d\n", (buffer[tmp]).seqnum);
      }
      tolayer3(A, buffer[tmp]);
      packets_resent++;
      starttimer(A, RTT);
      break;      
    }
  }
}

void A_init(void) {
  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1; /* At beginning no last packet is stored */
  windowcount = 0;
}

/********* Receiver (B) variables and functions ************/
static int expectedseqnum; /* the sequence number expected next by the receiver */
static int B_nextseqnum;   /* the sequence number for the next packets sent by B */
static int B_windowfirst; 
static int B_windowlast;
static int B_windowend;
static int B_windowcount;
static struct pkt B_buffer[WINDOWSIZE];

void B_input(struct pkt packet) {
  struct pkt sendpkt;
  B_windowend = (B_windowfirst + WINDOWSIZE-1) % SEQSPACE;

  if (!IsCorrupted(packet)) {

    if (TRACE > 0) 
    {
      printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
    }
     
    packets_received++;

    if(((B_windowfirst > B_windowend) && ((packet.seqnum >= B_windowfirst) && (packet.seqnum <= SEQSPACE - 1) ||((packet.seqnum >= 0) && (packet.seqnum <= B_windowend)) )) 
      || (B_windowfirst < B_windowend) && (packet.seqnum >= B_windowfirst) && (packet.seqnum <= B_windowend) ) 
    {
      int j;
      int k;
      /* If new packet */
      if (packet.acknum == -1) 
      {
        int index = packet.seqnum - B_windowfirst;
        B_buffer[index] = packet;
        B_buffer[index].acknum = packet.seqnum;
        B_buffer[index].seqnum = packet.seqnum;
        B_windowcount++;
      }
      sendpkt.acknum = packet.seqnum;
      memset(sendpkt.payload, 0, 20);
      sendpkt.checksum = ComputeChecksum(sendpkt);
      tolayer3(B, sendpkt);

    
      for (j = 0; j < WINDOWSIZE; j++)
      {
        if (B_buffer[0].acknum != -1)
        {
          /* Send to layer 5 */
          tolayer5(B, B_buffer[0].payload);
          /* Shift window */
            for (k=0; k < WINDOWSIZE - 1 ;k++){
              B_buffer[k] = B_buffer[k+1];
            }
            B_buffer[5].acknum=-1;
            
            /* Re calculate */
            B_windowfirst = (B_windowfirst+1) % SEQSPACE;
            B_windowlast = (B_windowlast + 1) % SEQSPACE;
            B_windowend = (B_windowend+1)%SEQSPACE;
            B_windowcount--;
        }
        else
        {
          break;
        }
      }
    } else {
      sendpkt.acknum = packet.seqnum;
      memset(sendpkt.payload, 0, 20);
      sendpkt.checksum = ComputeChecksum(sendpkt);
      tolayer3 (B, sendpkt);
    }
   
  } 
}

void B_output(struct msg message) {
  
} /* Unused in basic unidirectional implementation */

void B_timerinterrupt(void) {} /* Not used in this version */

void B_init(void) {
  int i;
  B_nextseqnum = 1;
  expectedseqnum = 0;
  B_windowfirst = 0;
  B_windowlast = -1;   /* no stored packet */
  B_windowcount = 0;

  for(i = 0; i < WINDOWSIZE; i++)
  {
    B_buffer[i].acknum = -1;
  }
}