#include <stdio.h>
#include <stdlib.h>
/* ******************************************************************
 ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.1  J.F.Kurose

   This code should be used for PA2, unidirectional or bidirectional
   data transfer protocols (from A to B. Bidirectional transfer of data
   is for extra credit and is not required).  Network properties:
   - one way network delay averages five time units (longer if there
     are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
     or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
     (although some can be lost).
**********************************************************************/

#define BIDIRECTIONAL 0 /* change to 1 if you're doing extra credit */
                        /* and write a routine called B_output */

#define A 0
#define B 1

#define WINDOW_SIZE 5
#define PAYLOAD_SIZE 20

/* a "msg" is the data unit passed from layer 5 (teachers code) to layer  */
/* 4 (students' code).  It contains the data (characters) to be delivered */
/* to layer 5 via the students transport level protocol entities.         */
typedef struct msg
{
  char data[20];
} msg_t;

/* a packet is the data unit passed from layer 4 (students code) to layer */
/* 3 (teachers code).  Note the pre-defined packet structure, which all   */
/* students must follow. */
typedef struct pkt
{
  int seqnum;
  int acknum;
  int checksum;
  char payload[PAYLOAD_SIZE];
} pkt_t;

void tolayer3(int AorB, pkt_t packet);
void tolayer5(int AorB, char *data);
void stoptimer(int);
void starttimer(int, float);
/********* STUDENTS WRITE THE NEXT SEVEN ROUTINES *********/

#define NOT_SEND 0
#define NOT_ACKED 1

typedef struct window_packet_s
{
  pkt_t *packet;
  struct window_packet_s *next;
  int status;
} window_packet_t;

typedef struct caller_state_s
{
  int id;

  int seqnum_base;
  int next_seqnum;
  int last_acked;
  int timeout;
  int timer_on;

  window_packet_t *window;
  int in_transit;

} caller_state_t;

caller_state_t a;
caller_state_t b;

void get_buffer_from_packet(pkt_t *packet, char *buffer)
{
  // Loads seqnum to buffer
  char *tmp_buffer = buffer;
  *(tmp_buffer + 0) = (packet->seqnum >> 0);
  *(tmp_buffer + 1) = (packet->seqnum >> 8);
  *(tmp_buffer + 2) = (packet->seqnum >> 16);
  *(tmp_buffer + 3) = (packet->seqnum >> 24);

  // Loads acknum to buffer
  tmp_buffer = buffer + 4;
  *(tmp_buffer + 0) = (packet->acknum >> 0);
  *(tmp_buffer + 1) = (packet->acknum >> 8);
  *(tmp_buffer + 2) = (packet->acknum >> 16);
  *(tmp_buffer + 3) = (packet->acknum >> 24);

  // Loads payload to buffer
  tmp_buffer = buffer + 8;
  for (int i = 0; i < 20; i++)
  {
    tmp_buffer[i] = packet->payload[i];
  }
}

int get_checksum_from_buffer(char *buffer, size_t size)
{
  unsigned int cksum = 0;
  while (size > 1)
  {
    cksum += *((unsigned short *)(buffer + 1));
    size -= 2;
    buffer += 2;
  }
  if (size)
    cksum += (unsigned short)(0x00FF & *buffer);

  cksum = (~cksum & 0xffff);
  return cksum;
}

int get_checksum(pkt_t *packet)
{
  char buffer[sizeof(pkt_t)] = {0x0};

  get_buffer_from_packet(packet, buffer);

  int checksum = get_checksum_from_buffer(buffer, 20);

  return checksum;
}

int is_corrupted(pkt_t *packet)
{
  int checksum = get_checksum(packet);
  return checksum != packet->checksum;
}

pkt_t *get_pkt_from_msg(msg_t *msg, int sequence, int acknum)
{
  pkt_t *packet = (pkt_t *)malloc(sizeof(pkt_t));

  packet->seqnum = sequence;
  packet->acknum = acknum;

  for (int i = 0; i < 20; i++)
  {
    packet->payload[i] = msg->data[i];
  }

  packet->checksum = get_checksum(packet);

  return packet;
}

