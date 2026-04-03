/*
 * Area- and Topology-Preserving Polygon Simplification
 * APSC algorithm — Kronenfeld et al. (2020)
 *
 * Build:  g++ -O2 -std=c++17 -o apsc apsc.cpp
 * Usage:  ./apsc input.csv target_vertices
 *         ./apsc --profile
 *
 * Output format (stdout):
 *   ring_id,vertex_id,x,y          (CSV with full double precision)
 *   Total signed area in input: X
 *   Total signed area in output: X
 *   Total areal displacement: X
 *
 * Diagnostics go to stderr.
 *
 * ─── Design ──────────────────────────────────────────────────────────────────
 *
 * Ring representation: doubly-linked circular list of Node objects.
 *   Each Node owns the directed segment this→next (identified by seg_id).
 *   A live/dead flag plus a per-node generation counter enable O(1) staleness
 *   detection in the heap.
 *
 * Candidate selection: std::priority_queue<Candidate> min-heap keyed by
 *   areal displacement. A Candidate stores snapshots of all four node pointers
 *   (A,B,C,D) and B's generation. On pop, staleness is checked by comparing
 *   the stored pointers with the current neighbourhood; stale entries are
 *   discarded in O(1) (lazy deletion). E is always recomputed from current
 *   ABCD at pop-time to guarantee it lies on the E-line.
 *
 * Segment index: unordered_map<seg_id, Node*> with bounding-box pre-filter.
 *   O(1) insert/remove; O(n) worst-case query (replace with R-tree for
 *   O(√n) expected on 2D inputs).
 *
 * Area preservation: Steiner point E is placed on the E-line (Eq. 1 of paper),
 *   which is parallel to AD. The signed shoelace area is unchanged by
 *   construction. Total signed area is preserved to within floating-point
 *   rounding (~1e-9 relative error per collapse).
 *
 * Topology: new edges AE and ED are checked against all live segments before
 *   each collapse; unsafe candidates are skipped.
 *
 * Areal displacement: accumulated exactly as the sum of per-collapse
 *   displacements |tri(A,B,E)| + |tri(E,C,D)|. This matches the McMaster
 *   (1986) definition used by Kronenfeld et al.
 *
 * Complexity: O(N) collapses × O(k log N) per collapse where k = segments
 *   in local bbox. Measured ≈ O(N^2) for the flat hash-map index.
 */

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

static const double EPS = 1e-9;
static const double PI  = std::acos(-1.0);  // portable, exact to double precision
struct Point { double x, y; };

// ─── geometry ────────────────────────────────────────────────────────────────

inline double cross2(const Point& O,const Point& A,const Point& B){
    return (A.x-O.x)*(B.y-O.y)-(A.y-O.y)*(B.x-O.x);
}
inline int sgn(double d){ return d>EPS?1:(d<-EPS?-1:0); }
inline double tri_signed(const Point& O,const Point& A,const Point& B){
    return 0.5*cross2(O,A,B);
}
inline bool on_seg(const Point& A,const Point& B,const Point& P){
    return std::min(A.x,B.x)-EPS<=P.x&&P.x<=std::max(A.x,B.x)+EPS&&
           std::min(A.y,B.y)-EPS<=P.y&&P.y<=std::max(A.y,B.y)+EPS;
}
bool segs_intersect(const Point& p1,const Point& p2,
                    const Point& p3,const Point& p4){
    double d1=cross2(p3,p4,p1),d2=cross2(p3,p4,p2);
    double d3=cross2(p1,p2,p3),d4=cross2(p1,p2,p4);
    if(sgn(d1)*sgn(d2)<0&&sgn(d3)*sgn(d4)<0)return true;
    if(std::abs(d1)<EPS&&on_seg(p3,p4,p1))return true;
    if(std::abs(d2)<EPS&&on_seg(p3,p4,p2))return true;
    if(std::abs(d3)<EPS&&on_seg(p1,p2,p3))return true;
    if(std::abs(d4)<EPS&&on_seg(p1,p2,p4))return true;
    return false;
}
inline double dist_pt_line(const Point& P,const Point& A,const Point& B){
    double dx=B.x-A.x,dy=B.y-A.y,len=std::sqrt(dx*dx+dy*dy);
    if(len<EPS) return std::hypot(P.x-A.x,P.y-A.y);
    return std::abs((P.x-A.x)*dy-(P.y-A.y)*dx)/len;
}
inline int side_of(const Point& P,const Point& A,const Point& B){
    return sgn(cross2(A,B,P));
}
double signed_area_vec(const std::vector<Point>& pts){
    double s=0; int n=(int)pts.size();
    for(int i=0;i<n;++i){int j=(i+1)%n;s+=pts[i].x*pts[j].y-pts[j].x*pts[i].y;}
    return 0.5*s;
}

