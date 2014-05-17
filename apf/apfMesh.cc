/*
 * Copyright 2011 Scientific Computation Research Center
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "apfCoordData.h"
#include "apfVectorField.h"
#include "apfShape.h"
#include "apfNumbering.h"
#include <PCU.h>

namespace apf {

/* note: these tables must be consistent
   with those of the mesh database, and
   in the best design should be read from
   the database.
   The goal of supporting multiple databases
   makes this difficult for now */

int const tri_edge_verts[3][2] =
{{0,1},{1,2},{2,0}};

int const quad_edge_verts[4][2] =
{{0,1},{1,2},{2,3},{3,0}};

/*
   Canonical vertex and edge numbering
   for a tetrahedron, the middle (3) vertex
   is higher out of the page than the 0-1-2 triangle.
             2
            /|\
           / | \
          /  |  \
         /   |   \
        /    |5   \
      2/     |     \1
      /     _|_     \
     /   __/ 3 \__   \
    / _3/        4\__ \
   /_/               \_\
  0---------------------1
             0
*/

int const tet_edge_verts[6][2] =
{{0,1}
,{1,2}
,{2,0}
,{0,3}
,{1,3}
,{2,3}};

int const tet_tri_verts[4][3] =
{{0,1,2}
,{0,1,3}
,{1,2,3}
,{0,2,3}};

int const prism_tri_verts[2][3] =
{{0,1,2}
,{3,4,5}};

int const prism_quad_verts[3][4] =
{{0,1,4,3}
,{1,2,5,4}
,{2,0,3,5}};

int const pyramid_tri_verts[4][3] =
{{0,1,4}
,{1,2,4}
,{2,3,4}
,{3,0,4}};

/* the following tables should be the same as
 * those found in the mesh databases, with the possible
 * exception of the behavior at the same dimension
 */

int const Mesh::adjacentCount[TYPES][4] = 
{{1,-1,-1,-1} //vertex
,{2,1,-1,-1} //edge
,{3,3,1,-1}//tri
,{4,4,1,-1}//quad
,{4,6,4,1}//tet
,{8,12,6,1}//hex
,{6,9,5,1}//prism
,{5,8,5,1}//pyramid
};

int const Mesh::typeDimension[TYPES] = 
{ 0, //vertex
  1, //edge
  2, //tri
  2, //quad
  3, //tet
  3, //hex
  3, //prism
  3 //pyramid
};

void Mesh::init(FieldShape* s)
{
  coordinateField = new VectorField();
  FieldBase* baseP = coordinateField;
  CoordData* data = new CoordData();
  baseP->init("coordinates",this,s,data);
  data->init(baseP);
  hasFrozenFields = false;
}

Mesh::~Mesh()
{
  delete coordinateField;
}

int Mesh::getEntityDimension(int type)
{
  return Mesh::typeDimension[type];
}

void Mesh::getPoint(MeshEntity* e, int node, Vector3& p)
{
  getVector(coordinateField,e,node,p);
}

FieldShape* Mesh::getShape() const
{
  return coordinateField->getShape();
}

void Mesh::changeCoordinateField(Field* f)
{
  delete coordinateField;
  coordinateField = f;
}

void Mesh::addField(Field* f)
{
  fields.push_back(f);
}

void Mesh::removeField(Field* f)
{
  fields.erase(std::find(fields.begin(),fields.end(),f));
}

Field* Mesh::findField(const char* name)
{
  std::string n(name);
  for (size_t i=0; i < fields.size(); ++i)
    if (n==getName(fields[i]))
      return fields[i];
  return 0;
}

int Mesh::countFields()
{
  return static_cast<int>(fields.size());
}

Field* Mesh::getField(int i)
{
  return fields[i];
}

void Mesh::addNumbering(Numbering* n)
{
  numberings.push_back(n);
}

void Mesh::removeNumbering(Numbering* n)
{
  numberings.erase(std::find(numberings.begin(),numberings.end(),n));
}

Numbering* Mesh::findNumbering(const char* name)
{
  std::string n(name);
  for (size_t i=0; i < numberings.size(); ++i)
    if (n==getName(numberings[i]))
      return numberings[i];
  return 0;
}

int Mesh::countNumberings()
{
  return static_cast<int>(numberings.size());
}

Numbering* Mesh::getNumbering(int i)
{
  return numberings[i];
}

void unite(Parts& into, Parts const& from)
{
  into.insert(from.begin(),from.end());
}

void getFacePeers(Mesh* m, Parts& peers)
{
  MeshEntity* face;
  MeshIterator* faces = m->begin(2);
  while ((face = m->iterate(faces)))
  {
    Parts residence;
    m->getResidence(face,residence);
    unite(peers,residence);
  }
  m->end(faces);
  peers.erase(m->getId());
}

