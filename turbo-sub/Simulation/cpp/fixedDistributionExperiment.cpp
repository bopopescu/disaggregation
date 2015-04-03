
#include <iostream>
#include <algorithm>
#include <fstream>
#include <stdlib.h>
#include <deque>
#include <stdint.h>
#include <cstdlib>
#include <ctime>
#include <map>

#include "flow.h"
#include "turboflow.h"

#include "packet.h"
#include "node.h"
#include "event.h"
#include "topology.h"
#include "simpletopology.h"
#include "params.h"
#include "assert.h"
#include "queue.h"

#include "factory.h"
#include "random_variable.h"

extern Topology *topology;
extern double current_time;
extern std::priority_queue<Event *, std::vector<Event *>, EventComparator> event_queue;
extern std::deque<Flow *> flows_to_schedule;
extern std::deque<Event *> flow_arrivals;

extern uint32_t num_outstanding_packets;
extern uint32_t max_outstanding_packets;
extern DCExpParams params;
extern void add_to_event_queue(Event *);
extern void read_experiment_parameters(std::string conf_filename, uint32_t exp_type);
extern void read_flows_to_schedule(std::string filename, uint32_t num_lines, Topology *topo);
extern uint32_t duplicated_packets_received;

extern double start_time;
extern double get_current_time();

extern void printQueueStatistics(Topology *topo);
extern void run_scenario();

void generate_flows_to_schedule_fd(std::string filename, uint32_t num_flows,
Topology *topo) {
  //double lambda = 4.0966649051007566;
  //use an NAryRandomVariable for true uniform/bimodal/trimodal/etc
  // EmpiricalRandomVariable *nv_bytes =
  // new NAryRandomVariable(filename);
  EmpiricalRandomVariable *nv_bytes = new CDFRandomVariable(filename);
  params.mean_flow_size = nv_bytes->mean_flow_size;

  double lambda = params.bandwidth * params.load /
  (params.mean_flow_size * 8.0 / 1460 * 1500);
  double lambda_per_host = lambda / (topo->hosts.size() - 1);
  std::cout << "Lambda: " << lambda_per_host << std::endl;




  ExponentialRandomVariable *nv_intarr = new ExponentialRandomVariable(1.0 / lambda_per_host);
  //* [expr ($link_rate*$load*1000000000)/($meanFlowSize*8.0/1460*1500)]
  for (uint32_t i = 0; i < topo->hosts.size(); i++) {
    for (uint32_t j = 0; j < topo->hosts.size(); j++) {
      if (i != j) {
        double first_flow_time = 1.0 + nv_intarr->value();
        add_to_event_queue(
        new FlowCreationForInitializationEvent(first_flow_time,
        topo->hosts[i], topo->hosts[j],
        nv_bytes, nv_intarr));
      }
    }
  }
  while (event_queue.size() > 0) {
    Event *ev = event_queue.top();
    event_queue.pop();
    current_time = ev->time;
    if (flows_to_schedule.size() < num_flows) {
      ev->process_event();
    }
    delete ev;
  }
  current_time = 0;
}

void generate_flows_to_schedule_fd_with_traffic_pattern(std::string filename, uint32_t num_flows,
Topology *topo) {

 EmpiricalRandomVariable *nv_bytes = new CDFRandomVariable(filename);
  params.mean_flow_size = nv_bytes->mean_flow_size;

  double lambda = params.bandwidth * params.load / (params.mean_flow_size * 8.0 / 1460 * 1500);



  GaussianRandomVariable popularity(10, params.traffic_imbalance);
  std::vector<int> sources;
  std::vector<int> destinations;

  int self_connection_count = 0;
  for(int i = 0; i < topo->hosts.size(); i++){
    int src_count = (int)(round(popularity.value()));
    int dst_count = (int)(round(popularity.value()));
    std::cout << "node:" << i << " #src:" << src_count << " #dst:" << dst_count << "\n";
    self_connection_count += src_count * dst_count;
    for(int j = 0; j < src_count; j++)
      sources.push_back(i);
    for(int j = 0; j < dst_count; j++)
      destinations.push_back(i);
  }

  double flows_per_host = (sources.size() * destinations.size() - self_connection_count) / (double)topo->hosts.size();
  double lambda_per_flow = lambda / flows_per_host;
  std::cout << "Lambda: " << lambda_per_flow << std::endl;
  ExponentialRandomVariable *nv_intarr = new ExponentialRandomVariable(1.0 / lambda_per_flow);


  //* [expr ($link_rate*$load*1000000000)/($meanFlowSize*8.0/1460*1500)]
  for (uint32_t i = 0; i < sources.size(); i++) {
    for (uint32_t j = 0; j < destinations.size(); j++) {
      if (sources[i] != destinations[j]) {
       double first_flow_time = 1.0 + nv_intarr->value();
        add_to_event_queue(
            new FlowCreationForInitializationEvent(first_flow_time,
                topo->hosts[sources[i]], topo->hosts[destinations[j]],
                nv_bytes, nv_intarr)
        );
      }
    }
  }


  while (event_queue.size() > 0) {
    Event *ev = event_queue.top();
    event_queue.pop();
    current_time = ev->time;
    if (flows_to_schedule.size() < num_flows) {
      ev->process_event();
    }
    delete ev;
  }
  current_time = 0;
}


