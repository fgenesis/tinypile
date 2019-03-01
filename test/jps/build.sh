#!/bin/sh
c++ testjps1.cpp -I../../ -DNDEBUG -o testjps1 -O3 -pipe -Wall -pedantic
c++ testjps2.cpp -I../../ ScenarioLoader.cpp -DNDEBUG -o testjps2 -O3 -pipe -Wall -pedantic
