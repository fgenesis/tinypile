#include "jps.hh"

#include <iostream>
#include <algorithm>
#include <string>
#include <string.h>
#include <assert.h>
#include <vector>

static const char *data[] =
{
    "##############################################",
    "#                           #    #           #",
    "#                 ###       ##   #    ####   #",
    "#      #######      #       ######   #       #",
    "#      #     #      #   2   #       #        #",
    "#      #     #      #       #      #5        #",
    "#      #            #      #     ######      #",
    "#    ###############            #            #",
    "#      #           3#           #            #",
    "#      #            ############             #",
    "#      #        ####  #     #                #",
    "#       #              #####             #####",
    "#                 #                        1 #",
    "#                 #            4             #",
    "##############################################",
    NULL
};

struct MyGrid
{
    MyGrid(const char *d[])
        : mapdata(d)
    {
        w = (unsigned)strlen(mapdata[0]);
        h = 0;
        for(h = 0; mapdata[h]; ++h)
            out.push_back(mapdata[h]);

        std::cout << "W: " << w << "; H: " << h << "; Total cells: " << (w*h) << std::endl;
    }

    // Called by JPS to figure out whether the tile at (x, y) is walkable
    inline unsigned operator()(unsigned x, unsigned y) const
    {
        unsigned canwalk = x < w && y < h;
        if(canwalk)
        {
            canwalk = mapdata[y][x] != '#';
            out[y][x] = canwalk ? '.' : '@'; // also visualize which area was scanned
        }
        return canwalk;
    }

    unsigned w, h;
    const char **mapdata;
    mutable std::vector<std::string> out;
};


int main(int argc, char **argv)
{
    MyGrid grid(data);

    // Collect waypoints from map
    JPS::PathVector waypoints;
    for(char a = '1'; a <= '9'; ++a)
    {
        for(unsigned y = 0; y < grid.h; ++y)
        {
            const char *sp = strchr(data[y], a);
            if(sp)
            {
                waypoints.push_back(JPS::Pos(JPS::PosType(sp - data[y]), y));
            }
        }
    }

    unsigned step = argc > 1 ? atoi(argv[1]) : 0;
    std::cout << "Calculating path with step " << step << std::endl;

    JPS::PathVector path;
    JPS::Searcher<MyGrid> search(grid);
    size_t totalsteps = 0, totalnodes = 0;
    for(size_t i = 1; i < waypoints.size(); ++i)
    {
        // Go from waypoint[i-1] to waypoint[i]
        bool found = search.findPath(path, waypoints[i-1], waypoints[i], step);
        if(!found)
        {
            std::cout << "Path not found!" << std::endl;
            break;
        }
        totalsteps += search.getStepsDone();
        totalnodes += search.getNodesExpanded();
    }

    // visualize path
    unsigned c = 0;
    for(JPS::PathVector::iterator it = path.begin(); it != path.end(); ++it)
        grid.out[it->y][it->x] = (c++ % 26) + 'a';

    for(unsigned i = 0; i < grid.h; ++i)
        std::cout << grid.out[i] << std::endl;

    std::cout << std::endl;
    std::cout << "Search steps:   " << totalsteps << std::endl;
    std::cout << "Nodes expanded: " << totalnodes << std::endl;
    std::cout << "Memory used: " << search.getTotalMemoryInUse() << " bytes" << std::endl;
    return 0;
}
