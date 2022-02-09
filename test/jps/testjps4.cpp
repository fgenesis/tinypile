#include "jps.hh"

#include <iostream>
#include <algorithm>
#include <string>
#include <string.h>
#include <assert.h>
#include <vector>
#include <math.h>

/*
# = always impassable
F = fence, FenceHopper can hop over 1 tile wide of this as long as not diagonal.
    Teleporter can teleport up to distance 4 over fence
. = FenceHopper can only walk over this diagonally
: = FenceHopper can only walk over this non-diagonally
*/

/*
static const char* data[] =
{
    "###################",
    "##:::::::::::::::##",
    "##:::::FF::::::::##",
    "##::::::F::::::::##",
    "#1:::::FF::::::::##",
    "##:::::FF::::::::##",
    "##:::::FFFFF:::::2#",
    "###################",
    NULL
};
*/

static const char *data[] =
{
    "#################################################################",
    "#                                                               #",
    "#  #########################################  FF        F    2  #",
    "#                                   #                   F       #",
    "#FF###############################  #  #######FF        FFF     #",
    "#                                   #         FFF FF#FFFFFFFF   #",
    "#  #########################################  FF    #    F      #",
    "#  #                  F             1#        F     #       F   #",
    "#  #                  F      FFFFFFF#FFFFFFF  F######    FFFFF  #",
    "#  #     F                 FFFFFFFFF#      F  FF    F    F      #",
    "#  #    FFFFFFFFFFFFFFFFFFFFFFFFFFFF#      F  FF    F    F      #",
    "#  #             FFFFFFFFFFFFFFFFFFF# FFFFFF  FFFFFFFFFFFF      #",
    "#  #                                          FF   FFFFFFFFFFF F#",
    "#  #FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF         F      #",
    "#  #FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF####FFF            #",
    "#  F   FF     F   FFF    F     F    F F F F          ############",
    "#  F    F    FF    F    FFF    F     F F F F         #3         #",
    "#  FFFF###############################################FFFF####  #",
    "#  FFFFF    .....   .........  #:::::::::::::::#     F          #",
    "#  F        ...   .............#:::::FF::::::::#      F   FFF   #",
    "#   F      .... F..............#::::::F::::::::#  F    F  FFF   #",
    "#   F .....     F..    .......  :::::FF::::::::#  F     F  FF   #",
    "#    F   .......F .............#:::::FF::::::::#  F      F  F   #",
    "#           ....F..  ........  #:::::FFFFF:::::   F         F   #",
    "#################################################################",
    NULL
};

struct MyGridBase
{
    MyGridBase(const char *d[])
        : mapdata(d)
    {
        w = (unsigned)strlen(mapdata[0]);
        h = 0;
        for (h = 0; mapdata[h]; ++h)
            out.push_back(mapdata[h]);

        std::cout << "W: " << w << "; H: " << h << "; Total cells: " << (w*h) << std::endl;
    }

    unsigned w, h;
    const char **mapdata;
    mutable std::vector<std::string> out;
};

struct MyNormalGrid : public MyGridBase
{
    MyNormalGrid(const char* d[])
        : MyGridBase(d) {}

    // Called by JPS to figure out whether the tile at (x, y) is walkable
    inline char operator()(unsigned x, unsigned y) const
    {
        // # and F both not walkable AT ALL
        if (x < w && y < h)
        {
            char c = mapdata[y][x];
            return c != '#' && c != 'F';
        }
        return 0; // not walkable
    }
};

// considers fences walkable
struct FenceHopperGrid : public MyGridBase
{
    FenceHopperGrid(const char* d[])
        : MyGridBase(d) {}

    inline char operator()(unsigned x, unsigned y) const
    {
        // # not walkable AT ALL, the rest controlled via manipulator
        if (x < w && y < h)
        {
            char c = mapdata[y][x];
            return c != '#'; // the rest is ok
        }
        return 0;
    }
};

inline static bool isstraight(JPS::Position from, JPS::Position to)
{
    return to.x == from.x || to.y == from.y;
}

struct Direction
{
    int x, y;
    inline bool operator==(const Direction& o) const { return x == o.x && y == o.y; }
    inline bool operator!=(const Direction& o) const { return x != o.x || y != o.y; }
};

inline static Direction direction(JPS::Position from, JPS::Position to)
{
    Direction dir { int(to.x - from.x), int(to.y- from.y) };
    return dir;
}

struct FenceHopperManip
{
    FenceHopperManip(const FenceHopperGrid& g) : grid(g) {}
    const FenceHopperGrid& grid;

    inline int getNodeBits(JPS::Position pos, const JPS::Node& parent) const
    {
        assert(grid(pos.x, pos.y)); // this won't be called if not walkable
        char c = grid.mapdata[pos.y][pos.x];
        char pc = grid.mapdata[parent.pos.y][parent.pos.x];

        bool straight = isstraight(parent.pos, pos);
        const Direction dir = direction(parent.pos, pos);

        // Only allow passing over fences straight, not diagonally
        if (pc == 'F') // parent is on fence...
        {
            if (const JPS::Node* parent2 = parent.getParentOpt()) // parent-of-parent may not exist
                if (!parent2 // parent must exist
                    || dir != direction(parent2->pos, parent.pos)) // path is also straight if same direction
                    return -1;
        }

        switch (c)
        {
            case ':': // must walk straight
                if (!straight)
                    return -1;
                break;
            case '.': // must walk diagonal
                if (straight)
                    return -1;
                break;
            case 'F': // can only walk over a single fence tile at a time, ie. parent must not be on fence
                if (pc == 'F' || !straight)
                    return -1;
                break;
        }
        return 0;
    }