void write_flows_to_file(std::deque<Flow *> flows, std::string file){
  std::ofstream output(file);
  output.precision(20);
  for(uint i = 0; i < flows.size(); i++){
    output << flows[i]->id << " " << flows[i]->start_time << " " << flows[i]->finish_time << " " << flows[i]->size_in_pkt << " "
        << (flows[i]->finish_time - flows[i]->start_time) << " " << 0 << " " <<flows[i]->src->id << " " << flows[i]->dst->id << "\n";
  }
  output.close();

}


//same as run_pFabric_experiment except with this generate_flows.
void run_fixedDistribution_experiment(int argc, char **argv, uint32_t exp_type) {
  if (argc < 3) {
    std::cout << "Usage: <exe> exp_type conf_file" << std::endl;
    return;
  }
  std::string conf_filename(argv[2]);
  read_experiment_parameters(conf_filename, exp_type);
  params.num_hosts = 144;
  params.num_agg_switches = 9;
  params.num_core_switches = 4;


  if (params.cut_through == 1) {
    topology = new CutThroughTopology(params.num_hosts, params.num_agg_switches,
    params.num_core_switches, params.bandwidth, params.queue_type);
  } else {
    if(params.big_switch){
      topology = new BigSwitchTopology(params.num_hosts, params.bandwidth, params.queue_type);
    }else{
      topology = new PFabricTopology(params.num_hosts, params.num_agg_switches,
      params.num_core_switches, params.bandwidth, params.queue_type);
    }
  }



  uint32_t num_flows = params.num_flows_to_run;

  //no reading flows to schedule in this mode.
  //read_flows_to_schedule(params.cdf_or_flow_trace, num_flows, topology);
  if(params.traffic_imbalance < 0.01)
      generate_flows_to_schedule_fd(params.cdf_or_flow_trace, num_flows, topology);
  else
      generate_flows_to_schedule_fd_with_traffic_pattern(params.cdf_or_flow_trace, num_flows, topology);
  std::deque<Flow *> flows_sorted = flows_to_schedule;
  struct FlowComparator {
    bool operator() (Flow *a, Flow *b) {
      return a->start_time < b->start_time;
    }
  } fc;
  std::sort (flows_sorted.begin(), flows_sorted.end(), fc);

  for(uint32_t i = 0; i < flows_sorted.size(); i++) {
    Flow *f = flows_sorted[i];
    flow_arrivals.push_back(new FlowArrivalEvent(f->start_time, f));
  }

  add_to_event_queue(new LoggingEvent((flows_sorted.front())->start_time + 0.01));

  std::cout << "Running " << num_flows << " Flows\nCDF_File " <<
    params.cdf_or_flow_trace << "\nBandwidth " << params.bandwidth/1e9 <<
    "\nQueueSize " << params.queue_size <<
    "\nCutThrough " << params.cut_through <<
    "\nFlowType " << params.flow_type <<
    "\nQueueType " << params.queue_type <<
    "\nInit CWND " << params.initial_cwnd <<
    "\nMax CWND " << params.max_cwnd <<
    "\nRtx Timeout " << params.retx_timeout_value  <<
    "\nload_balancing (0: pkt)" << params.load_balancing <<
    std::endl;

  run_scenario();

  write_flows_to_file(flows_sorted, "flow.tmp");
  // print statistics
  double sum = 0, sum_norm = 0, sum_inflation = 0, sum_waiting = 0;
  uint data_pkt_sent = 0, parity_pkt_sent = 0, data_pkt_drop = 0, parity_pkt_drop = 0;
  std::map<unsigned, int> rts_send_count_by_size;
  std::map<unsigned, double> flow_total_slowdown_by_size;
  std::map<unsigned, int> flow_count_by_size;
  for (uint32_t i = 0; i < flows_sorted.size(); i++) {
    Flow *f = flows_to_schedule[i];
    if(!f->finished)
      std::cout << "unfinished flow " << "size:" << f->size << " id:" << f->id << " next_seq:" << f->next_seq_no << " recv:" << f->received_bytes  << " src:" << f->src->id << " dst:" << f->dst->id << "\n";

    if(flow_count_by_size.find(f->size_in_pkt) == flow_count_by_size.end()){
        flow_count_by_size[f->size_in_pkt] = 0;
        flow_total_slowdown_by_size[f->size_in_pkt] = 0;
        rts_send_count_by_size[f->size_in_pkt] = 0;
    }
    flow_count_by_size[f->size_in_pkt]++;
    rts_send_count_by_size[f->size_in_pkt] += ((FountainFlowWithPipelineSchedulingHost*)f)->rts_send_count;
    flow_total_slowdown_by_size[f->size_in_pkt] += 1000000.0 * f->flow_completion_time / topology->get_oracle_fct(f);


    sum += 1000000.0 * f->flow_completion_time;
    sum_norm += 1000000.0 * f->flow_completion_time / topology->get_oracle_fct(f);
    sum_inflation += (double)f->total_pkt_sent / (f->size/f->mss);
    sum_waiting += ((FountainFlowWithPipelineSchedulingHost*)f)->first_send_time - f->start_time;
    data_pkt_sent += std::min(f->size_in_pkt, (int)f->total_pkt_sent);
    parity_pkt_sent += std::max(0, (int)(f->total_pkt_sent - f->size_in_pkt));
    data_pkt_drop += f->data_pkt_drop;
    parity_pkt_drop += std::max(0, f->pkt_drop - f->data_pkt_drop);
  }
  std::cout << "AverageFCT " << sum / flows_sorted.size() <<
  " MeanSlowdown " << sum_norm / flows_sorted.size() <<
  " MeanInflation " << sum_inflation / flows_sorted.size() <<
  " MeanWaiting " << sum_waiting / flows_sorted.size() <<
  "\n";
  for(auto it = flow_count_by_size.begin(); it != flow_count_by_size.end(); ++it){
      unsigned key = it->first;
      std::cout << key << ": " << flow_total_slowdown_by_size[it->first]/ it->second << " " << (double)rts_send_count_by_size[it->first]/it->second <<  "   ";
  }
  std::cout << "\n";
  printQueueStatistics(topology);
  std::cout << "Data Pkt Drop Rate: " << (double)data_pkt_drop/data_pkt_sent << " Parity Drop Rate:" << (double)parity_pkt_drop/parity_pkt_sent << "\n";
}


























