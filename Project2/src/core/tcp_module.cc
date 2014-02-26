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

int main(int argc, char *argv[])
{
  MinetHandle mux, sock;

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

        Packet p;
        MinetReceive(mux,p);
        unsigned tcphlen=TCPHeader::EstimateTCPHeaderLength(p);
        cerr << "estimated header len="<<tcphlen<<"\n";
        p.ExtractHeaderFromPayload<TCPHeader>(tcphlen);
        IPHeader iph=p.FindHeader(Headers::IPHeader);
        TCPHeader tcph=p.FindHeader(Headers::TCPHeader);

        cerr << "TCP Packet: IP Header is "<<iph<<" and ";
        cerr << "TCP Header is "<<tcph << " and ";

        cerr << "Checksum is " << (tcph.IsCorrectChecksum(p) ? "VALID" : "INVALID");

        Connection c;
        iph.GetDestIP(c.src);
        iph.GetSourceIP(c.dest);
        iph.GetProtocol(c.protocol);
        tcph.GetDestPort(c.srcport);
        tcph.GetSourcePort(c.destport);

        unsigned char flag;
        tcph.GetFlags(flag); 

        ConnectionList<TCPState>::iterator cs = clist.FindMatching(c);

        //hard-coding to test mux stuff without sock implemented
        // TCPState hardCodedState(1000,LISTEN,2);
        // ConnectionToStateMapping<TCPState> hardCodedConn(c, Time(5), hardCodedState, true);
        // clist.push_back(hardCodedConn);


        if (cs!=clist.end())
        {
          if (tcph.IsCorrectChecksum(p))
          {
            //corrupt packet
          } else {
            switch (cs->state.GetState()) {
              case CLOSED:

              break;
              case LISTEN:
              {
                //rcv SYN = snd SYN, ACK
                if (IS_SYN(flag))
                {
                  
                }
                //SEND = snd SYN

                //CLOSE = delete TCB

              }
              break;
              case SYN_RCVD:
              {
                //CLOSE = snd FIN

                //rcv ACK of SYN = x
              }
              break;
              case  SYN_SENT:
              {
                //CLOSE = delete TCB

                //rcv SYN = snd ACK

                //rcv SYN,ACK = snd ACK

              }
              break;
              case SYN_SENT1:
              {
                //
              }
              break;
              case  ESTABLISHED:

              break;
              case SEND_DATA:

              break;
              case CLOSE_WAIT:

              break;
              case FIN_WAIT1:

              break;
              case CLOSING:

              break;
              case LAST_ACK:

              break;
              case FIN_WAIT2:

              break;
              case TIME_WAIT:

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



            // unsigned bytes = MIN_MACRO(IP_PACKET_MAX_LENGTH-TCP_HEADER_MAX_LENGTH, req.data.GetSize());
            // // create the payload of the packet
            // Packet p(req.data.ExtractFront(bytes));
            // // Make the IP header first since we need it to do the tcp checksum
            // IPHeader ih;
            // ih.SetProtocol(IP_PROTO_TCP);
            // ih.SetSourceIP(req.connection.src);
            // ih.SetDestIP(req.connection.dest);
            // ih.SetTotalLength(bytes+TCP_HEADER_MAX_LENGTH+IP_HEADER_BASE_LENGTH);
            // // push it onto the packet
            // p.PushFrontHeader(ih);
            // // Now build the TCP header
            // TCPHeader th;
            // th.SetSourcePort(req.connection.srcport,p);
            // th.SetDestPort(req.connection.destport,p);
            // th.SetSeqNum(100, p);
            // th.SetAckNum(0,p);
            // th.SetHeaderLen(TCP_HEADER_MAX_LENGTH,p);
            
            // unsigned char flag;
            // SET_SYN(flag);
            // th.SetFlags(flag,p);
            // th.SetWinSize(100,p);
            
            // // Now we want to have the tcp header BEHIND the IP header
            // cout << p << endl;
            // p.PushBackHeader(th);
            // MinetSend(mux,p);




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
              IPHeader ih;
              ih.SetProtocol(IP_PROTO_TCP);
              ih.SetSourceIP(req.connection.src);
              ih.SetDestIP(req.connection.dest);
              ih.SetTotalLength(bytes+TCP_HEADER_MAX_LENGTH+IP_HEADER_BASE_LENGTH);
              // push it onto the packet
              p.PushFrontHeader(ih);
              // Now build the TCP header
              TCPHeader th;
              th.SetSourcePort(req.connection.srcport,p);
              th.SetDestPort(req.connection.destport,p);
              th.SetSeqNum(100, p);
              th.SetAckNum(0,p);
              th.SetHeaderLen(TCP_HEADER_MAX_LENGTH,p);

              unsigned char flag;
              SET_SYN(flag);
              th.SetFlags(flag,p);
              th.SetWinSize(100,p);

              //th.Set
              //th.SetLength(TCP_HEADER_MAX_LENGTH+bytes,p);
              // Now we want to have the tcp header BEHIND the IP header
              cout << p << endl;
              p.PushBackHeader(th);
              MinetSend(mux,p);
              cout << 5 << endl;
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