/****************************************************************************** 

  Copyright (c) 2004-2014 Scientific Computation Research Center, 
      Rensselaer Polytechnic Institute. All rights reserved.
  
  The LICENSE file included with this distribution describes the terms
  of the SCOREC Non-Commercial License this program is distributed under.
 
*******************************************************************************/
#include "maRefine.h"
#include "maAdapt.h"
#include "maTables.h"
#include "maSolutionTransfer.h"
#include "maLayer.h"
#include <cstdio>

namespace ma {

void splitTet_1(Refine* r, Entity* parent, Entity** v)
{
  Entity* sv = findSplitVert(r,v[0],v[1]);
  Entity* tv[4];
  tv[0] = v[0]; tv[1] = sv; tv[2] = v[2]; tv[3] = v[3];
  buildSplitElement(r,parent,TET,tv);
  tv[0] = sv; tv[1] = v[1]; tv[2] = v[2]; tv[3] = v[3];
  buildSplitElement(r,parent,TET,tv);
}

/* tetrahedronizes a sub-region that is
   shaped like a pyramid.
   The vertices come in the standard ordering
   for pyramid vertices.
   This function expects the diagonal edge across
   the "quad" to have been created already, and
   will split depending on where that edge is */
void pyramidToTets(Refine* r, Entity* parent, Entity** v)
{
  Mesh* m = r->adapt->mesh;
  Entity* ev[2];
  /* if the edge 0<->2 doesn't exist, we assume the edge 1<->3
     does and rotate the pyramid so that it becomes 0<->2 */
  ev[0] = v[0]; ev[1] = v[2];
  int rotation = 0;
  if ( ! findUpward(m,EDGE,ev))
    rotation = 1;
  Entity* v2[5];
  rotatePyramid(v,rotation,v2);
  ev[0] = v2[0]; ev[1] = v2[2];
  assert(findUpward(m,EDGE,ev));
  Entity* tv[4];
  tv[0] = v2[0]; tv[1] = v2[1]; tv[2] = v2[2]; tv[3] = v2[4];
  buildSplitElement(r,parent,TET,tv);
  tv[0] = v2[0]; tv[1] = v2[2]; tv[2] = v2[3]; tv[3] = v2[4];
  buildSplitElement(r,parent,TET,tv);
}

/* two edges split, one face has them both */
void splitTet_2_1(Refine* r, Entity* tet, Entity** v)
{
  Entity* sv[2];
  sv[0] = findSplitVert(r,v[0],v[2]);
  sv[1] = findSplitVert(r,v[1],v[2]);
  /* a tet splits off a tet leaving a pyramid */
  Entity* tv[4];
  tv[0] = sv[0]; tv[1] = sv[1]; tv[2] = v[2]; tv[3] = v[3];
  buildSplitElement(r,tet,TET,tv);
  Entity* pv[5];
  pv[0] = v[0]; pv[1] = v[1]; pv[2] = sv[1]; pv[3] = sv[0]; pv[4] = v[3];
  pyramidToTets(r,tet,pv);
}

/* two edges split, all faces have one edge */
void splitTet_2_2(Refine* r, Entity* tet, Entity** v)
{
  Entity* sv = findSplitVert(r,v[0],v[1]);
  /* same as two recursive edge splits */
  Entity* tv[4];
  tv[0] = v[3]; tv[1] = v[2]; tv[2] = sv; tv[3] = v[0];
  splitTet_1(r,tet,tv);
  tv[0] = v[3]; tv[1] = v[2]; tv[2] = v[1]; tv[3] = sv;
  splitTet_1(r,tet,tv);
}

/* one of the quad edges (0-1) is split */
void splitPyramid_1_1(Refine* r, Entity* parent, Entity* v[5])
{
  Entity* sv = findSplitVert(r,v[0],v[1]);
  /* a tet is split off leaving another pyramid with no splits */
  Entity* tv[4];
  tv[0] = v[0]; tv[1] = sv; tv[2] = v[3]; tv[3] = v[4];
  buildSplitElement(r,parent,TET,tv);
  Entity* pv[5];
  pv[0] = sv; pv[1] = v[1]; pv[2] = v[2]; pv[3] = v[3]; pv[4] = v[4];
  pyramidToTets(r,parent,pv);
}

/* all three edges of one face are split */
void splitTet_3_1(Refine* r, Entity* tet, Entity** v)
{
  Entity* sv[3];
  sv[0] = findSplitVert(r,v[0],v[1]);
  sv[2] = findSplitVert(r,v[2],v[0]);
  Entity* tv[4];
  /* divide into a tet and a pyramid with one split edge */
  tv[0] = v[0]; tv[1] = sv[0]; tv[2] = sv[2]; tv[3] = v[3];
  buildSplitElement(r,tet,TET,tv);
  Entity* pv[5];
  pv[0] = v[1]; pv[1] = v[2]; pv[2] = sv[2]; pv[3] = sv[0]; pv[4] = v[3];
  splitPyramid_1_1(r,tet,pv);
}

/* three split edges, two faces have two edges, version 1 */
void splitTet_3_2(Refine* r, Entity* tet, Entity** v)
{
  Entity* sv[3];
  sv[0] = findSplitVert(r,v[0],v[1]);
  sv[1] = findSplitVert(r,v[0],v[2]);
  sv[2] = findSplitVert(r,v[2],v[3]);
  /* separate into two pyramids with ambiguous quads and one tet */
  Downward dv;
  dv[0] = sv[1]; dv[1] = sv[2]; dv[2] = v[3]; dv[3] = v[0]; dv[4] = sv[0];
  pyramidToTets(r,tet,dv);
  dv[0] = sv[1]; dv[1] = sv[0]; dv[2] = v[1]; dv[3] = v[2]; dv[4] = sv[2];
  pyramidToTets(r,tet,dv);
  dv[0] = sv[0]; dv[1] = sv[2]; dv[2] = v[3]; dv[3] = v[1];
  buildSplitElement(r,tet,TET,dv);
}

/* three split edges, two faces have two edges, version 2 */
void splitTet_3_3(Refine* r, Entity* tet, Entity** v)
{
  Entity* sv[3];
  sv[0] = findSplitVert(r,v[0],v[1]);
  sv[1] = findSplitVert(r,v[1],v[2]);
  sv[2] = findSplitVert(r,v[2],v[3]);
  /* separate into two pyramids with ambiguous quads and one tet */
  Downward dv;
  dv[0] = v[0]; dv[1] = sv[0]; dv[2] = sv[1]; dv[3] = v[2]; dv[4] = sv[2];
  pyramidToTets(r,tet,dv);
  dv[0] = v[1]; dv[1] = v[3]; dv[2] = sv[2]; dv[3] = sv[1]; dv[4] = sv[0];
  pyramidToTets(r,tet,dv);
  dv[0] = v[0]; dv[1] = sv[0]; dv[2] = sv[2]; dv[3] = v[3];
  buildSplitElement(r,tet,TET,dv);
}

/* given a prism-shaped vertex set, returns a
   bit vector indicating the orientation of the
   quad diagonals. if one quad has no diagonal
   it will be listed as orientation 0, which
   is expected by other code. */
int getPrismDiagonalCode(Mesh* m, Entity** v)
{
  int code = 0;
  Entity* ev[2];
  for (int i=0; i < 3; ++i)
  {
    Entity* v2[6];
    rotatePrism(v,i,v2);
    ev[0] = v2[3]; ev[1] = v2[1];
    if (findUpward(m,EDGE,ev))
      code |= (1<<i);
  }
  return code;
}

/* tetrahedronizing a prism: the good case.
   vertex 0 is shared by two diagonals:
   split into a pyramid and a tet. */
void prismToTetsGoodCase(
    Refine* r,
    Entity* parent,
    Entity** v_in,
    int code)
{
  Entity* v[6];
  rotatePrism(v_in,prism_diag_match[code],v);
  Downward dv;
  dv[0] = v[3]; dv[1] = v[5]; dv[2] = v[4]; dv[3] = v[0];
  buildSplitElement(r,parent,TET,dv);
  dv[0] = v[1]; dv[1] = v[4]; dv[2] = v[5]; dv[3] = v[2]; dv[4] = v[0];
  pyramidToTets(r,parent,dv);
}

/* tetrahedronizing a prism: the bad case.
   in this case all diagonals go in the "0" direction,
   starting with v[0] <-> v[4].
   The current solution is to add a vertex in the
   center and connect all surface triangles to it.
   This is bad because it is over-refinement and
   creates tets with nearly invalid dihedral angles
   if the prism is flat.
   Use with caution, understand which code calls this function. */
Entity* prismToTetsBadCase(
    Refine* r,
    Entity* parent,
    Entity** v_in,
    int code,
    Vector const& point)
{
  Adapt* a = r->adapt;
  Mesh* m = a->mesh;
  Model* c = m->toModel(parent);
  Entity* v[6];
  rotatePrism(v_in,prism_diag_match[code],v);
  /* by definition, this vertex is inside a region, so
     it does not need geometric parametric coordinates at all... */
  Vector param(0,0,0);
  Entity* cv = buildVertex(a,c,point,param);
  Entity* v2[6];
  for (int i=0; i < 2; ++i)
  { /*tri faces into tets by doing the bottom, flip upside-down,
      do the bottom again (which was the top) */
    rotatePrism(v,i*3,v2);
    Entity* tv[4];
    tv[0] = v2[0]; tv[1] = v2[1]; tv[2] = v2[2]; tv[3] = cv;
    buildSplitElement(r,parent,TET,tv);
  }
  for (int i=0; i < 3; ++i)
  { /*quad faces into pyramids by doing the first quad
      then rotating so the next quad is the first */
    rotatePrism(v,i,v2);
    Entity* pv[5];
    pv[0] = v2[0]; pv[1] = v2[3]; pv[2] = v2[4]; pv[3] = v2[1]; pv[4] = cv;
    pyramidToTets(r,parent,pv);
  }
  return cv;
}

bool checkPrismDiagonalCode(int code)
{
  if (code == 0x0 || code == 0x7)
    return false;
  return true;
}

/* given a split placed at some 0<=p<=1 from v0
   to v1 (v1 is at p=1), returns the tetrahedral
   coordinates xi_1,xi_2,xi_3 of the split point */
Vector getSplitXi(double place, int v0, int v1)
{
/* all 4 tetrahedral parent coordinates */
  double xi[4] = {0,0,0,0};
/* in APF vertex 1 is xi_1, vertex 3 is xi_3, and vertex 0 is xi_4 */
  int coordOf[4] = {3,0,1,2};
  xi[coordOf[v1]] = place;
  xi[coordOf[v0]] = 1-place;
  return Vector(xi[0],xi[1],xi[2]);
}

Vector splitTet_3_4_getCentroidXi(
    Mesh* m,
    Entity* tet,
    Entity** tv,
    double* places,
    Entity**)
{
  Vector xi(0,0,0);
  for (int i=0; i < 3; ++i)
    xi = xi + getSplitXi(places[i],3,i);
  xi = xi + Vector(0,0,0); //vertex 0
  xi = xi + Vector(1,0,0); //vertex 1
  xi = xi + Vector(0,1,0); //vertex 2
  xi = xi/6; //average the 6 xi coordinates of prism vertices
  int rotation = findTetRotation(m,tet,tv);
  unrotateTetXi(xi,rotation);
  return xi;
}

typedef Vector (*GetCentroidFunction)(
    Mesh* m,
    Entity* tet,
    Entity** tv,
    double* places,
    Entity** pv);

/* This functions takes care of the extra work needed to handle
   centroid vertices in the case of prism tetrahedronization,
   primarily computing element-local coordinates based on edge
   split placements and using that for solution transfer */

bool splitTet_prismToTets(
    Refine* r,
    Entity* tet,
    Entity** tv,
    double* places,
    Entity** pv,
    GetCentroidFunction getCentroidXi)
{
  Adapt* a = r->adapt;
  Mesh* m = a->mesh;
  int code = getPrismDiagonalCode(m,pv);
  if (checkPrismDiagonalCode(code))
  {
    prismToTetsGoodCase(r,tet,pv,code);
    return true;
  }
  Vector xi = getCentroidXi(m,tet,tv,places,pv);
  apf::MeshElement* me = apf::createMeshElement(m,tet);
  Vector point;
  apf::mapLocalToGlobal(me,xi,point);
  Entity* vert = prismToTetsBadCase(r,tet,pv,code,point);
  a->sizeField->interpolate(me,xi,vert);
  a->solutionTransfer->onVertex(me,xi,vert);
  apf::destroyMeshElement(me);
  return false;
}

/* three split edges around one vertex.
   a capping tet is removed leaving a
   prism that can have worst-case splitting. */
void splitTet_3_4(Refine* r, Entity* tet, Entity** v)
{
  Entity* sv[3];
  double places[3];
  for (int i=0; i < 3; ++i)
    sv[i] = findPlacedSplitVert(r,v[3],v[i],places[i]);
  Entity* tv[4];
  tv[0] = sv[0]; tv[1] = sv[1]; tv[2] = sv[2]; tv[3] = v[3];
  buildSplitElement(r,tet,TET,tv);
  Entity* pv[6];
  for (int i=0; i < 3; ++i)
  { //vertices defining the prism
    pv[i+3] = sv[i];
    pv[i]   =  v[i];
  }
  splitTet_prismToTets(r,tet,v,places,pv,
      splitTet_3_4_getCentroidXi);
}

/* four split edges, three on one face.
   Splits into two pyramids and two tets. */
void splitTet_4_1(Refine* r, Entity* tet, Entity** v)
{
  Entity* sv[4];
  sv[0] = findSplitVert(r,v[0],v[1]);
  sv[1] = findSplitVert(r,v[1],v[2]);
  sv[2] = findSplitVert(r,v[2],v[0]);
  sv[3] = findSplitVert(r,v[3],v[2]);
  Downward dv;
  for (int i=0; i < 4; ++i) dv[i]=sv[i];
  buildSplitElement(r,tet,TET,dv);
  dv[0] = sv[2]; dv[1] = sv[1]; dv[2] = v[2]; dv[3] = sv[3];
  buildSplitElement(r,tet,TET,dv);
  dv[0] = v[0]; dv[1] = sv[2]; dv[2] = sv[3]; dv[3] = v[3]; dv[4] = sv[0];
  pyramidToTets(r,tet,dv);
  dv[0] = v[1]; dv[1] = v[3]; dv[2] = sv[3]; dv[3] = sv[1]; dv[4] = sv[0];
  pyramidToTets(r,tet,dv);
}

/* given a prism-shaped volume with all diagonals
   created except the one on the first face,
   this returns a bit vector as an integer that
   indicates which diagonal(s) would prevent
   the bad case of tetrahedronization.
   the first bit means v[0] <-> v[4] is ok,
   the second bit means v[1] <-> v[3] is ok.
 */
int getPrismDiagonalChoices(Mesh* m, Entity** v)
{
  int code = getPrismDiagonalCode(m,v);
  code >>= 1;//forget the state of the first face
  return prism_diag_choices[code];
}

/* splits a quad into two tris using the v[0]-v[2] diagonal */
void quadToTris(Refine* r, Entity* parent, Entity** v)
{
  Entity* tv[3];
  tv[0] = v[0]; tv[1] = v[1]; tv[2] = v[2];
  buildSplitElement(r,parent,TRI,tv);
  tv[0] = v[0]; tv[1] = v[2]; tv[2] = v[3];
  buildSplitElement(r,parent,TRI,tv);
}

int quadToTrisGeometric(Refine* r, Entity* parent, Entity** v)
{
  Adapt* a = r->adapt;
  int rotation = 0;
  if (getDistance(a,v[1],v[3])<getDistance(a,v[0],v[2]))
    rotation = 1;
  Entity* v2[4];
  rotateQuad(v,rotation,v2);
  quadToTris(r,parent,v2);
  return rotation;
}

/* given a quad-shaped area, splits into
   triangles based a bit vector of
   good diagonals, falls back to shortest edge.
   The first bit means v[0]-v[2] is ok,
   the second bit means v[1]-v[3] is ok.
   returns 0 if v[0]-v[2] is chosen, 1 otherwise. */
int quadToTrisRestricted(Refine* r, Entity* parent, Entity** v, int good)
{
  //if both diagonals are bad or both are good, choose the shortest edge
  if ((good == 0x0)||(good == 0x3))
    return quadToTrisGeometric(r,parent,v);
  int rotation = 0;
  if (good == 0x2) rotation = 1;
  Entity* v2[4];
  rotateQuad(v,rotation,v2);
  quadToTris(r,parent,v2);
  return rotation;
}

/* knowing how vertex-along-edge placements are retrieved,
   converts placements to element-local tet coordinates,
   picks the right vertex coordinates to average in,
   and comes up with an element-local point for the centroid
   vertex of one of the two prisms */
Vector splitTet_4_2_getCentroidXi(
    Mesh* m,
    Entity* tet,
    Entity** tv,
    double* places,
    Entity** pv)
{
  Vector xi(0,0,0);
/* make sure all this matches what is in splitTet_4_2 */
  xi = xi + getSplitXi(places[0],0,2);
  xi = xi + getSplitXi(places[1],1,2);
  xi = xi + getSplitXi(places[2],1,3);
  xi = xi + getSplitXi(places[3],0,3);
  int whichPrism = (pv[2]==tv[2]) ? (0) : (1);
  if (whichPrism == 0)
  {
    assert(pv[2]==tv[2]);
    assert(pv[5]==tv[3]);
    xi = xi + Vector(0,1,0); //tet vertex 2
    xi = xi + Vector(0,0,1); //tet vertex 3
  }
  else //prism 1
  {
    assert(pv[2]==tv[1]);
    assert(pv[5]==tv[0]);
    xi = xi + Vector(0,0,0); //tet vertex 0
    xi = xi + Vector(1,0,0); //tet vertex 1
  }
  xi = xi / 6;
  int rotation = findTetRotation(m,tet,tv);
  unrotateTetXi(xi,rotation);
  return xi;
}

/* four split edges divide a tet into two prisms,
   with ambiguity in all the faces.
   the quad inside the tet is undetermined, so
   we try to find a good diagonal for it, otherwise
   one of the prisms will invoke the bad case. */
void splitTet_4_2(Refine* r, Entity* tet, Entity** v)
{
  Entity* sv[4];
  double places[4];
  sv[0] = findPlacedSplitVert(r,v[0],v[2],places[0]);
  sv[1] = findPlacedSplitVert(r,v[1],v[2],places[1]);
  sv[2] = findPlacedSplitVert(r,v[1],v[3],places[2]);
  sv[3] = findPlacedSplitVert(r,v[0],v[3],places[3]);
  Entity* p0[6];
  p0[0] = sv[0]; p0[1] = sv[1]; p0[2] = v[2];
  p0[3] = sv[3]; p0[4] = sv[2]; p0[5] = v[3];
  Entity* p1[6];
  p1[0] = sv[2]; p1[1] = sv[1]; p1[2] = v[1];
  p1[3] = sv[3]; p1[4] = sv[0]; p1[5] = v[0];
  Mesh* m = r->adapt->mesh;
  int ok0 = getPrismDiagonalChoices(m,p0);
  int ok1 = getPrismDiagonalChoices(m,p1);
/* we can do this because the edges match from
   the perspectives of both prisms: */
  int ok = ok0 & ok1;
// if no diagonals are ok, someone will lose
  int diag = quadToTrisRestricted(r,tet,sv,ok);
  bool wasOk;
  wasOk = splitTet_prismToTets(r,tet,v,places,p0,
      splitTet_4_2_getCentroidXi);
  assert(wasOk == static_cast<bool>(ok0 & (1<<diag)));
  wasOk = splitTet_prismToTets(r,tet,v,places,p1,
      splitTet_4_2_getCentroidXi);
  assert(wasOk == static_cast<bool>(ok1 & (1<<diag)));
}

/* five edges are split, creating two tets,
   a pyramid, and a prism. the quad between
   the pyramid and prism is undecided; we
   use it to prevent the bad prism case. */
void splitTet_5(Refine* r, Entity* tet, Entity** v)
{
  Entity* py[5]; //pyramid vertices
  py[0] = findSplitVert(r,v[0],v[2]);
  py[1] = findSplitVert(r,v[1],v[2]);
  py[2] = findSplitVert(r,v[1],v[3]);
  py[3] = findSplitVert(r,v[0],v[3]);
  py[4] = findSplitVert(r,v[0],v[1]);
  Entity** q = py; //quad vertices
  Entity* pr[6]; //prism vertices
  pr[3] = q[3]; pr[4] = q[2]; pr[5] = v[3];
  pr[0] = q[0]; pr[1] = q[1]; pr[2] = v[2];
  Mesh* m = r->adapt->mesh;
  int ok = getPrismDiagonalChoices(m,pr);
  quadToTrisRestricted(r,tet,q,ok);
  pyramidToTets(r,tet,py);
  int code = getPrismDiagonalCode(m,pr);
  assert(checkPrismDiagonalCode(code));
  prismToTetsGoodCase(r,tet,pr,code);
  Entity* t[4]; //tet vertices
  t[0] = v[0]; t[1] = py[4]; t[2] = py[0]; t[3] = py[3];
  buildSplitElement(r,tet,TET,t);
  t[0] = v[1]; t[1] = py[1]; t[2] = py[4]; t[3] = py[2];
  buildSplitElement(r,tet,TET,t);
}

/* tetrahedronizes an octahedron, using an edge
   across the 0--5 diagonal */
void octToTets(Refine* r, Entity* parent, Entity** v)
{
  for (int i=0; i < 4; ++i)
  { //create each of the 4 tets through rotation
    Entity* v2[6];
    rotateOct(v,i,v2);
    Entity* tv[4];
    tv[0] = v2[0]; tv[1] = v2[1]; tv[2] = v2[2]; tv[3] = v2[5];
    buildSplitElement(r,parent,TET,tv);
  }
}

/* tetrahedronizes an octahedron, using an edge
   across the shortest diagonal. */
void octToTetsGeometric(Refine* r, Entity* parent, Entity** v)
{
  Entity* pairs[3][2];
  pairs[0][0] = v[0]; pairs[0][1] = v[5];
  pairs[1][0] = v[1]; pairs[1][1] = v[3];
  pairs[2][0] = v[2]; pairs[2][1] = v[4];
  int n = getClosestPair(r->adapt,pairs,3);
  Entity* v2[6];
  rotateOct(v,n*4,v2);
  assert(v2[0]==v[n]);
  octToTets(r,parent,v2);
}

/* uniform refinement of a tetrahedron. */
void splitTet_6(Refine* r, Entity* tet, Entity** v)
{
  /* the indexing of the octahedron and the tetrahedron
     line up such that the numbering of vertices in the
     dual octahedron of a tetrahedron matches the edge
     numbering of the tetrahedron.
     As such, we have a convenient way of generating
     the octahedron using the tet edge vertex index table. */
  Entity* ov[6];
  for (int i=0; i < 6; ++i)
  {
    int const* evi = apf::tet_edge_verts[i];
    ov[i] = findSplitVert(r,v[evi[0]],v[evi[1]]);
  }
  octToTetsGeometric(r,tet,ov);
  /* now we generate the corner tets by repeated rotation. */
  for (int i=0; i < 4; ++i)
  {
    Entity* v2[4];
    rotateTet(v,i*3,v2);
    assert(v2[0]==v[i]);
    Entity* tv[4];
    tv[0] = v2[0];
    for (int j=1; j < 4; ++j)
      tv[j] = findSplitVert(r,v2[0],v2[j]);
    buildSplitElement(r,tet,TET,tv);
  }
}

SplitFunction tet_templates[tet_edge_code_count] =
{0
,splitTet_1    // 1
,splitTet_2_1  // 2
,splitTet_2_2  // 3
,splitTet_3_1  // 4
,splitTet_3_2  // 5
,splitTet_3_3  // 6
,splitTet_3_4  // 7
,splitTet_4_1  // 8
,splitTet_4_2  // 9
,splitTet_5    //10
,splitTet_6    //11
};

/* this is the template used on quads during tetrahedronization.
   the choice of diagonal has been made by the algorithms in
   maLayer.cc, we just retrieve it and split the quad */
void splitQuad_0(Refine* r, Entity* q, Entity** v)
{
  Adapt* a = r->adapt;
  int rotation = getDiagonalFromFlag(a,q);
  if (rotation == -1)
  {
    fprintf(stderr,"(splitQuad) quad on model dim %d has no flag\n",
        a->mesh->getModelType(a->mesh->toModel(q)));
    abort();
  }
  Entity* v2[4];
  rotateQuad(v,rotation,v2);
  quadToTris(r,q,v2);
}

/* splits the quad in half along edges v[0]-v[1] and v[2]-v[3] */
void splitQuad_2(Refine* r, Entity* q, Entity** v)
{
  Entity* sv[2];
  sv[0] = findSplitVert(r,v[0],v[1]);
  sv[1] = findSplitVert(r,v[2],v[3]);
  Entity* qv[4];
  qv[0] = v[0]; qv[1] = sv[0]; qv[2] = sv[1]; qv[3] = v[3];
  buildSplitElement(r,q,QUAD,qv);
  qv[0] = sv[0]; qv[1] = v[1]; qv[2] = v[2]; qv[3] = sv[1];
  buildSplitElement(r,q,QUAD,qv);
}

/* splits the quad into 4 along all edges */
void splitQuad_4(Refine* r, Entity* q, Entity** v)
{
  Adapt* a = r->adapt;
  Mesh* m = a->mesh;
  Entity* sv[4];
  double places[4];
  sv[0] = findPlacedSplitVert(r,v[0],v[1],places[0]);
  sv[1] = findPlacedSplitVert(r,v[1],v[2],places[1]);
  sv[2] = findPlacedSplitVert(r,v[3],v[2],places[2]);
  sv[3] = findPlacedSplitVert(r,v[0],v[3],places[3]);
  double x = (places[0] + places[2])/2;
  double y = (places[1] + places[3])/2;
  Vector xi(x*2-1,y*2-1,0);
/* since no rotation should have been applied, we actually don't
   need to unrotate xi */
  apf::MeshElement* me = apf::createMeshElement(m,q);
  Vector point;
  apf::mapLocalToGlobal(me,xi,point);
/* TODO: in truth, we should be transferring parametric
   coordinates here. since we are a ways away from boundary
   layer snapping and the transfer logic is non-trivial,
   this is left alone for now */
  Entity* cv = buildVertex(a,m->toModel(q),point,Vector(0,0,0));
  a->sizeField->interpolate(me,xi,cv);
  a->solutionTransfer->onVertex(me,xi,cv);
  apf::destroyMeshElement(me);
  for (int i=0; i < 4; ++i)
  {
    Entity* v2[4];
    Entity* sv2[4];
    rotateQuad(v,i,v2);
    rotateQuad(sv,i,sv2);
    Entity* qv[4];
    qv[0] = v2[0]; qv[1] = sv2[0]; qv[2] = cv; qv[3] = sv2[3];
    buildSplitElement(r,q,QUAD,qv);
  }
}

SplitFunction quad_templates[quad_edge_code_count] = 
{splitQuad_0,
 splitQuad_2,
 splitQuad_4
};

/* this is the template used on prisms during tetrahedronization.
   the algorithms in maLayer.cc are required to ensure no centroids,
   so we check that and use the good case template */
void splitPrism_0(Refine* r, Entity* p, Entity** v)
{
  Adapt* a = r->adapt;
  Mesh* m = a->mesh;
  int code = getPrismDiagonalCode(m,v);
  assert(checkPrismDiagonalCode(code));
  prismToTetsGoodCase(r,p,v,code);
}

/* split the prism into two prisms separated by a quad face.
   edges v[0]-v[1] and v[3]-v[4] are split */
void splitPrism_2(Refine* r, Entity* p, Entity** v)
{
  Entity* sv[2];
  sv[0] = findSplitVert(r,v[0],v[1]);
  sv[1] = findSplitVert(r,v[3],v[4]);
  Entity* pv[6];
  pv[3] = v[3]; pv[4] = sv[1]; pv[5] = v[5];
  pv[0] = v[0]; pv[1] = sv[0]; pv[2] = v[2];
  buildSplitElement(r,p,PRISM,pv);
  pv[3] = sv[1]; pv[4] = v[4]; pv[5] = v[5];
  pv[0] = sv[0]; pv[1] = v[1]; pv[2] = v[2];
  buildSplitElement(r,p,PRISM,pv);
}

/* split the prism into four when all 6 triangular
   face edges have been split.
   This has to be told the split vertices since
   it is called from splitPrism_9 which has to deal
   with quad-associated vertices
 */
void splitPrism_6(Refine* r, Entity* p, Entity** v, Entity** sv)
{
/* make center prism */
  buildSplitElement(r,p,PRISM,sv);
/* make the three corner prisms through rotation */
  for (int i=0; i < 3; ++i)
  {
    Entity* v2[6];
    Entity* sv2[6];
    rotatePrism(v,i,v2);
    rotatePrism(sv,i,sv2);
    Entity* pv[6];
    pv[3] = sv2[3]; pv[4] = v2[4]; pv[5] = sv2[4];
    pv[0] = sv2[0]; pv[1] = v2[1]; pv[2] = sv2[1];
    buildSplitElement(r,p,PRISM,pv);
  }
}

/* uniform refinement of a prism: start by dividing
   the prism across the middle, splitting the vertical
   edges. give the resulting sub-problems to 
   splitPrism_6 */
void splitPrism_9(Refine* r, Entity* p, Entity** v)
{
  Adapt* a = r->adapt;
  Mesh* m = a->mesh;
  Entity* botv[3];
  botv[0] = findSplitVert(r,v[0],v[1]);
  botv[1] = findSplitVert(r,v[1],v[2]);
  botv[2] = findSplitVert(r,v[2],v[0]);
  Entity* midv[3];
  midv[0] = findSplitVert(r,v[0],v[3]);
  midv[1] = findSplitVert(r,v[1],v[4]);
  midv[2] = findSplitVert(r,v[2],v[5]);
  Entity* topv[3];
  topv[0] = findSplitVert(r,v[3],v[4]);
  topv[1] = findSplitVert(r,v[4],v[5]);
  topv[2] = findSplitVert(r,v[5],v[3]);
  Entity* quads[3];
  Entity* qv[4];
  qv[3] = v[3]; qv[2] = v[4];
  qv[0] = v[0]; qv[1] = v[1];
  quads[0] = apf::findElement(m,QUAD,qv);
  qv[3] = v[4]; qv[2] = v[5];
  qv[0] = v[1]; qv[1] = v[2];
  quads[1] = apf::findElement(m,QUAD,qv);
  qv[3] = v[5]; qv[2] = v[3];
  qv[0] = v[2]; qv[1] = v[0];
  quads[2] = apf::findElement(m,QUAD,qv);
  Entity* cenv[3];
  cenv[0] = findSplitVert(r,quads[0]);
  cenv[1] = findSplitVert(r,quads[1]);
  cenv[2] = findSplitVert(r,quads[2]);
  Entity* pv[6];
  Entity* sv[6];
  pv[3] =    v[3]; pv[4] =    v[4]; pv[5] =    v[5];
  pv[0] = midv[0]; pv[1] = midv[1]; pv[2] = midv[2];
  sv[3] = topv[0]; sv[4] = topv[1]; sv[5] = topv[2];
  sv[0] = cenv[0]; sv[1] = cenv[1]; sv[2] = cenv[2];
  splitPrism_6(r,p,pv,sv);
  pv[3] = midv[0]; pv[4] = midv[1]; pv[5] = midv[2];
  pv[0] =    v[0]; pv[1] =    v[1]; pv[2] =    v[2];
  sv[3] = cenv[0]; sv[4] = cenv[1]; sv[5] = cenv[2];
  sv[0] = botv[0]; sv[1] = botv[1]; sv[2] = botv[2];
  splitPrism_6(r,p,pv,sv);
}

SplitFunction prism_templates[prism_edge_code_count] = 
{splitPrism_0
,splitPrism_2
,splitPrism_9
};

/* split the pyramid into two new ones when edges
   v[0]-v[1] and v[2]-v[3] have been split */
void splitPyramid_2(Refine* r, Entity* p, Entity** v)
{
  Entity* sv[2];
  sv[0] = findSplitVert(r,v[0],v[1]);
  sv[1] = findSplitVert(r,v[2],v[3]);
  Entity* pv[5];
  pv[0] = v[0]; pv[1] = sv[0];
  pv[3] = v[3]; pv[2] = sv[1];
  pv[4] = v[4];
  buildSplitElement(r,p,PYRAMID,pv);
  pv[0] = sv[0]; pv[1] = v[1];
  pv[3] = sv[1]; pv[2] = v[2];
  pv[4] = v[4];
  buildSplitElement(r,p,PYRAMID,pv);
}

/* uniform refinement of a pyramid */
void splitPyramid_4(Refine* r, Entity* p, Entity** v)
{
  Mesh* m = r->adapt->mesh;
  Entity* botv[4];
  botv[0] = findSplitVert(r,v[0],v[1]);
  botv[1] = findSplitVert(r,v[1],v[2]);
  botv[2] = findSplitVert(r,v[2],v[3]);
  botv[3] = findSplitVert(r,v[3],v[0]);
  Entity* midv[4];
  midv[0] = findSplitVert(r,v[0],v[4]);
  midv[1] = findSplitVert(r,v[1],v[4]);
  midv[2] = findSplitVert(r,v[2],v[4]);
  midv[3] = findSplitVert(r,v[3],v[4]);
/* the first four entries of v also specify the bottom quad */
  Entity* quad = apf::findElement(m,QUAD,v);
  Entity* cv = findSplitVert(r,quad);
/* make the four new pyramids and four new tets by rotation */
  for (int i=0; i < 4; ++i)
  {
    Entity* midv2[4];
    rotateQuad(midv,i,midv2);
    Entity* botv2[4];
    rotateQuad(botv,i,botv2);
    Entity* v2[4];
    rotateQuad(v,i,v2);
    Entity* pv[5];
    pv[3] = cv;       pv[2] = botv2[1];
    pv[0] = botv2[0]; pv[1] = v2[1];
    pv[4] = midv2[1];
    buildSplitElement(r,p,PYRAMID,pv);
    Entity* tv[4];
    tv[0] = midv2[0]; tv[1] = midv2[1];
    tv[2] = botv2[0]; tv[3] = cv;
    buildSplitElement(r,p,TET,tv);
  }
/* tetrahedronize the central octahedron by choosing the
   best metric space diagonal */
  Entity* octv[6];
  octv[5] = v[4];
  octv[4] = midv[3]; octv[3] = midv[2];
  octv[1] = midv[0]; octv[2] = midv[1];
  octv[0] = cv;
  octToTetsGeometric(r,p,octv);
}

SplitFunction pyramid_templates[pyramid_edge_code_count] = 
{pyramidToTets
,splitPyramid_2
,splitPyramid_4
};

}