pkt_t *get_ack_pkt(caller_state_t *caller, pkt_t *packet, pkt_t *received_pkt)
{
  packet->seqnum = caller->seqnum_base;

  if (received_pkt == NULL)
  {
    packet->acknum = caller->last_acked;
  }
  else
  {
    packet->acknum = received_pkt->seqnum + PAYLOAD_SIZE;
  }

  packet->payload[0] = 'A';
  packet->payload[1] = 'C';
  packet->payload[2] = 'K';
  packet->payload[3] = '\0';

  for (int i = 4; i < 20; i++)
  {
    packet->payload[i] = 0;
  }

  packet->checksum = get_checksum(packet);

  return packet;
}

int is_ack_packet(pkt_t *packet)
{
  return packet->payload[0] == 'A' &&
         packet->payload[1] == 'C' &&
         packet->payload[2] == 'K';
}

void send_pkt(caller_state_t *caller, pkt_t *packet)
{
  tolayer3(caller->id, *packet);
  if (!caller->timer_on)
  {
    starttimer(caller->id, caller->timeout);
    caller->timer_on = 1;
  }
}

void add_to_window(caller_state_t *caller, pkt_t *packet)
{
  window_packet_t *w_pkt = NULL;
  if (caller->window == NULL)
  {
    caller->window = (window_packet_t *)malloc(sizeof(window_packet_t));
    w_pkt = caller->window;
  }
  else
  {
    w_pkt = caller->window;
    while (w_pkt->next != NULL)
      w_pkt = w_pkt->next;
    w_pkt->next = (window_packet_t *)malloc(sizeof(window_packet_t));
    w_pkt = w_pkt->next;
  }

  w_pkt->packet = packet;
  w_pkt->status = NOT_SEND;
  w_pkt->next = NULL;
}

void send_authorized(caller_state_t *caller)
{
  window_packet_t *window = caller->window;
  while (window != NULL &&
         caller->in_transit < WINDOW_SIZE)
  {
    if (window->status == NOT_SEND)
    {
      send_pkt(caller, window->packet);
      window->status = NOT_ACKED;
      caller->in_transit++;
    }
    window = window->next;
  }
}
void resend_in_transit(caller_state_t *caller)
{
  window_packet_t *window = caller->window;
  while (window != NULL &&
         window->status == NOT_ACKED)
  {
    send_pkt(caller, window->packet);
    window = window->next;
  }
}

void handle_output(caller_state_t *caller, msg_t *message)
{
  pkt_t *packet = get_pkt_from_msg(message, caller->next_seqnum, caller->last_acked);
  add_to_window(&a, packet);
  caller->next_seqnum += PAYLOAD_SIZE;
  send_authorized(caller);
}

void handle_input(caller_state_t *caller, pkt_t *packet)
{
  if (!is_corrupted(packet))
  {
    if (is_ack_packet(packet))
    {
      if (caller->window != NULL &&
          packet->acknum == (caller->window->packet->seqnum + PAYLOAD_SIZE))
      {
        // printf("%c Packet ack:%d acked\n", caller->id == A ? 'A' : 'B', packet->acknum);

        window_packet_t *tmp_window = caller->window->next;
        free(caller->window->packet);
        free(caller->window);
        caller->window = tmp_window;

        caller->in_transit--;

        stoptimer(caller->id);
        if (caller->in_transit > 0)
        {
          starttimer(caller->id, caller->timeout);
        }
        else
        {
          caller->timer_on = 0;
        }

        send_authorized(caller);
      }
      else
      {
        // if (caller->window != NULL)
        // printf("ack: %d exp: %d\n", packet->acknum, (caller->window->packet->seqnum + PAYLOAD_SIZE));
        // printf("%c Ack %d out of order\n", caller->id == A ? 'A' : 'B', packet->acknum);
        if (caller->timer_on)
        {
          stoptimer(caller->id);
          starttimer(caller->id, caller->timeout);
        }
        resend_in_transit(caller);
      }
    }
    else
    {
      if (packet->seqnum == caller->last_acked)
      {
        if (caller->last_acked < (packet->seqnum + PAYLOAD_SIZE))
          tolayer5(caller->id, packet->payload);
        pkt_t ack_pkt;
        get_ack_pkt(&a, &ack_pkt, packet);
        tolayer3(caller->id, ack_pkt);
        caller->last_acked = ack_pkt.acknum;
        // printf("Sending ACK %d\n", ack_pkt.acknum);
        // printf("%c Packet seq:%d received ack:%d\n", caller->id == A ? 'A' : 'B', packet->seqnum, ack_pkt.acknum);
      }
      else
      {
        // printf("seq: %d exp: %d\n", packet->seqnum, caller->last_acked);
        // printf("%c Packet %d out of order\n", caller->id == A ? 'A' : 'B', packet->seqnum);
        pkt_t ack_pkt;
        get_ack_pkt(caller, &ack_pkt, packet);
        // printf("Sending ACK %d\n", ack_pkt.seqnum + PAYLOAD_SIZE);
        tolayer3(caller->id, ack_pkt);
      }
    }
  }
  else
  {
    // printf("Corrupted packet arrive %c\n", caller->id == A ? 'A' : 'B');
  }
}

