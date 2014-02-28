#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <list>

#include <iostream>

#include "Minet.h"
#include "tcpstate.h"
#include "packet_queue.h"

using namespace std;

enum flagToSend { ACK, FIN, SYNACK, SYN };

unsigned int SendPkt(list<Packet> pktQ, Connection c, unsigned int sendFlagType, unsigned int seqNum, unsigned int ackNum, MinetHandle mux, Buffer data) {

  unsigned bytes = MIN_MACRO(IP_PACKET_MAX_LENGTH-TCP_HEADER_BASE_LENGTH, data.GetSize());
  // create the payload of the packet
  Packet sendP(data.ExtractFront(bytes));
  // Make the IP header first since we need it to do the tcp checksum
  IPHeader sendIPHead;
  sendIPHead.SetProtocol(IP_PROTO_TCP);
  sendIPHead.SetSourceIP(c.src);
  sendIPHead.SetDestIP(c.dest);
  sendIPHead.SetTotalLength(bytes+TCP_HEADER_BASE_LENGTH+IP_HEADER_BASE_LENGTH);
  // push it onto the packet
  sendP.PushFrontHeader(sendIPHead);
  // Now build the TCP header
  TCPHeader sendTCPHead;
  sendTCPHead.SetSourcePort(c.srcport,sendP);
  sendTCPHead.SetDestPort(c.destport,sendP);
  sendTCPHead.SetSeqNum(seqNum, sendP);

  if (sendFlagType == ACK || sendFlagType == SYNACK) {
    sendTCPHead.SetAckNum(ackNum,sendP);
  }

  sendTCPHead.SetHeaderLen(TCP_HEADER_BASE_LENGTH/4,sendP);

  unsigned char sendFlag;
  switch (sendFlagType)
  {
    case ACK:
      SET_ACK(sendFlag);
    break;
    case FIN:
      SET_FIN(sendFlag);
    break;
    case SYNACK:
      SET_SYN(sendFlag);
      SET_ACK(sendFlag);
    break;
    case SYN:
      SET_SYN(sendFlag);
    break;
  }
  sendTCPHead.SetFlags(sendFlag,sendP);
  sendTCPHead.SetWinSize(100,sendP);

  // Now we want to have the tcp header BEHIND the IP header
  sendP.PushBackHeader(sendTCPHead);

  MinetSend(mux, sendP);
  pktQ.push_back(sendP);

  return seqNum + 1;
}

unsigned int SendBlankPkt(list<Packet> pktQ, Connection c, unsigned int sendFlagType, unsigned int seqNum, unsigned int ackNum, MinetHandle mux) {
  return SendPkt(pktQ, c, sendFlagType, seqNum, ackNum, mux, Buffer(NULL,0));
}

