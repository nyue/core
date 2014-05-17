/****************************************************************************** 

  Copyright 2014 Scientific Computation Research Center, 
      Rensselaer Polytechnic Institute. All rights reserved.
  
  This work is open source software, licensed under the terms of the
  BSD license as described in the LICENSE file in the top-level directory.

*******************************************************************************/

#include "mds_apf.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <PCU.h>

struct queue {
  mds_id* e;
  mds_id end;
  mds_id first;
};

static void make_queue(struct queue* q, mds_id n)
{
  q->e = malloc(n * sizeof(mds_id));
  q->end = 0;
  q->first = 0;
}

static void free_queue(struct queue* q)
{
  free(q->e);
}

static void push_queue(struct queue* q, mds_id e)
{
  q->e[q->end] = e;
  ++(q->end);
}

static mds_id pop_queue(struct queue* q)
{
  mds_id e = q->e[q->first];
  ++(q->first);
  return e;
}

static int queue_empty(struct queue* q)
{
  return q->first == q->end;
}

static mds_id other_vert(struct mds* m, mds_id e, mds_id v)
{
  struct mds_set vs;
  mds_get_adjacent(m,e,0,&vs);
  if (vs.e[0] == v)
    return vs.e[1];
  return vs.e[0];
}

static int visit(
    struct mds* m,
    struct mds_tag* tag,
    mds_id label[MDS_TYPES],
    mds_id e)
{
  mds_id* l;
  int t;
  if (mds_has_tag(tag,e))
    return 0;
  mds_give_tag(tag,m,e);
  l = mds_get_tag(tag,e);
  t = mds_type(e);
  *l = label[t];
  --(label[t]);
  return 1;
}

static mds_id find_seed(struct mds_apf* m)
{
  int best_dim = 4;
  mds_id best_v = MDS_NONE;
  int dim;
  mds_id v;
  for (v = mds_begin(&m->mds, 0);
       v != MDS_NONE;
       v = mds_next(&m->mds, v)) {
    dim = mds_model_dim(m, mds_apf_model(m, v));
    if (dim < best_dim) {
      best_dim = dim;
      best_v = v;
    }
  }
  return best_v;
}

struct mds_tag* mds_number(struct mds_apf* m)
{
  struct queue q;
  struct mds_tag* tag;
  mds_id label[MDS_TYPES];
  struct mds_set adj[4];
  mds_id v;
  int i,j;
  make_queue(&q,m->mds.n[MDS_VERTEX]);
  tag = mds_create_tag(&m->tags, &m->mds, "mds_number", sizeof(mds_id), 1);
  for (i = 0; i < MDS_TYPES; ++i)
    label[i] = m->mds.n[i] - 1;
  v = find_seed(m);
  visit(&m->mds, tag, label, v);
  push_queue(&q, v);
  while ( ! queue_empty(&q)) {
    v = pop_queue(&q);
    for (i = 1; i <= m->mds.d; ++i)
      mds_get_adjacent(&(m->mds),v,i,adj + i);
    adj[0].n = adj[1].n;
    for (i = 0; i < adj[1].n; ++i)
      adj[0].e[i] = other_vert(&(m->mds),adj[1].e[i],v);
    for (i = 0; i < adj[0].n; ++i)
      if (visit(&(m->mds),tag,label,adj[0].e[i]))
        push_queue(&q,adj[0].e[i]);
    for (i = 1; i <= m->mds.d; ++i)
      for (j = 0; j < adj[i].n; ++j)
        visit(&(m->mds),tag,label,adj[i].e[j]);
  }
  for (i = 0; i < MDS_TYPES; ++i)
    assert(label[i] == -1);
  free_queue(&q);
  return tag;
}

static mds_id lookup(struct mds_tag* tag, mds_id old)
{
  mds_id* ip;
  ip = mds_get_tag(tag,old);
  return mds_identify(mds_type(old),*ip);
}