// ─── node ────────────────────────────────────────────────────────────────────

static int g_seg_ctr=0;
inline int fresh_seg(){return ++g_seg_ctr;}

struct Node{
    Point p;
    int ring_id,orig_vid;
    bool alive;
    int gen;       // bumped when neighbourhood changes
    int seg_id;    // id of segment this→next
    Node*prev,*next;
    Node(const Point&pt,int rid,int vid)
        :p(pt),ring_id(rid),orig_vid(vid),alive(true),
         gen(0),seg_id(fresh_seg()),prev(nullptr),next(nullptr){}
};

// ─── ring ────────────────────────────────────────────────────────────────────

struct Ring{
    int ring_id,n_live;
    double target_area;   // signed shoelace area of original ring
    Node*head;
    static constexpr int MIN_V=3;
};

// ─── segment index ───────────────────────────────────────────────────────────
// Hash map of live segments with bbox pre-filter before exact intersection test.
// For very large n, replace with R-tree for O(log n + k) query cost.

struct SegIndex{
    std::unordered_map<int,Node*>table;
    void insert(Node*nd){table[nd->seg_id]=nd;}
    void remove(int sid){table.erase(sid);}
    std::vector<Node*>query(double xlo,double xhi,double ylo,double yhi,
                            const std::set<int>&excl)const{
        std::vector<Node*>res;res.reserve(16);
        for(auto&[sid,nd]:table){
            if(excl.count(sid))continue;
            const Point&a=nd->p;const Point&b=nd->next->p;
            if(std::max(a.x,b.x)<xlo-EPS||std::min(a.x,b.x)>xhi+EPS)continue;
            if(std::max(a.y,b.y)<ylo-EPS||std::min(a.y,b.y)>yhi+EPS)continue;
            res.push_back(nd);
        }
        return res;
    }
};

// ─── placement function  (Kronenfeld et al. §Methods) ────────────────────────
/*
 * E-line: a·x + b·y + c = 0, parallel to AD.
 * Any E on this line preserves the signed shoelace area of the ring exactly.
 *
 *   a = D.y − A.y
 *   b = A.x − D.x
 *   c = −B.y·A.x + (A.y−C.y)·B.x + (B.y−D.y)·C.x + C.y·D.x
 *
 * Placement rule (Figure 4 of paper):
 *   B,C on opposite sides of AD:
 *     use AB if B is on same side as E-line, else use CD.
 *   B,C on same side of AD:
 *     use AB if dist(B,AD) > dist(C,AD), else use CD.
 * Fallback: project midpoint(B,C) onto E-line (provably preserves area).
 */
void e_line_coeffs(const Point&A,const Point&B,const Point&C,const Point&D,
                   double&a,double&b,double&c){
    a=D.y-A.y; b=A.x-D.x;
    c=-B.y*A.x+(A.y-C.y)*B.x+(B.y-D.y)*C.x+C.y*D.x;
}
bool intersect_e_line(double a,double b,double c,
                      const Point&P,const Point&Q,Point&out){
    double dx=Q.x-P.x,dy=Q.y-P.y,den=a*dx+b*dy;
    if(std::abs(den)<EPS)return false;
    double t=-(a*P.x+b*P.y+c)/den;
    out={P.x+t*dx,P.y+t*dy};
    return true;
}
bool placement_func(const Point&A,const Point&B,const Point&C,const Point&D,Point&E){
    double a,b,c; e_line_coeffs(A,B,C,D,a,b,c);
    if(std::abs(a)<EPS&&std::abs(b)<EPS){
        E={0.5*(A.x+D.x),0.5*(A.y+D.y)};return true;
    }
    int sB=side_of(B,A,D),sC=side_of(C,A,D);
    bool use_AB;
    if(sB==sC){
        use_AB=dist_pt_line(B,A,D)>dist_pt_line(C,A,D)+EPS;
    }else{
        Point Ep; if(std::abs(a)>EPS)Ep={-c/a,0.};else Ep={0.,-c/b};
        use_AB=(sB==side_of(Ep,A,D));
    }
    if(use_AB){
        if(intersect_e_line(a,b,c,A,B,E))return true;
        if(intersect_e_line(a,b,c,C,D,E))return true;
    }else{
        if(intersect_e_line(a,b,c,C,D,E))return true;
        if(intersect_e_line(a,b,c,A,B,E))return true;
    }
    // Fallback: project midpoint(B,C) onto E-line.
    // Proof: E = mid + t*(a,b) satisfies a*E.x+b*E.y+c = 0 when
    //        t = -(a*mid.x+b*mid.y+c)/(a²+b²). ∎
    Point m={0.5*(B.x+C.x),0.5*(B.y+C.y)};
    double d2=a*a+b*b,t=-(a*m.x+b*m.y+c)/d2;
    E={m.x+a*t,m.y+b*t};
    return true;
}
inline double displacement(const Point&A,const Point&B,const Point&C,
                           const Point&D,const Point&E){
    // Two triangular wedge areas: left=|tri(A,B,E)|, right=|tri(E,C,D)|.
    // Since E is on E-line, left == right; total = left + right.
    return std::abs(tri_signed(A,B,E))+std::abs(tri_signed(E,C,D));
}