static bool residesOn(Mesh* m, MeshEntity* e, int part)
{
  Parts residence;
  m->getResidence(e,residence);
  return residence.count(part);
}

MeshEntity* iterateBoundary(Mesh* m, MeshIterator* it, int part)
{
  MeshEntity* e;
  while ((e = m->iterate(it))&&( ! residesOn(m,e,part)));
  return e;
}

Migration::Migration(Mesh* m)
{
  mesh = m;
  tag = m->createIntTag("apf_migrate",1);
}

Migration::~Migration()
{
  for (size_t i=0; i < elements.size(); ++i)
    mesh->removeTag(elements[i],tag);
  mesh->destroyTag(tag);
}

int Migration::count()
{
  return elements.size();
}

MeshEntity* Migration::get(int i)
{
  return elements[i];
}

bool Migration::has(MeshEntity* e)
{
  return mesh->hasTag(e,tag);
}

void Migration::send(MeshEntity* e, int to)
{
  if (!has(e))
    elements.push_back(e);
  mesh->setIntTag(e,tag,&to);
}

int Migration::sending(MeshEntity* e)
{
  int to;
  mesh->getIntTag(e,tag,&to);
  return to;
}

void removeTagFromDimension(Mesh* m, MeshTag* tag, int d)
{
  MeshIterator* it = m->begin(d);
  MeshEntity* e;
  while ((e = m->iterate(it)))
    if (m->hasTag(e,tag))
      m->removeTag(e,tag);
  m->end(it);
}

int findIn(MeshEntity** a, int n, MeshEntity* e)
{
  for (int i=0; i < n; ++i)
    if (a[i]==e)
      return i;
  return -1;
}

/* returns true if the arrays have the same entities,
   regardless of ordering */
static bool sameContent(int n, MeshEntity** a, MeshEntity** b)
{
  for (int i=0; i < n; ++i)
    if (findIn(b,n,a[i])<0)
      return false;
  return true;
}

MeshEntity* findUpward(Mesh* m, int type, MeshEntity** down)
{
  if ( ! down[0]) return 0;
  Up ups;
  m->getUp(down[0],ups);
  int d = Mesh::typeDimension[type];
  int nd = Mesh::adjacentCount[type][d-1];
  Downward down2;
  for (int i=0; i < ups.n; ++i)
  {
    MeshEntity* up = ups.e[i];
    if (m->getType(up)!=type) continue;
    m->getDownward(up,d-1,down2);
    if (sameContent(nd,down,down2))
      return up;
  }
  return 0;
}

static void runEdgeDown(
    ElementVertOp*,
    MeshEntity** verts,
    MeshEntity** down)
{
  down[0] = verts[0];
  down[1] = verts[1];
}

static void runTriDown(
    ElementVertOp* o,
    MeshEntity** verts,
    MeshEntity** down)
{
  Downward ev;
  for (int i=0; i < 3; ++i)
  {
    ev[0] = verts[tri_edge_verts[i][0]]; ev[1] = verts[tri_edge_verts[i][1]];
    down[i] = o->run(Mesh::EDGE,ev);
  }
}

static void runQuadDown(
    ElementVertOp* o,
    MeshEntity** verts,
    MeshEntity** down)
{
  Downward ev;
  for (int i=0; i < 4; ++i)
  {
    ev[0] = verts[quad_edge_verts[i][0]]; ev[1] = verts[quad_edge_verts[i][1]];
    down[i] = o->run(Mesh::EDGE,ev);
  }
}

static void runTetDown(
    ElementVertOp* o,
    MeshEntity** verts,
    MeshEntity** down)
{
  Downward fv;
  for (int i=0; i < 4; ++i)
  {
    for (int j=0; j < 3; ++j)
      fv[j] = verts[tet_tri_verts[i][j]];
    down[i] = o->run(Mesh::TRIANGLE,fv);
  }
}

static void runPrismDown(
    ElementVertOp* o,
    MeshEntity** verts,
    MeshEntity** down)
{
  Downward fv;
  for (int j=0; j < 3; ++j)
    fv[j] = verts[prism_tri_verts[0][j]];
  down[0] = o->run(Mesh::TRIANGLE,fv);
  for (int i=0; i < 3; ++i)
  {
    for (int j=0; j < 4; ++j)
      fv[j] = verts[prism_quad_verts[i][j]];
    down[i+1] = o->run(Mesh::QUAD,fv);
  }
  for (int j=0; j < 3; ++j)
    fv[j] = verts[prism_tri_verts[1][j]];
  down[4] = o->run(Mesh::TRIANGLE,fv);
}