static void update_remotes(struct mds_apf* m, struct mds_tag* new_of)
{
  int d;
  mds_id e;
  mds_id ne;
  struct mds_copies* cs;
  int i;
  int from;
  PCU_Comm_Begin();
  for (d = 0; d <= m->mds.d; ++d)
    for (e = mds_begin(&m->mds, d);
         e != MDS_NONE;
         e = mds_next(&m->mds, e)) {
      cs = mds_get_copies(&m->remotes, e);
      if (!cs)
        continue;
      ne = lookup(new_of, e);
      for (i = 0; i < cs->n; ++i) {
        PCU_COMM_PACK(cs->c[i].p, cs->c[i].e);
        PCU_COMM_PACK(cs->c[i].p, ne);
      }
    }
  PCU_Comm_Send();
  while (PCU_Comm_Listen()) {
    from = PCU_Comm_Sender();
    while (!PCU_Comm_Unpacked()) {
      PCU_COMM_UNPACK(e);
      PCU_COMM_UNPACK(ne);
      cs = mds_get_copies(&m->remotes, e);
      for (i = 0; i < cs->n; ++i)
        if (cs->c[i].p == from)
          cs->c[i].e = ne;
    }
  }
}

static struct mds_tag* invert(
    struct mds* m,
    struct mds_apf* m2,
    struct mds_tag* new_of)
{
  struct mds_tag* old_of;
  int d;
  mds_id e;
  mds_id ne;
  mds_id* ip;
  old_of = mds_create_tag(&(m2->tags),&(m2->mds),
      "mds_inverse",sizeof(mds_id),1);
  for (d = 0; d <= m->d; ++d) {
    for (e = mds_begin(m,d);
         e != MDS_NONE;
         e = mds_next(m,e)) {
      ne = lookup(new_of,e);
      mds_give_tag(old_of,&(m2->mds),ne);
      ip = mds_get_tag(old_of,ne);
      *ip = mds_index(e);
    }
  }
  return old_of;
}

static void rebuild_verts(
    struct mds_apf* m,
    struct mds_apf* m2,
    struct mds_tag* old_of)
{
  mds_id i;
  mds_id e;
  mds_id ne;
  void* model;
  int j;
  double* p;
  double* q;
  for (i = 0; i < m->mds.n[MDS_VERTEX]; ++i) {
    ne = mds_identify(MDS_VERTEX,i);
    assert(mds_has_tag(old_of,ne));
    e = lookup(old_of,ne);
    model = mds_apf_model(m,e);
    ne = mds_apf_create_entity(m2,MDS_VERTEX,model,0);
    assert(ne == mds_identify(MDS_VERTEX,i));
    p = mds_apf_point(m,e);
    q = mds_apf_point(m2,ne);
    for (j = 0; j < 3; ++j)
      q[j] = p[j];
    p = mds_apf_param(m,e);
    q = mds_apf_param(m2,ne);
    for (j = 0; j < 2; ++j)
      q[j] = p[j];
  }
  assert(m2->mds.n[MDS_VERTEX] == m->mds.n[MDS_VERTEX]);
}

static void rebuild_ents(
    struct mds_apf* m,
    struct mds_apf* m2,
    struct mds_tag* old_of,
    struct mds_tag* new_of)
{
  int t;
  mds_id i;
  mds_id e;
  mds_id ne;
  void* model;
  int j;
  struct mds_set old_down;
  struct mds_set new_down;
  for (t = 1; t < MDS_TYPES; ++t) {
    for (i = 0; i < m->mds.n[t]; ++i) {
      ne = mds_identify(t,i);
      e = lookup(old_of,ne);
      model = mds_apf_model(m,e);
      mds_get_adjacent(&(m->mds),e,mds_dim[mds_type(e)]-1,&old_down);
      for (j = 0; j < old_down.n; ++j)
        new_down.e[j] = lookup(new_of,old_down.e[j]);
      ne = mds_apf_create_entity(m2,t,model,new_down.e);
      assert(ne == mds_identify(t,i));
    }
    assert(m->mds.n[t] == m2->mds.n[t]);
  }
}

