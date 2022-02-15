// How to use:
// Set working directory to test/jps (= where this file resides), then run:
//  ./testjps maps/*.scen
// for a quick benchmark and correctness test.

#include "jps.hh"

#include <iostream>
#include "ScenarioLoader.h"
#include <fstream>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

// Testing material from http://www.movingai.com/benchmarks/

static void die(const char *msg)
{
    std::cerr << msg << std::endl;
    abort();
}

struct MapGrid
{
    MapGrid(const char *file)
    {
        std::ifstream in(file);
        if(!in)
            die(file);

        std::string s;
        std::getline(in, s);
        in >> s >> h;
        in >> s >> w;
        in >> s;

        while(in >> s)
            if(s.length() == w)
                lines.push_back(s);

        if(h != lines.size())
            die("Wrong number of lines");

        std::cout << "[" << file << "] W: " << w << "; H: " << h << "; Total cells: " << (w*h) << std::endl;
    }

    bool operator()(unsigned x, unsigned y) const
    {
        if(x < w && y < h)
        {
            const char c = lines[y][x];
            switch(c)
            {
                case '.':
                case 'G':
                case 'S':
                    return true;
            }
        }
        return false;
    }

    unsigned w, h;
    std::vector<std::string> lines;
};

static double pathcost(unsigned startx, unsigned starty, const JPS::PathVector& path)
{
    unsigned lastx = startx;
    unsigned lasty = starty;
    double accu = 0;
    assert(path.empty() || path[0] != JPS::Pos(startx, starty));
    for(size_t i = 0; i < path.size(); ++i)
    {
        unsigned x = path[i].x;
        unsigned y = path[i].y;

        int dx = int(x - lastx);
        int dy = int(y - lasty);
        JPS::ScoreType dmh = JPS::Heuristic::Manhattan(path[i], JPS::Pos(lastx, lasty));
        int maxdmh = !!dx + !!dy;
        if(dmh > maxdmh)
            die("incoherent path!");

        accu += sqrt(double(dx)*dx + double(dy)*dy);

        lastx = x;
        lasty = y;
    }
    return accu;
}

double runScenario(const char *file)
{
    ScenarioLoader loader(file);
    const unsigned N = loader.GetNumExperiments();
    if(!N)
        die(file);
    MapGrid grid(loader.GetNthExperiment(0).GetMapName());
    double sum = 0, diffsum = 0;
    JPS::PathVector path;
    JPS::Searcher<MapGrid> search(grid);
    for(unsigned i = 0; i < N; ++i)
    {
        const Experiment& ex = loader.GetNthExperiment(i);
        path.clear();
        int runs = 0;

        // single-call
        //bool found = JPS::findPath(path, grid, ex.GetStartX(), ex.GetStartY(), ex.GetGoalX(), ex.GetGoalY(), 0, 0, &stepsDone, &nodesExpanded);

        // Testing incremental runs
        bool found = false;
        const JPS::Position startpos = JPS::Pos(ex.GetStartX(), ex.GetStartY());
        const JPS::Position endpos = JPS::Pos(ex.GetGoalX(), ex.GetGoalY());
        JPS_Result res = search.findPathInit(startpos, endpos/*, JPS_Flag_AStarOnly*/);
        if(res == JPS_EMPTY_PATH)
            found = true;
        else
        {
            while(res == JPS_NEED_MORE_STEPS)
            {
                ++runs;
                res = search.findPathStep(10000);
            }
            found = (res == JPS_FOUND_PATH) && search.findPathFinish(path, 1);
        }

        if(!found)
        {
            printf("#### [%s:%d] PATH NOT FOUND: (%d, %d) -> (%d, %d)\n",
                file, i, ex.GetStartX(), ex.GetStartY(), ex.GetGoalX(), ex.GetGoalY());
            die("Path not found!"); // all paths known to be valid, so this is bad
            continue;
        }

        assert((path.empty() && startpos == endpos) || path.back() == endpos);

        // Starting position is NOT included in vector
        double cost = pathcost(ex.GetStartX(), ex.GetStartY(), path);
        double diff = fabs(cost - ex.GetDistance());
#ifdef _DEBUG
        printf("[%s] [%s:%d] Path len: %.3f (%.3f); Diff: %.3f; Steps: %u; Nodes: %u; Runs: %u\n",
            (cost > ex.GetDistance()+0.5f ? "##" : "  "), file, i, cost, ex.GetDistance(),
            diff, (unsigned)search.getStepsDone(), (unsigned)search.getNodesExpanded(), runs);
#else
        (void)runs;
#endif

        sum += cost;
        diffsum += diff;
    }
    printf("%u runs done. Req. memory: %u KB.\nDiffsum = %f (smaller is better)\n", N, (unsigned)search.getTotalMemoryInUse() / 1024, diffsum);
    return sum;
}

int main(int argc, char **argv)
{
    double sum = 0;
    for(int i = 1; i < argc; ++i)
        sum += runScenario(argv[i]);

    std::cout << "Total distance travelled: " << sum << std::endl;

    return 0;
}

