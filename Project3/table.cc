#include "table.h"

#if defined(GENERIC)
ostream & Table::Print(ostream &os) const
{
  // WRITE THIS
  os << "Table()";
  return os;
}
#endif

/*#if defined(LINKSTATE)

#endif*/

#if defined(DISTANCEVECTOR)

deque<Row>::iterator Table::FindMatching(const unsigned dest)
{
  for (deque<Row>::iterator i = m.begin(); i != m.end(); ++i)
  {
    if (i->dest_node == dest)
    {
      return i;
    }
  }

  return m.end();
}

deque<Row> Table::GetDeque()
{
  return m;
}

Row *Table::GetNext(const unsigned dest)
{
  deque<Row>::iterator d = this->FindMatching(dest);
  return new Row(d->dest_node, d->next_node, d->cost);
}

void Table::SetNext(const unsigned dest, const Row &r)
{
  deque<Row>::iterator d = this->FindMatching(dest);

  d->next_node = r.next_node;
  d->cost = r.cost;
}

Row::Row(const unsigned dest, const unsigned next, const double c) :
  dest_node(dest), next_node(next), cost(c)
{}

ostream & Row::Print(ostream &os) const
{
  os <<"Row(dest="<<dest_node<<", next="<<next_node<<", cost="<<cost<<")";
  return os;
}

ostream & Table::Print(ostream &os) const
{
  os<<"Table(rows={";
  for (deque<Row>::const_iterator i=m.begin();i!=m.end();++i) {
    os <<(*i)<<", ";
  }
  os<<"})";
  return os;
}

#endif