static void rebuild_tags(
    struct mds_apf* m,
    struct mds_apf* m2,
    struct mds_tag* old_of,
    struct mds_tag* new_of)
{
  struct mds_tag* t;
  struct mds_tag* nt;
  int d;
  mds_id e;
  mds_id ne;
  void* p;
  void* q;
  for (t = m->tags.first; t; t = t->next) {
    if (t == new_of)
      continue;
    nt = mds_create_tag(&(m2->tags),&(m2->mds),
        t->name,t->bytes,t->user_type);
    for (d = 0; d <= m2->mds.d; ++d) {
      for (ne = mds_begin(&(m2->mds),d);
           ne != MDS_NONE;
           ne = mds_next(&(m2->mds),ne)) {
        e = lookup(old_of,ne);
        if ( ! mds_has_tag(t,e))
          continue;
        p = mds_get_tag(t,e);
        mds_give_tag(nt,&(m2->mds),ne);
        q = mds_get_tag(nt,ne);
        memcpy(q,p,t->bytes);
      }
    }
  }
}

static void rebuild_coords(
    struct mds_apf* m,
    struct mds_apf* m2,
    struct mds_tag* old_of,
    struct mds_tag* new_of)
{
  mds_id ne;
  mds_id e;
  for (ne = mds_begin(&m2->mds, 0);
       ne != MDS_NONE;
       ne = mds_next(&m2->mds, ne)) {
    e = lookup(old_of, ne);
    memcpy(mds_apf_point(m2, ne),
           mds_apf_point(m, e),
           3 * sizeof(double));
    memcpy(mds_apf_param(m2, ne),
           mds_apf_param(m, e),
           2 * sizeof(double));
  }
}

static void rebuild_parts(
    struct mds_apf* m,
    struct mds_apf* m2,
    struct mds_tag* old_of,
    struct mds_tag* new_of)
{
  int d;
  mds_id ne;
  mds_id e;
  for (d = 0; d < m->mds.d; ++d)
    for (ne = mds_begin(&m2->mds, d);
         ne != MDS_NONE;
         ne = mds_next(&m2->mds, ne)) {
      e = lookup(old_of, ne);
      mds_set_part(m2, ne, mds_get_part(m, e));
    }
}

static void rebuild_remotes(
    struct mds_apf* m,
    struct mds_apf* m2,
    struct mds_tag* old_of,
    struct mds_tag* new_of)
{
  int d;
  mds_id ne;
  mds_id e;
  int t;
  for (d = 0; d < m->mds.d; ++d)
    for (ne = mds_begin(&m2->mds, d);
         ne != MDS_NONE;
         ne = mds_next(&m2->mds, ne)) {
      e = lookup(old_of, ne);
      mds_set_copies(&m2->remotes, &m2->mds,
                     ne,
                     mds_get_copies(&m->remotes, e));
    }
  for (t = 0; t < MDS_TYPES; ++t) {
    free(m->remotes.data[t]);
    m->remotes.data[t] = NULL;
  }
}

static struct mds_apf* rebuild(
    struct mds_apf* m,
    struct mds_tag* new_of)
{
  struct mds_apf* m2;
  struct mds_tag* old_of;
  update_remotes(m, new_of);
  m2 = mds_apf_create(m->user_model, m->mds.d, m->mds.n);
  old_of = invert(&m->mds, m2, new_of);
  rebuild_verts(m, m2, old_of);
  rebuild_ents(m, m2, old_of, new_of);
  rebuild_tags(m, m2, old_of, new_of);
  rebuild_coords(m, m2, old_of, new_of);
  rebuild_parts(m, m2, old_of, new_of);
  rebuild_remotes(m, m2, old_of, new_of);
  mds_destroy_tag(&m2->tags, old_of);
  return m2;
}

struct mds_apf* mds_reorder(struct mds_apf* m)
{
  struct mds_tag* new_of;
  struct mds_apf* m2;
  new_of = mds_number(m);
  m2 = rebuild(m, new_of);
  mds_apf_destroy(m);
  return m2;
}