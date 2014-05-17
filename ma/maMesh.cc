/****************************************************************************** 

  Copyright (c) 2004-2014 Scientific Computation Research Center, 
      Rensselaer Polytechnic Institute. All rights reserved.
  
  The LICENSE file included with this distribution describes the terms
  of the SCOREC Non-Commercial License this program is distributed under.
 
*******************************************************************************/
#include "maMesh.h"
#include "maTables.h"
#include <algorithm>
#include <cfloat>
#include <apf.h>
#include <PCU.h>

namespace ma {

Vector getPosition(Mesh* m, Entity* vertex)
{
  Vector r;
  m->getPoint(vertex,0,r);
  return r;
}

void addRemote(Mesh* m, Entity* e, int p, Entity* r)
{
  Remotes remotes;
  m->getRemotes(e,remotes);
  remotes[p]=r;
  m->setRemotes(e,remotes);
}

/* returns true if the arrays are equal */
static bool same(int n, Entity** a, Entity** b)
{
  for (int i=0; i < n; ++i)
    if (a[i]!=b[i])
      return false;
  return true;
}

void rotateEdge(Entity** i, int, Entity** o)
{
  o[0] = i[0];
  o[1] = i[1];
}

void rotateFace(int nv, Entity** iv, int n, Entity** ov)
{
  for (int i=0; i < nv; ++i)
    ov[i] = iv[(i+n)%nv];
}

void rotateTri(Entity** i, int n, Entity** o)
{
  rotateFace(3,i,n,o);
}

void rotateQuad(Entity** i, int n, Entity** o)
{
  rotateFace(4,i,n,o);
}

void rotateTet(Entity** iv, int n, Entity** ov)
{
  for (int i=0; i < 4; ++i)
    ov[i] = iv[tet_rotation[n][i]];
}

void rotatePrism(Entity** iv, int n, Entity** ov)
{
  for (int i=0; i < 6; ++i)
    ov[i] = iv[prism_rotation[n][i]];
}

/* pyramid rotation is essentially just quad rotation,
   and we are lucky enough that the quad vertices occupy
   the first part of the array in the appropriate order. */
void rotatePyramid(Entity** iv, int n, Entity** ov)
{
  rotateQuad(iv,n,ov);
  ov[4] = iv[4];
}

void rotateEntity(int type, Entity** iv, int n, Entity** ov)
{
  typedef void (*RotateFunction)(Entity** iv, int n, Entity** ov);
  static RotateFunction table[TYPES] =
  {0//vertex
  ,rotateEdge
  ,rotateTri
  ,rotateQuad
  ,rotateTet
  ,0//hex
  ,rotatePrism//prism
  ,rotatePyramid//pyramid
  };
  RotateFunction rotateFunction = table[type];
  rotateFunction(iv,n,ov);
}

void rotateEntity(Mesh* m, Entity* e, int n, Entity** v)
{
  Downward dv;
  m->getDownward(e,0,dv);
  int type = m->getType(e);
  rotateEntity(type,dv,n,v);
}

/* the inverse of rotateEntity on a tet, finds the rotation
   code based on the array of rotated vertices */
int findTetRotation(Mesh* m, Entity* tet, Entity** v)
{
  Entity* tv[4];
  m->getDownward(tet,0,tv);
  int first = findIn(tv,4,v[0]);
  int begin = first*3;
  int end = first*3 + 3;
  Entity* rv[4];
  for (int r = begin; r < end; ++r)
  {
    rotateTet(tv,r,rv);
    if (same(4,rv,v))
      return r;
  }
  return -1;
}

/* given a set of element-local coordinates computed
   based on a rotated set of vertices, this function
   takes the rotation code and gives the coordinates
   that match the original set of vertices */
void unrotateTetXi(Vector& xi, int rotation)
{
  double a[4];
  a[0] = 1-xi[0]-xi[1]-xi[2]; a[1] = xi[0]; a[2] = xi[1]; a[3] = xi[2];
  int const* originalIndexOf = tet_rotation[rotation];
  double b[4];
  for (int i=0; i < 4; ++i)
    b[ originalIndexOf[i] ] = a[i];
  xi[0] = b[1]; xi[1] = b[2]; xi[2] = b[3];
}

void rotateOct(Entity** iv, int n, Entity** ov)
{
  for (int i=0; i < 6; ++i)
    ov[i] = iv[oct_rotation[n][i]];
}

Vector averagePositions(Mesh* m, Entity** v, int n)
{
  Vector result = getPosition(m,v[0]);
  for (int i=1; i < n; ++i)
    result = result + getPosition(m,v[i]);
  result = result / n;
  return result;
}

int getDownIndex(Mesh* m, Entity* e, Entity* de)
{
  Downward down;
  int nd = m->getDownward(e,getDimension(m,e)-1,down);
  return findIn(down,nd,de);
}

Entity* getTriEdgeOppositeVert(Mesh* m, Entity* tri, Entity* v)
{
  Entity* tv[3];
  m->getDownward(tri,0,tv);
  Entity* te[3];
  m->getDownward(tri,1,te);
  static int const table[3] = {1,2,0};
  int n = findIn(tv,3,v);
  assert(n >= 0);
  return te[table[n]];
}

Entity* getTriVertOppositeEdge(Mesh* m, Entity* tri, Entity* e)
{
  Entity* tv[3];
  m->getDownward(tri,0,tv);
  Entity* te[3];
  m->getDownward(tri,1,te);
  static int const table[3] = {2,0,1};
  int n = findIn(te,3,e);
  assert(n >= 0);
  return tv[table[n]];
}

Entity* getTetVertOppositeTri(Mesh* m, Entity* tet, Entity* tri)
{
  Entity* tetv[4];
  m->getDownward(tet,0,tetv);
  Entity* triv[3];
  m->getDownward(tri,0,triv);
  for (int i=0; i < 4; ++i)
    if (-1==findIn(triv,3,tetv[i]))
      return tetv[i];
  return 0;
}

Entity* findTetByTwoTris(Mesh* m, Entity** tris)
{
  apf::Up rs;
  m->getUp(tris[0],rs);
  for (int i=0; i < rs.n; ++i)
  {
    Entity* r = rs.e[i];
    if (m->getType(r) != TET)
      continue;
    Downward rf;
    m->getDownward(r,2,rf);
    if (findIn(rf,4,tris[1])>=0)
      return r;
  }
  return 0;
}

Entity* rebuildElement(
    Mesh* m,
    Entity* original,
    Entity* oldVert,
    Entity* newVert,
    apf::BuildCallback* cb)
{
  int type = m->getType(original);
  if (type==VERT)
  {
    assert(original != newVert);
    if (original==oldVert)
      return newVert;
    return original;
  }
  int d = Mesh::typeDimension[type];
  Downward down;
  int nd = m->getDownward(original,d-1,down);
  for (int i=0; i < nd; ++i)
    down[i] = rebuildElement(m,down[i],oldVert,newVert,cb);
  return makeOrFind(m,m->toModel(original),type,down,cb);
}

bool isEdgeInTri(Mesh* m, Entity* tri, Entity* edge)
{
  Entity* edges[3];
  m->getDownward(tri,1,edges);
  return findIn(edges,3,edge)!=-1;
}

void getBoundingBox(Mesh* m, Vector& lower, Vector& upper)
{
  Iterator* it = m->begin(0);
  Entity* v = m->iterate(it);
  lower = upper = getPosition(m,v);
  while ((v = m->iterate(it)))
  {
    Vector p = getPosition(m,v);
    for (int i=0; i < 3; ++i)
    {
      lower[i] = std::min(lower[i],p[i]);
      upper[i] = std::max(upper[i],p[i]);
    }
  }
  m->end(it);
  PCU_Min_Doubles(&(lower[0]),3);
  PCU_Max_Doubles(&(upper[0]),3);
}

Vector getCentroid(Mesh* m)
{
  Iterator* it = m->begin(0);
  Entity* v;
  double values[4];
  Vector pointSum(0,0,0);
  double& count = values[3];
  count = 0;
  while ((v = m->iterate(it)))
  {
    if ( ! m->isOwned(v))
      continue;
    pointSum = pointSum + getPosition(m,v);
    count += 1.0;
  }
  m->end(it);
  pointSum.toArray(values);
  PCU_Add_Doubles(&(values[0]),4);
  return Vector(values)/values[3];
}

Entity* findTriFromVerts(Mesh* m, Entity** v)
{
  Entity* e[3];
  for (int i=0; i < 3; ++i)
  {
    Entity* ev[2] =
    { v[apf::tri_edge_verts[i][0]], v[apf::tri_edge_verts[i][1]] };
    e[i] = findUpward(m,EDGE,ev);
  }
  return findUpward(m,TRI,e);
}

double measure(Mesh* m, Entity* e)
{
  apf::MeshElement* me = apf::createMeshElement(m,e);
  double size = apf::measure(me);
  apf::destroyMeshElement(me);
  return size;
}

bool isOnModelEdge(Mesh* m, Entity* e)
{
  return m->getModelType(m->toModel(e))==1;
}

bool isOnModelFace(Mesh* m, Entity* e)
{
  return m->getModelType(m->toModel(e))==2;
}

Vector getTriNormal(Mesh* m, Entity** v)
{
  Vector x[3];
  for (int i=0; i < 3; ++i)
    x[i] = getPosition(m,v[i]);
  return apf::cross((x[1]-x[0]),(x[2]-x[0]));
}

bool isTwoTriAngleAcute(Mesh* m, Entity** va, Entity** vb)
{
  return (getTriNormal(m,va)*getTriNormal(m,vb)) > 0;
}

bool isTwoTriAngleAcute(Mesh* m, Entity* a, Entity* b)
{
  Entity* va[3];
  m->getDownward(a,0,va);
  Entity* vb[3];
  m->getDownward(b,0,vb);
  return isTwoTriAngleAcute(m,va,vb);
}

double getAverageElementSize(Mesh* m)
{
  double sums[2];
  double& sizeSum = sums[0];
  sizeSum = 0;
  Iterator* it = m->begin(m->getDimension());
  Entity* e;
  while ((e = m->iterate(it)))
    sizeSum += measure(m,e); 
  m->end(it);
  double& count = sums[1];
  count = m->count(m->getDimension());
  PCU_Add_Doubles(sums,2);
  return sizeSum / count;
}

double getMinimumElementSize(Mesh* m)
{
  double minimum = DBL_MAX;
  Iterator* it = m->begin(m->getDimension());
  Entity* e;
  while ((e = m->iterate(it)))
  {
    double size = measure(m,e); 
    if (size < minimum) minimum=size;
  }
  m->end(it);
  PCU_Min_Doubles(&minimum,1);
  return minimum;
}

void getFaceEdgesAndDirections(
    Mesh* m,
    Entity* face,
    Entity** edges,
    int* directions)
{
  int n = m->getDownward(face,1,edges);
  Downward v;
  m->getDownward(face,0,v);
  for (int i=0; i < n; ++i)
  {
    Entity* ev[2];
    m->getDownward(edges[i],0,ev);
    if (ev[0]==v[i])
      directions[i] = 0;
    else
      directions[i] = 1;
  }
}

}