    inline int getNodeBitsNoParent(JPS::Position pos) const
    {
        assert(grid(pos.x, pos.y)); // this won't be called if not walkable
        char c = grid.mapdata[pos.y][pos.x];
        if (c == 'F')
            return -1; // can't be here
        return 0;
    }
};

static float distance(const JPS::Position& a, const JPS::Position& b)
{
    const int dx = (int(a.x - b.x));
    const int dy = (int(a.y - b.y));
    return sqrtf(float(dx * dx + dy * dy));
}

struct TeleportManip
{
    TeleportManip(const FenceHopperGrid& g, float telemax) : grid(g), teleportDistance(telemax) {}
    const FenceHopperGrid& grid;
    const float teleportDistance;

    int getNodeBits(JPS::Position pos, const JPS::Node& parent) const
    {
        assert(grid(pos.x, pos.y)); // this won't be called if not walkable

        if (parent.getUserBits()) // On a fence or trying to leave one?
        {
            // walk back chain of parents
            const JPS::Node* p = &parent;
            for (;;)
            {
                p = p->getParentOpt();
                if (!p) // Didn't find parent not on fence, this is bad
                    return -1;
                if (!p->getUserBits()) // was not on fence? That's where we started teleporting
                {
                    if (distance(pos, p->pos) <= teleportDistance) // teleporting some short distance is ok
                        break; // all good, teleport start is close enough
                    else
                        return -1; // fail if that node was too far away
                }
            }
        }

        // sets userbits to 1 if on fence, 0 if not
        return grid.mapdata[pos.y][pos.x] == 'F';
    }

    int getNodeBitsNoParent(JPS::Position pos) const
    {
        assert(grid(pos.x, pos.y)); // this won't be called if not walkable
        char c = grid.mapdata[pos.y][pos.x];
        if (c == 'F')
            return -1; // can't be here
        return 0;
    }
};


// Not used by JPS but added here for convenience
static void manglePath(JPS::PathVector& pv)
{
    const size_t N = pv.size();
    size_t w = 0;
    for (size_t i = 0; i < N; ++i)
    {
        const JPS::Position& p = pv[i];
        if (data[p.y][p.x] != 'F')
            pv[w++] = p;
    }
    pv.resize(w);
}

template<typename GRID, typename Manipulator>
void doRun(const GRID& grid, const Manipulator& manip, JPS_Flags flags, bool mangle)
{
    // Collect waypoints from map
    JPS::PathVector waypoints;
    for (char a = '1'; a <= '9'; ++a)
    {
        for (unsigned y = 0; y < grid.h; ++y)
        {
            const char* sp = strchr(data[y], a);
            if (sp)
            {
                waypoints.push_back(JPS::Pos(JPS::PosType(sp - data[y]), y));
            }
        }
    }

    JPS::PathVector path;
    JPS::Searcher<GRID, Manipulator> search(grid, NULL, manip);
    size_t totalsteps = 0, totalnodes = 0;
    for (size_t i = 1; i < waypoints.size(); ++i)
    {
        // Go from waypoint[i-1] to waypoint[i]
        bool found = search.findPath(path, waypoints[i - 1], waypoints[i], 1, flags);
        if (!found)
        {
            std::cout << "Path not found!" << std::endl;
            break;
        }
        totalsteps += search.getStepsDone();
        totalnodes += search.getNodesExpanded();
    }

    if(mangle)
        manglePath(path);

    // visualize path
    unsigned c = 0;
    for (JPS::PathVector::iterator it = path.begin(); it != path.end(); ++it)
        grid.out[it->y][it->x] = (c++ % 26) + 'a';

    for (unsigned i = 0; i < grid.h; ++i)
        std::cout << grid.out[i] << std::endl;

    std::cout << std::endl;
    std::cout << "Search steps:   " << totalsteps << std::endl;
    std::cout << "Nodes expanded: " << totalnodes << std::endl;
    std::cout << "Memory used: " << search.getTotalMemoryInUse() << " bytes" << std::endl;
}


int main(int argc, char **argv)
{
    puts("---- Original map ----");
    for (size_t i = 0; data[i]; ++i)
        puts(data[i]);

    {
        std::cout << "\n---- Normal pathfinding run ----\n";
        MyNormalGrid grid(data);
        JPS::NoManipulator nomanip;
        doRun(grid, nomanip, 0, false);
    }

    {
        std::cout << "\n---- Fence hopper can hop straight over 1 fence ----\n";
        FenceHopperGrid fhgrid(data);
        FenceHopperManip manip(fhgrid);
        doRun(fhgrid, manip, JPS_Flag_AStarOnly, false);
    }

    {
        std::cout << "\n---- Teleporter can teleport a short distance ----\n";
        FenceHopperGrid fhgrid(data);
        TeleportManip manip(fhgrid, 4.0f);
        doRun(fhgrid, manip, JPS_Flag_AStarOnly, true);
    }

    return 0;
}