// ─── candidate ───────────────────────────────────────────────────────────────
/*
 * Stores all four node pointers (A,B,C,D) and B's generation snapshot.
 * Staleness at pop-time: B/C dead, B->gen changed, or any ptr drifted.
 * E is RECOMPUTED at pop-time from current ABCD — eliminates stale-E errors
 * that arise when D is replaced by a Steiner node in a prior collapse while
 * B's generation was not bumped (D is not adjacent to B directly).
 */
struct Candidate{
    double disp;
    Node*A,*B,*C,*D;
    int B_gen;
    bool operator>(const Candidate&o)const{return disp>o.disp;}
};
using Heap=std::priority_queue<Candidate,std::vector<Candidate>,
                                std::greater<Candidate>>;

std::optional<Candidate> make_candidate(Node*B){
    if(!B->alive)return std::nullopt;
    Node*A=B->prev,*C=B->next;
    if(!A||!C||A==C)return std::nullopt;
    Node*D=C->next;
    if(!D||D==B)return std::nullopt;
    Point E; placement_func(A->p,B->p,C->p,D->p,E);
    return Candidate{displacement(A->p,B->p,C->p,D->p,E),A,B,C,D,B->gen};
}

// ─── topology check ──────────────────────────────────────────────────────────
// Verify that new edges AE and ED do not cross any other live segment.
// Segments incident to A and D are excluded (shared endpoints, not crossings).

bool is_safe(Node*A,Node*B,Node*C,Node*D,const Point&E,const SegIndex&idx){
    const Point&Ap=A->p,&Dp=D->p;
    std::set<int>excl={A->seg_id,B->seg_id,C->seg_id,A->prev->seg_id,D->seg_id};
    double xlo=std::min({Ap.x,E.x,Dp.x}),xhi=std::max({Ap.x,E.x,Dp.x});
    double ylo=std::min({Ap.y,E.y,Dp.y}),yhi=std::max({Ap.y,E.y,Dp.y});
    for(Node*nd:idx.query(xlo,xhi,ylo,yhi,excl)){
        const Point&P=nd->p,&Q=nd->next->p;
        bool pA=std::abs(P.x-Ap.x)<EPS&&std::abs(P.y-Ap.y)<EPS;
        bool pD=std::abs(P.x-Dp.x)<EPS&&std::abs(P.y-Dp.y)<EPS;
        bool qA=std::abs(Q.x-Ap.x)<EPS&&std::abs(Q.y-Ap.y)<EPS;
        bool qD=std::abs(Q.x-Dp.x)<EPS&&std::abs(Q.y-Dp.y)<EPS;
        if(!(pA||qA)&&segs_intersect(Ap,E,P,Q))return false;
        if(!(pD||qD)&&segs_intersect(E,Dp,P,Q))return false;
    }
    return true;
}

// ─── collapse ────────────────────────────────────────────────────────────────
// Remove B and C, insert Steiner point E, update linked list and segment index.
// Returns the displacement contributed by this collapse.

double do_collapse(Node*A,Node*B,Node*C,Node*D,const Point&E,
                   Ring&ring,SegIndex&idx,Heap&heap){
    double disp=displacement(A->p,B->p,C->p,D->p,E);
    idx.remove(A->seg_id); idx.remove(B->seg_id); idx.remove(C->seg_id);
    B->alive=false; C->alive=false;
    Node*En=new Node(E,ring.ring_id,-1);
    A->next=En; En->prev=A; En->next=D; D->prev=En;
    A->seg_id=fresh_seg();
    idx.insert(A); idx.insert(En);
    // Bump generations of A and D so their old heap entries become stale.
    A->gen++; D->gen++;
    --ring.n_live;
    ring.head=En;
    if(auto cv=make_candidate(A))  heap.push(*cv);
    if(auto cv=make_candidate(En)) heap.push(*cv);
    return disp;
}