void handle_timerinterrupt(caller_state_t *caller)
{
  caller->timer_on = 0;
  resend_in_transit(caller);
}

/* called from layer 5, passed the data to be sent to other side */
void A_output(msg_t message)
{
  handle_output(&a, &message);
  return;
}

void B_output(msg_t message) /* need be completed only for extra credit */
{
  handle_output(&b, &message);
  return;
}

/* called from layer 3, when a packet arrives for layer 4 */
void A_input(pkt_t packet)
{
  handle_input(&a, &packet);
  return;
}

/* called when A's timer goes off */
void A_timerinterrupt()
{
  handle_timerinterrupt(&a);
  return;
}

/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init()
{
  a.id = A;
  a.seqnum_base = 0;
  a.next_seqnum = 0;
  a.last_acked = 0;
  a.timeout = 20;
  a.timer_on = 0;
  a.window = NULL;
  a.in_transit = 0;

  return;
}

/* Note that with simplex transfer from a-to-B, there is no B_output() */

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(pkt_t packet)
{
  handle_input(&b, &packet);
  return;
}

/* called when B's timer goes off */
void B_timerinterrupt()
{
  handle_timerinterrupt(&b);
  return;
}

/* the following rouytine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init()
{
  b.id = B;
  b.seqnum_base = 0;
  b.next_seqnum = 0;
  b.last_acked = 0;
  b.timeout = 20;
  b.timer_on = 0;
  b.window = NULL;
  b.in_transit = 0;
  return;
}

/*****************************************************************
***************** NETWORK EMULATION CODE STARTS BELOW ***********
The code below emulates the layer 3 and below network environment:
  - emulates the tranmission and delivery (possibly with bit-level corruption
    and packet loss) of packets across the layer 3/4 interface
  - handles the starting/stopping of a timer, and generates timer
    interrupts (resulting in calling students timer handler).
  - generates message to be sent (passed from later 5 to 4)

THERE IS NOT REASON THAT ANY STUDENT SHOULD HAVE TO READ OR UNDERSTAND
THE CODE BELOW.  YOU SHOLD NOT TOUCH, OR REFERENCE (in your code) ANY
OF THE DATA STRUCTURES BELOW.  If you're interested in how I designed
the emulator, you're welcome to look at the code - but again, you should have
to, and you defeinitely should not have to modify
******************************************************************/

typedef struct event
{
  float evtime;       /* event time */
  int evtype;         /* event type code */
  int eventity;       /* entity where event occurs */
  struct pkt *pktptr; /* ptr to packet (if any) assoc w/ this event */
  struct event *prev;
  struct event *next;
} event_t;

event_t *evlist = NULL; /* the event list */

// Function definition
void insertevent(event_t *p);
void generate_next_arrival();
void init();

/* possible events: */
#define TIMER_INTERRUPT 0
#define FROM_LAYER5 1
#define FROM_LAYER3 2

#define OFF 0
#define ON 1

