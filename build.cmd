@echo off
if not exist demo mkdir demo
g++ -std=c++23 -Wall -static -o musicpp.exe main.cpp -lwinmm
g++ -std=c++23 -Wall -static -I. -o demo/api_demo.exe examples/api_demo.cpp -lwinmm
g++ -std=c++23 -Wall -static -I. -o demo/async_demo.exe examples/async_demo.cpp -lwinmm
g++ -std=c++23 -Wall -static -I. -o demo/stream_demo.exe examples/stream_demo.cpp -lwinmm
