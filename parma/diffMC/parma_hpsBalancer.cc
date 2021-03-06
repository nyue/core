#include <PCU.h>
#include <apfPartition.h>
#include "parma_sides.h"
#include "parma_entWeights.h"
#include "parma_targets.h"
#include "parma_selector.h"
#include "zeroOneKnapsack.h"
#include "maximalIndependentSet/mis.h"
#include "../zoltan/apfZoltan.h"
#include <limits>

using std::vector;
using std::set;

namespace parma {

class MergeTargets {
  public:
    //maxW == HeavyImb
    //Produces the optimal merging combination of the part's neighbors into
    // itself for a given maxW
    MergeTargets(Sides* s, Weights* w, double maxW) {
      //If part is heavy or empty, exit function
      if (w->self() >= maxW || w->self() == 0) {
        return;
      }
      //Create array of neighbor partId's for assignment in mergingNet
      int* nborPartIds = new int[w->size()];
      const Weights::Item* weight;
      unsigned int weightIdx = 0;
      w->begin();
      while( (weight = w->iterate()) )
        nborPartIds[weightIdx++] = weight->first;
      w->end();

      double minWeight = std::numeric_limits<double>::max();
      int* normalizedIntWeights = normalizeWeights(w, minWeight);

      //How much weight can be added on to the current part before it
      // reaches the HeavyImb
      const double weightCapacity = (maxW - w->self());

      PCU_Debug_Print("maxW %.3f selfW %.3f weightCapacity %.3f\n",
          maxW, w->self(), weightCapacity);
      //total weight that can be added to self, normalized to min
      const int knapsackCapacity = floor(scale(weightCapacity/minWeight));

      //Knapsack execution
      //Declared for knapsack class
      int* value = new int[s->total()];
      std::fill (value, value + s->total(),1);

      knapsack* ks = new knapsack(knapsackCapacity, w->size(),
          normalizedIntWeights, value);
      const int solnVal = ks->solve();
      mergeTargetsResults.reserve(solnVal);
      ks->getSolution(mergeTargetsResults);

      //Converting mergeTargetsResults to partId's
      vector<int> partIdMergeTargets;
      partIdMergeTargets.reserve(mergeTargetsResults.size());
      for(size_t i=0; i<mergeTargetsResults.size(); i++)  {
        partIdMergeTargets.push_back(nborPartIds[mergeTargetsResults[i]]);
      }
      //Constant function to change vectors
      mergeTargetsResults.swap(partIdMergeTargets);

      //Debug to see which parts contained in the results
      for(size_t i=0; i<mergeTargetsResults.size(); i++)  {
        PCU_Debug_Print("merge Target result %d\n", mergeTargetsResults[i]);
      }

      delete [] nborPartIds;
      delete [] value;
      delete [] normalizedIntWeights;
      delete ks;
    }

    size_t total() {
      return mergeTargetsResults.size();
    }

    int mergeTargetIndex(int index) {
      return mergeTargetsResults.at(index);
    }

  private:
    MergeTargets();
    vector<int> mergeTargetsResults;

    double scale(double v) {
      double divideFactor = .1;
      return v/divideFactor;
    }

    int* normalizeWeights(Weights* w, double& minWeight){
      //iterating through the neighbor weights to determine the minimum weight
      const Weights::Item* weight;
      w->begin();
      while( (weight = w->iterate()) )
        if ( weight->second < minWeight )
          minWeight = weight->second;
      w->end();

      // PCU_Debug_Print("min weight == %f\n", minWeight); rating 2

      //normalizing the neighbor weights to the minimum neighbor weight with a
      // dividing factor to increase the accuracy of knapsack
      int* normalizedIntWeights = new int[w->size()];
      int weightIdx = 0;
      w->begin();
      while( (weight = w->iterate()) ){
        //Divide Factor normalizing weight code
        double normalizedWeight = weight->second / minWeight;
        normalizedWeight = scale(normalizedWeight);
        normalizedIntWeights[weightIdx++] = (int) ceil(normalizedWeight);

        // PCU_Debug_Print("weight %d, normalized to %d from %f\n",
        // weight->first, normalizedIntWeights[(weightIdx-1)], weight->second); //rating 1, 2 if CHI
      }
      w->end();

      return normalizedIntWeights;
    }
};

