#include "node.h"
#include "context.h"
#include "error.h"


Node::Node(const unsigned n, SimulationContext *c, double b, double l) :
    number(n), context(c), bw(b), lat(l)
{}

Node::Node()
{ throw GeneralException(); }

Node::Node(const Node &rhs) :
  number(rhs.number), context(rhs.context), bw(rhs.bw), lat(rhs.lat) {}

Node & Node::operator=(const Node &rhs)
{
  return *(new(this)Node(rhs));
}

void Node::SetNumber(const unsigned n)
{ number=n;}

unsigned Node::GetNumber() const
{ return number;}

void Node::SetLatency(const double l)
{ lat=l;}

double Node::GetLatency() const
{ return lat;}

void Node::SetBW(const double b)
{ bw=b;}

double Node::GetBW() const
{ return bw;}

Node::~Node()
{}

void Node::SendToNeighbors(const RoutingMessage *m)
{
  context->SendToNeighbors(this,m);
}

void Node::SendToNeighbor(const Node *n, const RoutingMessage *m)
{
  context->SendToNeighbor(this,n,m);
}

deque<Node*> *Node::GetNeighbors()
{
  return context->GetNeighbors(this);
}

void Node::SetTimeOut(const double timefromnow)
{
  context->TimeOut(this,timefromnow);
}


bool Node::Matches(const Node &rhs) const
{
  return number==rhs.number;
}


#if defined(GENERIC)
void Node::LinkUpdate(const Link *l)
{
  cerr << *this << " got a link update: "<<*l<<endl;
  //Do Something generic:
  SendToNeighbors(new RoutingMessage);
}


void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
  cerr << *this << " got a routing messagee: "<<*m<<" Ignored "<<endl;
}


void Node::TimeOut()
{
  cerr << *this << " got a timeout: ignored"<<endl;
}

Node *Node::GetNextHop(const Node *destination) const
{
  return 0;
}

Table *Node::GetRoutingTable() const
{
  return new Table;
}


ostream & Node::Print(ostream &os) const
{
  os << "Node(number="<<number<<", lat="<<lat<<", bw="<<bw<<")";
  return os;
}

#endif

/*#if defined(LINKSTATE)

const unsigned Node::maxttl=5;

void Node::LinkUpdate(const Link *l)
{
  // Add To table and update graph
  // Now flood to rest of network
  cerr << *this<<": Link Update: "<<*l<<endl;
  RoutingMessage *m = new RoutingMessage(*this,*l,maxttl,seqno++);
  SendToNeighbors(m);
}


void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
  cerr << *this << " Routing Message: "<<*m;
  if (m->srcnode.Matches(*this)) {
    // skip, one of ours
    cerr << " IGNORED"<<endl;
  } else {
    // update our table
    if (m->ttl==0) {
      cerr << " DROPPED, TTL=0"<<endl;
    } else {
      // Forward to neighbors
      RoutingMessage *out=new RoutingMessage(*m);
      out->ttl--;
      cerr << " FORWARDED"<<endl;
      SendToNeighbors(out);
    }
  }
}

void Node::TimeOut()
{
  cerr << *this << " got a timeout: ignored"<<endl;
}

Node *Node::GetNextHop(const Node *destination) const
{
  // WRITE
  return 0;
}

Table *Node::GetRoutingTable() const
{
  // WRITE
  return 0;
}


ostream & Node::Print(ostream &os) const
{
  os << "Node(number="<<number<<", lat="<<lat<<", bw="<<bw<<")";
  return os;
}
#endif*/


#if defined(DISTANCEVECTOR)

void Node::LinkUpdate(const Link *l)
{
  cerr << *this << ": Link Update: " << *l << endl;

  Row* r = table.GetNext(l->GetDest());
  if (r->next_node == l->GetDest() || r->cost < 1 || r->cost < l->GetLatency())
  {
    // update our table for the link that just changed
    UpdateTableRow(l->GetDest(), l->GetDest(), l->GetLatency());

    // Update all costs as necessary based on changed link
    UpdateRoutingTable();
  }
}

void Node::PN(unsigned n) //pretty print number
{
  if (n > 99)
  {
    cerr << n;
  }
  else if (n > 9)
  {
    cerr << " " << n;
  }
  else
  {
    cerr << "  " << n;
  }
}