int TRACE = 1;   /* for my debugging */
int nsim = 0;    /* number of messages from 5 to 4 so far */
int nsimmax = 0; /* number of msgs to generate, then stop */
float time = 0.000;
float lossprob;    /* probability that a packet is dropped  */
float corruptprob; /* probability that one bit is packet is flipped */
float lambda;      /* arrival rate of messages from layer 5 */
int ntolayer3;     /* number sent into layer 3 */
int nlost;         /* number lost in media */
int ncorrupt;      /* number corrupted by media*/

void main()
{
  event_t *eventptr;
  struct msg msg2give;
  struct pkt pkt2give;

  int i, j;
  char c;

  init();
  A_init();
  B_init();

  while (1)
  {
    eventptr = evlist; /* get next event to simulate */
    if (eventptr == NULL)
      goto terminate;
    evlist = evlist->next; /* remove this event from event list */
    if (evlist != NULL)
      evlist->prev = NULL;
    if (TRACE >= 2)
    {
      printf("\nEVENT time: %f,", eventptr->evtime);
      printf("  type: %d", eventptr->evtype);
      if (eventptr->evtype == 0)
        printf(", timerinterrupt  ");
      else if (eventptr->evtype == 1)
        printf(", fromlayer5 ");
      else
        printf(", fromlayer3 ");
      printf(" entity: %c\n", eventptr->eventity == A ? 'A' : 'B');
    }
    time = eventptr->evtime; /* update time to next event time */
    if (nsim == nsimmax)
    {
      break; /* all done with simulation */
    }
    if (eventptr->evtype == FROM_LAYER5)
    {
      generate_next_arrival(); /* set up future arrival */
      /* fill in msg to give with string of same letter */
      j = nsim % 26;
      for (i = 0; i < 20; i++)
        msg2give.data[i] = 97 + j;
      if (TRACE > 2)
      {
        printf("          MAINLOOP: data given to student: ");
        for (i = 0; i < 20; i++)
          printf("%c", msg2give.data[i]);
        printf("\n");
      }
      nsim++;
      if (eventptr->eventity == A)
        A_output(msg2give);
      else
        B_output(msg2give);
    }
    else if (eventptr->evtype == FROM_LAYER3)
    {
      pkt2give.seqnum = eventptr->pktptr->seqnum;
      pkt2give.acknum = eventptr->pktptr->acknum;
      pkt2give.checksum = eventptr->pktptr->checksum;
      for (i = 0; i < 20; i++)
      {
        pkt2give.payload[i] = eventptr->pktptr->payload[i];
      }
      if (eventptr->eventity == A)
      {                    /* deliver packet by calling */
        A_input(pkt2give); /* appropriate entity */
      }
      else
      {
        B_input(pkt2give);
      }
      free(eventptr->pktptr); /* free the memory for packet */
    }
    else if (eventptr->evtype == TIMER_INTERRUPT)
    {
      if (eventptr->eventity == A)
        A_timerinterrupt();
      else
        B_timerinterrupt();
    }
    else
    {
      printf("INTERNAL PANIC: unknown event type \n");
    }
    free(eventptr);
  }

terminate:
  printf(" Simulator terminated at time %f\n after sending %d msgs from layer5\n", time, nsim);
}

void init() /* initialize the simulator */
{
  int i;
  float sum, avg;
  float jimsrand();

  printf("-----  Stop and Wait Network Simulator Version 1.1 -------- \n\n");
  printf("Enter the number of messages to simulate: ");
  // scanf("%d", &nsimmax);
  nsimmax = 100;
  printf("Enter  packet loss probability [enter 0.0 for no loss]:");
  // scanf("%f", &lossprob);
  lossprob = 0.2;
  printf("Enter packet corruption probability [0.0 for no corruption]:");
  // scanf("%f", &corruptprob);
  corruptprob = 0.2;
  printf("Enter average time between messages from sender's layer5 [ > 0.0]:");
  // scanf("%f", &lambda);
  lambda = 10;
  printf("Enter TRACE:");
  TRACE = 2;
  // scanf("%d", &TRACE);

  srand(9999); /* init random number generator */
  sum = 0.0;   /* test random number generator for students */
  for (i = 0; i < 1000; i++)
    sum = sum + jimsrand(); /* jimsrand() should be uniform in [0,1] */
  avg = sum / 1000.0;
  if (avg < 0.25 || avg > 0.75)
  {
    printf("It is likely that random number generation on your machine\n");
    printf("is different from what this emulator expects.  Please take\n");
    printf("a look at the routine jimsrand() in the emulator code. Sorry. \n");
    exit(1);
  }

  ntolayer3 = 0;
  nlost = 0;
  ncorrupt = 0;

  time = 0.0;              /* initialize time to 0.0 */
  generate_next_arrival(); /* initialize event list */
}