  void generatemMisPart(apf::Mesh* m, Sides* s, MergeTargets& tgts,
    misLuby::partInfo& part){
      //Generating misLuby part info for current part
      part.id = m->getId();

      //Passing in the adjPartIds
      const Sides::Item* partId;
      s->begin();
      while( (partId = s->iterate()) )
        part.adjPartIds.push_back(partId->first);
      s->end();

      PCU_Debug_Print("adjpartIds size = %ld\n", (long)(part.adjPartIds.size())); //rating 2

      PCU_Debug_Print("mergeNet size %ld\n", (long)(tgts.total()));

      //Passing in the mergingNet
      for(size_t i = 0; i < tgts.total(); ++i){
        part.net.push_back(tgts.mergeTargetIndex(i));
        PCU_Debug_Print("mergeTarget[%lu] %d\n", i , part.net[i]);//rating 1 (CHI 2)
      }
      part.net.push_back(part.id);
  }

  //Using MIS Luby, selects the merges to be executed based on the merging nets
  // and returns it in the plan
  apf::Migration* selectMerges(apf::Mesh* m, Sides* s, MergeTargets& tgts) {
    misLuby::partInfo part;

    generatemMisPart(m,s,tgts,part);

    int randNumSeed = PCU_Comm_Self()+1;

    mis_init(randNumSeed,false);
    const int isInMis = mis(part);

    // Debug if in MIS rating 0
    if (isInMis && tgts.total())
      PCU_Debug_Print("Part %d in MIS\n", PCU_Comm_Self()); //rank 0 (1 if CHI)

    PCU_Comm_Begin();
    //If the current part is in the MIS, send notification to its mergingNet
    if (isInMis) {
      for(size_t i=0; i < tgts.total(); ++i){
        //PCU_Debug_Print("send dest = %d\n", tgts.mergeTargetIndex(i)); rating 1
        PCU_Comm_Pack(tgts.mergeTargetIndex(i), NULL, 0);
      }
    }

    PCU_Comm_Send();
    //Listening to see if it needs to merge into another part, one msg only
    //Only data needed is sender
    bool received = false;
    int destination = -1;
    while (PCU_Comm_Listen()) {
      assert(!received);
      destination = PCU_Comm_Sender();
      received = true;
      PCU_Debug_Print("destination = %d\n", destination);
    }

    //Migration of part entities to its destination part if received one
    apf::MeshIterator* it = m->begin(m->getDimension());
    apf::MeshEntity* e;
    apf::Migration* plan = new apf::Migration(m);
    while ((e = m->iterate(it)) && received)
      plan->send(e, destination);
    m->end(it);

    return plan;
  }


  int splits(Weights* w, double tgtWeight) {
    return static_cast<int>(ceil(w->self()/tgtWeight))-1;
  }

  int isEmpty(apf::Mesh* m, apf::Migration* plan) {
    return (m->count(m->getDimension()) - plan->count() == 0) ? 1 : 0;
  }

  int numSplits(Weights* w, double tgtWeight) {
    int ns = splits(w, tgtWeight);
    PCU_Add_Ints(&ns, 1);
    return ns;
  }

  int numEmpty(apf::Mesh* m, apf::Migration* plan) {
    int empty = isEmpty(m, plan);
    PCU_Add_Ints(&empty, 1);

    return empty;
  }

  bool canSplit(apf::Mesh* m, Weights* w, apf::Migration* plan, double tgt) {
    int verbose = 1;
    const int empties = numEmpty(m, plan);
    const int splits = numSplits(w, tgt);
    const int extra = empties - splits;
    if( !PCU_Comm_Self() && verbose )
      fprintf(stdout, "HPS_STATUS chi <imb empties splits> %.3f %6d %6d\n",
        tgt, empties, splits);
    if ( extra < 0 )
      return false;
    else
      return true;
  }

  double avgWeight(Weights* w) {
    double avg = w->self();
    PCU_Add_Doubles(&avg, 1);
    return avg/PCU_Comm_Peers();
  }

  double maxWeight(Weights* w) {
    double max = w->self();
    PCU_Max_Doubles(&max, 1);
    return max;
  }