void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
  unsigned srcNum = m->srcnode.GetNumber(),
           destNum = m->dest.GetNumber();

  if (destNum==GetNumber()) {
    //cerr << " ourself - ignored\n";
    return;
  }

  Row* pathToSource = table.GetNext(srcNum);
  Row* pathToDest = table.GetNext(destNum);
  // PN(number); cerr << " got: "; PN(srcNum); cerr << "--$"; PN((unsigned)m->cost); cerr << "-->"; PN(destNum);
  // cerr << "checking: "; PN(number); cerr << "--$"; PN((unsigned)pathToSource->cost); cerr << "-->";
  // PN(srcNum); cerr << "--$"; PN((unsigned)m->cost); cerr << "-->"; PN(destNum); cerr << " vs. $" << pathToDest->cost << ". ";

  //   (pathToDest->next_node == srcNum
  // && pathToDest->cost != pathToSource->cost + m->cost) I used to go through src in my best route but now his cost changed
  // || pathToDest->cost < 1                              I currently don't know how to get to dest
  // || pathToDest->cost > pathToSource->cost + m->cost   His path is better than mine
  if ((pathToDest->next_node == srcNum && pathToDest->cost != pathToSource->cost + m->cost)
        || pathToDest->cost < 1 || pathToDest->cost > pathToSource->cost + m->cost)
  {
    //cerr << " updating." << endl;
    UpdateTableRow(destNum, srcNum, pathToSource->cost + m->cost);
  } else {
    //cerr << " not updating." << endl;
  }

  UpdateRoutingTable();
}

void Node::TimeOut()
{
  cerr << *this << " got a timeout: ignored"<<endl;
}


Node *Node::GetNextHop(const Node *destination) const
{
  Row* r = GetRoutingTable()->GetNext(destination->GetNumber());

  return new Node(r->next_node, context, bw, r->cost);
}

Table *Node::GetRoutingTable() const
{
  return new Table(table);
}

void Node::UpdateRoutingTable()
{
  bool printout = false;
  Table* t = GetRoutingTable();
  deque<Row> tDeque = t->GetDeque();
  // for each y in N:

  for (deque<Row>::iterator y = tDeque.begin(); y != tDeque.end(); ++y)
  {
    unsigned yDest = y->dest_node;
    double cCost, dCost, tmpCost;
    double bestCost = -1;
    unsigned bestNextHop, vNum;

    deque<Node*> *neighbors = GetNeighbors();
    for (deque<Node*>::iterator v = neighbors->begin(); v != neighbors->end(); ++v)
    {
      // Dx(y) = minv{c(x,v) + Dv(y)}
      vNum = (*v)->GetNumber();
      cCost = (t->GetNext(vNum))->cost;
      Row* vRow = ((*v)->GetRoutingTable())->GetNext(yDest);
      dCost = vRow->cost;
      tmpCost = cCost + dCost;

      if (printout)
      {
        cerr << number << "=(" << cCost << ")=>" << vNum << "=(" << dCost << ")=>" << yDest
             << " [" << tmpCost << "]";
      }

      // vRow->next_node != number            Make sure my neighbor's best route isn't through me
      // cCost > 0                            If its 0, I don't know how long it takes to get to my neighbor yet
      // dCost > 0 || yDest == vNum           Either my neighbor doesn't know how to get to dest, or my neighbor is dest
      // tmpCost < bestCost || bestCost == -1 My neighbor's route is better than my current one
      if (vRow->next_node != number && cCost > 0 && (dCost > 0 || yDest == vNum) && (tmpCost < bestCost || bestCost == -1))
      {
        bestCost = tmpCost;
        bestNextHop = vNum;

        if (printout) cerr << " << new best";
      }
      if (printout) cerr << endl;
    }

    // if Dx(y) changed for any destination y
    // send distance vector Dx = [Dx(y): y in N] to all neighbors
    if (printout) cerr << "ycost: " << y->cost << ", bestCost: " << bestCost << ", ynext: " << y->next_node << ", bestnext: " << bestNextHop << endl;
    if (bestCost > 0 && (y->cost != bestCost || y->next_node != bestNextHop))
    {
      if (printout && y->cost == bestCost)
      {
        cerr << "because ycost = bestcost ";
      }
      if (printout && y->next_node == bestNextHop)
      {
        cerr << "because next_node = bestNextHop ";
      }
      if (printout) cerr << "changed value from " << y->cost << "! reaching " << yDest << " via " << bestNextHop << " with cost " << bestCost << endl;
      UpdateTableRow(yDest, bestNextHop, bestCost);
    }
  }
}

void Node::UpdateTableRow(unsigned dest, unsigned nextHop, double cost)
{
  //cerr << "Updating table for node: " << number << ", dest: " << dest << ", next hop: " << nextHop << ", cost: " << cost << endl;
  //Table* t = this->GetRoutingTable();
  const Row newRow(dest, nextHop, cost);
  table.SetNext(dest, newRow);

  Node msgNode(dest, context, bw, cost);
  SendToNeighbors(new RoutingMessage(*this, msgNode, cost));
}

ostream & Node::Print(ostream &os) const
{
  os << "Node(number="<<number<<", lat="<<lat<<", bw="<<bw;
  os << ", table="<<table<<")";
  return os;
}
#endif