/****************************************************************************/
/* jimsrand(): return a float in range [0,1].  The routine below is used to */
/* isolate all random number generation in one location.  We assume that the*/
/* system-supplied rand() function return an int in therange [0,mmm]        */
/****************************************************************************/
float jimsrand()
{
  double mmm = 2147483647; /* largest int  - MACHINE DEPENDENT!!!!!!!!   */
  float x;                 /* individual students may need to change mmm */
  x = rand() / mmm;        /* x should be uniform in [0,1] */
  return (x);
}

/********************* EVENT HANDLINE ROUTINES *******/
/*  The next set of routines handle the event list   */
/*****************************************************/

void printevlist()
{
  event_t *q;
  int i;
  printf("--------------\nEvent List Follows:\n");
  for (q = evlist; q != NULL; q = q->next)
  {
    printf("Event time: %f, type: %d entity: %d\n", q->evtime, q->evtype, q->eventity);
  }
  printf("--------------\n");
}

void generate_next_arrival()
{
  double x, log(), ceil();
  event_t *evptr;
  // char *malloc();
  float ttime;
  int tempint;

  if (TRACE > 2)
    printf("          GENERATE NEXT ARRIVAL: creating new arrival\n");

  x = lambda * jimsrand() * 2; /* x is uniform on [0,2*lambda] */
                               /* having mean of lambda        */
  evptr = (event_t *)malloc(sizeof(event_t));
  evptr->evtime = time + x;
  evptr->evtype = FROM_LAYER5;
  if (BIDIRECTIONAL && (jimsrand() > 0.5))
    evptr->eventity = B;
  else
    evptr->eventity = A;
  insertevent(evptr);
}

void insertevent(event_t *p)
{
  event_t *q, *qold;

  if (TRACE > 2)
  {
    printf("            INSERTEVENT: time is %lf\n", time);
    printf("            INSERTEVENT: future time will be %lf\n", p->evtime);
  }
  q = evlist; /* q points to header of list in which p struct inserted */
  if (q == NULL)
  { /* list is empty */
    evlist = p;
    p->next = NULL;
    p->prev = NULL;
  }
  else
  {
    for (qold = q; q != NULL && p->evtime > q->evtime; q = q->next)
      qold = q;
    if (q == NULL)
    { /* end of list */
      qold->next = p;
      p->prev = qold;
      p->next = NULL;
    }
    else if (q == evlist)
    { /* front of list */
      p->next = evlist;
      p->prev = NULL;
      p->next->prev = p;
      evlist = p;
    }
    else
    { /* middle of list */
      p->next = q;
      p->prev = q->prev;
      q->prev->next = p;
      q->prev = p;
    }
  }
  // printevlist();
}

/********************** Student-callable ROUTINES ***********************/

/* called by students routine to cancel a previously-started timer */
void stoptimer(AorB) int AorB; /* A or B is trying to stop timer */
{
  event_t *q, *qold;

  if (TRACE > 2)
    printf("          STOP TIMER: stopping timer at %f\n", time);
  /* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next)  */
  for (q = evlist; q != NULL; q = q->next)
    if ((q->evtype == TIMER_INTERRUPT && q->eventity == AorB))
    {
      /* remove this event */
      if (q->next == NULL && q->prev == NULL)
        evlist = NULL;          /* remove first and only event on list */
      else if (q->next == NULL) /* end of list - there is one in front */
        q->prev->next = NULL;
      else if (q == evlist)
      { /* front of list - there must be event after */
        q->next->prev = NULL;
        evlist = q->next;
      }
      else
      { /* middle of list */
        q->next->prev = q->prev;
        q->prev->next = q->next;
      }
      free(q);
      return;
    }
  printf("Warning: unable to cancel your timer. It wasn't running.\n");
}