  double imbalance(Weights* w) {
    return maxWeight(w)/avgWeight(w);
  }

  double imbalance(apf::Mesh* m, apf::MeshTag* wtag) {
    Sides* s = makeElmBdrySides(m);
    Weights* w = makeEntWeights(m, wtag, s, m->getDimension());
    double imb = imbalance(w);
    delete w;
    delete s;
    return imb;
  }
  //Function used to find the optimal heavy imbalance for executing HPS
  double chi(apf::Mesh* m, apf::MeshTag*, Sides* s, Weights* w) {
    double t0 = MPI_Wtime();
    int verbose = 1;
    double testW = maxWeight(w);
    double step = 0.05 * avgWeight(w);
    bool splits = false;
    do {
      testW -= step;
      MergeTargets mergeTgts(s, w, testW);
      apf::Migration* plan = selectMerges(m, s, mergeTgts);
      splits = canSplit(m, w, plan, testW);
      delete plan; // not migrating
   } while ( splits );
   testW += step;
   if( !PCU_Comm_Self() && verbose )
     fprintf(stdout, "HPS_STATUS chi ran in seconds %.3f\n", MPI_Wtime()-t0);
   return testW;
  }

  apf::Migration* splitPart(apf::Mesh* m, apf::MeshTag* w, int factor) {
    apf::Migration* splitPlan = NULL;
    if( !factor ) return splitPlan;
    bool isSync = false; bool isDebug = false;
    apf::Splitter* s =
      makeZoltanSplitter(m, apf::GRAPH, apf::PARTITION, isDebug, isSync);
    double imb = 1.05;
    splitPlan = s->split(w, imb, factor+1);
    delete s;
    return splitPlan;
  }


  void assignSplits(std::vector<int>& tgts,
      apf::Migration* plan) {
    assert( plan->count() );
    for (int i = 0; i < plan->count(); ++i) {
      apf::MeshEntity* e = plan->get(i);
      int p = plan->sending(e);
      assert((size_t)p <= tgts.size());
      plan->send(e, tgts[p-1]);
    }
  }

  int splits(apf::Mesh* m, Weights* w, double tgtWeight, apf::Migration* plan) {
    int extra = numEmpty(m, plan) - numSplits(w, tgtWeight);
    int split = splits(w, tgtWeight);
    int empty = isEmpty(m, plan);
    PCU_Debug_Print("extra %d\n", extra);
    while (extra != 0) {
      double maxW = w->self();
      if( split || empty )
        maxW = 0;
      PCU_Max_Doubles(&maxW, 1);
      int id = m->getId();
      if( split || empty || maxW != w->self() )
        id = -1;
      PCU_Max_Ints(&id, 1);
      if( m->getId() == id ) {
        split = 1;
        PCU_Debug_Print("part %d w %f assigned to extra %d\n", 
                        m->getId(), w->self(), extra);
      }
      extra--;
    }
    return split;
  }

  void writePlan(apf::Migration* plan) {
    typedef std::map<int,int> mii;
    mii dest;
    for (int i = 0; i < plan->count(); ++i) {
      apf::MeshEntity* e = plan->get(i);
      dest[plan->sending(e)]++;
    }
    PCU_Debug_Print("plan->count() %d\n", plan->count());
    APF_ITERATE(mii, dest, dItr)
      PCU_Debug_Print("p %d c %d\n", dItr->first, dItr->second);
  }

