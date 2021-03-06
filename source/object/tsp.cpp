#include "c4d.h"
#include "c4d_symbols.h"
#include "c4d_tools.h"
#include "ge_dynamicarray.h"
#include "octtsp.h"
#include "kd_tree.h"

class TSPData : public ObjectData
{
	private:
		LineObject *PrepareSingleSpline(BaseObject *generator, BaseObject *op, Matrix *ml, HierarchyHelp *hh, Bool *dirty);
		void Transform(PointObject *op, const Matrix &m);
		void DoRecursion(BaseObject *op, BaseObject *child, GeDynamicArray<Vector> &points, Matrix ml);
		Random rng;
	public:
		virtual BaseObject* GetVirtualObjects(BaseObject *op, HierarchyHelp *hh);
		virtual Bool Init(GeListNode *node);

		static NodeData *Alloc(void) { return NewObjClear(TSPData); }
};

void TSPData::Transform(PointObject *op, const Matrix &m)
{
	Vector	*padr=op->GetPointW();
	Int32	pcnt=op->GetPointCount(),i;
	
	for (i=0; i<pcnt; i++)
		padr[i] = m * padr[i];
	
	op->Message(MSG_UPDATE);
}

Bool TSPData::Init(GeListNode *node)
{	
	BaseObject		*op   = (BaseObject*)node;
	BaseContainer *data = op->GetDataInstance();

	data->SetFloat(CTTSPOBJECT_MAXSEG,3.);
	data->SetBool(CTTSPOBJECT_REL,TRUE);
	return TRUE;
}

void TSPData::DoRecursion(BaseObject *op, BaseObject *child, GeDynamicArray<Vector> &points, Matrix ml)
{
	BaseObject *tp;
	if (child){
		tp = child->GetDeformCache();
		ml = ml * child->GetMl();
		if (tp){
			DoRecursion(op,tp,points,ml);
		}
		else{
			tp = child->GetCache(NULL);
			if (tp){
				DoRecursion(op,tp,points,ml);
			}
			else{

				if (!child->GetBit(BIT_CONTROLOBJECT)){
					if (child->IsInstanceOf(Opoint)){
						PointObject * pChild = ToPoint(child);
						Int32 pcnt = pChild->GetPointCount();
						const Vector *childVerts = pChild->GetPointR();
						for(Int32 i=0;i<pcnt;i++){
							points.Push(ml * childVerts[i]);
						}
					}
				}
			}
		}
		for (tp = child->GetDown(); tp; tp=tp->GetNext()){
			DoRecursion(op,tp,points,ml);
		}
	}
}

BaseObject *TSPData::GetVirtualObjects(BaseObject *op, HierarchyHelp *hh)
{

	BaseObject *orig = op->GetDown();

	if (!orig) return NULL;

	SplineObject	*pp=NULL;
	Bool			dirty=FALSE;
	Matrix			ml;
	BaseObject *child = op->GetAndCheckHierarchyClone(hh,orig,HIERARCHYCLONEFLAGS_ASPOLY,&dirty,NULL,FALSE);
	BaseThread    *bt=hh->GetThread();
	BaseContainer *data = op->GetDataInstance();
	Float maxSeg = data->GetFloat(CTTSPOBJECT_MAXSEG,3.);
	Bool relativeMaxSeg  = data->GetBool(CTTSPOBJECT_REL,TRUE);

	if (!dirty) dirty = op->CheckCache(hh);					
	if (!dirty) dirty = op->IsDirty(DIRTYFLAGS_DATA);		
	if (!dirty) return op->GetCache(hh);

	GeDynamicArray<Vector> points;
	StatusSetBar(0);
	StatusSetText("Collecting Points");
	DoRecursion(op,child,points, ml);
	StatusSetBar(5);

	rng.Init(1244);
	KDNode *kdTree;
	buildKDTree(points, &kdTree, rng);

	GeDynamicArray<Int32> segments;

	pp=SplineObject::Alloc(0,SPLINETYPE_LINEAR);
	if (!pp) return NULL;

	Int32 pcnt = points.GetCount();
	if(pcnt > 0){
		GeDynamicArray<Int32> pointList(pcnt);
		GeDynamicArray<Int32> path(pcnt);
		Int32 currentPoint = 0;
		for(Int32 i=0;i<pcnt;i++){
			pointList[i] = 1;
		}
		path[0] = 0;
		pointList[0] = 0;
		Int32 currentSeg = 0;
		StatusSetText("Connecting Points");
		Float dist;
		Float prevDist  = -1.;
		Float prev2Dist = -1.;
		for(Int32 i=1;i<pcnt;i++){
			dist = -1.;
			Int32 closestPoint = kdTree->getNearestNeighbor(points,points[currentPoint],pointList, dist, 0);
			if(closestPoint == -1){
				GePrint("error finding neighbor");
				pcnt = i-1;
				break;
			}
			pointList[closestPoint] = 0;
			path[i] = closestPoint;
			currentPoint = closestPoint;
			if( relativeMaxSeg){
				if(prevDist > 0. && prev2Dist > 0.){
					if( dist/prevDist > maxSeg && dist/prev2Dist > maxSeg){
						segments.Push(i-currentSeg);
						currentSeg = i;
					}
				}
			}
			else{
				if(dist > maxSeg){
					segments.Push(i-currentSeg);
					currentSeg = i;
				}
			}
			if(i % 20 == 0){
				StatusSetBar(10 + (90*i)/pcnt);
				if (bt && bt->TestBreak()){
					pcnt = i;
					break;
				}
			}
			prev2Dist = prevDist;
			prevDist  = dist;
		}
		if(currentSeg < pcnt){
			segments.Push(pcnt-currentSeg);
		}

		pp->ResizeObject(pcnt,segments.GetCount());

		Segment* seg = pp->GetSegmentW();
		for(Int32 i=0;i<segments.GetCount();i++){
			seg[i].cnt = segments[i];
			seg[i].closed = FALSE;
		}

		Vector *padr=pp->GetPointW();
		
		for(Int32 i=0;i<pcnt;i++){
			padr[i] = points[path[i]];
		}
	}
	DeleteMem(kdTree);
	pp->Message(MSG_UPDATE);
	pp->SetName(op->GetName());
	StatusClear();
	
	return pp;
}

// be sure to use a unique ID obtained from www.plugincafe.com
#define ID_TSPOBJECT 1030812

Bool RegisterTSP(void)
{
	return RegisterObjectPlugin(ID_TSPOBJECT,GeLoadString(IDS_TSP),OBJECT_GENERATOR|OBJECT_INPUT,TSPData::Alloc,"Octtsp",AutoBitmap("tsp.tif"),0);
}
