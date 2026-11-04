#include "gpu/ds/binary_heap.hpp"
#include "gpu/priority.hpp"
#include "gpu/self_test.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void foundation_demo() {
    gpu::BinaryHeap<gpu::Event, gpu::EventBefore> events;
    // A container demonstration only, not a dependency-aware schedule.
    events.push({6,gpu::EventKind::completion,2,0,2});
    events.push({0,gpu::EventKind::arrival,0,0,0});
    events.push({3,gpu::EventKind::completion,1,1,1});
    events.push({3,gpu::EventKind::arrival,3,0,3});
    std::ofstream csv;
    csv.exceptions(std::ios::failbit | std::ios::badbit);
    csv.open("results/heap_event_demo.csv");
    csv<<"time,event,task_index,unit_index\n";
    while(!events.empty()) {
        auto e=events.pop();
        csv<<e.time<<','<<(e.kind==gpu::EventKind::arrival?"arrival":"completion")
           <<','<<e.task<<','<<e.unit<<'\n';
    }
    csv.close();
    std::ofstream status;
    status.exceptions(std::ios::failbit | std::ios::badbit);
    status.open("results/project_status.txt");
    status<<"FOUNDATION CHECKPOINT - not a finished simulator\n"
          <<"Implemented: binary heap, priority keys, shared types, foundation tests.\n"
          <<"Pending: ring deque, graph, hash table, parser, engine, metrics, charts.\n"
          <<"heap_event_demo.csv demonstrates heap order only. No benchmark claims.\n";
    status.close();
    std::cout<<"Foundation demo written to code/results/. Simulator implementation is pending.\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--self-test")return gpu::run_self_tests(std::cout);
        if(argc==2 && std::string(argv[1])=="--foundation-demo"){foundation_demo();return 0;}
        std::cerr<<"Use root ./run.sh for the foundation build and tests.\n";
        return 2;
    } catch(const std::exception& e) {std::cerr<<"Error: "<<e.what()<<'\n';return 1;}
}