/* Runs a initialized scenario */
void run_scenario_shuffle_traffic() {
  // Flow Arrivals create new flow arrivals
  // Add the first flow arrival

  while (event_queue.size() > 0) {
    Event *ev = event_queue.top();
    event_queue.pop();
    current_time = ev->time;
    if (start_time < 0) {
      start_time = current_time;
    }
    //event_queue.pop();
    //std::cout << "main.cpp::run_scenario():" << get_current_time() << " Processing " << ev->type << " " << event_queue.size() << std::endl;
    if (ev->cancelled) {
      if(ev->unique_id == 7394)
        std::cout << get_current_time() << " fixed..cpp:186 "  << " cancel " << ev->unique_id  << std::endl;
      delete ev; //TODO: Smarter
      continue;
    }
    ev->process_event();
    delete ev;
  }
}


EmpiricalRandomVariable *nv_bytes;



//same as run_pFabric_experiment except with this generate_flows.
void run_fixedDistribution_experiment_shuffle_traffic(int argc, char **argv, uint32_t exp_type) {
  std::cout << "run_fixedDistribution_experiment_shuffle_traffic\n";
  if (argc < 3) {
    std::cout << "Usage: <exe> exp_type conf_file" << std::endl;
    return;
  }
  std::string conf_filename(argv[2]);
  read_experiment_parameters(conf_filename, exp_type);
  params.num_hosts = 144;
  params.num_agg_switches = 9;
  params.num_core_switches = 4;


  if (params.cut_through == 1) {
    topology = new CutThroughTopology(params.num_hosts, params.num_agg_switches,
    params.num_core_switches, params.bandwidth, params.queue_type);
  } else {
    topology = new PFabricTopology(params.num_hosts, params.num_agg_switches,
    params.num_core_switches, params.bandwidth, params.queue_type);
  }
  PFabricTopology *topo = (PFabricTopology *) topology;


  nv_bytes = new CDFRandomVariable(params.cdf_or_flow_trace);

//
//  uint32_t num_flows = params.num_flows_to_run;
//
//  //no reading flows to schedule in this mode.
//  generate_flows_to_schedule_fd(params.cdf_or_flow_trace, num_flows, topo);
//
//  std::deque<Flow *> flows_sorted = flows_to_schedule;
//  struct FlowComparator {
//    bool operator() (Flow *a, Flow *b) {
//      return a->start_time < b->start_time;
//    }
//  } fc;
//  std::sort (flows_sorted.begin(), flows_sorted.end(), fc);
//
//  for(uint32_t i = 0; i < flows_sorted.size(); i++) {
//    Flow *f = flows_sorted[i];
//    flow_arrivals.push_back(new FlowArrivalEvent(f->start_time, f));
//  }


  int traffic_matrix[144];
  for(int i = 0; i < 144; i++)
    traffic_matrix[i] = i;

  for(int i = 0; i < 144; i++){
    int j = std::rand()%(i+1);
    int s = traffic_matrix[i];
    traffic_matrix[i] = traffic_matrix[j];
    traffic_matrix[j] = s;
  }

  for(int i = 0; i < 144; i++){
    Host* src = topo->hosts[i];
    Host* dst = topo->hosts[traffic_matrix[i]];
    Flow* flow = Factory::get_flow(1, nv_bytes->value() * 1460, src, dst, params.flow_type);
    flows_to_schedule.push_back(flow);
    Event * event = new FlowArrivalEvent(flow->start_time, flow);
    add_to_event_queue(event);
  }




  add_to_event_queue(new LoggingEvent(1 + 0.01));

  std::cout << "Running " << params.num_flows_to_run << " Flows\nCDF_File " <<
    params.cdf_or_flow_trace << "\nBandwidth " << params.bandwidth/1e9 <<
    "\nQueueSize " << params.queue_size <<
    "\nCutThrough " << params.cut_through <<
    "\nFlowType " << params.flow_type <<
    "\nQueueType " << params.queue_type <<
    "\nInit CWND " << params.initial_cwnd <<
    "\nMax CWND " << params.max_cwnd <<
    "\nRtx Timeout " << params.retx_timeout_value  <<
    "\nload_balancing (0: pkt)" << params.load_balancing <<
    std::endl;

  run_scenario_shuffle_traffic();

  std::deque<Flow *> flows_sorted = flows_to_schedule;
  struct FlowComparator {
    bool operator() (Flow *a, Flow *b) {
      return a->start_time < b->start_time;
    }
  } fc;
  std::sort(flows_sorted.begin(), flows_sorted.end(), fc);

  // print statistics
  double sum = 0, sum_norm = 0, sum_inflation = 0;
  for (uint32_t i = 0; i < flows_sorted.size(); i++) {
    Flow *f = flows_to_schedule[i];
    if(!f->finished)
      std::cout << "unfinished flow " << "size:" << f->size << " id:" << f->id << " next_seq:" << f->next_seq_no << " recv:" << f->received_bytes << "\n";
    sum += 1000000.0 * f->flow_completion_time;
    sum_norm += 1000000.0 * f->flow_completion_time / topology->get_oracle_fct(f);
    sum_inflation += (double)f->total_pkt_sent / (f->size/f->mss);
  }
  std::cout << "AverageFCT " << sum / flows_sorted.size() <<
  " MeanSlowdown " << sum_norm / flows_sorted.size() <<
  " MeanInflation " << sum_inflation / flows_sorted.size() <<
  "\n";
  printQueueStatistics(topo);
}

