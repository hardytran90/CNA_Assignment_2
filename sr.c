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
#define SEQSPACE 2    /* only need 0 and 1 for basic alternating bit protocol */

/* Compute checksum */
int ComputeChecksum(struct pkt packet) {
  int checksum = packet.seqnum + packet.acknum;
  for (int i = 0; i < 20; i++) {
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
static int A_nextseqnum;
static struct pkt A_last_packet; /* Store the last packet for possible retransmission */

void A_output(struct msg message) {
  struct pkt sendpkt;
  sendpkt.seqnum = A_nextseqnum;
  sendpkt.acknum = -1; /* Not used for data packets */
  memcpy(sendpkt.payload, message.data, 20);
  sendpkt.checksum = ComputeChecksum(sendpkt);

  A_last_packet = sendpkt; /* Save a copy for potential retransmission */

  printf("A: Sending packet %d with message '%s'\n", sendpkt.seqnum, sendpkt.payload);
  tolayer3(A, sendpkt);
  starttimer(A, RTT);
}

void A_input(struct pkt packet) {
  if (!IsCorrupted(packet) && packet.acknum == A_nextseqnum) {
    printf("A: Received correct ACK %d\n", packet.acknum);
    stoptimer(A);
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
  } else if (IsCorrupted(packet)) {
    printf("A: Received corrupted ACK, ignoring.\n");
  } else {
    printf("A: Received ACK %d, but unexpected (expected %d). Ignoring.\n", packet.acknum, A_nextseqnum);
  }
}

void A_timerinterrupt(void) {
  printf("A: Timer expired, retransmitting last packet %d\n", A_last_packet.seqnum);
  tolayer3(A, A_last_packet);
  starttimer(A, RTT);
}

void A_init(void) {
  A_nextseqnum = 0;
}

/********* Receiver (B) variables and functions ************/
static int B_expectedseqnum;

void B_input(struct pkt packet) {
  if (!IsCorrupted(packet) && packet.seqnum == B_expectedseqnum) {
    printf("B: Received correct packet %d with message '%s'\n", packet.seqnum, packet.payload);
    tolayer5(B, packet.payload);

    /* Send ACK */
    struct pkt ackpkt;
    ackpkt.seqnum = -1;
    ackpkt.acknum = packet.seqnum;
    memset(ackpkt.payload, 0, 20);
    ackpkt.checksum = ComputeChecksum(ackpkt);
    tolayer3(B, ackpkt);
    printf("B: Sending ACK %d\n", ackpkt.acknum);

    B_expectedseqnum = (B_expectedseqnum + 1) % SEQSPACE;
  } else if (IsCorrupted(packet)) {
    printf("B: Received corrupted packet. Ignoring.\n");
  } else {
    printf("B: Received unexpected packet %d (expected %d). Resending ACK.\n", packet.seqnum, B_expectedseqnum);
    
    /* Resend ACK for last correct packet */
    struct pkt ackpkt;
    ackpkt.seqnum = -1;
    ackpkt.acknum = (B_expectedseqnum + SEQSPACE - 1) % SEQSPACE;
    memset(ackpkt.payload, 0, 20);
    ackpkt.checksum = ComputeChecksum(ackpkt);
    tolayer3(B, ackpkt);
    printf("B: Resending ACK %d\n", ackpkt.acknum);
  }
}

void B_output(struct msg message) {} /* Unused in basic unidirectional implementation */

void B_timerinterrupt(void) {} /* Not used in this version */

void B_init(void) {
  B_expectedseqnum = 0;
}