// ─── ring traversal utilities ────────────────────────────────────────────────

double ring_signed_area(const Ring&r){
    double s=0; Node*cur=r.head;
    do{Node*n=cur->next;s+=cur->p.x*n->p.y-n->p.x*cur->p.y;cur=n;}while(cur!=r.head);
    return 0.5*s;
}

// ─── I/O ─────────────────────────────────────────────────────────────────────

struct Polygon{
    std::vector<Ring*>rings;
    std::unordered_map<int,Ring*>by_id;
    double input_total_signed_area=0;
};

Polygon read_csv(const std::string& fileName){
    std::string path = "./Input_Files/" + fileName;
    std::ifstream f(path);
    if(!f){std::cerr<<"Cannot open: "<<path<<"\n";std::exit(1);}
    std::map<int,std::vector<std::pair<int,Point>>>by_ring;
    std::string line; std::getline(f,line);
    while(std::getline(f,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty())continue;
        std::istringstream ss(line);std::string tok;
        int rid,vid;double x,y;
        std::getline(ss,tok,',');rid=std::stoi(tok);
        std::getline(ss,tok,',');vid=std::stoi(tok);
        std::getline(ss,tok,',');x=std::stod(tok);
        std::getline(ss,tok,',');y=std::stod(tok);
        by_ring[rid].push_back({vid,{x,y}});
    }
    Polygon poly;
    for(auto&[rid,verts]:by_ring){
        std::sort(verts.begin(),verts.end(),[](const auto&a,const auto&b){return a.first<b.first;});
        Ring*ring=new Ring();ring->ring_id=rid;ring->n_live=(int)verts.size();
        std::vector<Point>pts; for(auto&[v,p]:verts)pts.push_back(p);
        ring->target_area=signed_area_vec(pts);
        poly.input_total_signed_area+=ring->target_area;
        std::vector<Node*>nodes; nodes.reserve(verts.size());
        for(auto&[v,p]:verts)nodes.push_back(new Node(p,rid,v));
        int n=(int)nodes.size();
        for(int i=0;i<n;++i){nodes[i]->next=nodes[(i+1)%n];nodes[i]->prev=nodes[(i+n-1)%n];}
        ring->head=nodes[0];
        poly.rings.push_back(ring); poly.by_id[rid]=ring;
    }
    return poly;
}

int total_live(const Polygon&poly){int t=0;for(Ring*r:poly.rings)t+=r->n_live;return t;}

// Write CSV with full double precision, then append summary lines.
void write_output(const Polygon&poly,double total_displacement,std::ostream&out){
    // Full precision for coordinates
    out<<std::setprecision(15);
    out<<"ring_id,vertex_id,x,y\n";
    for(Ring*ring:poly.rings){
        int vid=0;Node*start=ring->head,*cur=start;
        do{
            out<<ring->ring_id<<','<<vid<<','<<cur->p.x<<','<<cur->p.y<<'\n';
            ++vid;cur=cur->next;
        }while(cur!=start);
    }
    // Summary footer matching reference format
    double out_total=0;
    for(Ring*ring:poly.rings) out_total+=ring_signed_area(*ring);
    out<<std::scientific<<std::setprecision(6);
    out<<"Total signed area in input: "<<poly.input_total_signed_area<<"\n";
    out<<"Total signed area in output: "<<out_total<<"\n";
    out<<"Total areal displacement: "<<total_displacement<<"\n";
}

// ─── simplification loop ─────────────────────────────────────────────────────