int main(int argc, char *argv[])
{
  MinetHandle mux, sock;
  unsigned int currSeqNum = 1000;

  ConnectionList<TCPState> clist;
  list<Packet> pktQ;

  //MinetSend(mux, packet);

  MinetInit(MINET_TCP_MODULE);

  mux=MinetIsModuleInConfig(MINET_IP_MUX) ? MinetConnect(MINET_IP_MUX) : MINET_NOHANDLE;
  sock=MinetIsModuleInConfig(MINET_SOCK_MODULE) ? MinetAccept(MINET_SOCK_MODULE) : MINET_NOHANDLE;

  if (MinetIsModuleInConfig(MINET_IP_MUX) && mux==MINET_NOHANDLE) {
    MinetSendToMonitor(MinetMonitoringEvent("Can't connect to mux"));
    return -1;
  }

  if (MinetIsModuleInConfig(MINET_SOCK_MODULE) && sock==MINET_NOHANDLE) {
    MinetSendToMonitor(MinetMonitoringEvent("Can't accept from sock module"));
    return -1;
  }

  MinetSendToMonitor(MinetMonitoringEvent("tcp_module handling TCP traffic"));

  MinetEvent event;
  Time timeout(1);
  cerr << "entering while loop" << endl;
  while (MinetGetNextEvent(event, timeout)==0) {
    //cerr << "2" << endl;
    // if we received an unexpected type of event, print error

    if (event.eventtype == MinetEvent::Timeout) {
      for (ConnectionList<TCPState>::iterator i = clist.begin(); i != clist.end(); ++i) {
        bool expired = false;

        if (i->bTmrActive) {
          expired = i->state.ExpireTimerTries();
        };

        if (!expired) {
          //Remove already acked packets
          list<Packet> tmpList = pktQ;
          while(!tmpList.empty()){
            Packet sendP = tmpList.front();
            unsigned int lastAcked = i->state.GetLastAcked();
            TCPHeader recTCPHead=sendP.FindHeader(Headers::TCPHeader);
            unsigned int seqNum;
            recTCPHead.GetSeqNum(seqNum);
            if (seqNum > lastAcked)
            {
              cerr << "Resending Packet" << endl;
              MinetSend(mux, sendP);
            }
            else
            {
              pktQ.pop_front();
            }

            tmpList.pop_front();
          }
        }

        switch (i->state.GetState()) {
          case SYN_RCVD:
          {
            cerr << "SYN_RCVD: ";
          }
          break;
          case SYN_SENT:
          {
            cerr << "SYN_SENT: ";
          }
          break;
          case LAST_ACK:
          {
            cerr << "LAST_ACK: ";
          }
          case TIME_WAIT:
          {
            cerr << "TIME_WAIT: ";
          }
          break;
        }

        if (expired) {
          cerr << "timed out => CLOSED" << endl;
          i->state.SetState(CLOSED);
          i->bTmrActive = false;
        }

        if (i->state.GetState() == CLOSED) {
          cerr << "CLOSED: for now switching to LISTEN" << endl;
          i->state.SetState(LISTEN);
        }
      }

      //cerr << "tic ";
    } else if (event.eventtype!=MinetEvent::Dataflow || event.direction!=MinetEvent::IN) {
      MinetSendToMonitor(MinetMonitoringEvent("Unknown event ignored."));
      // if we received a valid event from Minet, do processing
    } else {
      cerr << "Event Received: ";
        //  Data from the IP layer below  //
      if (event.handle==mux) {
        cerr << "Received Mux" << endl;

        Packet receiveP;
        MinetReceive(mux,receiveP);
        unsigned tcphlen=TCPHeader::EstimateTCPHeaderLength(receiveP);
        cerr << "estimated header len="<<tcphlen<<"\n";
        receiveP.ExtractHeaderFromPayload<TCPHeader>(tcphlen);
        IPHeader recIPHead=receiveP.FindHeader(Headers::IPHeader);
        TCPHeader recTCPHead=receiveP.FindHeader(Headers::TCPHeader);

        unsigned int recSeqNum;
        recTCPHead.GetSeqNum(recSeqNum);
        unsigned int ackNum = recSeqNum + 1;

        cerr << "IP Header is " << recIPHead << endl;
        cerr << "TCP Header is " << recTCPHead << endl;

        cerr << "Checksum is " << (recTCPHead.IsCorrectChecksum(receiveP) ? "VALID" : "INVALID") << endl;

        Connection c;
        recIPHead.GetDestIP(c.src);
        recIPHead.GetSourceIP(c.dest);
        recIPHead.GetProtocol(c.protocol);
        recTCPHead.GetDestPort(c.srcport);
        recTCPHead.GetSourcePort(c.destport);

        unsigned char flag;
        recTCPHead.GetFlags(flag);

        ConnectionList<TCPState>::iterator cs = clist.FindMatching(c);
        cs->state.SetLastRecvd(recSeqNum);

        unsigned int ack;
        bool goodLastAcked = false;
        if (IS_ACK(flag)) {
          recTCPHead.GetAckNum(ack);
          goodLastAcked = cs->state.SetLastAcked(ack);
        }

        //hard-coding to test mux stuff without sock implemented
        // TCPState hardCodedState(1000,LISTEN,2);
        // ConnectionToStateMapping<TCPState> hardCodedConn(c, Time(5), hardCodedState, true);
        // clist.push_back(hardCodedConn);


        if (cs!=clist.end())
        {
          if (!recTCPHead.IsCorrectChecksum(receiveP))
          {
            cerr << "corrupt packet" << endl;
            //corrupt packet
          } else {
            //cerr << "State:" << cs->state.GetState() << endl;
            cs->state.SetLastAcked(ackNum);

            if (cs->state.GetState() != TIME_WAIT) {
              cs->state.SetTimerTries(3);
            }
            cs->bTmrActive = true;

            switch (cs->state.GetState()) {
              case CLOSED:
              {
                cerr << "CLOSED: ";
              }

              break;
              case LISTEN:
              {
                cerr << "LISTEN: ";
                // rcv SYN
                // -------  => SYN_RCVD
                // snd SYN, ACK
                if (IS_SYN(flag))
                {
                  cerr << "rcv SYN, snd SYNACK => SYN_RCVD" << endl;
                  currSeqNum = SendBlankPkt(pktQ, c, SYNACK, currSeqNum, ackNum, mux);
                  cs->state.SetState(SYN_RCVD);
                }
              }
              break;
              case SYN_RCVD:
              {
                cerr << "SYN_RCVD: ";
                if (IS_ACK(flag))
                {
                  // rcv ACK of SYN
                  // --------------  => ESTABLISHED
                  //       x
                  cerr << "rcv ACK => ESTABLISHED" << endl;
                  cs->state.SetState(ESTABLISHED);
                }
              }
              break;
              case  SYN_SENT:
              {
                cerr << "SYN_SENT: ";
                if (IS_SYN(flag))
                {
                  if (IS_ACK(flag))
                  {
                    // rcv SYN, ACK
                    // ------------  => ESTABLISHED
                    //   snd ACK
                    cerr << "rcv SYNACK, snd ACK => ESTABLISHED" << endl;
                    currSeqNum = SendBlankPkt(pktQ, c, ACK, currSeqNum, ackNum, mux);
                    cs->state.SetState(ESTABLISHED);
                    cs->state.SetLastAcked(ackNum);
                  } else {
                    // rcv SYN
                    // -------  => SYN_RCVD
                    // snd ACK
                    cerr << "rcv SYN, snd ACK => SYN_RCVD" << endl;
                    currSeqNum = SendBlankPkt(pktQ, c, ACK, currSeqNum, ackNum, mux);
                    cs->state.SetState(SYN_RCVD);
                  }
                }
              }
              break;
              case SYN_SENT1:
              {
                cerr << "SYN_SENT1: ";

              }
              break;
              case  ESTABLISHED:
              {
                cerr << "ESTABLISHED: ";
                // rcv FIN
                // -------  => CLOSE_WAIT
                // snd ACK
                if (IS_FIN(flag)) {
                  cerr << "rcv FIN, snd ACK => CLOSE_WAIT" << endl;
                  currSeqNum = SendBlankPkt(pktQ, c, ACK, currSeqNum, ackNum, mux);
                  cs->state.SetState(CLOSE_WAIT);
                }
              }
              break;
              case SEND_DATA:
              {
                cerr << "SEND_DATA: ";
              }
              break;
              case CLOSE_WAIT:
              {
                cerr << "CLOSE_WAIT: ";

                if (IS_FIN(flag)) {
                  cerr << "rcv FINACK, snd FIN => LAST_ACK" << endl;
                  currSeqNum = SendBlankPkt(pktQ, c, FIN, currSeqNum, ackNum, mux);
                  cs->state.SetState(LAST_ACK);
                  cs->state.SetTimerTries(1);
                }
              }

              break;
              case FIN_WAIT1: //see page 38 of TCP doc
              {
                cerr << "FIN_WAIT1: ";
                // rcv FIN
                // -------  => CLOSING
                // snd ACK
                if (IS_FIN(flag) && !IS_ACK(flag)) {
                  cerr << "rcv FIN, snd ACK => CLOSING" << endl;
                  currSeqNum = SendBlankPkt(pktQ, c, ACK, currSeqNum, ackNum, mux);
                  cs->state.SetState(CLOSING);
                } else if (IS_FIN(flag) && IS_ACK(flag)) {
                  if (goodLastAcked) {
                    currSeqNum = SendBlankPkt(pktQ, c, ACK, currSeqNum, ackNum, mux);
                    cs->state.SetState(TIME_WAIT);
                  }
                } else if (IS_ACK(flag) && goodLastAcked) {
                  cerr << "rcv ACK => FIN_WAIT2" << endl;
                  cs->state.SetState(FIN_WAIT2);
                }

                // rcv ACK of FIN
                // --------------  => FIN_WAIT2
                //       x
              }

              break;
              case FIN_WAIT2:
              {
                cerr << "FIN_WAIT2: ";
                // rcv FIN
                // -------  => TIME_WAIT
                // snd ACK
                if (IS_FIN(flag)) {
                  cerr << "rcv FIN => TIME_WAIT" << endl;
                  currSeqNum = SendBlankPkt(pktQ, c, ACK, currSeqNum, ackNum, mux);
                  cs->state.SetState(TIME_WAIT);
                  cs->state.SetTimerTries(2);
                }
              }

              break;
              case CLOSING:
              {
                cerr << "CLOSING: ";
                // rcv ACK of FIN
                // --------------  => TIME_WAIT
                //       x
                if (IS_ACK(flag) && goodLastAcked) {
                  cerr << "rcv ACK => TIME_WAIT" << endl;
                  cs->state.SetState(TIME_WAIT);
                  cs->state.SetTimerTries(2);
                } else if (IS_FIN(flag)) {
                  currSeqNum = SendBlankPkt(pktQ, c, ACK, currSeqNum, ackNum, mux);
                }
              }

              break;
              case LAST_ACK:
              {
                cerr << "LAST_ACK: ";
                // rcv ACK of FIN
                // --------------  => CLOSED
                //       x

                if (IS_ACK(flag) && goodLastAcked) {
                  cerr << "rcv ACK => CLOSED" << endl;
                  cs->state.SetState(CLOSED);
                }
              }

              break;
              case TIME_WAIT:
              {
                cerr << "TIME_WAIT: ";
                // timeout=2MSL
                // ------------  => CLOSED
                //  delete TCB

                if(IS_FIN(flag) || IS_ACK(flag)){
                  currSeqNum = SendBlankPkt(pktQ, c, ACK, currSeqNum, ackNum, mux);
                }
              }

              break;
              default:
              {

              }
            }
          }
        } else {
          cerr << "Could not find matching connection" << endl;
        }
      }

        //  Data from the Sockets layer above  //
      if (event.handle==sock) {
        SockRequestResponse req;
        unsigned int initialTimeout = 3,
                    initialTimerTries = 3;
        MinetReceive(sock,req);
        cerr << "Received Socket Request:" << req << endl;

        switch (req.type) {
          case CONNECT:
          {
            // active open
            // -----------  => SYN_SENT
            // Create TCB
            //  snd SYN
            //cerr << "CONNECT (active open) => LISTEN" << endl;
            ConnectionList<TCPState>::iterator cs = clist.FindMatching(req.connection);
            if (cs==clist.end()) {
              cerr << "active open, snd SYN => SYN_SENT" << endl;

              TCPState tcps(currSeqNum, SYN_SENT, initialTimerTries);
              tcps.SetSendRwnd(3000);
              tcps.SetLastRecvd(0);
              tcps.SetLastSent(currSeqNum - 1);
              tcps.SetLastAcked(currSeqNum - 1);
              ConnectionToStateMapping<TCPState> m(req.connection,
                                                     initialTimeout, //const Time &t ??,
                                                     tcps, //const STATE &s(seqNum, state, timerTries) ??
                                                     true); //const bool &b); ??
              clist.push_back(m);

              currSeqNum = SendBlankPkt(pktQ, req.connection, SYN, currSeqNum, 0, mux);
            }



            SockRequestResponse repl;
            repl.type=STATUS;
            repl.connection=req.connection;
            repl.bytes=0;
            repl.error=EOK;
            MinetSend(sock,repl);



            // SockRequestResponse write;
            // write.type=WRITE;
            // write.connection = req.connection;
            // write.bytes = 0;
            // write.error = EOK;

            // MinetSend(sock,write);
          }
          break;
          case ACCEPT:
          {
            // passive open
            // ------------  => LISTEN
            //  create TCB
            cerr << "ACCEPT (passive open) => LISTEN" << endl;
            TCPState tcps(currSeqNum, LISTEN, initialTimerTries);
            tcps.SetSendRwnd(3000);
            tcps.SetLastRecvd(0);
            tcps.SetLastSent(currSeqNum - 1);
            tcps.SetLastAcked(currSeqNum - 1);

            ConnectionToStateMapping<TCPState> m(req.connection,
                                                 initialTimeout, //const Time &t ??,
                                                 tcps, //const STATE &s(seqNum, state, timerTries) ??
                                                 false); //const bool &b); ??


            clist.push_back(m);

            SockRequestResponse repl;
            repl.type=STATUS;
            repl.connection=req.connection;
            repl.bytes=0;
            repl.error=EOK;

            MinetSend(sock,repl);
          }
          break;
          case WRITE:
          {
            cerr << "WRITE" << endl;
            ConnectionList<TCPState>::iterator cs = clist.FindMatching(req.connection);
            SockRequestResponse repl;
            repl.connection=req.connection;
            repl.type=STATUS;
            if (cs==clist.end()) {
              repl.error=ENOMATCH;
              cout << clist << endl;
            } else {
              //   SEND
              // -------  => SYN_SENT
              // snd SYN
              //cerr << "SEND, snd SYN => SYN_SENT" << endl;

              if (cs->state.GetState() == ESTABLISHED)
              {
                unsigned int ackNum = 0;//cs->state.GetLastAcked();
                currSeqNum = SendBlankPkt(pktQ, req.connection, SYN, currSeqNum, ackNum, mux);//, req.data);

                cs->state.SetState(SYN_SENT);
              }



              // unsigned bytes = MIN_MACRO(IP_PACKET_MAX_LENGTH-TCP_HEADER_MAX_LENGTH, req.data.GetSize());
              // // create the payload of the packet
              // Packet p(req.data.ExtractFront(bytes));
              // // Make the IP header first since we need it to do the tcp checksum
              // IPHeader sendIPHead;
              // sendIPHead.SetProtocol(IP_PROTO_TCP);
              // sendIPHead.SetSourceIP(req.connection.src);
              // sendIPHead.SetDestIP(req.connection.dest);
              // sendIPHead.SetTotalLength(bytes+TCP_HEADER_MAX_LENGTH+IP_HEADER_BASE_LENGTH);
              // // push it onto the packet
              // p.PushFrontHeader(sendIPHead);
              // // Now build the TCP header
              // TCPHeader sendTCPHead;
              // sendTCPHead.SetSourcePort(req.connection.srcport,p);
              // sendTCPHead.SetDestPort(req.connection.destport,p);
              // sendTCPHead.SetSeqNum(100, p);
              // sendTCPHead.SetAckNum(0,p);
              // sendTCPHead.SetHeaderLen(TCP_HEADER_MAX_LENGTH,p);

              // unsigned char flag;
              // SET_SYN(flag);
              // sendTCPHead.SetFlags(flag,p);
              // sendTCPHead.SetWinSize(100,p);

              // //sendTCPHead.Set
              // //sendTCPHead.SetLength(TCP_HEADER_MAX_LENGTH+bytes,p);
              // // Now we want to have the tcp header BEHIND the IP header
              // p.PushBackHeader(sendTCPHead);
              // MinetSend(mux,p);

              repl.bytes=req.data.GetSize();//bytes;
              repl.error=EOK;
            }

            MinetSend(sock,repl);
          }
          break;
          case FORWARD:
          {// ignored, send OK response
            cerr << "FORWARD" << endl;
            SockRequestResponse repl;
            repl.type=STATUS;
            repl.connection=req.connection;
            // buffer is zero bytes
            repl.bytes=0;
            repl.error=EOK;
            MinetSend(sock,repl);
          }
          break;
          case CLOSE:
          {
            cerr << "CLOSE" << endl;
            ConnectionList<TCPState>::iterator cs = clist.FindMatching(req.connection);
            SockRequestResponse repl;
            repl.connection=req.connection;
            repl.type=STATUS;

            if (cs==clist.end()) {
              repl.error=ENOMATCH;
              MinetSend(sock,repl);
            } else {
              repl.error=EOK;
              MinetSend(sock,repl);

              switch (cs->state.GetState()) {
                case LISTEN:
                case SYN_SENT:
                {
                  //   CLOSE
                  // ----------  => CLOSED
                  // delete TCB
                  clist.erase(cs);
                }
                case SYN_RCVD:
                case ESTABLISHED:
                {
                  //  CLOSE
                  // -------  => FIN_WAIT1
                  // snd FIN
                  cerr << "CLOSE, snd FIN => FIN_WAIT1" << endl;
                  currSeqNum = SendBlankPkt(pktQ, req.connection, FIN, currSeqNum, 0, mux);
                  cs->state.SetState(FIN_WAIT1);
                }
                break;
                case CLOSE_WAIT:
                {
                  //  CLOSE
                  // -------  => LAST_ACK
                  // snd FIN
                  cerr << "CLOSE, snd FIN => LAST_ACK" << endl;
                  currSeqNum = SendBlankPkt(pktQ, req.connection, FIN, currSeqNum, 0, mux);
                  cs->state.SetState(LAST_ACK);
                }
                break;
                default:

                break;
              }
            }


          }
          break;
          case STATUS:
          break;
          default:
          {
            SockRequestResponse repl;
            repl.type=STATUS;
            repl.error=EWHAT;
            MinetSend(sock,repl);
          }
        }
      }

      cerr << "========end main while loop========\n\n";
    }
  }

  return 0;
}