static void runPyramidDown(
    ElementVertOp* o,
    MeshEntity** verts,
    MeshEntity** down)
{
/* first 4 verts are in order for the quad */
  down[0] = o->run(Mesh::QUAD,verts);
  for (int i=0; i < 4; ++i)
  {
    MeshEntity* tv[3];
    for (int j=0; j < 3; ++j)
      tv[j] = verts[pyramid_tri_verts[i][j]];
    down[i+1] = o->run(Mesh::TRIANGLE,tv);
  }
}

void ElementVertOp::runDown(
    int type,
    MeshEntity** verts,
    MeshEntity** down)
{
  typedef void (*RunDownFunction)(
      ElementVertOp* o,
      MeshEntity** verts,
      MeshEntity** down);
  static RunDownFunction table[Mesh::TYPES] =
  {0,//vertex
   runEdgeDown,
   runTriDown,
   runQuadDown,
   runTetDown,
   0,//hex
   runPrismDown,
   runPyramidDown
  };
  RunDownFunction runDownFunction = table[type];
  runDownFunction(this,verts,down);
}

MeshEntity* ElementVertOp::run(int type, MeshEntity** verts)
{
  Downward down;
  this->runDown(type,verts,down);
  return this->apply(type,down);
}

class ElementFinder : public ElementVertOp
{
  public:
    ElementFinder(Mesh* m)
    {
      mesh = m;
    }
    virtual MeshEntity* apply(int type, MeshEntity** down)
    {
      return findUpward(mesh,type,down);
    }
  private:
    Mesh* mesh;
};

void findTriDown(
    Mesh* m,
    MeshEntity** verts,
    MeshEntity** down)
{
  ElementFinder f(m);
  return f.runDown(Mesh::TRIANGLE,verts,down);
}

MeshEntity* findElement(
    Mesh* m,
    int type,
    MeshEntity** verts)
{
  ElementFinder f(m);
  return f.run(type,verts);
}

MeshEntity* getEdgeVertOppositeVert(Mesh* m, MeshEntity* edge, MeshEntity* v)
{
  MeshEntity* ev[2];
  m->getDownward(edge,0,ev);
  if (ev[0]==v)
    return ev[1];
  return ev[0];
}

int countEntitiesOfType(Mesh* m, int type)
{
  MeshIterator* it = m->begin(Mesh::typeDimension[type]);
  MeshEntity* e;
  int count = 0;
  while ((e = m->iterate(it)))
    if (m->getType(e)==type)
      ++count;
  m->end(it);
  return count;
}

void changeMeshShape(Mesh* m, FieldShape* newShape, bool project)
{
  Field* oldCoordinateField = m->getCoordinateField();
  VectorField* newCoordinateField = new VectorField();
  newCoordinateField->init(oldCoordinateField->getName(),m,newShape);
  if (project)
    newCoordinateField->project(oldCoordinateField);
  m->changeCoordinateField(newCoordinateField);
}

void unfreezeFields(Mesh* m) {
  Field* f;
  for (int i=0; i<m->countFields(); i++) {
    f = m->getField(i);
    if (isFrozen(f)) {
      unfreeze(f);
    }
  }
  m->hasFrozenFields = false;
}

std::pair<int,MeshEntity*> getOtherCopy(Mesh* m, MeshEntity* s)
{
  Copies remotes;
  m->getRemotes(s,remotes);
  assert(remotes.size()==1);
  return *(remotes.begin());
}

bool hasCopies(Mesh* m, MeshEntity* e)
{
  if (!m->hasMatching())
    return m->isShared(e);
  Matches ms;
  m->getMatches(e,ms);
  return ms.getSize();
}

bool isOriginal(Mesh* m, MeshEntity* e)
{
  if (!m->hasMatching())
    return m->isOwned(e);
  Matches ms;
  m->getMatches(e,ms);
  int self = m->getId();
  for (size_t i = 0; i < ms.getSize(); ++i)
  {
    if (ms[i].peer < self)
      return false;
    if ((ms[i].peer == self) &&
        (ms[i].entity < e))
      return false;
  }
  return true;
}

int getDimension(Mesh* m, MeshEntity* e)
{
  return Mesh::typeDimension[m->getType(e)];
}

bool isSimplex(int type)
{
  bool const table[Mesh::TYPES] =
  {true,//VERT
   true,//EDGE
   true,//TRI
   false,//QUAD
   true,//TET
   false,//HEX
   false,//PRISM
   false,//PYRAMID
  };
  return table[type];
}

} //namespace apf