double simplify(Polygon&poly,int target_n){
    int current=total_live(poly);
    if(current<=target_n){
        std::cerr<<"Already at or below target ("<<current<<" <= "<<target_n<<").\n";
        return 0.0;
    }
    SegIndex idx;
    for(Ring*ring:poly.rings){Node*cur=ring->head;do{idx.insert(cur);cur=cur->next;}while(cur!=ring->head);}
    Heap heap;
    for(Ring*ring:poly.rings){
        if(ring->n_live<=Ring::MIN_V)continue;
        Node*cur=ring->head;
        do{if(auto c=make_candidate(cur))heap.push(*c);cur=cur->next;}while(cur!=ring->head);
    }
    int nc=0,ns=0,nt=0,nm=0;
    double total_disp=0;
    while(current>target_n&&!heap.empty()){
        Candidate cand=heap.top();heap.pop();
        // Staleness: B/C dead, B->gen changed, or structural neighbourhood drifted
        if(!cand.B->alive){++ns;continue;}
        if(cand.B->gen!=cand.B_gen){++ns;continue;}
        if(!cand.C->alive){++ns;continue;}
        if(cand.B->prev!=cand.A||cand.B->next!=cand.C||cand.C->next!=cand.D){++ns;continue;}
        Ring*ring=poly.by_id.at(cand.B->ring_id);
        if(ring->n_live<=Ring::MIN_V){++nm;continue;}
        // Recompute E from current ABCD (O(1), eliminates stale-E errors)
        Point E; placement_func(cand.A->p,cand.B->p,cand.C->p,cand.D->p,E);
        if(!is_safe(cand.A,cand.B,cand.C,cand.D,E,idx)){++nt;continue;}
        total_disp+=do_collapse(cand.A,cand.B,cand.C,cand.D,E,*ring,idx,heap);
        ++nc;--current;
    }
    std::cerr<<"Collapses: "<<nc<<"  Stale: "<<ns
             <<"  Topo-blocked: "<<nt<<"  Min-verts: "<<nm<<"\n"
             <<"Final vertex count: "<<total_live(poly)<<"\n";
    for(Ring*ring:poly.rings){
        double act=ring_signed_area(*ring),err=std::abs(act-ring->target_area);
        std::cerr<<"  Ring "<<ring->ring_id<<": target="<<ring->target_area
                 <<"  actual="<<act<<"  |err|="<<err<<"\n";
    }
    return total_disp;
}

// ─── profiling (synthetic large polygon) ─────────────────────────────────────

void run_profile(){
    auto make_ring=[](int N,double R)->std::vector<Point>{
        std::vector<Point>pts;pts.reserve(N);
        for(int i=0;i<N;++i){
            double t=2*PI*i/N;
            double r=R*(1.0+0.02*std::sin(7.0*t)+0.01*std::sin(13.0*t));
            pts.push_back({r*std::cos(t),r*std::sin(t)});
        }
        return pts;
    };
    for(int N:{500,2000,8000,32000}){
        auto pts=make_ring(N,1000.0);
        Ring*ring=new Ring();ring->ring_id=0;ring->n_live=N;
        std::vector<Point>pv;for(auto&p:pts)pv.push_back(p);
        ring->target_area=signed_area_vec(pv);
        std::vector<Node*>nodes;nodes.reserve(N);
        for(auto&p:pts)nodes.push_back(new Node(p,0,-1));
        for(int i=0;i<N;++i){nodes[i]->next=nodes[(i+1)%N];nodes[i]->prev=nodes[(i+N-1)%N];}
        ring->head=nodes[0];
        Polygon poly;poly.rings.push_back(ring);poly.by_id[0]=ring;
        poly.input_total_signed_area=ring->target_area;
        int target=std::max(Ring::MIN_V,(int)(N*0.1));
        auto t0=std::chrono::high_resolution_clock::now();
        double disp=simplify(poly,target);
        auto t1=std::chrono::high_resolution_clock::now();
        double ms=std::chrono::duration<double,std::milli>(t1-t0).count();
        std::cerr<<"Profile N="<<N<<" -> "<<target<<" verts: "<<ms
                 <<" ms  disp="<<disp<<"\n\n";
        Node*cur=ring->head;
        do{Node*nx=cur->next;delete cur;cur=nx;}while(cur!=ring->head);
        delete ring;
    }
}

// ─── main ────────────────────────────────────────────────────────────────────

int main(int argc,char*argv[]){
    if(argc>=2&&std::string(argv[1])=="--profile"){
        run_profile();return 0;
    }
    if(argc<3){
        std::cerr<<"Usage: "<<argv[0]<<" <input.csv> <target_vertices>\n"
                 <<"       "<<argv[0]<<" --profile\n";
        return 1;
    }
    int target_n=std::atoi(argv[2]);
    auto t0=std::chrono::high_resolution_clock::now();
    Polygon poly=read_csv(argv[1]);
    int total_min=0;for([[maybe_unused]]Ring*r:poly.rings)total_min+=Ring::MIN_V;
    std::cerr<<"Loaded: "<<poly.rings.size()<<" ring(s), "<<total_live(poly)<<" vertices\n";
    for(Ring*r:poly.rings)
        std::cerr<<"  Ring "<<r->ring_id<<": "<<r->n_live
                 <<" verts, area="<<r->target_area<<"\n";
    std::cerr<<"Target: "<<target_n<<"  (min feasible: "<<total_min<<")\n\n";
    double total_disp=simplify(poly,target_n);
    auto t1=std::chrono::high_resolution_clock::now();
    std::cerr<<"Wall time: "
             <<std::chrono::duration<double,std::milli>(t1-t0).count()<<" ms\n";
    write_output(poly,total_disp,std::cout);
    return 0;
}
