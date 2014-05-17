/****************************************************************************** 

  Copyright (c) 2004-2014 Scientific Computation Research Center, 
      Rensselaer Polytechnic Institute. All rights reserved.
  
  The LICENSE file included with this distribution describes the terms
  of the SCOREC Non-Commercial License this program is distributed under.
 
*******************************************************************************/
#include "maSnap.h"
#include "maRefine.h"
#include "maAdapt.h"
#include <apfCavityOp.h>
#include <PCU.h>

namespace ma {

/* this is the logic to deal with discontinuous
   periodic parametric coordinates.
   We assume that if the difference between
   the vertex coordinates of a mesh edge is more than half
   the periodic range, then the edge
   crosses over the discontinuity and we need
   to interpolate differently. */
static double interpolateParametricCoordinate(
    double t,
    double a,
    double b,
    double range[2],
    bool isPeriodic)
{
  if ( ! isPeriodic)
    return (1-t)*a + t*b;
  if (range[0] > range[1])
    std::swap(range[0],range[1]);
  if (a > b)
  {
    std::swap(a,b);
    t = 1-t;
  }
  double period = range[1]-range[0];
  double span = b-a;
  if (span < (period/2))
    return (1-t)*a + t*b;
  a += period;
  double result = (1-t)*b + t*a;
  if (result > range[1])
    result -= period;
  assert(result > range[0]);
  assert(result < range[1]);
  return result;
}

static void interpolateParametricCoordinates(
    Mesh* m,
    Model* g,
    double t,
    Vector const& a,
    Vector const& b,
    Vector& p)
{
  double range[2];
  int dim = m->getModelType(g);
  for (int d=0; d < dim; ++d)
  {
    bool isPeriodic = m->getPeriodicRange(g,d,range);
    p[d] = interpolateParametricCoordinate(t,a[d],b[d],range,isPeriodic);
  }
}

void transferParametricOnEdgeSplit(
    Mesh* m,
    Entity* e,
    double t,
    Vector& p)
{
  Model* g = m->toModel(e);
  int modelDimension = m->getModelType(g);
  if (modelDimension==m->getDimension()) return;
  Entity* ev[2];
  m->getDownward(e,0,ev);
  Vector ep[2];
  for (int i=0; i < 2; ++i)
    m->getParamOn(g,ev[i],ep[i]);
  interpolateParametricCoordinates(m,g,t,ep[0],ep[1],p);
}

static void getSnapPoint(Mesh* m, Entity* v, Vector& x)
{
  m->getPoint(v,0,x);
  Vector p;
  m->getParam(v,p);
  Model* g = m->toModel(v);
  m->snapToModel(g,p,x);
}

class Snapper : public apf::CavityOp
{
  public:
    Snapper(Adapt* a):
      apf::CavityOp(a->mesh)
    {
      adapter = a;
      successCount = 0;
    }
    Outcome setEntity(Entity* e)
    {
      if ( ! getFlag(adapter,e,SNAP))
        return SKIP;
      if ( ! requestLocality(&e,1))
        return REQUEST;
      vert = e;
      return OK;
    }
    void apply()
    {
      Mesh* m = adapter->mesh;
      Vector original = getPosition(m,vert);
      Vector x;
      getSnapPoint(m,vert,x);
      m->setPoint(vert,0,x);
      Upward elements;
      m->getAdjacent(vert,m->getDimension(),elements);
      bool success = true;
      for (size_t i=0; i < elements.getSize(); ++i)
        if ( ! isElementValid(adapter,elements[i]))
        {
          m->setPoint(vert,0,original);
          success = false;
          break;
        }
      if (success) ++successCount;
      clearFlag(adapter,vert,SNAP);
    }
    int successCount;
  private:
    Adapt* adapter;
    Entity* vert;
};

int markVertsToSnap(Refine* r)
{
  int count = 0;
  Adapt* a = r->adapt;
  Mesh* m = r->adapt->mesh;
  int dim = m->getDimension();
/* currently we ignore mid-quad vertices in the context
   of snapping.
   When we re-develop boundary layer snapping this will
   be revisited */
  for (size_t i=0; i < r->newEntities[1].getSize(); ++i)
  {
    Entity* v = findSplitVert(r,1,i);
    int modelDimension = m->getModelType(m->toModel(v));
    if (modelDimension==dim) continue;
    setFlag(a,v,SNAP);
    if (m->isOwned(v))
      ++count;
  }
  return count;
}

void snap(Refine* r)
{
  if ( ! r->adapt->input->shouldSnap)
    return;
  long counts[2];
  long& targetCount = counts[0];
  targetCount = markVertsToSnap(r);
  Snapper snapper(r->adapt);
  snapper.applyToDimension(0);
  counts[1] = snapper.successCount;
  PCU_Add_Longs(counts,2);
  long& successCount = counts[1];
  print("snapped %li of %li vertices",successCount,targetCount);
}

void visualizeGeometricInfo(Mesh* m, const char* name)
{
  Tag* dimensionTag = m->createIntTag("ma_geom_dim",1);
  Tag* idTag = m->createIntTag("ma_geom_id",1);
  apf::Field* field = apf::createLagrangeField(m,"ma_param",apf::VECTOR,1);
  Iterator* it = m->begin(0);
  Entity* v;
  while ((v = m->iterate(it)))
  {
    Model* c = m->toModel(v);
    int dimension = m->getModelType(c);
    m->setIntTag(v,dimensionTag,&dimension);
    int id = m->getModelTag(c);
    m->setIntTag(v,idTag,&id);
    Vector p;
    m->getParam(v,p);
    apf::setVector(field,v,0,p);
  }
  m->end(it);
  apf::writeVtkFiles(name,m);
  it = m->begin(0);
  while ((v = m->iterate(it)))
  {
    m->removeTag(v,dimensionTag);
    m->removeTag(v,idTag);
  }
  m->end(it);
  m->destroyTag(dimensionTag);
  m->destroyTag(idTag);
  apf::destroyField(field);
}

}