void starttimer(AorB, increment) int AorB; /* A or B is trying to stop timer */
float increment;
{

  event_t *q;
  event_t *evptr;
  // char *malloc();

  if (TRACE > 2)
    printf("          START TIMER: starting timer at %f\n", time);
  /* be nice: check to see if timer is already started, if so, then  warn */
  /* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next)  */
  for (q = evlist; q != NULL; q = q->next)
    if ((q->evtype == TIMER_INTERRUPT && q->eventity == AorB))
    {
      printf("Warning: attempt to start a timer that is already started\n");
      return;
    }

  /* create future event for when timer goes off */
  evptr = (event_t *)malloc(sizeof(event_t));
  evptr->evtime = time + increment;
  evptr->evtype = TIMER_INTERRUPT;
  evptr->eventity = AorB;
  insertevent(evptr);
}

/************************** TOLAYER3 ***************/
void tolayer3(AorB, packet) int AorB; /* A or B is trying to stop timer */
struct pkt packet;
{
  struct pkt *mypktptr;
  event_t *evptr, *q;
  // char *malloc();
  float lastime, x, jimsrand();
  int i;

  ntolayer3++;

  /* simulate losses: */
  if (jimsrand() < lossprob)
  {
    nlost++;
    if (TRACE > 0)
      printf("          TOLAYER3: packet being lost\n");
    return;
  }

  /* make a copy of the packet student just gave me since he/she may decide */
  /* to do something with the packet after we return back to him/her */
  mypktptr = (struct pkt *)malloc(sizeof(struct pkt));
  mypktptr->seqnum = packet.seqnum;
  mypktptr->acknum = packet.acknum;
  mypktptr->checksum = packet.checksum;
  for (i = 0; i < 20; i++)
    mypktptr->payload[i] = packet.payload[i];
  if (TRACE > 2)
  {
    printf("          TOLAYER3: seq: %d, ack %d, check: %d ", mypktptr->seqnum,
           mypktptr->acknum, mypktptr->checksum);
    for (i = 0; i < 20; i++)
      printf("%c", mypktptr->payload[i]);
    printf("\n");
  }

  /* create future event for arrival of packet at the other side */
  evptr = (event_t *)malloc(sizeof(event_t));
  evptr->evtype = FROM_LAYER3;      /* packet will pop out from layer3 */
  evptr->eventity = (AorB + 1) % 2; /* event occurs at other entity */
  evptr->pktptr = mypktptr;         /* save ptr to my copy of packet */
                                    /* finally, compute the arrival time of packet at the other end.
                                       medium can not reorder, so make sure packet arrives between 1 and 10
                                       time units after the latest arrival time of packets
                                       currently in the medium on their way to the destination */
  lastime = time;
  /* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next) */
  for (q = evlist; q != NULL; q = q->next)
    if ((q->evtype == FROM_LAYER3 && q->eventity == evptr->eventity))
      lastime = q->evtime;
  evptr->evtime = lastime + 1 + 9 * jimsrand();

  /* simulate corruption: */
  if (jimsrand() < corruptprob)
  {
    ncorrupt++;
    if ((x = jimsrand()) < .75)
      mypktptr->payload[0] = 'Z'; /* corrupt payload */
    else if (x < .875)
      mypktptr->seqnum = 999999;
    else
      mypktptr->acknum = 999999;
    if (TRACE > 0)
      printf("          TOLAYER3: packet being corrupted\n");
  }

  if (TRACE > 2)
    printf("          TOLAYER3: scheduling arrival on other side\n");
  insertevent(evptr);
}

void tolayer5(AorB, datasent) int AorB;
char datasent[20];
{
  int i;
  if (TRACE > 2)
  {
    printf("          TOLAYER5: data received: ");
    for (i = 0; i < 20; i++)
      printf("%c", datasent[i]);
    printf("\n");
  }
}