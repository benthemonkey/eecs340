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


#include <iostream>

#include "Minet.h"
#include "tcpstate.h"

using std::cout;
using std::endl;
using std::cerr;
using std::string;

Packet CreateSendPkt(Connection c, unsigned char flag, unsigned int seqNum, unsigned int ackNum){
  unsigned bytes = 0;//MIN_MACRO(IP_PACKET_MAX_LENGTH-TCP_HEADER_MAX_LENGTH, req.data.GetSize());
  // create the payload of the packet
  Packet sendP;//(req.data.ExtractFront(bytes));
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
  sendTCPHead.SetAckNum(ackNum,sendP);
  sendTCPHead.SetHeaderLen(TCP_HEADER_BASE_LENGTH/4,sendP);

  sendTCPHead.SetFlags(flag,sendP);
  sendTCPHead.SetWinSize(100,sendP);

  // Now we want to have the tcp header BEHIND the IP header
  sendP.PushBackHeader(sendTCPHead);
  return sendP;
}

int main(int argc, char *argv[])
{
  MinetHandle mux, sock;
  unsigned int currSeqNum = 1;

  ConnectionList<TCPState> clist;

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
  cerr << "1" << endl;
  while (MinetGetNextEvent(event)==0) {
    cerr << "2" << endl;
    // if we received an unexpected type of event, print error

    if (event.eventtype!=MinetEvent::Dataflow || event.direction!=MinetEvent::IN) {
      MinetSendToMonitor(MinetMonitoringEvent("Unknown event ignored."));
      // if we received a valid event from Minet, do processing
    } else {
      cerr << "3" << endl;
        //  Data from the IP layer below  //
      if (event.handle==mux) {
        cerr << "4-mux" << endl;

        Packet receiveP;
        MinetReceive(mux,receiveP);
        unsigned tcphlen=TCPHeader::EstimateTCPHeaderLength(receiveP);
        cerr << "estimated header len="<<tcphlen<<"\n";
        receiveP.ExtractHeaderFromPayload<TCPHeader>(tcphlen);
        IPHeader recIPHead=receiveP.FindHeader(Headers::IPHeader);
        TCPHeader recTCPHead=receiveP.FindHeader(Headers::TCPHeader);

        cerr << "TCP Packet: IP Header is "<<recIPHead<<" and ";
        cerr << "TCP Header is "<<recTCPHead << " and ";

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

        //hard-coding to test mux stuff without sock implemented
        // TCPState hardCodedState(1000,LISTEN,2);
        // ConnectionToStateMapping<TCPState> hardCodedConn(c, Time(5), hardCodedState, true);
        // clist.push_back(hardCodedConn);

        
        if (cs!=clist.end())
        {
          cerr << "5" << endl;

          if (!recTCPHead.IsCorrectChecksum(receiveP))
          {
            //corrupt packet
          } else {
            cerr << "State:" << cs->state.GetState() << endl;

            switch (cs->state.GetState()) {
              case CLOSED:
              // active open
              // -----------  => SYN_SENT
              // Create TCB
              //  snd SYN


              // passive open
              // ------------  => LISTEN
              //  create TCB

              break;
              case LISTEN:
              {
                // rcv SYN
                // -------  => SYN_RCVD
                // snd SYN, ACK
                if (IS_SYN(flag))
                {
                  unsigned char sendFlag;
                  SET_SYN(sendFlag);
                  SET_ACK(sendFlag);
                  unsigned int recSeqNum;
                  recTCPHead.GetSeqNum(recSeqNum);
                  Packet sendP = CreateSendPkt(c, sendFlag, currSeqNum, recSeqNum+1);
                  MinetSend(mux,sendP);

                  cs->state.SetState(SYN_RCVD);
                }

                //   SEND
                // -------  => SYN_SENT
                // snd SYN


                //   CLOSE
                // ----------  => CLOSED
                // delete TCB
                if (IS_FIN(flag) && IS_ACK(flag)) {
                  cs->state.SetState(CLOSED);
                  clist.erase(cs);
                }

              }
              break;
              case SYN_RCVD:
              {
                if (IS_ACK(flag))
                {
                  //  CLOSE
                  // -------  => FIN_WAIT1
                  // snd FIN
                  if (IS_FIN(flag))
                  {
                    unsigned char sendFlag;
                    SET_FIN(sendFlag);
                    unsigned int recSeqNum;
                    recTCPHead.GetSeqNum(recSeqNum);
                    Packet sendP = CreateSendPkt(c, sendFlag, currSeqNum, recSeqNum+1);
                    MinetSend(mux,sendP);

                    cs->state.SetState(FIN_WAIT1);
                  }
                  // rcv ACK of SYN
                  // --------------  => ESTABLISHED
                  //       x
                  else
                  {
                    cs->state.SetState(ESTABLISHED);
                  }
                } 
              }
              break;
              case  SYN_SENT:
              {
                //   CLOSE
                // ----------  => CLOSED
                // delete TCB
                if (IS_FIN(flag) && IS_ACK(flag)) {
                  unsigned char sendFlag;
                  SET_FIN(sendFlag);
                  unsigned int recSeqNum;
                  recTCPHead.GetSeqNum(recSeqNum);
                  Packet sendP = CreateSendPkt(c, sendFlag, currSeqNum, recSeqNum+1);
                  MinetSend(mux,sendP);

                  cs->state.SetState(LAST_ACK);
                }


                // rcv SYN
                // -------  => SYN_RCVD
                // snd ACK


                // rcv SYN, ACK
                // ------------  => ESTABLISHED
                //   snd ACK
              }
              break;
              case SYN_SENT1:
              {
                
              }
              break;
              case  ESTABLISHED:
              {
                
                if (IS_FIN(flag)) {
                  //  CLOSE
                  // -------  => FIN_WAIT1
                  // snd FIN
                  if (IS_ACK(flag)) {
                    unsigned char sendFlag;
                    SET_FIN(sendFlag);
                    unsigned int recSeqNum;
                    recTCPHead.GetSeqNum(recSeqNum);
                    Packet sendP = CreateSendPkt(c, sendFlag, currSeqNum, recSeqNum+1);
                    MinetSend(mux,sendP);

                    cs->state.SetState(FIN_WAIT1);
                  }
                  // rcv FIN
                  // -------  => CLOSE_WAIT
                  // snd ACK
                  else
                  {
                    unsigned char sendFlag;
                    SET_ACK(sendFlag);
                    unsigned int recSeqNum;
                    recTCPHead.GetSeqNum(recSeqNum);
                    Packet sendP = CreateSendPkt(c, sendFlag, currSeqNum, recSeqNum+1);
                    MinetSend(mux,sendP);

                    cs->state.SetState(CLOSE_WAIT);
                  }
                  
                }
              }
              break;
              case SEND_DATA:
              {

              }
              break;
              case CLOSE_WAIT:
              {
                //  CLOSE
                // -------  => LAST_ACK
                // snd FIN
                if (IS_FIN(flag) && IS_ACK(flag)) {
                  unsigned char sendFlag;
                  SET_FIN(sendFlag);
                  unsigned int recSeqNum;
                  recTCPHead.GetSeqNum(recSeqNum);
                  Packet sendP = CreateSendPkt(c, sendFlag, currSeqNum, recSeqNum+1);
                  MinetSend(mux,sendP);

                  cs->state.SetState(LAST_ACK);
                }
              }

              break;
              case FIN_WAIT1:
              {
                // rcv ACK of FIN
                // --------------  => FIN_WAIT2
                //       x
                if (IS_ACK(flag)) {
                  cs->state.SetState(FIN_WAIT2);
                }

                // rcv FIN
                // -------  => CLOSING
                // snd ACK
                if (IS_FIN(flag)) {
                  unsigned char sendFlag;
                  SET_ACK(sendFlag);
                  unsigned int recSeqNum;
                  recTCPHead.GetSeqNum(recSeqNum);
                  Packet sendP = CreateSendPkt(c, sendFlag, currSeqNum, recSeqNum+1);
                  MinetSend(mux,sendP);

                  cs->state.SetState(CLOSING);
                }
              }

              break;
              case CLOSING:
              {
                // rcv ACK of FIN
                // --------------  => TIME_WAIT
                //       x
                if (IS_ACK(flag)) {
                  cs->state.SetState(TIME_WAIT);
                }
              }

              break;
              case LAST_ACK:
              {
                // rcv ACK of FIN
                // --------------  => CLOSED
                //       x
                if (IS_ACK(flag)) {
                  cs->state.SetState(CLOSED);
                }
              }

              break;
              case FIN_WAIT2:
              {
                // rcv FIN
                // -------  => TIME_WAIT
                // snd ACK
                if (IS_FIN(flag)) {
                  unsigned char sendFlag;
                  SET_ACK(sendFlag);
                  unsigned int recSeqNum;
                  recTCPHead.GetSeqNum(recSeqNum);
                  Packet sendP = CreateSendPkt(c, sendFlag, currSeqNum, recSeqNum+1);
                  MinetSend(mux,sendP);

                  cs->state.SetState(TIME_WAIT);
                }
              }

              break;
              case TIME_WAIT:
              {
                // timeout=2MSL
                // ------------  => CLOSED
                //  delete TCB
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
        cerr << "4-sock" << endl;

        SockRequestResponse req;
        MinetReceive(sock,req);
        cerr << "Received Socket Request:" << req << endl;

        switch (req.type) {
          case CONNECT:
          {//active open
            ConnectionList<TCPState>::iterator cs = clist.FindMatching(req.connection);
            if (cs!=clist.end()) {
              ConnectionToStateMapping<TCPState> m(req.connection,
                                                   5, //const Time &t ??,
                                                   TCPState(1000,LISTEN,2), //const STATE &s(seqNum, state, timerTries) ??
                                                   false); //const bool &b); ??
              clist.push_back(m);
            }

            SockRequestResponse repl;
            repl.type=STATUS;
            repl.connection=req.connection;
            repl.bytes=0;
            repl.error=EOK;
            MinetSend(sock,repl);

            SockRequestResponse write;
            write.type=WRITE;
            write.connection = req.connection;
            write.bytes = 0;
            write.error = EOK;

            MinetSend(sock,write);
          }
          break;
          case ACCEPT:
          {//passive open
            ConnectionToStateMapping<TCPState> m(req.connection,
                                                 5, //const Time &t ??,
                                                 TCPState(1000,LISTEN,2), //const STATE &s(seqNum, state, timerTries) ??
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
            ConnectionList<TCPState>::iterator cs = clist.FindMatching(req.connection);
            SockRequestResponse repl;
            repl.connection=req.connection;
            repl.type=STATUS;
            if (cs==clist.end()) {
              repl.error=ENOMATCH;
              cout << clist << endl;
            } else {
              unsigned bytes = MIN_MACRO(IP_PACKET_MAX_LENGTH-TCP_HEADER_MAX_LENGTH, req.data.GetSize());
              // create the payload of the packet
              Packet p(req.data.ExtractFront(bytes));
              // Make the IP header first since we need it to do the tcp checksum
              IPHeader sendIPHead;
              sendIPHead.SetProtocol(IP_PROTO_TCP);
              sendIPHead.SetSourceIP(req.connection.src);
              sendIPHead.SetDestIP(req.connection.dest);
              sendIPHead.SetTotalLength(bytes+TCP_HEADER_MAX_LENGTH+IP_HEADER_BASE_LENGTH);
              // push it onto the packet
              p.PushFrontHeader(sendIPHead);
              // Now build the TCP header
              TCPHeader sendTCPHead;
              sendTCPHead.SetSourcePort(req.connection.srcport,p);
              sendTCPHead.SetDestPort(req.connection.destport,p);
              sendTCPHead.SetSeqNum(100, p);
              sendTCPHead.SetAckNum(0,p);
              sendTCPHead.SetHeaderLen(TCP_HEADER_MAX_LENGTH,p);

              unsigned char flag;
              SET_SYN(flag);
              sendTCPHead.SetFlags(flag,p);
              sendTCPHead.SetWinSize(100,p);

              //sendTCPHead.Set
              //sendTCPHead.SetLength(TCP_HEADER_MAX_LENGTH+bytes,p);
              // Now we want to have the tcp header BEHIND the IP header
              p.PushBackHeader(sendTCPHead);
              MinetSend(mux,p);

              repl.bytes=bytes;
              repl.error=EOK;
            }

            MinetSend(sock,repl);
          }
          break;
          case FORWARD:
          {// ignored, send OK response
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
            ConnectionList<TCPState>::iterator cs = clist.FindMatching(req.connection);
            SockRequestResponse repl;
            repl.connection=req.connection;
            repl.type=STATUS;
            cout << 6 << endl;
            if (cs==clist.end()) {
              repl.error=ENOMATCH;
            } else {
              repl.error=EOK;
              clist.erase(cs);
            }
            MinetSend(sock,repl);
            cout << 7 << endl;
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
    }
  }

  return 0;
}