  void split(apf::Mesh* m, apf::MeshTag* wtag, Weights* w, double tgt,
      apf::Migration** plan) {
    int verbose = 1;
    assert(*plan);
    const int partId = m->getId();
    int split = splits(m, w, tgt, *plan);
    assert(split == 0 || split == 1);
    int totSplit = split;
    PCU_Add_Ints(&totSplit, 1);
    int empty = isEmpty(m, *plan);
    int totEmpty = numEmpty(m,*plan);
    if( !PCU_Comm_Self() && verbose )
      fprintf(stdout, "HPS_STATUS numEmpty %d numSplits %d\n", totEmpty, totSplit);
    PCU_Debug_Print("split %d empty %d\n", split, empty);
    assert( totSplit == totEmpty );
    assert(!(split && empty));
    int hl[2] = {split, empty};
    //number the heavies and empties
    PCU_Exscan_Ints(hl, 2);
    PCU_Debug_Print("hl[0] %d hl[1] %d\n", hl[0], hl[1]);
    //send heavy part ids to brokers
    PCU_Comm_Begin();
    if( split ) {
      PCU_Debug_Print("sending to %d\n", hl[0]);
      PCU_COMM_PACK(hl[0], partId);
    }
    PCU_Comm_Send();
    int heavyPartId = 0;
    int count = 0;
    while(PCU_Comm_Listen()) {
      count++;
      PCU_COMM_UNPACK(heavyPartId);
      PCU_Debug_Print("%d heavyPartId %d\n", PCU_Comm_Sender(), heavyPartId);
    }
    //send empty part ids to brokers
    PCU_Comm_Begin();
    if( empty ) {
      PCU_Debug_Print("sendingB to %d\n", hl[1]);
      PCU_COMM_PACK(hl[1], partId);
    }
    PCU_Comm_Send();
    int emptyPartId = -1;
    count = 0;
    while(PCU_Comm_Listen()) {
      count++;
      PCU_COMM_UNPACK(emptyPartId);
      PCU_Debug_Print("%d emptyPartId %d\n", PCU_Comm_Sender(), emptyPartId);
    }
    //brokers send empty part assignment to heavies
    PCU_Comm_Begin();
    if ( emptyPartId != -1 ) {
      PCU_Debug_Print("sendingC %d to %d\n", emptyPartId, heavyPartId);
      PCU_COMM_PACK(heavyPartId, emptyPartId);
    }
    PCU_Comm_Send();
    std::vector<int> tgtEmpties;
    while(PCU_Comm_Listen()) {
      int tgtPartId = 0;
      PCU_COMM_UNPACK(tgtPartId);
      tgtEmpties.push_back(tgtPartId);
      PCU_Debug_Print("target empty %d\n", tgtPartId);
    }
    if( split ) {
      delete *plan;
      *plan = splitPart(m, wtag, split);
      assignSplits(tgtEmpties, *plan);
    }
    writePlan(*plan);
    PCU_Debug_Print("elms %lu plan->count() %d\n",
        m->count(m->getDimension()), (*plan)->count());
  }

  void hps(apf::Mesh* m, apf::MeshTag* wtag, Sides* s, Weights* w, double tgt) {
    PCU_Debug_Print("---hps---\n");
    MergeTargets mergeTargets(s, w, tgt);
    apf::Migration* plan = selectMerges(m, s, mergeTargets);
    split(m, wtag, w, tgt, &plan);
    m->migrate(plan);
  }

  class HpsBalancer : public apf::Balancer {
    public:
      HpsBalancer(apf::Mesh* m, int v)
        : mesh(m), verbose(v)
      {
        (void) verbose; // silence!
      }
      void run(apf::MeshTag* wtag) {
        Sides* sides = makeElmBdrySides(mesh);
        Weights* w = makeEntWeights(mesh, wtag, sides, mesh->getDimension());
        double tgt = chi(mesh, wtag, sides, w);
        PCU_Debug_Print("Final Chi = %.3f\n",tgt); //rating 0
        hps(mesh, wtag, sides, w, tgt); //**Uncomment when done with testing
        delete sides;
        delete w;
        return;
      }
      virtual void balance(apf::MeshTag* weights, double tolerance) {
        (void) tolerance; // shhh
        double initImb = imbalance(mesh, weights);
        double t0 = MPI_Wtime();
        run(weights);
        double elapsed = MPI_Wtime()-t0;
        PCU_Max_Doubles(&elapsed, 1);
        double finalImb = imbalance(mesh, weights);
        if (!PCU_Comm_Self())
          printf("elements balanced from %f to %f in %f seconds\n",
              initImb, finalImb, elapsed);
      }
    private:
      apf::Mesh* mesh;
      int verbose;
  };
} //end parma namespace

apf::Balancer* Parma_MakeHpsBalancer(apf::Mesh* m, int verbosity) {
  return new parma::HpsBalancer(m, verbosity);
}
